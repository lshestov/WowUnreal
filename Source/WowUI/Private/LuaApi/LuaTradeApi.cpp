#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"
#include "GameFramework/Actor.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void Register(lua_State* L,const char* Name,lua_CFunction Fn) {lua_pushcfunction(L,Fn); lua_setglobal(L,Name);}
UWowConnectionManager* Connection(lua_State* L) {const auto* C=WowLuaApi::GetContext(L); return C?C->ConnectionManager:nullptr;}
const FWowTradeItem* Row(lua_State* L,bool Target)
{
    const auto* C=Connection(L); const int32 Index=luaL_checkinteger(L,1)-1; if(!C || !C->PacketHandler.CurrentTrade.bTradeOpen) return nullptr;
    const auto& Rows=Target?C->PacketHandler.CurrentTrade.TargetItems:C->PacketHandler.CurrentTrade.PlayerItems;
    return Rows.IsValidIndex(Index) && Rows[Index].ItemId?&Rows[Index]:nullptr;
}
int Info(lua_State* L,bool Target)
{
    auto* C=WowLuaApi::GetContext(L); const auto* R=Row(L,Target); const auto* T=R?WowLuaApi::QueryItemTemplate(C,R->ItemId):nullptr;
    if(!R) {lua_pushnil(L); lua_pushnil(L); lua_pushinteger(L,0); lua_pushinteger(L,0); if(Target) lua_pushnil(L); lua_pushnil(L); return Target?6:5;}
    lua_pushstring(L,T?TCHAR_TO_UTF8(*T->Name):"Retrieving item information..."); lua_pushstring(L,TCHAR_TO_UTF8(*WowLuaApi::ItemIconForUi(C,R->ItemId,R->DisplayId)));
    lua_pushinteger(L,R->Count); lua_pushinteger(L,T && T->InventoryType?int32(T->Quality):-1);
    if(Target) {if(!T || WowLuaApi::CanUseItem(C,*T)) lua_pushinteger(L,1); else lua_pushnil(L);}
    const auto& Trade=Connection(L)->PacketHandler.CurrentTrade; const uint32 Enchant=Target?Trade.PlayerSpell:Trade.TargetSpell;
    const auto* Spell=R->Slot==6?FDbcStore::Get().Spells().GetById(Enchant):nullptr;
    if(Spell) lua_pushstring(L,TCHAR_TO_UTF8(*Spell->SpellName)); else lua_pushnil(L); return Target?6:5;
}
int Link(lua_State* L,bool Target)
{
    const auto* R=Row(L,Target); if(!R) {lua_pushnil(L); return 1;}
    FString Value=WowLuaApi::ItemLinkForUi(WowLuaApi::GetContext(L),R->ItemId);
    const auto Gem=[](uint32 Id){const auto* Enchant=FDbcStore::Get().ItemUi().Enchant(Id); return Enchant?Enchant->Gem:0;};
    const FString Empty=FString::Printf(TEXT("item:%u:0:0:0:0:0:0:0:0"),R->ItemId);
    Value.ReplaceInline(*Empty,*FString::Printf(TEXT("item:%u:%u:%u:%u:%u:0:%d:%u:0"),R->ItemId,R->PermanentEnchantId,Gem(R->GemEnchantId1),Gem(R->GemEnchantId2),Gem(R->GemEnchantId3),R->RandomPropertyId,R->SuffixFactor));
    lua_pushstring(L,TCHAR_TO_UTF8(*Value)); return 1;
}
int TargetTrade(lua_State* L,bool Confirm)
{
    auto* C=Connection(L); if(!C) return 0;
    if(C->GetCursorMoney()) {C->AddCursorTradeMoney(); return 0;}
    if(!Confirm && luaL_checkinteger(L,1)!=7) return 0;
    const auto& T=C->PacketHandler.CurrentTrade;
    if(!C->HasPendingItemSpell() || !T.bTradeOpen || !T.TargetItems.IsValidIndex(6) || !T.TargetItems[6].ItemId) return 0;
    if(!Confirm) if(const auto* Spell=FDbcStore::Get().Spells().GetById(C->PendingItemSpell.Spell)) for(int32 I=0;I<3;++I) if(Spell->Effect[I]==53)
    {
        const auto* Old=FDbcStore::Get().ItemUi().Enchant(T.TargetItems[6].PermanentEnchantId);
        const auto* New=FDbcStore::Get().ItemUi().Enchant(Spell->EffectMiscValue[I]);
        if(Old && New) {const auto* Context=WowLuaApi::GetContext(L); if(Context && Context->EventSystem) Context->EventSystem->FireEvent(TEXT("TRADE_REPLACE_ENCHANT"),{Old->Name,New->Name}); return 0;}
    }
    C->TargetItemSpell(0,true); return 0;
}
}
void WowLuaApi::RegisterTradeApi(lua_State* L)
{
    Register(L,"GetTradePlayerItemInfo",[](lua_State* S)->int{return Info(S,false);}); Register(L,"GetTradeTargetItemInfo",[](lua_State* S)->int{return Info(S,true);});
    Register(L,"GetTradePlayerItemLink",[](lua_State* S)->int{return Link(S,false);}); Register(L,"GetTradeTargetItemLink",[](lua_State* S)->int{return Link(S,true);});
    Register(L,"GetPlayerTradeMoney",[](lua_State* S)->int{const auto* C=Connection(S); const auto* T=C?&C->PacketHandler.CurrentTrade:nullptr; lua_pushinteger(S,T && T->bTradeOpen?T->PlayerMoney:0); return 1;});
    Register(L,"GetTargetTradeMoney",[](lua_State* S)->int{const auto* C=Connection(S); lua_pushinteger(S,C && C->PacketHandler.CurrentTrade.bTradeOpen?C->PacketHandler.CurrentTrade.TargetMoney:0); return 1;});
    Register(L,"AcceptTrade",[](lua_State* S)->int{if(auto* C=Connection(S)) C->SendAcceptTrade(); return 0;});
    Register(L,"CancelTradeAccept",[](lua_State* S)->int{if(auto* C=Connection(S)) C->SendUnacceptTrade(); return 0;});
    Register(L,"CloseTrade",[](lua_State* S)->int{if(auto* C=Connection(S)) C->SendCancelTrade(); return 0;});
    Register(L,"SetTradeMoney",[](lua_State* S)->int{const double Value=luaL_checknumber(S,1); if(FMath::IsFinite(Value) && Value>=0 && Value<=MAX_int32) if(auto* C=Connection(S)) C->SendSetTradeGold(int32(Value)); return 0;});
    Register(L,"ClickTradeButton",[](lua_State* S)->int{if(auto* C=Connection(S)) {if(C->GetCursorMoney()) C->AddCursorTradeMoney(); else C->ClickTradeItem(luaL_checkinteger(S,1)-1,lua_toboolean(S,2)!=0);} return 0;});
    Register(L,"ClickTargetTradeButton",[](lua_State* S)->int{return TargetTrade(S,false);});
    Register(L,"ReplaceTradeEnchant",[](lua_State* S)->int{return TargetTrade(S,true);});
    Register(L,"GetCursorMoney",[](lua_State* S)->int{const auto* C=Connection(S); lua_pushinteger(S,C?C->GetCursorMoney():0); return 1;});
    Register(L,"PickupPlayerMoney",[](lua_State* S)->int{const double Value=luaL_checknumber(S,1); if(FMath::IsFinite(Value) && Value>0 && Value<=MAX_int32) if(auto* C=Connection(S)) C->PickupCursorMoney(int32(Value)); return 0;});
    Register(L,"PickupTradeMoney",[](lua_State* S)->int{const double Value=luaL_checknumber(S,1); if(FMath::IsFinite(Value) && Value>0 && Value<=MAX_int32) if(auto* C=Connection(S)) C->PickupCursorMoney(int32(Value),true); return 0;});
    Register(L,"AddTradeMoney",[](lua_State* S)->int{if(auto* C=Connection(S)) C->AddCursorTradeMoney(); return 0;});
    Register(L,"InitiateTrade",[](lua_State* S)->int{auto* C=Connection(S); if(!C) return 0; const auto* Unit=lua_isnoneornil(S,1)?C->PacketHandler.EntityManager.Find(uint64(C->GetTargetGuid())):ResolveUnitToken(S); if(Unit && Unit->IsPlayer()) C->SendInitiateTrade(int64(Unit->Guid)); return 0;});
}
void WowLuaApi::UpdateTrade(lua_State* L)
{
    auto* C=GetContext(L); auto* N=C?C->ConnectionManager:nullptr; if(!N) return; N->TickTradeState();
    const auto& T=N->PacketHandler.CurrentTrade; if(!T.bTradeOpen || !C->ResolveActor) return;
    const auto* A=C->ResolveActor(N->PacketHandler.EntityManager.LocalPlayerGuid); const auto* B=C->ResolveActor(T.TraderGuid);
    if(IsValid(A) && IsValid(B) && FVector::DistSquared(A->GetActorLocation(),B->GetActorLocation())>FMath::Square(1111.f)) N->SendCancelTrade();
}
#else
void WowLuaApi::RegisterTradeApi(lua_State*) {}
void WowLuaApi::UpdateTrade(lua_State*) {}
#endif
