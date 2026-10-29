#include "WowMacros.h"
#include "../WowModifiedClicks.h"
#include "WowMacroRuntime.h"
#include "LuaApiRegistry.h"
#include "WowSavedVariables.h"
#include "WowAddonLoader.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "WowFrameManager.h"
#include "Formats/Dbc/DbcStore.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void Register(lua_State* L,const char* Name,lua_CFunction Fn) { lua_pushcfunction(L,Fn); lua_setglobal(L,Name); }
FWowTocData MacroToc()
{
    FWowTocData Toc; Toc.SavedVariables.Add(TEXT("IdleWowAccountMacros")); Toc.SavedVariablesPerCharacter.Add(TEXT("IdleWowCharacterMacros")); return Toc;
}
void SaveMacros(lua_State* L)
{
    lua_getfield(L,LUA_REGISTRYINDEX,"IdleWowMacrosLoaded"); const bool Loaded=lua_toboolean(L,-1)!=0; lua_pop(L,1);
    if(Loaded) FWowSavedVariables::SaveForAddon(L,TEXT("IdleWowMacros"),MacroToc());
}
const TArray<FString>& Icons()
{
    static TArray<FString> Values;
    static int32 SpellCount=-1,ItemCount=-1;
    const auto& Dbc=FDbcStore::Get();
    if(SpellCount!=Dbc.SpellIcons().Num() || ItemCount!=Dbc.ItemDisplayInfo().Num())
    {
        SpellCount=Dbc.SpellIcons().Num(); ItemCount=Dbc.ItemDisplayInfo().Num();
        Values={TEXT("Interface\\Icons\\INV_Misc_QuestionMark")}; TSet<FString> Seen; Seen.Add(Values[0].ToLower());
        const auto Add=[&](FString Path) { if(Path.IsEmpty()) return; if(!Path.Contains(TEXT("\\"))) Path=TEXT("Interface\\Icons\\")+Path; if(!Seen.Contains(Path.ToLower())) { Seen.Add(Path.ToLower()); Values.Add(Path); } };
        for(const auto& Row:Dbc.SpellIcons().GetAll()) Add(Row.TexturePath);
        for(const auto& Row:Dbc.ItemDisplayInfo().GetAll()) Add(Row.Icon1);
    }
    return Values;
}
uint32 ResolveSpell(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->ConnectionManager) return 0;
    FString Name=UTF8_TO_TCHAR(luaL_checkstring(L,1)); Name.TrimStartAndEndInline(); Name.RemoveFromStart(TEXT("!"));
    if(Name.IsEmpty() || Name.Equals(TEXT("null"),ESearchCase::IgnoreCase)) return 0;
    int32 Link=Name.Find(TEXT("spell:"));
    const uint32 Numeric=FCString::Strtoui64(*(Link>=0?Name.Mid(Link+6):Name),nullptr,10);
    if(Numeric && FDbcStore::Get().Spells().GetById(Numeric)) return Numeric;
    FString Rank; int32 Parenthesis;
    if(Name.FindChar('(',Parenthesis)) { Rank=Name.Mid(Parenthesis+1); Rank.RemoveFromEnd(TEXT(")")); Name=Name.Left(Parenthesis).TrimEnd(); }
    uint32 Known=0,Any=0;
    // Auto Attack is implicit and may be absent from SMSG_INITIAL_SPELLS.
    if(const auto* Attack=FDbcStore::Get().Spells().GetById(6603); Attack && Attack->SpellName.Equals(Name,ESearchCase::IgnoreCase) && Rank.IsEmpty()) Known=6603;
    for(const auto& Row:FDbcStore::Get().Spells().GetAll()) if(Row.SpellName.Equals(Name,ESearchCase::IgnoreCase) && (Rank.IsEmpty() || Row.Rank.Equals(Rank,ESearchCase::IgnoreCase)))
    { Any=FMath::Max(Any,Row.ID); if(C->ConnectionManager->PacketHandler.KnownSpells.Contains(Row.ID)) Known=FMath::Max(Known,Row.ID); }
    return Known?Known:Any;
}
struct FMacroItem { uint32 Id=0; uint64 Guid=0; uint8 Bag=255,Slot=0; };
FMacroItem ResolveItem(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->ConnectionManager) return {};
    auto& H=C->ConnectionManager->PacketHandler; const auto* Player=H.EntityManager.GetLocalPlayer(); if(!Player) return {};
    const FString Text=UTF8_TO_TCHAR(luaL_checkstring(L,1)); const int32 Link=Text.Find(TEXT("item:"));
    const uint32 Id=FCString::Strtoui64(*(Link>=0?Text.Mid(Link+5):Text),nullptr,10);
    const auto Candidate=[&](uint64 Guid,uint8 Bag,uint8 Slot,bool Force=false)->FMacroItem
    {
        const auto* Item=H.EntityManager.FindItem(Guid); if(!Item) return {};
        const auto* Template=H.ItemTemplates.Find(Item->Entry); C->ConnectionManager->RequestItemTemplate(Item->Entry);
        if(Force || (Id && Item->Entry==Id) || (Template && Template->Name.Equals(Text,ESearchCase::IgnoreCase))) return {Item->Entry,Guid,Bag,Slot};
        return {};
    };
    if(Link<0 && Id>=1 && Id<=19) return Candidate(Player->GetEquipmentItemGuid(Id-1),255,Id-1,true);
    for(uint8 I=0;I<19;++I) { const auto Value=Candidate(Player->GetEquipmentItemGuid(I),255,I); if(Value.Id) return Value; }
    for(uint8 I=0;I<16;++I) { const auto Value=Candidate(Player->GetBackpackItemGuid(I),255,I+23); if(Value.Id) return Value; }
    for(uint8 B=1;B<=4;++B) if(const auto* Bag=H.EntityManager.FindContainer(Player->GetBagGuid(B)))
        for(int32 I=0;I<Bag->GetNumSlots();++I) { const auto Value=Candidate(Bag->GetItemGuidAtSlot(I),B+18,I); if(Value.Id) return Value; }
    return {};
}
uint64 ExplicitTarget(lua_State* L,int32 Index,bool& Valid)
{
    Valid=true; if(!lua_isstring(L,Index)) return 0;
    const auto* Unit=WowLuaApi::ResolveUnitToken(L,Index); Valid=Unit!=nullptr; return Unit?Unit->Guid:0;
}
int CastInfo(lua_State* L,bool Channel)
{
    auto* C=WowLuaApi::GetContext(L); const auto* Unit=WowLuaApi::ResolveUnitToken(L); if(!C || !C->ConnectionManager || !Unit) return 0;
    const auto* Cast=C->ConnectionManager->PacketHandler.Casts.Find(Unit->Guid); if(!Cast || Cast->Channel!=Channel || Cast->Ends<=FPlatformTime::Seconds()) return 0;
    const auto* Spell=FDbcStore::Get().Spells().GetById(Cast->Spell); if(!Spell) return 0;
    lua_pushstring(L,TCHAR_TO_UTF8(*Spell->SpellName)); lua_pushstring(L,TCHAR_TO_UTF8(*Spell->Rank)); lua_pushstring(L,TCHAR_TO_UTF8(*Spell->SpellName));
    const auto* Icon=FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID); lua_pushstring(L,Icon?TCHAR_TO_UTF8(*Icon->TexturePath):nullptr);
    lua_pushnumber(L,Cast->Started*1000); lua_pushnumber(L,Cast->Ends*1000); lua_pushboolean(L,(Spell->Attributes&0x20)!=0);
    if(!Channel) lua_pushinteger(L,Cast->Counter); lua_pushboolean(L,false); return Channel?8:9;
}
void CancelAura(lua_State* L,bool Form)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->ConnectionManager) return; auto* N=C->ConnectionManager; const auto* Player=N->PacketHandler.EntityManager.GetLocalPlayer(); if(!Player) return;
    if(!Form) {const auto* Unit=WowLuaApi::ResolveUnitToken(L); if(!Unit || Unit->Guid!=Player->Guid) return;}
    const bool ByIndex=!Form && lua_isnumber(L,2); int32 Index=ByIndex?lua_tointeger(L,2):0;
    const FString Name=!Form && !ByIndex?UTF8_TO_TCHAR(luaL_optstring(L,2,"")):FString(),Rank=!Form?UTF8_TO_TCHAR(luaL_optstring(L,3,"")):FString();
    for(const auto& Aura:Player->Auras) if(Aura.bActive && !(Aura.Flags&0x80) && (!Aura.DurationMs || Aura.UpdatedAt+Aura.RemainingMs/1000.>FPlatformTime::Seconds()))
    {
        const auto* Spell=FDbcStore::Get().Spells().GetById(Aura.SpellId); if(!Spell) continue;
        if(ByIndex && Rank.Contains(TEXT("PLAYER")) && Aura.CasterGuid!=Player->Guid) continue;
        bool Match=ByIndex?--Index==0:!Form && Spell->SpellName.Equals(Name,ESearchCase::IgnoreCase) && (Rank.IsEmpty() || Spell->Rank.Equals(Rank,ESearchCase::IgnoreCase));
        if(Form) for(uint32 Type:Spell->EffectApplyAuraName) Match|=Type==36;
        if(Match) {if(Spell->Attributes&0x80000000u) return; TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Aura.SpellId),4); N->SendRawPacket(0x0136,Data); return;}
    }
}
}
void FWowMacros::RegisterApi(lua_State* L)
{
    Register(L,"GetNumMacroIcons",[](lua_State* S)->int { lua_pushinteger(S,Icons().Num()); return 1; });
    Register(L,"GetMacroIconInfo",[](lua_State* S)->int { const int32 I=luaL_checkinteger(S,1)-1; if(Icons().IsValidIndex(I)) lua_pushstring(S,TCHAR_TO_UTF8(*Icons()[I])); else lua_pushnil(S); return 1; });
    Register(L,"_IdleWowTextLength",[](lua_State* S)->int { const FString Text=UTF8_TO_TCHAR(luaL_checkstring(S,1)); lua_pushinteger(S,Text.Len()); return 1; });
    Register(L,"_IdleWowResolveMacroSpell",[](lua_State* S)->int { const uint32 Id=ResolveSpell(S); if(Id) lua_pushinteger(S,Id); else lua_pushnil(S); return 1; });
    Register(L,"_IdleWowResolveMacroItem",[](lua_State* S)->int { const auto Item=ResolveItem(S); if(Item.Id) lua_pushinteger(S,Item.Id); else lua_pushnil(S); return 1; });
    Register(L,"_IdleWowMacroItemSpell",[](lua_State* S)->int {const auto Item=ResolveItem(S); auto* C=WowLuaApi::GetContext(S); const auto* T=C && C->ConnectionManager?C->ConnectionManager->PacketHandler.ItemTemplates.Find(Item.Id):nullptr; if(T && T->UseSpellId) lua_pushinteger(S,T->UseSpellId); else lua_pushnil(S); return 1;});
    Register(L,"UnitCastingInfo",[](lua_State* S)->int{return CastInfo(S,false);}); Register(L,"UnitChannelInfo",[](lua_State* S)->int{return CastInfo(S,true);});
    Register(L,"SpellStopCasting",[](lua_State* S)->int {
        auto* C=WowLuaApi::GetContext(S); if(!C || !C->ConnectionManager) return 0;
        auto* N=C->ConnectionManager; const auto* Cast=N->PacketHandler.Casts.Find(N->PacketHandler.EntityManager.LocalPlayerGuid);
        const uint32 Spell=Cast?Cast->Spell:0; const bool Channel=Cast && Cast->Channel;
        TArray<uint8> Data; if(!Channel) Data.Add(Cast?Cast->Counter:uint8(0));
        Data.Append(reinterpret_cast<const uint8*>(&Spell),4); N->SendRawPacket(Channel?0x013B:0x012F,Data); return 0;
    });
    Register(L,"CancelUnitBuff",[](lua_State* S)->int{CancelAura(S,false); return 0;}); Register(L,"CancelShapeshiftForm",[](lua_State* S)->int{CancelAura(S,true); return 0;});
    Register(L,"EquipItemByName",[](lua_State* S)->int {auto* C=WowLuaApi::GetContext(S); if(!C || !C->ConnectionManager) return 0; auto* N=C->ConnectionManager; const auto Item=ResolveItem(S); const auto* T=N->PacketHandler.ItemTemplates.Find(Item.Id); if(!Item.Guid || !T || !T->InventoryType || N->IsItemInTrade(Item.Guid) || N->IsItemInOutgoingMail(Item.Guid)) return 0; if(lua_isnoneornil(S,2)) N->SendAutoEquipItem(Item.Bag,Item.Slot); else {const int32 Slot=luaL_checkinteger(S,2)-1; if(Slot>=0 && Slot<19) {TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Item.Guid),8); Data.Add(uint8(Slot)); N->SendRawPacket(0x010F,Data);}} return 0;});
    Register(L,"StartAttack",[](lua_State* S)->int {auto* C=WowLuaApi::GetContext(S); if(!C || !C->ConnectionManager) return 0; const auto* Unit=lua_isnoneornil(S,1) || (lua_isstring(S,1) && !*lua_tostring(S,1))?C->ConnectionManager->PacketHandler.EntityManager.Find(C->ConnectionManager->GetTargetGuid()):WowLuaApi::ResolveUnitToken(S); if(Unit && Unit->IsUnit() && Unit->GetHealth()>0 && !(WowLuaApi::CombatLogFlags(C,Unit->Guid)&0x10)) C->ConnectionManager->SendAttackSwing(Unit->Guid); return 0;});
    Register(L,"IsEquippedItemType",[](lua_State* S)->int {const FString Name=UTF8_TO_TCHAR(luaL_checkstring(S,1)); auto* C=WowLuaApi::GetContext(S); auto* N=C?C->ConnectionManager:nullptr; const auto* P=N?N->PacketHandler.EntityManager.GetLocalPlayer():nullptr; bool Found=false; if(P) for(uint8 I=0;I<19;++I) if(const auto* Item=N->PacketHandler.EntityManager.FindItem(P->GetEquipmentItemGuid(I))) {N->RequestItemTemplate(Item->Entry); if(const auto* T=N->PacketHandler.ItemTemplates.Find(Item->Entry)) {const auto* Class=FDbcStore::Get().ItemUi().ClassName(T->Class); const auto* Sub=FDbcStore::Get().ItemUi().SubclassName(T->Class,T->Subclass); Found|=(Class && Class->Equals(Name,ESearchCase::IgnoreCase)) || (Sub && Sub->Equals(Name,ESearchCase::IgnoreCase)); if(Name.Equals(TEXT("Shields"),ESearchCase::IgnoreCase)) Found|=T->InventoryType==14;}} lua_pushboolean(S,Found); return 1;});
    Register(L,"IsFlyableArea",[](lua_State* S)->int {auto* C=WowLuaApi::GetContext(S); const uint32 Map=C && C->ConnectionManager?C->ConnectionManager->PacketHandler.CurrentMapId:0; const auto* Area=FDbcStore::Get().AreaTable().GetById(WowLuaApi::ResolveAreaId(C)); lua_pushboolean(S,(Map==530 || Map==571) && Area && !(Area->Flags&0x20000000u)); return 1;});
    Register(L,"_IdleWowMacroVisualChanged",[](lua_State* S)->int {if(auto* C=WowLuaApi::GetContext(S); C && C->EventSystem) {C->EventSystem->FireEvent(TEXT("ACTIONBAR_SLOT_CHANGED"),{TEXT("0")}); C->EventSystem->FireEvent(TEXT("ACTIONBAR_UPDATE_STATE")); C->EventSystem->FireEvent(TEXT("ACTIONBAR_UPDATE_COOLDOWN"));} return 0;});
    Register(L,"_IdleWowMacroButton",[](lua_State* S)->int {const auto* C=WowLuaApi::GetContext(S); lua_pushstring(S,C && C->FrameManager?TCHAR_TO_UTF8(*C->FrameManager->CurrentMouseButton):"LeftButton"); return 1;});
    const lua_CFunction CastSpell=[](lua_State* S)->int {
        auto* C=WowLuaApi::GetContext(S); const uint32 Id=ResolveSpell(S); if(!C || !C->ConnectionManager || (Id!=6603 && !C->ConnectionManager->PacketHandler.KnownSpells.Contains(Id))) return 0;
        bool Valid; uint64 Target=ExplicitTarget(S,2,Valid); if(!Valid) return 0; if(!lua_isstring(S,2)) Target=WowModifiedClicks::ActionTarget(S,C->ConnectionManager->GetTargetGuid());
        if(Id==6603) { if(Target) C->ConnectionManager->SendAttackSwing(Target); } else C->ConnectionManager->SendCastSpell(Id,Target); return 0;
    };
    Register(L,"CastSpellByName",CastSpell); Register(L,"CastSpellByID",CastSpell);
    Register(L,"UseItemByName",[](lua_State* S)->int {
        auto* C=WowLuaApi::GetContext(S); const auto Item=ResolveItem(S); if(!C || !C->ConnectionManager || !Item.Id) return 0;
        bool Valid; uint64 Target=ExplicitTarget(S,2,Valid); if(!Valid) return 0; if(!lua_isstring(S,2)) Target=WowModifiedClicks::ActionTarget(S,Target);
        if(const auto* T=C->ConnectionManager->PacketHandler.ItemTemplates.Find(Item.Id))
        { if(T->UseSpellId) C->ConnectionManager->SendUseItem(Item.Bag,Item.Slot,Item.Guid,T->UseSpellId,Target); else if(T->InventoryType && (Item.Bag!=255 || Item.Slot>=19)) C->ConnectionManager->SendAutoEquipItem(Item.Bag,Item.Slot); }
        return 0;
    });
    Register(L,"_IdleWowPickupMacro",[](lua_State* S)->int { auto* C=WowLuaApi::GetContext(S); if(C && C->ConnectionManager) C->ConnectionManager->PickupMacroCursor(luaL_checkinteger(S,1)); return 0; });
    Register(L,"_IdleWowSaveMacros",[](lua_State* S)->int { SaveMacros(S); return 0; });
    Register(L,"_IdleWowExecuteMacroLine",[](lua_State* S)->int { auto* C=WowLuaApi::GetContext(S); if(C && C->EventSystem) C->EventSystem->FireEvent(TEXT("EXECUTE_CHAT_LINE"),{UTF8_TO_TCHAR(luaL_checkstring(S,1))}); return 0; });
    Register(L,"_IdleWowMacrosChanged",[](lua_State* S)->int {
        auto* C=WowLuaApi::GetContext(S); if(!C) return 0;
        const uint32 Deleted=luaL_optinteger(S,1,0);
        if(Deleted && C->ConnectionManager)
        {
            auto& Handler=C->ConnectionManager->PacketHandler;
            for(int32 I=0;I<Handler.ActionButtons.Num();++I) if((Handler.ActionButtons[I]>>24)==64 && (Handler.ActionButtons[I]&0xffffff)==Deleted)
            { C->ConnectionManager->SendClearActionButton(I); Handler.ActionButtons[I]=0; }
            Handler.OnActionButtonsUpdated.Broadcast();
        }
        SaveMacros(S);
        if(C->EventSystem) { C->EventSystem->FireEvent(TEXT("UPDATE_MACROS")); C->EventSystem->FireEvent(TEXT("ACTIONBAR_SLOT_CHANGED"),{TEXT("0")}); C->EventSystem->FireEvent(TEXT("ACTIONBAR_UPDATE_COOLDOWN")); }
        return 0;
    });
    if(luaL_dostring(L,WowMacroRuntime::Source)!=0) { UE_LOG(LogTemp,Error,TEXT("Macro runtime: %hs"),lua_tostring(L,-1)); lua_pop(L,1); }
}
void FWowMacros::LoadSavedVariables(lua_State* L)
{
    const FWowTocData Toc=MacroToc();
    FWowSavedVariables::RegisterAddon(TEXT("IdleWowMacros"),Toc); FWowSavedVariables::LoadForAddon(L,TEXT("IdleWowMacros"),Toc);
    lua_pushboolean(L,true); lua_setfield(L,LUA_REGISTRYINDEX,"IdleWowMacrosLoaded");
}
bool FWowMacros::RunWireMacro(lua_State* L,uint32 Id)
{
    if(!L) return false;
    const int32 Top=lua_gettop(L); lua_getglobal(L,"_IdleWowRunMacroWire"); lua_pushinteger(L,Id);
    bool Success=false;
    if(lua_pcall(L,1,1,0)==0) Success=lua_toboolean(L,-1)!=0;
    else UE_LOG(LogTemp,Warning,TEXT("Macro execution: %hs"),lua_tostring(L,-1));
    lua_settop(L,Top); return Success;
}
bool FWowMacros::SetActionTooltip(lua_State* L,int64 Handle,uint32 Id)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) return false;
    const int32 Top=lua_gettop(L); lua_getglobal(L,"_IdleWowMacroByWire"); lua_pushinteger(L,Id);
    if(lua_pcall(L,1,2,0)!=0 || !lua_istable(L,-2)) { lua_settop(L,Top); return false; }
    lua_getfield(L,-2,"name"); const FString Name=UTF8_TO_TCHAR(lua_tostring(L,-1)); lua_pop(L,1);
    const int32 Index=lua_tointeger(L,-1);
    lua_getglobal(L,"GetMacroSpell"); lua_pushinteger(L,Index);
    uint32 Spell=0; if(lua_pcall(L,1,3,0)==0) Spell=lua_tointeger(L,-1);
    lua_settop(L,Top);
    if(Spell) C->FrameManager->TooltipSetSpell(Handle,Spell);
    else C->FrameManager->TooltipSetText(Handle,Name);
    return true;
}
void FWowMacros::CastSent(lua_State* L,uint32 Spell,uint8 Counter)
{
    const auto* C=WowLuaApi::GetContext(L); if(C && C->EntityManager) WowLuaApi::NotifyCastState(L,C->EntityManager->LocalPlayerGuid,Spell,6,true,Counter);
}
void FWowMacros::CastResult(lua_State* L,uint32 Spell,uint8 Counter,bool Success,uint8 Reason)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->ConnectionManager || !C->EntityManager) return;
    auto& Requests=C->ConnectionManager->PacketHandler.LocalCastRequests; const auto* Requested=Requests.Find(Counter); const bool Match=Requested && *Requested==Spell;
    if(Match) Requests.Remove(Counter);
    WowLuaApi::NotifyCastState(L,C->EntityManager->LocalPlayerGuid,Spell,Success?1:Reason==40?7:2,Match,Counter);
    const int32 Top=lua_gettop(L); lua_getglobal(L,"_IdleWowMacroVisualChanged"); if(lua_isfunction(L,-1) && lua_pcall(L,0,0,0)) UE_LOG(LogTemp,Warning,TEXT("Macro icon update: %hs"),lua_tostring(L,-1)); lua_settop(L,Top);
}
#else
void FWowMacros::RegisterApi(lua_State*) {}
void FWowMacros::LoadSavedVariables(lua_State*) {}
bool FWowMacros::RunWireMacro(lua_State*,uint32) { return false; }
bool FWowMacros::SetActionTooltip(lua_State*,int64,uint32) { return false; }
void FWowMacros::CastSent(lua_State*,uint32,uint8) {}
void FWowMacros::CastResult(lua_State*,uint32,uint8,bool,uint8) {}
#endif
