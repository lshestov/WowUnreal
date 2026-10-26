#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"
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
void Register(lua_State* L,const char* Name,lua_CFunction Fn) { lua_pushcfunction(L,Fn); lua_setglobal(L,Name); }
void Close(FWowLuaContext* C)
{
    if(!C || !C->ConnectionManager || !C->ConnectionManager->PacketHandler.CurrentBankerGuid) return;
    C->ConnectionManager->CloseBank(); if(C->EventSystem) C->EventSystem->FireEvent(TEXT("BANKFRAME_CLOSED"));
}
}
void WowLuaApi::RegisterBankApi(lua_State* L)
{
    Register(L,"BankButtonIDToInvSlotID",[](lua_State* S)->int { const int32 Id=luaL_checkinteger(S,1); const bool Bag=lua_toboolean(S,2)!=0; lua_pushinteger(S,Id+(Bag?63:39)); return 1; });
    Register(L,"GetBankSlotCost",[](lua_State* S)->int { auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; lua_pushinteger(S,P?FDbcStore::Get().ItemUi().BankBagPrice(P->GetBankBagSlotCount()+1):0); return 1; });
    Register(L,"PurchaseSlot",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) C->ConnectionManager->SendBuyBankSlot(); return 0; });
    Register(L,"CloseBankFrame",[](lua_State* S)->int { Close(GetContext(S)); return 0; });
}
void WowLuaApi::UpdateBank(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->ConnectionManager) return;
    const auto Guid=C->ConnectionManager->PacketHandler.CurrentBankerGuid; if(!Guid) return;
    const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    if(!P || !P->GetHealth() || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame) { Close(C); return; }
    if(Guid==P->Guid) return; // The server's GM bank command uses the player GUID.
    const auto* N=C->EntityManager->FindUnit(Guid); if(!N) { Close(C); return; }
    if(C->ResolveActor)
    {
        const auto* A=C->ResolveActor(P->Guid); const auto* B=C->ResolveActor(Guid);
        if(IsValid(A) && IsValid(B) && FVector::DistSquared(A->GetActorLocation(),B->GetActorLocation())>FMath::Square(500.f)) Close(C);
    }
}
#else
void WowLuaApi::RegisterBankApi(lua_State*) {}
void WowLuaApi::UpdateBank(lua_State*) {}
#endif
