#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void Register(lua_State* L,const char* Name,lua_CFunction Function) { lua_pushcfunction(L,Function); lua_setglobal(L,Name); }
using FFaction=FDbcStore::FFaction;
TArray<const FFaction*> Rows(FWowLuaContext* C)
{
    TArray<const FFaction*> Result; if(!C || !C->ConnectionManager) return Result;
    const auto& State=C->ConnectionManager->PacketHandler.Reputations;
    for(const auto& F:FDbcStore::Get().Factions()) if(State.IsValidIndex(F.Index) && (State[F.Index].Flags&1) && !(State[F.Index].Flags&12)) Result.Add(&F);
    Result.Sort([&State](const auto& A,const auto& B) { const bool InactiveA=(State[A.Index].Flags&32)!=0,InactiveB=(State[B.Index].Flags&32)!=0; return InactiveA!=InactiveB?!InactiveA:A.Name<B.Name; });
    return Result;
}
const FFaction* Row(FWowLuaContext* C,int32 I) { const auto List=Rows(C); return List.IsValidIndex(I-1)?List[I-1]:nullptr; }
int32 Value(FWowLuaContext* C,const FFaction& F)
{
    const auto* Player=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    return C->ConnectionManager->PacketHandler.Reputations[F.Index].Standing+(Player?F.BaseFor(Player->GetRaceId(),Player->GetClassId()):0);
}
int32 Watched(FWowLuaContext* C)
{
    const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; return P?int32(P->GetField(PlayerField::BASE+0x043A)):-1;
}
void Standing(lua_State* L,int32 V)
{
    static const int32 Minimum[]={-42000,-6000,-3000,0,3000,9000,21000,42000,43000};
    V=FMath::Clamp(V,-42000,42999); int32 Rank=0; while(Rank<7 && V>=Minimum[Rank+1]) ++Rank;
    lua_pushinteger(L,Rank+1); lua_pushinteger(L,Minimum[Rank]); lua_pushinteger(L,Minimum[Rank+1]); lua_pushinteger(L,V);
}
void Update(FWowLuaContext* C) { if(C && C->EventSystem) C->EventSystem->FireEvent(TEXT("UPDATE_FACTION")); }
}
void WowLuaApi::RegisterReputationApi(lua_State* L)
{
    Register(L,"GetNumFactions",[](lua_State* S)->int { lua_pushinteger(S,Rows(GetContext(S)).Num()); return 1; });
    Register(L,"GetFactionInfoByID",[](lua_State* S)->int {
        auto* C=GetContext(S); const uint32 Id=luaL_checkinteger(S,1); const FFaction* Found=nullptr; for(const auto& F:FDbcStore::Get().Factions()) if(F.Id==Id) { Found=&F; break; }
        if(!Found || !C || !C->ConnectionManager || !C->ConnectionManager->PacketHandler.Reputations.IsValidIndex(Found->Index)) { lua_pushnil(S); return 1; }
        const auto& F=*Found; const auto Flags=C->ConnectionManager->PacketHandler.Reputations[F.Index].Flags;
        lua_pushstring(S,TCHAR_TO_UTF8(*F.Name)); lua_pushstring(S,TCHAR_TO_UTF8(*F.Description)); Standing(S,Value(C,F)); lua_pushboolean(S,(Flags&2)!=0); lua_pushboolean(S,!(Flags&16)); lua_pushboolean(S,0); lua_pushboolean(S,0); lua_pushboolean(S,1); lua_pushboolean(S,Watched(C)==F.Index); lua_pushboolean(S,0); lua_pushinteger(S,F.Id); return 14;
    });
    Register(L,"GetFactionInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* F=Row(C,luaL_checkinteger(S,1)); if(!F) { lua_pushnil(S); return 1; }
        const auto Flags=C->ConnectionManager->PacketHandler.Reputations[F->Index].Flags;
        lua_pushstring(S,TCHAR_TO_UTF8(*F->Name)); lua_pushstring(S,TCHAR_TO_UTF8(*F->Description)); Standing(S,Value(C,*F));
        lua_pushboolean(S,(Flags&2)!=0); lua_pushboolean(S,!(Flags&16) && F->Races[0]==1791); lua_pushboolean(S,0); lua_pushboolean(S,0); lua_pushboolean(S,1); lua_pushboolean(S,Watched(C)==F->Index); lua_pushboolean(S,0); lua_pushinteger(S,F->Id); return 14;
    });
    Register(L,"GetWatchedFactionInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); const int32 Index=Watched(C);
        for(const auto* F:Rows(C)) if(F->Index==Index) { lua_pushstring(S,TCHAR_TO_UTF8(*F->Name)); Standing(S,Value(C,*F)); return 5; }
        lua_pushnil(S); return 1;
    });
    Register(L,"SetSelectedFaction",[](lua_State* S)->int { auto* C=GetContext(S); if(C) C->SelectedRows.FindOrAdd(TEXT("faction"))=FMath::Clamp(int32(luaL_checkinteger(S,1)),0,Rows(C).Num()); return 0; });
    Register(L,"GetSelectedFaction",[](lua_State* S)->int { auto* C=GetContext(S); lua_pushinteger(S,C?C->SelectedRows.FindRef(TEXT("faction")):0); return 1; });
    Register(L,"SetWatchedFactionIndex",[](lua_State* S)->int {
        auto* C=GetContext(S); if(!C || !C->ConnectionManager) return 0; const int32 Requested=luaL_checkinteger(S,1); const auto* F=Row(C,Requested); if(Requested && !F) return 0;
        const int32 Index=F?F->Index:-1; TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Index),4); C->ConnectionManager->SendRawPacket(0x0318,Data); return 0;
    });
    Register(L,"IsFactionInactive",[](lua_State* S)->int { auto* C=GetContext(S); const auto* F=Row(C,luaL_checkinteger(S,1)); lua_pushboolean(S,F && (C->ConnectionManager->PacketHandler.Reputations[F->Index].Flags&32)); return 1; });
    Register(L,"FactionToggleAtWar",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* F=Row(C,luaL_checkinteger(S,1)); if(!F || F->Races[0]!=1791) return 0;
        auto& State=C->ConnectionManager->PacketHandler.Reputations[F->Index]; if((State.Flags&16) || Value(C,*F)<-3000) return 0;
        TArray<uint8> Data; const uint32 Index=F->Index; Data.Append(reinterpret_cast<const uint8*>(&Index),4); Data.Add((State.Flags&2)?0:1); C->ConnectionManager->SendRawPacket(0x0125,Data); State.Flags^=2; Update(C); return 0;
    });
    Register(L,"SetFactionInactive",[](lua_State* S)->int { auto* C=GetContext(S); const auto* F=Row(C,luaL_checkinteger(S,1)); if(F) { TArray<uint8> Data; const uint32 Index=F->Index; Data.Append(reinterpret_cast<const uint8*>(&Index),4); Data.Add(1); C->ConnectionManager->SendRawPacket(0x0317,Data); C->ConnectionManager->PacketHandler.Reputations[F->Index].Flags|=32; Update(C); } return 0; });
    Register(L,"SetFactionActive",[](lua_State* S)->int { auto* C=GetContext(S); const auto* F=Row(C,luaL_checkinteger(S,1)); if(F) { TArray<uint8> Data; const uint32 Index=F->Index; Data.Append(reinterpret_cast<const uint8*>(&Index),4); Data.Add(0); C->ConnectionManager->SendRawPacket(0x0317,Data); C->ConnectionManager->PacketHandler.Reputations[F->Index].Flags&=~32; Update(C); } return 0; });
}
#else
void WowLuaApi::RegisterReputationApi(lua_State*) {}
#endif
