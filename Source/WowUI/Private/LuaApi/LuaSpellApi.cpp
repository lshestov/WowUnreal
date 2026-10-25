#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"
#include "WowFrameManager.h"
#include "Formats/Dbc/DbcStore.h"

struct FWowSpellBookView
{
    struct FTab { FString Name,Texture; int32 Offset=0,Count=0,HighestOffset=0,HighestCount=0; };
    TArray<FTab> Tabs;
    TArray<uint32> Spells;
    TSet<uint32> Known;
    uint8 Class=0,Race=0;
};
#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void RegisterSpell(lua_State* L,const char* Name,lua_CFunction Function) { lua_pushcfunction(L,Function); lua_setglobal(L,Name); }
TArray<uint32> Forms(FWowLuaContext* C)
{
    TArray<uint32> Result; if(!C || !C->ConnectionManager) return Result;
    for(uint32 Id:C->ConnectionManager->PacketHandler.KnownSpells) if(const auto* Spell=FDbcStore::Get().Spells().GetById(Id))
        for(int32 I=0;I<3;++I) if(Spell->Effect[I]==6 && Spell->EffectApplyAuraName[I]==36) { Result.Add(Id); break; }
    Result.Sort([](uint32 A,uint32 B) { return FDbcStore::Get().Spells().GetById(A)->EffectMiscValue[0]<FDbcStore::Get().Spells().GetById(B)->EffectMiscValue[0]; });
    return Result;
}
uint32 Form(lua_State* L)
{
    const auto List=Forms(WowLuaApi::GetContext(L)); const int32 I=luaL_checkinteger(L,1)-1; return List.IsValidIndex(I)?List[I]:0;
}
TArray<uint32> Companions(lua_State* L)
{
    TArray<uint32> Result; auto* C=WowLuaApi::GetContext(L); const FString Kind=UTF8_TO_TCHAR(luaL_checkstring(L,1)); const uint32 Skill=Kind==TEXT("MOUNT")?777:Kind==TEXT("CRITTER")?778:0;
    if(!Skill || !C || !C->ConnectionManager) return Result;
    for(uint32 Id:C->ConnectionManager->PacketHandler.KnownSpells) if(const auto* Abilities=FDbcStore::Get().SpellBook().Abilities(Id))
        for(const auto& A:*Abilities) if(A.Skill==Skill && FDbcStore::Get().Spells().GetById(Id)) { Result.Add(Id); break; }
    Result.Sort([](uint32 A,uint32 B){return FDbcStore::Get().Spells().GetById(A)->SpellName<FDbcStore::Get().Spells().GetById(B)->SpellName;}); return Result;
}
FWowSpellBookView* Book(FWowLuaContext* C)
{
    if(!C || !C->ConnectionManager) return nullptr;
    const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    const auto& Known=C->ConnectionManager->PacketHandler.KnownSpells;
    if(C->SpellBook && C->SpellBook->Class==(P?P->GetClassId():0) && C->SpellBook->Race==(P?P->GetRaceId():0) && C->SpellBook->Known.Num()==Known.Num() && C->SpellBook->Known.Includes(Known)) return C->SpellBook.Get();
    C->SpellBook=MakeShared<FWowSpellBookView>(); auto* B=C->SpellBook.Get(); B->Known=Known; B->Class=P?P->GetClassId():0; B->Race=P?P->GetRaceId():0;
    const auto& D=FDbcStore::Get(); TMap<uint32,TArray<uint32>> Groups;
    Groups.Add(0);
    for(const uint32 Id:Known)
    {
        const auto* S=D.Spells().GetById(Id); if(!S) continue;
        uint32 Group=0;
        if(const auto* Abilities=D.SpellBook().Abilities(Id)) for(const auto& A:*Abilities)
        {
            if((A.ClassMask && (B->Class<1 || !(A.ClassMask&(1u<<(B->Class-1))))) || (A.RaceMask && (B->Race<1 || !(A.RaceMask&(1u<<(B->Race-1)))))) continue;
            const auto* Skill=D.SpellBook().Skill(A.Skill);
            if(Skill && (Skill->Category==7 || Skill->Category==9 || Skill->Category==11)) { Group=A.Skill; break; }
        }
        Groups.FindOrAdd(Group).Add(Id);
    }
    TArray<uint32> Keys; Groups.GetKeys(Keys); Keys.Sort();
    if(Keys.Num()>8) { for(int32 I=8; I<Keys.Num(); ++I) Groups[0].Append(Groups[Keys[I]]); Keys.SetNum(8); }
    for(uint32 Key:Keys)
    {
        auto& Ids=Groups[Key];
        Ids.Sort([&](uint32 A,uint32 Z){ const auto* X=D.Spells().GetById(A); const auto* Y=D.Spells().GetById(Z); return X->SpellName==Y->SpellName?X->SpellLevel==Y->SpellLevel?A<Z:X->SpellLevel<Y->SpellLevel:X->SpellName<Y->SpellName; });
        FWowSpellBookView::FTab Tab; Tab.Name=TEXT("General"); Tab.Texture=TEXT("Interface\\Icons\\INV_Misc_Book_09");
        if(const auto* Skill=D.SpellBook().Skill(Key)) { Tab.Name=Skill->Name; if(const auto* Icon=D.SpellIcons().GetById(Skill->Icon)) Tab.Texture=Icon->TexturePath; }
        Tab.Offset=B->Spells.Num(); Tab.Count=Ids.Num(); B->Spells.Append(Ids); B->Tabs.Add(MoveTemp(Tab));
    }
    for(int32 I=0; I<Keys.Num(); ++I)
    {
        auto& Tab=B->Tabs[I]; Tab.HighestOffset=B->Spells.Num(); TMap<FString,uint32> Highest;
        for(uint32 Id:Groups[Keys[I]]) Highest.Add(D.Spells().GetById(Id)->SpellName,Id);
        for(uint32 Id:Groups[Keys[I]]) if(Highest[D.Spells().GetById(Id)->SpellName]==Id) B->Spells.Add(Id);
        Tab.HighestCount=B->Spells.Num()-Tab.HighestOffset;
    }
    return B;
}
uint32 Resolve(lua_State* L,int32 Index=1)
{
    auto* C=WowLuaApi::GetContext(L);
    if(lua_gettop(L)>Index && lua_isstring(L,Index+1) && FString(UTF8_TO_TCHAR(lua_tostring(L,Index+1)))==TEXT("pet"))
    {
        const int32 Slot=luaL_checkinteger(L,Index)-1;
        const auto* Spells=C && C->ConnectionManager?&C->ConnectionManager->PacketHandler.PetActionBar.KnownSpells:nullptr;
        return Spells && Spells->IsValidIndex(Slot)?(*Spells)[Slot]:0;
    }
    if(lua_gettop(L)>Index && lua_isstring(L,Index+1) && FString(UTF8_TO_TCHAR(lua_tostring(L,Index+1)))==TEXT("spell"))
    {
        const int32 Slot=luaL_checkinteger(L,Index)-1; auto* B=Book(C); return B && B->Spells.IsValidIndex(Slot)?B->Spells[Slot]:0;
    }
    if(lua_isnumber(L,Index)) return lua_tointeger(L,Index);
    const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,Index,"")); uint32 Found=0;
    if(C && C->ConnectionManager) for(uint32 Id:C->ConnectionManager->PacketHandler.KnownSpells)
        if(const auto* Spell=FDbcStore::Get().Spells().GetById(Id); Spell && Spell->SpellName.Equals(Name,ESearchCase::IgnoreCase)) Found=FMath::Max(Found,Id);
    return Found;
}
int SpellName(lua_State* L)
{
    const auto* S=FDbcStore::Get().Spells().GetById(Resolve(L));
    if(!S) { lua_pushnil(L); return 1; }
    lua_pushstring(L,TCHAR_TO_UTF8(*S->SpellName)); lua_pushstring(L,TCHAR_TO_UTF8(*S->Rank)); return 2;
}
int SpellTexture(lua_State* L)
{
    const auto* S=FDbcStore::Get().Spells().GetById(Resolve(L)); const auto* Icon=S?FDbcStore::Get().SpellIcons().GetById(S->SpellIconID):nullptr;
    if(Icon) lua_pushstring(L,TCHAR_TO_UTF8(*Icon->TexturePath)); else lua_pushnil(L); return 1;
}
}
int WowLuaApi::PushSpellCooldown(lua_State* L,uint32 SpellId)
{
    const auto* C=GetContext(L); const auto* Info=C && C->ConnectionManager?C->ConnectionManager->PacketHandler.SpellCooldownInfo.Find(SpellId):nullptr;
    const bool Active=Info && Info->Start+Info->Duration>FPlatformTime::Seconds();
    lua_pushnumber(L,Active?Info->Start:0); lua_pushnumber(L,Active?Info->Duration:0); lua_pushinteger(L,1); return 3;
}
int WowLuaApi::SetSpellTooltip(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->FrameManager) return 0;
    const uint32 Id=Resolve(L,2); lua_getfield(L,1,"__handle"); const int64 H=lua_tointeger(L,-1); lua_pop(L,1);
    const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    C->FrameManager->TooltipSetSpell(H,Id,P?P->GetLevel():1); return 0;
}
int WowLuaApi::SetAuraTooltip(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->FrameManager) return 0;
    lua_getfield(L,1,"__handle"); const int64 H=lua_tointeger(L,-1); lua_pop(L,1);
    const char* Kind=lua_tostring(L,lua_upvalueindex(1)); lua_getglobal(L,Kind?Kind:"UnitAura");
    lua_pushvalue(L,2); lua_pushvalue(L,3); lua_pushvalue(L,4);
    if(lua_pcall(L,3,11,0)!=0) return lua_error(L);
    const uint32 Id=lua_tointeger(L,-1); lua_pop(L,11);
    const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    C->FrameManager->TooltipSetSpell(H,Id,P?P->GetLevel():1,true); return 0;
}
void WowLuaApi::RegisterSpellApi(lua_State* L)
{
    RegisterSpell(L,"GetNumCompanions",[](lua_State* S)->int { lua_pushinteger(S,Companions(S).Num()); return 1; });
    RegisterSpell(L,"GetCompanionInfo",[](lua_State* S)->int {
        const auto List=Companions(S); const int32 I=luaL_checkinteger(S,2)-1; if(!List.IsValidIndex(I)) { lua_pushnil(S); return 1; }
        const auto* Spell=FDbcStore::Get().Spells().GetById(List[I]); uint32 Creature=0; for(int32 N=0;N<3;++N) if(Spell->Effect[N]==28 || Spell->EffectApplyAuraName[N]==78) Creature=Spell->EffectMiscValue[N];
        const auto* Icon=FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID); auto* C=GetContext(S); const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; bool Active=false;
        if(P) { for(const auto& Aura:P->Auras) Active|=Aura.bActive && Aura.SpellId==List[I]; const auto* Critter=C->EntityManager->Find(P->GetField64(UnitField::CRITTER)); Active|=Critter && Critter->Entry==Creature; }
        lua_pushinteger(S,Creature); lua_pushstring(S,TCHAR_TO_UTF8(*Spell->SpellName)); lua_pushinteger(S,List[I]); lua_pushstring(S,Icon?TCHAR_TO_UTF8(*Icon->TexturePath):""); lua_pushboolean(S,Active); return 5;
    });
    RegisterSpell(L,"CallCompanion",[](lua_State* S)->int { const auto List=Companions(S); const int32 I=luaL_checkinteger(S,2)-1; auto* C=GetContext(S); if(List.IsValidIndex(I)) C->ConnectionManager->SendCastSpell(List[I],C->EntityManager->LocalPlayerGuid); return 0; });
    RegisterSpell(L,"DismissCompanion",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) { const FString Kind=UTF8_TO_TCHAR(luaL_checkstring(S,1)); if(Kind==TEXT("MOUNT")) C->ConnectionManager->SendRawPacket(0x375,{}); else if(Kind==TEXT("CRITTER")) { const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const uint64 Guid=P?P->GetField64(UnitField::CRITTER):0; if(Guid) { TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Guid),8); C->ConnectionManager->SendRawPacket(0x48D,Data); } } } return 0; });
    RegisterSpell(L,"GetCompanionCooldown",[](lua_State* S)->int { const auto List=Companions(S); const int32 I=luaL_checkinteger(S,2)-1; return PushSpellCooldown(S,List.IsValidIndex(I)?List[I]:0); });
    RegisterSpell(L,"GetNumSpellTabs",[](lua_State* S)->int { const auto* B=Book(GetContext(S)); lua_pushinteger(S,B?B->Tabs.Num():0); return 1; });
    RegisterSpell(L,"GetSpellTabInfo",[](lua_State* S)->int {
        const auto* B=Book(GetContext(S)); const int32 I=luaL_optinteger(S,1,1)-1;
        if(!B || !B->Tabs.IsValidIndex(I)) { lua_pushnil(S); lua_pushnil(S); for(int32 J=0; J<4; ++J) lua_pushinteger(S,0); return 6; }
        const auto& T=B->Tabs[I]; lua_pushstring(S,TCHAR_TO_UTF8(*T.Name)); lua_pushstring(S,TCHAR_TO_UTF8(*T.Texture));
        lua_pushinteger(S,T.Offset); lua_pushinteger(S,T.Count); lua_pushinteger(S,T.HighestOffset); lua_pushinteger(S,T.HighestCount); return 6;
    });
    RegisterSpell(L,"GetSpellName",SpellName); RegisterSpell(L,"GetSpellTexture",SpellTexture);
    RegisterSpell(L,"GetSpellCooldown",[](lua_State* S)->int { return PushSpellCooldown(S,Resolve(S)); });
    RegisterSpell(L,"GetSpellAutocast",[](lua_State* S)->int {
        auto* C=GetContext(S); const uint32 Id=Resolve(S); bool Allowed=false,Enabled=false;
        if(C && C->ConnectionManager) for(const auto& Slot:C->ConnectionManager->PacketHandler.PetActionBar.ActionSlots)
            if(Slot.IsSpell() && Slot.ActionId==Id && Id) { Allowed=Slot.IsAutocastCapable(); Enabled=Slot.IsAutocastEnabled(); break; }
        lua_pushboolean(S,Allowed); lua_pushboolean(S,Enabled); return 2;
    });
    RegisterSpell(L,"IsSelectedSpell",[](lua_State* S)->int { auto* C=GetContext(S); const uint32 Id=Resolve(S); FString Type,Detail; int32 Selected=0; const bool Has=C && C->ConnectionManager && C->ConnectionManager->GetCursorInfo(Type,Selected,Detail); lua_pushboolean(S,Has && Type==TEXT("spell") && Selected==int32(Id)); return 1; });
    RegisterSpell(L,"GetKnownSlotFromHighestRankSlot",[](lua_State* S)->int {
        const auto* B=Book(GetContext(S)); const int32 Slot=luaL_checkinteger(S,1)-1;
        if(!B || !B->Spells.IsValidIndex(Slot)) { lua_pushnil(S); return 1; }
        lua_pushinteger(S,B->Spells.Find(B->Spells[Slot])+1); return 1;
    });
    RegisterSpell(L,"UpdateSpells",[](lua_State* S)->int {
        for(int32 I=1; I<=12; ++I)
        {
            lua_getglobal(S,"SpellButton_UpdateButton"); if(!lua_isfunction(S,-1)) { lua_pop(S,1); break; }
            lua_getglobal(S,TCHAR_TO_UTF8(*FString::Printf(TEXT("SpellButton%d"),I)));
            if(lua_pcall(S,1,0,0)!=0) return lua_error(S);
        }
        return 0;
    });
    RegisterSpell(L,"GetSpellBookItemInfo",[](lua_State* S)->int { const uint32 Id=Resolve(S); if(!Id) { lua_pushnil(S); return 1; } lua_pushstring(S,"SPELL"); lua_pushinteger(S,Id); return 2; });
    RegisterSpell(L,"IsPassiveSpell",[](lua_State* S)->int { const auto* Spell=FDbcStore::Get().Spells().GetById(Resolve(S)); lua_pushboolean(S,Spell && (Spell->Attributes&0x40)); return 1; });
    RegisterSpell(L,"PickupSpell",[](lua_State* S)->int { auto* C=GetContext(S); const uint32 Id=Resolve(S); if(C && C->ConnectionManager && Id) C->ConnectionManager->PickupSpellCursor(Id); return 0; });
    RegisterSpell(L,"CastSpell",[](lua_State* S)->int { auto* C=GetContext(S); const uint32 Id=Resolve(S); if(C && C->ConnectionManager && Id) C->ConnectionManager->SendCastSpell(Id,C->ConnectionManager->GetTargetGuid()); return 0; });
    RegisterSpell(L,"GetNumShapeshiftForms",[](lua_State* S)->int { lua_pushinteger(S,Forms(GetContext(S)).Num()); return 1; });
    RegisterSpell(L,"GetShapeshiftFormInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); const uint32 Id=Form(S); const auto* Spell=FDbcStore::Get().Spells().GetById(Id); if(!Spell) { lua_pushnil(S); return 1; }
        const auto* Icon=FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
        bool Active=false; for(int32 I=0;I<3;++I) if(Spell->EffectApplyAuraName[I]==36) Active=P && P->GetFieldByte(UnitField::BYTES_2,3)==Spell->EffectMiscValue[I];
        lua_pushstring(S,Icon?TCHAR_TO_UTF8(*Icon->TexturePath):""); lua_pushstring(S,TCHAR_TO_UTF8(*Spell->SpellName)); lua_pushboolean(S,Active); lua_pushboolean(S,P && !P->IsDead()); lua_pushinteger(S,Id); return 5;
    });
    RegisterSpell(L,"GetShapeshiftForm",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const auto List=Forms(C); int32 Active=0;
        if(P) for(int32 I=0;I<List.Num();++I) { const auto* Spell=FDbcStore::Get().Spells().GetById(List[I]); for(int32 E=0;E<3;++E) if(Spell->EffectApplyAuraName[E]==36 && Spell->EffectMiscValue[E]==P->GetFieldByte(UnitField::BYTES_2,3)) Active=I+1; }
        lua_pushinteger(S,Active); return 1;
    });
    RegisterSpell(L,"GetShapeshiftFormCooldown",[](lua_State* S)->int { return PushSpellCooldown(S,Form(S)); });
    RegisterSpell(L,"CastShapeshiftForm",[](lua_State* S)->int { auto* C=GetContext(S); const uint32 Id=Form(S); if(C && C->ConnectionManager && Id) C->ConnectionManager->SendCastSpell(Id,C->EntityManager->LocalPlayerGuid); return 0; });
}
#else
void WowLuaApi::RegisterSpellApi(lua_State*) {}
int WowLuaApi::SetSpellTooltip(lua_State*) { return 0; }
int WowLuaApi::SetAuraTooltip(lua_State*) { return 0; }
int WowLuaApi::PushSpellCooldown(lua_State*,uint32) { return 0; }
#endif
