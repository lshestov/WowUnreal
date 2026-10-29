#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void RegisterUnit(lua_State* L,const char* Name,lua_CFunction Function) { lua_pushcfunction(L,Function); lua_setglobal(L,Name); }
int32 Field(lua_State* L,uint16 Offset) { const auto* E=WowLuaApi::ResolveUnitToken(L); return E?int32(E->GetField(Offset)):0; }
float Float(lua_State* L,uint16 Offset) { const auto* E=WowLuaApi::ResolveUnitToken(L); return E?E->GetFieldFloat(Offset):0; }
float Ratio(lua_State* L,const TCHAR* Table)
{
    const auto* E=WowLuaApi::ResolveUnitToken(L); if(!E || !E->IsUnit()) return 0;
    const auto* U=static_cast<const FWowUnitEntity*>(E);
    return FDbcStore::Get().StatRatio(FName(Table),(FMath::Max(int32(U->GetClassId()),1)-1)*100+FMath::Clamp(U->GetLevel(),1,100)-1);
}
TPair<int32,int32> Skill(const FWowEntity* E,uint32 Id)
{
    if(E) for(int32 I=0; I<128; ++I)
    {
        const uint16 Base=PlayerField::SKILL_INFO+I*3;
        if((E->GetField(Base)&0xffff)==Id)
        {
            const uint32 Bonus=E->GetField(Base+2);
            return {int32(E->GetField(Base+1)&0xffff),int32(int16(Bonus&0xffff))+int32(int16(Bonus>>16))};
        }
    }
    return {0,0};
}
bool KnownTitle(FWowLuaContext* C,int32 Mask)
{
    const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    return P && Mask>=0 && Mask<192 && (P->GetField(PlayerField::BASE+0x01DE+Mask/32)&(1u<<(Mask%32)))!=0;
}
TPair<int32,int32> WeaponSkill(lua_State* L,uint8 Slot)
{
    auto* C=WowLuaApi::GetContext(L); auto* E=WowLuaApi::ResolveUnitToken(L);
    uint32 Id=162; // unarmed
    if(C && C->EntityManager && E && E->IsPlayer())
    {
        const auto* P=static_cast<const FWowPlayerEntity*>(E);
        const auto* Item=C->EntityManager->FindItem(P->GetEquipmentItemGuid(Slot));
        const auto* T=Item && C->ConnectionManager?C->ConnectionManager->PacketHandler.ItemTemplates.Find(Item->Entry):nullptr;
        static const uint32 WeaponSkills[]={44,172,45,46,54,160,229,43,55,0,136,0,0,473,0,173,176,0,226,228,356};
        if(T && T->Class==2 && T->Subclass<UE_ARRAY_COUNT(WeaponSkills)) Id=WeaponSkills[T->Subclass];
    }
    return Skill(E,Id);
}
int Resist(lua_State* L)
{
    const int32 School=luaL_checkinteger(L,2); if(School<0 || School>6) return luaL_argerror(L,2,"invalid resistance school");
    const int32 Total=Field(L,UnitField::BASE+0x005D+School),Pos=Field(L,UnitField::BASE+0x0064+School),Neg=Field(L,UnitField::BASE+0x006B+School);
    lua_pushinteger(L,Total-Pos-Neg); lua_pushinteger(L,Total); lua_pushinteger(L,Pos); lua_pushinteger(L,Neg); return 4;
}
int AttackPower(lua_State* L,bool Ranged)
{
    const uint16 Base=UnitField::BASE+(Ranged?0x0078:0x0075); const uint32 Mods=uint32(Field(L,Base+1));
    lua_pushinteger(L,Field(L,Base)); lua_pushinteger(L,int16(Mods&0xffff)); lua_pushinteger(L,int16(Mods>>16)); return 3;
}
}
void WowLuaApi::UpdateCharacterStats(lua_State* L)
{
    auto* C=GetContext(L); if(!C) return;
    const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    if(!P) { C->CharacterStatGuid=0; C->CharacterStatFields.Reset(); return; }
    if(C->CharacterStatGuid!=P->Guid) { C->CharacterStatFields.Reset(); C->CharacterStatGuid=P->Guid; }
    struct FWatch {uint16 First,Count; const TCHAR* Event;};
    const FWatch Watches[]={
        {UnitField::STAT0,15,TEXT("UNIT_STATS")},
        {UnitField::BASE+0x005D,21,TEXT("UNIT_RESISTANCES")},
        {UnitField::BASE+0x0038,3,TEXT("UNIT_ATTACK_SPEED")},
        {UnitField::BASE+0x0040,4,TEXT("UNIT_DAMAGE")},
        {UnitField::ATTACK_POWER,3,TEXT("UNIT_DAMAGE")},
        {UnitField::RANGED_ATTACK_POWER,3,TEXT("UNIT_RANGED_ATTACK_POWER")},
        {UnitField::BASE+0x007B,2,TEXT("UNIT_RANGEDDAMAGE")},
        {PlayerField::BASE+0x03FF,24,TEXT("PLAYER_DAMAGE_DONE_MODS")},
        {PlayerField::BASE+0x036C,17,TEXT("UNIT_STATS")},
        {UnitField::BASE+0x0022,14,TEXT("UNIT_STATS")},
        {UnitField::BYTES_0,1,TEXT("UNIT_STATS")},
        {PlayerField::COMBAT_RATING_1,25,TEXT("COMBAT_RATING_UPDATE")},
        {UnitField::LEVEL,1,TEXT("UNIT_LEVEL")}
    };
    TSet<FString> Changed;
    for(const auto& Watch:Watches) for(uint16 I=0;I<Watch.Count;++I)
    {
        const uint16 Index=Watch.First+I; const uint32 Value=P->GetField(Index);
        const auto* Before=C->CharacterStatFields.Find(Index);
        if(!Before || *Before!=Value) {C->CharacterStatFields.Add(Index,Value); Changed.Add(Watch.Event);}
    }
    // Publish after the complete snapshot, so original handlers read one state.
    if(C->EventSystem) for(const auto& Event:Changed)
        C->EventSystem->FireEvent(Event,Event==TEXT("COMBAT_RATING_UPDATE")?TArray<FString>():TArray<FString>{TEXT("player")});
}

void WowLuaApi::RegisterUnitApi(lua_State* L)
{
    RegisterUnit(L,"FocusUnit",[](lua_State* S)->int {
        auto* C=GetContext(S); if(!C) return 0; if(lua_isnoneornil(S,1)) {lua_settop(S,0); lua_pushstring(S,"target");}
        const auto* E=ResolveUnitToken(S); if(!E || !E->IsUnit() || C->FocusGuid==E->Guid) return 0;
        C->FocusGuid=E->Guid; if(C->EventSystem) C->EventSystem->FireEvent(TEXT("PLAYER_FOCUS_CHANGED")); return 0;
    });
    RegisterUnit(L,"ClearFocus",[](lua_State* S)->int {auto* C=GetContext(S); if(C && C->FocusGuid) {C->FocusGuid=0; if(C->EventSystem) C->EventSystem->FireEvent(TEXT("PLAYER_FOCUS_CHANGED"));} return 0;});
    RegisterUnit(L,"GetCombatRatingBonus",[](lua_State* S)->int {
        const int32 Rating=luaL_checkinteger(S,1)-1; lua_settop(S,0); lua_pushstring(S,"player"); const auto* E=ResolveUnitToken(S);
        double Bonus=0;
        if(E && E->IsPlayer() && Rating>=0 && Rating<25)
        {
            const auto* P=static_cast<const FWowPlayerEntity*>(E); const auto& Dbc=FDbcStore::Get();
            const float Ratio=Dbc.StatRatio(TEXT("gtCombatRatings"),Rating*100+FMath::Clamp(P->GetLevel(),1,100)-1);
            const float Scalar=Dbc.StatRatio(TEXT("gtOCTClassCombatRatingScalar"),(FMath::Max(int32(P->GetClassId()),1)-1)*32+Rating+1);
            if(Ratio>0) Bonus=E->GetField(PlayerField::BASE+0x043B+Rating)*Scalar/Ratio;
        }
        lua_pushnumber(S,Bonus); return 1;
    });
    RegisterUnit(L,"GetArmorPenetration",[](lua_State* S)->int { lua_settop(S,0); lua_pushstring(S,"player"); lua_pushnumber(S,Float(S,PlayerField::BASE+0x037C)); return 1; });
    RegisterUnit(L,"GetHonorCurrency",[](lua_State* S)->int { lua_settop(S,0); lua_pushstring(S,"player"); lua_pushinteger(S,Field(S,PlayerField::BASE+0x0469)); return 1; });
    RegisterUnit(L,"GetArenaCurrency",[](lua_State* S)->int { lua_settop(S,0); lua_pushstring(S,"player"); lua_pushinteger(S,Field(S,PlayerField::BASE+0x046A)); return 1; });
    RegisterUnit(L,"GetLifetimeHonorableKills",[](lua_State* S)->int { lua_settop(S,0); lua_pushstring(S,"player"); lua_pushinteger(S,Field(S,PlayerField::BASE+0x0438)); return 1; });
    RegisterUnit(L,"GetCursorPosition",[](lua_State* S)->int { const auto* C=GetContext(S); const auto P=C && C->FrameManager?C->FrameManager->GetPointerPosition()/C->FrameManager->GetScreenScale():FVector2D::ZeroVector; lua_pushnumber(S,P.X); lua_pushnumber(S,768-P.Y); return 2; });
    RegisterUnit(L,"UnitResistance",Resist);
    RegisterUnit(L,"UnitStat",[](lua_State* S)->int {
        const int32 Index=luaL_checkinteger(S,2)-1; if(Index<0 || Index>4) return luaL_argerror(S,2,"invalid stat index");
        const int32 Total=Field(S,UnitField::BASE+0x004E + Index),Pos=Field(S,UnitField::BASE+0x0053+Index),Neg=Field(S,UnitField::BASE+0x0058+Index);
        lua_pushinteger(S,Total-Pos-Neg); lua_pushinteger(S,Total); lua_pushinteger(S,Pos); lua_pushinteger(S,Neg); return 4;
    });
    RegisterUnit(L,"UnitArmor",[](lua_State* S)->int {
        const int32 Total=Field(S,UnitField::RESISTANCES),Pos=Field(S,UnitField::BASE+0x0064),Neg=Field(S,UnitField::BASE+0x006B);
        lua_pushinteger(S,Total-Pos-Neg); lua_pushinteger(S,Total); lua_pushinteger(S,Total); lua_pushinteger(S,Pos); lua_pushinteger(S,Neg); return 5;
    });
    RegisterUnit(L,"UnitAttackPower",[](lua_State* S)->int { return AttackPower(S,false); });
    RegisterUnit(L,"UnitRangedAttackPower",[](lua_State* S)->int { return AttackPower(S,true); });
    RegisterUnit(L,"UnitAttackSpeed",[](lua_State* S)->int {
        lua_pushnumber(S,Field(S,UnitField::BASE+0x0038)/1000.0); lua_pushnumber(S,Field(S,UnitField::BASE+0x0039)/1000.0); return 2;
    });
    RegisterUnit(L,"UnitDamage",[](lua_State* S)->int {
        for(uint16 I=0; I<4; ++I) lua_pushnumber(S,Float(S,UnitField::BASE+0x0040+I));
        lua_pushnumber(S,0); lua_pushnumber(S,0); lua_pushnumber(S,1); return 7;
    });
    RegisterUnit(L,"UnitRangedDamage",[](lua_State* S)->int {
        lua_pushnumber(S,Field(S,UnitField::BASE+0x003A)/1000.0);
        lua_pushnumber(S,Float(S,UnitField::BASE+0x007B)); lua_pushnumber(S,Float(S,UnitField::BASE+0x007C));
        lua_pushnumber(S,0); lua_pushnumber(S,0); lua_pushnumber(S,1); return 6;
    });
    RegisterUnit(L,"UnitDefense",[](lua_State* S)->int { const auto Value=Skill(ResolveUnitToken(S),95); lua_pushinteger(S,Value.Key); lua_pushinteger(S,Value.Value); return 2; });
    RegisterUnit(L,"UnitAttackBothHands",[](lua_State* S)->int { const auto A=WeaponSkill(S,15),B=WeaponSkill(S,16); lua_pushinteger(S,A.Key); lua_pushinteger(S,A.Value); lua_pushinteger(S,B.Key); lua_pushinteger(S,B.Value); return 4; });
    RegisterUnit(L,"UnitRangedAttack",[](lua_State* S)->int { const auto A=WeaponSkill(S,17); lua_pushinteger(S,A.Key); lua_pushinteger(S,A.Value); lua_pushinteger(S,0); lua_pushinteger(S,0); return 4; });
    RegisterUnit(L,"UnitHasMana",[](lua_State* S)->int { const auto* E=ResolveUnitToken(S); lua_pushboolean(S,E && E->IsUnit() && E->GetField(UnitField::MAXPOWER1)>0); return 1; });
    RegisterUnit(L,"UnitHasRelicSlot",[](lua_State* S)->int { const auto* E=ResolveUnitToken(S); const uint8 Class=E && E->IsUnit()?static_cast<const FWowUnitEntity*>(E)->GetClassId():0; lua_pushboolean(S,Class==2 || Class==6 || Class==7 || Class==11); return 1; });
    RegisterUnit(L,"GetCritChance",[](lua_State* S)->int { lua_pushnumber(S,Float(S,PlayerField::BASE+0x0371)); return 1; });
    RegisterUnit(L,"GetRangedCritChance",[](lua_State* S)->int { lua_pushnumber(S,Float(S,PlayerField::BASE+0x0372)); return 1; });
    RegisterUnit(L,"GetDodgeChance",[](lua_State* S)->int { lua_pushnumber(S,Float(S,PlayerField::BASE+0x036D)); return 1; });
    RegisterUnit(L,"GetParryChance",[](lua_State* S)->int { lua_pushnumber(S,Float(S,PlayerField::BASE+0x036E)); return 1; });
    RegisterUnit(L,"GetBlockChance",[](lua_State* S)->int { lua_pushnumber(S,Float(S,PlayerField::BASE+0x036C)); return 1; });
    RegisterUnit(L,"GetShieldBlock",[](lua_State* S)->int { lua_pushnumber(S,Field(S,PlayerField::BASE+0x037B)); return 1; });
    RegisterUnit(L,"GetSpellCritChance",[](lua_State* S)->int { const int32 School=FMath::Clamp(int32(luaL_optinteger(S,1,1)),1,7); lua_settop(S,0); lua_pushstring(S,"player"); lua_pushnumber(S,Float(S,PlayerField::BASE+0x0374+School-1)); return 1; });
    RegisterUnit(L,"GetSpellBonusDamage",[](lua_State* S)->int {
        const int32 School=luaL_checkinteger(S,1)-1;
        if(School<0 || School>=7) return luaL_argerror(S,1,"spell school must be 1 through 7");
        const auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
        // The negative array contains signed penalties, not absolute magnitudes.
        lua_pushnumber(S,P?int32(P->GetField(PlayerField::BASE+0x03FF+School))+int32(P->GetField(PlayerField::BASE+0x0406+School)):0);
        return 1;
    });
    RegisterUnit(L,"GetSpellBonusHealing",[](lua_State* S)->int {
        const auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
        lua_pushnumber(S,P?int32(P->GetField(PlayerField::BASE+0x0414)):0); return 1;
    });
    RegisterUnit(L,"GetManaRegen",[](lua_State* S)->int {
        const auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
        // Wrath returns per-second rates plus a small rounding epsilon. Energy,
        // rage and other active resources do not expose a mana-regeneration row.
        const bool Mana=P && P->GetPowerTypeId()==0;
        lua_pushnumber(S,Mana?P->GetFieldFloat(UnitField::BASE+0x0022)+.001f:0);
        lua_pushnumber(S,Mana?P->GetFieldFloat(UnitField::BASE+0x0029)+.001f:0); return 2;
    });
    RegisterUnit(L,"GetExpertisePercent",[](lua_State* S)->int { lua_settop(S,0); lua_pushstring(S,"player"); lua_pushnumber(S,Field(S,PlayerField::BASE+0x036F)*.25); lua_pushnumber(S,Field(S,PlayerField::BASE+0x0370)*.25); return 2; });
    RegisterUnit(L,"GetUnitMaxHealthModifier",[](lua_State* S)->int {
        const auto* E=ResolveUnitToken(S); double Factor=1;
        if(E) for(const auto& Aura:E->Auras) if(Aura.bActive) if(const auto* Spell=FDbcStore::Get().Spells().GetById(Aura.SpellId))
            for(int32 I=0; I<3; ++I) if(Spell->EffectApplyAuraName[I]==133) Factor*=1+(Spell->EffectBasePoints[I]+1)*FMath::Max(int32(Aura.Charges),1)/100.0;
        lua_pushnumber(S,Factor); return 1;
    });
    RegisterUnit(L,"GetAttackPowerForStat",[](lua_State* S)->int {
        const int32 Stat=luaL_checkinteger(S,1); const float Value=luaL_checknumber(S,2);
        lua_settop(S,0); lua_pushstring(S,"player"); const auto* E=ResolveUnitToken(S);
        const uint8 Class=E && E->IsUnit()?static_cast<const FWowUnitEntity*>(E)->GetClassId():0;
        const bool Hybrid=Class==3 || Class==4 || Class==7;
        const bool StrengthTwo=Class==1 || Class==2 || Class==6 || Class==11;
        const bool Cat=Class==11 && ((E->GetField(UnitField::BYTES_2)>>24)&0xff)==1;
        lua_pushnumber(S,Stat==1?Value*(StrengthTwo?2:1):Stat==2 && (Hybrid || Cat)?Value:0); return 1;
    });
    RegisterUnit(L,"GetCritChanceFromAgility",[](lua_State* S)->int { lua_pushnumber(S,Field(S,UnitField::STAT1)*Ratio(S,TEXT("gtChanceToMeleeCrit"))*100); return 1; });
    RegisterUnit(L,"GetSpellCritChanceFromIntellect",[](lua_State* S)->int { lua_pushnumber(S,Field(S,UnitField::STAT3)*Ratio(S,TEXT("gtChanceToSpellCrit"))*100); return 1; });
    RegisterUnit(L,"GetUnitHealthRegenRateFromSpirit",[](lua_State* S)->int { const float Spirit=Field(S,UnitField::STAT4); lua_pushnumber(S,(FMath::Min(Spirit,50.f)*Ratio(S,TEXT("gtOCTRegenHP"))+FMath::Max(Spirit-50,0.f)*Ratio(S,TEXT("gtRegenHPPerSpt")))); return 1; });
    RegisterUnit(L,"GetUnitManaRegenRateFromSpirit",[](lua_State* S)->int { lua_pushnumber(S,FMath::Sqrt(FMath::Max(float(Field(S,UnitField::STAT3)),0.f))*Field(S,UnitField::STAT4)*Ratio(S,TEXT("gtRegenMPPerSpt"))); return 1; });
    RegisterUnit(L,"GetDodgeBlockParryChanceFromDefense",[](lua_State* S)->int { const auto* E=ResolveUnitToken(S); const auto V=Skill(E,95); const int32 Level=E && E->IsUnit()?static_cast<const FWowUnitEntity*>(E)->GetLevel():0; lua_pushnumber(S,(V.Key+V.Value-Level*5)*.04); return 1; });
    RegisterUnit(L,"GetCurrentTitle",[](lua_State* S)->int { lua_settop(S,0); lua_pushstring(S,"player"); const int32 Id=Field(S,PlayerField::BASE+0x00AD); lua_pushinteger(S,Id?Id:-1); return 1; });
    RegisterUnit(L,"GetNumTitles",[](lua_State* S)->int { lua_pushinteger(S,FDbcStore::Get().MaxTitleIndex()); return 1; });
    RegisterUnit(L,"IsTitleKnown",[](lua_State* S)->int { lua_pushinteger(S,KnownTitle(GetContext(S),luaL_checkinteger(S,1))?1:0); return 1; });
    RegisterUnit(L,"GetTitleName",[](lua_State* S)->int {
        const auto* T=FDbcStore::Get().Title(luaL_checkinteger(S,1)); auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
        if(!T) { lua_pushnil(S); return 1; }
        const FString Name=(P && P->GetGenderId()==1?T->Female:T->Male).Replace(TEXT("%s"),TEXT("")).TrimStartAndEnd();
        lua_pushstring(S,TCHAR_TO_UTF8(*Name)); return 1;
    });
    RegisterUnit(L,"SetCurrentTitle",[](lua_State* S)->int { auto* C=GetContext(S); const int32 Id=luaL_checkinteger(S,1); if(C && C->ConnectionManager && (Id<=0 || KnownTitle(C,Id))) C->ConnectionManager->SendSetTitle(FMath::Max(Id,0)); return 0; });
    RegisterUnit(L,"UnitPVPName",[](lua_State* S)->int {
        const auto* E=ResolveUnitToken(S); const uint32 Id=E?E->GetField(PlayerField::BASE+0x00AD):0;
        lua_getglobal(S,"UnitName"); lua_pushvalue(S,1); if(lua_pcall(S,1,1,0)!=0) return lua_error(S);
        FString Name=UTF8_TO_TCHAR(lua_tostring(S,-1)?lua_tostring(S,-1):""); lua_pop(S,1);
        if(Id && E && E->IsPlayer()) if(const auto* T=FDbcStore::Get().Title(Id)) Name=(static_cast<const FWowPlayerEntity*>(E)->GetGenderId()==1?T->Female:T->Male).Replace(TEXT("%s"),*Name);
        lua_pushstring(S,TCHAR_TO_UTF8(*Name)); return 1;
    });
}
#else
void WowLuaApi::RegisterUnitApi(lua_State*) {}
void WowLuaApi::UpdateCharacterStats(lua_State*) {}
FWowEntity* WowLuaApi::ResolveUnitToken(lua_State*,int32) { return nullptr; }
#endif
