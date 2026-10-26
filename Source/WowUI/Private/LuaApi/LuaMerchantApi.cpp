#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"
#include "WowEventSystem.h"
#include "WowFrameManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "GameFramework/Actor.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void Register(lua_State* L,const char* Name,lua_CFunction Fn) { lua_pushcfunction(L,Fn); lua_setglobal(L,Name); }
const FWowPlayerEntity* Player(FWowLuaContext* C) { return C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; }
const FWowVendorItem* Row(FWowLuaContext* C,int32 Index)
{
    const auto* M=C && C->ConnectionManager?&C->ConnectionManager->PacketHandler.Merchant:nullptr;
    return M && M->Open && M->Items.IsValidIndex(Index-1)?&M->Items[Index-1]:nullptr;
}
int32 Standing(FWowLuaContext* C,uint32 Faction)
{
    const auto* P=Player(C); if(!P || !C->ConnectionManager) return 0;
    for(const auto& F:FDbcStore::Get().Factions()) if(F.Id==Faction)
    {
        const auto& Reputations=C->ConnectionManager->PacketHandler.Reputations;
        return F.BaseFor(P->GetRaceId(),P->GetClassId())+(Reputations.IsValidIndex(F.Index)?Reputations[F.Index].Standing:0);
    }
    return 0;
}
int32 StandingRank(int32 Value)
{
    static const int32 Limits[]={-42000,-6000,-3000,0,3000,9000,21000,42000};
    int32 Rank=0; for(int32 I=1;I<8;++I) if(Value>=Limits[I]) Rank=I; return Rank;
}
const FWowUnitEntity* Npc(FWowLuaContext* C)
{
    return C && C->ConnectionManager && C->EntityManager && C->ConnectionManager->PacketHandler.Merchant.Open?C->EntityManager->FindUnit(C->ConnectionManager->PacketHandler.Merchant.Guid):nullptr;
}
bool CanRepair(FWowLuaContext* C) { const auto* U=Npc(C); return U && (U->GetNpcFlags()&WowNpcFlags::REPAIR)!=0; }
float Discount(FWowLuaContext* C)
{
    const auto* U=Npc(C); const uint32 Faction=U?FDbcStore::Get().ItemUi().FactionForTemplate(U->GetFactionTemplate()):0;
    return Faction?1.f-.05f*FMath::Max(0,StandingRank(Standing(C,Faction))-3):1.f;
}
TArray<int32> BuybackSlots(FWowLuaContext* C)
{
    TArray<int32> Slots; const auto* P=Player(C); if(!P || !C->ConnectionManager || !C->ConnectionManager->PacketHandler.Merchant.Open) return Slots;
    for(int32 I=0;I<12;++I) if(P->GetField64(PlayerField::BUYBACK_SLOT_START+I*2) && P->GetField(PlayerField::BUYBACK_PRICE_START+I)) Slots.Add(I);
    Slots.Sort([P](int32 A,int32 B) { const auto T1=P->GetField(PlayerField::BUYBACK_TIME_START+A),T2=P->GetField(PlayerField::BUYBACK_TIME_START+B); return T1==T2?A<B:T1<T2; }); return Slots;
}
TArray<const FWowItemEntity*> Inventory(FWowLuaContext* C)
{
    return C && C->EntityManager?C->EntityManager->GetInventoryItems():TArray<const FWowItemEntity*>();
}
void Close(FWowLuaContext* C)
{
    if(!C || !C->ConnectionManager) return;
    const bool WasOpen=C->ConnectionManager->PacketHandler.Merchant.Open;
    C->ConnectionManager->CloseMerchant();
    if(WasOpen && C->EventSystem) C->EventSystem->FireEvent(TEXT("MERCHANT_CLOSED"));
}
int MerchantInfo(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const auto* Item=Row(C,luaL_checkinteger(L,1)); const auto* T=Item?WowLuaApi::QueryItemTemplate(C,Item->ItemId):nullptr;
    if(T) lua_pushstring(L,TCHAR_TO_UTF8(*T->Name)); else lua_pushnil(L);
    if(Item) lua_pushstring(L,TCHAR_TO_UTF8(*WowLuaApi::ItemIconForUi(C,Item->ItemId,Item->DisplayId))); else lua_pushnil(L);
    lua_pushinteger(L,Item?Item->Price:0); lua_pushinteger(L,Item?FMath::Max(1u,Item->BuyCount):1); lua_pushinteger(L,Item?int32(Item->MaxCount):0);
    lua_pushboolean(L,!T || WowLuaApi::CanUseItem(C,*T));
    lua_pushboolean(L,Item && FDbcStore::Get().ItemUi().ExtendedCost(Item->ExtendedCost)); return 7;
}
int BuybackInfo(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); int32 Slot=-1; auto* Item=WowLuaApi::BuybackItemForUi(C,luaL_checkinteger(L,1),&Slot); const auto* T=Item?WowLuaApi::QueryItemTemplate(C,Item->Entry):nullptr;
    if(T) lua_pushstring(L,TCHAR_TO_UTF8(*T->Name)); else lua_pushnil(L);
    if(Item) lua_pushstring(L,TCHAR_TO_UTF8(*WowLuaApi::ItemIconForUi(C,Item->Entry))); else lua_pushnil(L);
    const auto* P=Player(C); lua_pushinteger(L,P && Slot>=74?P->GetField(PlayerField::BUYBACK_PRICE_START+Slot-74):0);
    lua_pushinteger(L,Item?FMath::Max(1,Item->GetStackCount()):1); lua_pushinteger(L,0); lua_pushboolean(L,!T || WowLuaApi::CanUseItem(C,*T)); return 6;
}
}
bool WowLuaApi::CanUseItem(FWowLuaContext* C,const FWowItemTemplate& T)
{
    const auto* P=Player(C); if(!P || !C->ConnectionManager) return false;
    if(uint32(FMath::Max(0,P->GetLevel()))<T.RequiredLevel) return false;
    if(P->GetClassId()>0 && P->GetClassId()<=32 && !(T.AllowableClass&(1u<<(P->GetClassId()-1)))) return false;
    if(P->GetRaceId()>0 && P->GetRaceId()<=32 && !(T.AllowableRace&(1u<<(P->GetRaceId()-1)))) return false;
    if(T.RequiredSpell && !C->ConnectionManager->PacketHandler.KnownSpells.Contains(T.RequiredSpell)) return false;
    if(T.RequiredSkill)
    {
        bool Found=false; for(int32 I=0;I<128;++I) if((P->GetField(PlayerField::SKILL_INFO+I*3)&0xffff)==T.RequiredSkill && (P->GetField(PlayerField::SKILL_INFO+I*3+1)&0xffff)>=T.RequiredSkillRank) {Found=true; break;} if(!Found) return false;
    }
    const auto& Handler=C->ConnectionManager->PacketHandler;
    if(T.Class==2 && T.Subclass<32 && Handler.HasWeaponProficiency && !(Handler.WeaponProficiency&(1u<<T.Subclass))) return false;
    if(T.Class==4 && T.Subclass>0 && T.Subclass<32 && Handler.HasArmorProficiency && !(Handler.ArmorProficiency&(1u<<T.Subclass))) return false;
    return !T.RequiredReputationFaction || StandingRank(Standing(C,T.RequiredReputationFaction))>=int32(T.RequiredReputationRank);
}
uint32 WowLuaApi::ItemRepairCost(FWowLuaContext* C,const FWowItemEntity& Item)
{
    if(!CanRepair(C)) return 0;
    const uint32 Max=Item.GetField(ItemField::MAX_DURABILITY),Current=Item.GetField(ItemField::DURABILITY); if(Current>=Max) return 0;
    const auto* T=QueryItemTemplate(C,Item.Entry); return T?FDbcStore::Get().ItemUi().RepairCost(T->ItemLevel,T->Quality,T->Class,T->Subclass,Max-Current,Discount(C)):0;
}
FWowItemEntity* WowLuaApi::BuybackItemForUi(FWowLuaContext* C,int32 Index,int32* Slot)
{
    const auto Slots=BuybackSlots(C); const auto* P=Player(C); if(!P || !Slots.IsValidIndex(Index-1)) return nullptr;
    const int32 I=Slots[Index-1]; if(Slot) *Slot=74+I;
    return C->EntityManager->FindItem(P->GetField64(PlayerField::BUYBACK_SLOT_START+I*2));
}
void WowLuaApi::UpdateMerchant(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->ConnectionManager || !C->ConnectionManager->PacketHandler.Merchant.Open) return;
    const auto* P=Player(C); const auto* U=Npc(C);
    if(!P || !U || !P->GetHealth() || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame) { Close(C); return; }
    auto& H=C->ConnectionManager->PacketHandler; const uint32 Guild=P->GetField(PlayerField::BASE+3);
    if(H.GuildRepairId!=Guild) { H.GuildRepairId=Guild; H.GuildRepairRequested=false; H.GuildCanRepair=false; H.GuildWithdrawMoney=0; }
    if(Guild && !H.GuildRepairRequested) { H.GuildRepairRequested=true; C->ConnectionManager->SendRawPacket(0x0089,{}); C->ConnectionManager->SendRawPacket(0x03FE,{}); }
    for(const auto* Item:Inventory(C)) if(Item->GetField(ItemField::FLAGS)&0x1000) C->ConnectionManager->RequestItemRefundInfo(Item->Guid);
    if(C->ResolveActor)
    {
        const auto* A=C->ResolveActor(P->Guid); const auto* B=C->ResolveActor(U->Guid);
        if(IsValid(A) && IsValid(B) && FVector::DistSquared(A->GetActorLocation(),B->GetActorLocation())>FMath::Square(1000.f)) Close(C);
    }
}
void WowLuaApi::RegisterMerchantApi(lua_State* L)
{
    Register(L,"GetMerchantNumItems",[](lua_State* S)->int {auto* C=GetContext(S); lua_pushinteger(S,C && C->ConnectionManager && C->ConnectionManager->PacketHandler.Merchant.Open?C->ConnectionManager->PacketHandler.Merchant.Items.Num():0); return 1;});
    Register(L,"GetMerchantItemInfo",MerchantInfo); Register(L,"GetBuybackItemInfo",BuybackInfo);
    Register(L,"GetMerchantItemLink",[](lua_State* S)->int {auto* C=GetContext(S); const auto* I=Row(C,luaL_checkinteger(S,1)); if(I && QueryItemTemplate(C,I->ItemId)) lua_pushstring(S,TCHAR_TO_UTF8(*ItemLinkForUi(C,I->ItemId))); else lua_pushnil(S); return 1;});
    Register(L,"GetMerchantItemMaxStack",[](lua_State* S)->int {auto* C=GetContext(S); const auto* I=Row(C,luaL_checkinteger(S,1)); const auto* T=I?QueryItemTemplate(C,I->ItemId):nullptr; lua_pushinteger(S,I && I->BuyCount<=1 && T?T->MaxStack:1); return 1;});
    Register(L,"GetMerchantItemCostInfo",[](lua_State* S)->int {auto* C=GetContext(S); const auto* I=Row(C,luaL_checkinteger(S,1)); const auto* Cost=I?FDbcStore::Get().ItemUi().ExtendedCost(I->ExtendedCost):nullptr; int32 Count=0; if(Cost) for(auto Id:Cost->Items) Count+=Id!=0; lua_pushinteger(S,Cost?Cost->Honor:0); lua_pushinteger(S,Cost?Cost->Arena:0); lua_pushinteger(S,Count); return 3;});
    Register(L,"GetMerchantItemCostItem",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* I=Row(C,luaL_checkinteger(S,1)); const auto* Cost=I?FDbcStore::Get().ItemUi().ExtendedCost(I->ExtendedCost):nullptr; int32 Index=luaL_checkinteger(S,2);
        if(Cost) for(int32 N=0;N<5;++N) if(Cost->Items[N] && --Index==0) { lua_pushstring(S,TCHAR_TO_UTF8(*ItemIconForUi(C,Cost->Items[N]))); lua_pushinteger(S,Cost->Counts[N]); lua_pushstring(S,TCHAR_TO_UTF8(*ItemLinkForUi(C,Cost->Items[N]))); return 3; }
        lua_pushnil(S); lua_pushinteger(S,0); return 2;
    });
    Register(L,"BuyMerchantItem",[](lua_State* S)->int {auto* C=GetContext(S); const auto* I=Row(C,luaL_checkinteger(S,1)); if(I) { C->ConnectionManager->ClearCursorPayload(); C->ConnectionManager->SendBuyVendorSlot(I->Slot,FMath::Clamp(int32(luaL_optinteger(S,2,1)),1,255)); } return 0;});
    Register(L,"PickupMerchantItem",[](lua_State* S)->int {auto* C=GetContext(S); if(C && C->ConnectionManager) { const int32 Index=luaL_checkinteger(S,1); if(Index==0) C->ConnectionManager->SellCursorToMerchant(); else C->ConnectionManager->PickupMerchantCursor(Index); } return 0;});
    Register(L,"GetNumBuybackItems",[](lua_State* S)->int {lua_pushinteger(S,BuybackSlots(GetContext(S)).Num()); return 1;});
    Register(L,"GetBuybackItemLink",[](lua_State* S)->int {auto* C=GetContext(S); auto* I=BuybackItemForUi(C,luaL_checkinteger(S,1)); if(I) lua_pushstring(S,TCHAR_TO_UTF8(*ItemLinkForUi(C,I->Entry))); else lua_pushnil(S); return 1;});
    Register(L,"BuybackItem",[](lua_State* S)->int {auto* C=GetContext(S); int32 Slot=-1; if(BuybackItemForUi(C,luaL_checkinteger(S,1),&Slot)) C->ConnectionManager->SendBuybackItem(Slot); return 0;});
    Register(L,"CloseMerchant",[](lua_State* S)->int {Close(GetContext(S)); return 0;});
    Register(L,"CanMerchantRepair",[](lua_State* S)->int {lua_pushboolean(S,CanRepair(GetContext(S))); return 1;});
    Register(L,"GetRepairAllCost",[](lua_State* S)->int {
        auto* C=GetContext(S); uint64 Cost=0;
        for(const auto* Item:Inventory(C)) Cost+=ItemRepairCost(C,*Item);
        lua_pushnumber(S,double(Cost)); lua_pushboolean(S,Cost>0); return 2;
    });
    Register(L,"RepairAllItems",[](lua_State* S)->int {auto* C=GetContext(S); if(CanRepair(C)) C->ConnectionManager->SendRepairItems(0,lua_toboolean(S,1)!=0); return 0;});
    Register(L,"InRepairMode",[](lua_State* S)->int {auto* C=GetContext(S); lua_pushboolean(S,C && C->ConnectionManager && C->ConnectionManager->IsRepairMode()); return 1;});
    Register(L,"ShowRepairCursor",[](lua_State* S)->int {auto* C=GetContext(S); if(CanRepair(C)) C->ConnectionManager->SetRepairMode(true); return 0;});
    Register(L,"HideRepairCursor",[](lua_State* S)->int {auto* C=GetContext(S); if(C && C->ConnectionManager) C->ConnectionManager->SetRepairMode(false); return 0;});
    // A guild-less player has no guild repair allowance. Guild rank/withdrawal
    // state is queried separately and filled by the guild UI packet handlers.
    Register(L,"CanGuildBankRepair",[](lua_State* S)->int {auto* C=GetContext(S); lua_pushboolean(S,C && C->ConnectionManager && C->ConnectionManager->PacketHandler.GuildCanRepair); return 1;});
    Register(L,"GetGuildBankWithdrawMoney",[](lua_State* S)->int {auto* C=GetContext(S); lua_pushnumber(S,C && C->ConnectionManager?C->ConnectionManager->PacketHandler.GuildWithdrawMoney:0); return 1;});
}
#else
void WowLuaApi::RegisterMerchantApi(lua_State*) {}
void WowLuaApi::UpdateMerchant(lua_State*) {}
bool WowLuaApi::CanUseItem(FWowLuaContext*,const FWowItemTemplate&) { return false; }
uint32 WowLuaApi::ItemRepairCost(FWowLuaContext*,const FWowItemEntity&) { return 0; }
FWowItemEntity* WowLuaApi::BuybackItemForUi(FWowLuaContext*,int32,int32*) { return nullptr; }
#endif
