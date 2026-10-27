#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"

uint32 WowLuaApi::ResolveAreaId(const FWowLuaContext* C)
{
    if(!C || !C->ConnectionManager || !C->EntityManager || !C->EntityManager->GetLocalPlayer()) return 0;
    const auto& H=C->ConnectionManager->PacketHandler; const auto& Areas=FDbcStore::Get().AreaTable();
    if(C->ResolveArea) if(const auto* A=Areas.GetById(C->ResolveArea()); A && A->MapID==H.CurrentMapId) return A->ID;
    if(H.ServerAreaMapId==H.CurrentMapId)
        if(const auto* A=Areas.GetById(H.ServerAreaId?H.ServerAreaId:H.ServerZoneId); A && A->MapID==H.CurrentMapId) return A->ID;
    return 0;
}

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
const FAreaTableDbcEntry* Area(lua_State* L) {return FDbcStore::Get().AreaTable().GetById(WowLuaApi::ResolveAreaId(WowLuaApi::GetContext(L)));}
const FAreaTableDbcEntry* Zone(const FAreaTableDbcEntry* A)
{
    for(int32 Depth=0;A && A->ParentAreaID && Depth<8;++Depth) A=FDbcStore::Get().AreaTable().GetById(A->ParentAreaID);
    return A;
}
void Register(lua_State* L,const char* Name,lua_CFunction Function) {lua_pushcfunction(L,Function); lua_setglobal(L,Name);}
int ZoneText(lua_State* L) {const auto* A=Zone(Area(L)); lua_pushstring(L,A?TCHAR_TO_UTF8(*A->Name):""); return 1;}
}
void WowLuaApi::RegisterMapApi(lua_State* L)
{
    Register(L,"GetZoneText",ZoneText); Register(L,"GetRealZoneText",ZoneText);
    Register(L,"GetSubZoneText",[](lua_State* S)->int {const auto* A=Area(S); const auto* Z=Zone(A); lua_pushstring(S,A && Z && A->ID!=Z->ID?TCHAR_TO_UTF8(*A->Name):""); return 1;});
    Register(L,"GetMinimapZoneText",[](lua_State* S)->int {const auto* A=Area(S); lua_pushstring(S,A?TCHAR_TO_UTF8(*A->Name):""); return 1;});
    Register(L,"GetZonePVPInfo",[](lua_State* S)->int {
        const auto* A=Area(S); const auto* Z=Zone(A); if(!A || !Z) return 0;
        const char* Type=A->Flags&0x800?"sanctuary":A->Flags&0x80?"arena":A->Flags&0x1000000?"combat":nullptr;
        if(Type) {lua_pushstring(S,Type); if(A!=Z) lua_pushinteger(S,1); else lua_pushnil(S); return 2;}
        const auto* C=GetContext(S); const auto* Player=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
        if(!C || !C->ConnectionManager || !Player) return 0;
        bool PvpRealm=false; for(const auto& R:C->ConnectionManager->GetCachedRealms()) if(R.Name==C->ConnectionManager->GetRealmName()) PvpRealm=R.Type==1 || R.Type==8;
        if(!PvpRealm && !(Z->Flags&0x10)) return 0;
        const auto* Race=FDbcStore::Get().ChrRaces().GetById(Player->GetRaceId()); const uint32 Team=Race && Race->FactionGroup==1?2:4;
        Type=A->Flags&0x40000?"contested":Z->Team&Team?"friendly":Z->Team && Z->Team!=6?"hostile":"contested";
        lua_pushstring(S,Type); lua_pushnil(S);
        const char* Faction=Z->Team==2?"FACTION_ALLIANCE":Z->Team==4?"FACTION_HORDE":nullptr;
        if(Faction) {lua_getglobal(S,Faction); if(!lua_isstring(S,-1)) {lua_pop(S,1); lua_pushstring(S,"");}} else lua_pushstring(S,""); return 3;
    });
}
void WowLuaApi::UpdateMapState(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->ConnectionManager || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame || !C->EventSystem) return;
    const auto* A=Area(L); const auto* Z=Zone(A); if(!A || !Z) return;
    const bool AreaChanged=C->LastAreaId!=A->ID,ZoneChanged=C->LastZoneId!=Z->ID;
    C->LastAreaId=A->ID; C->LastZoneId=Z->ID;
    if(ZoneChanged)
    {
        TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Z->ID),4); C->ConnectionManager->SendRawPacket(0x1F4,Data);
        C->EventSystem->FireEvent(TEXT("ZONE_CHANGED_NEW_AREA"));
    }
    if(AreaChanged) {C->EventSystem->FireEvent(TEXT("ZONE_CHANGED")); C->EventSystem->FireEvent(TEXT("MINIMAP_ZONE_CHANGED"));}
}
#else
void WowLuaApi::RegisterMapApi(lua_State*) {}
void WowLuaApi::UpdateMapState(lua_State*) {}
#endif
