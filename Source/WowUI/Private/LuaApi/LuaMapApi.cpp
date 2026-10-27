#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowMapUi.h"
#include "WowMinimapUi.h"

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
TPair<FString,FString> Names(lua_State* L)
{
    const auto* A=Area(L); const auto* Z=Zone(A); TPair<FString,FString> Result(Z?Z->Name:FString(),A && Z && A->ID!=Z->ID?A->Name:FString());
    if(auto* C=WowLuaApi::GetContext(L))
    {
        const auto& W=WowMinimapUi::View(C).Location;
        if(!W.ZoneName.IsEmpty() && W.ZoneName!=Result.Value) {Result.Key=W.ZoneName; Result.Value.Empty();}
        if(!W.SubzoneName.IsEmpty()) Result.Value=W.SubzoneName;
    }
    return Result;
}
}
void WowLuaApi::RegisterMapApi(lua_State* L)
{
    Register(L,"GetRealZoneText",ZoneText);
    Register(L,"GetZoneText",[](lua_State* S)->int {const auto N=Names(S); lua_pushstring(S,TCHAR_TO_UTF8(*N.Key)); return 1;});
    Register(L,"GetSubZoneText",[](lua_State* S)->int {const auto N=Names(S); lua_pushstring(S,TCHAR_TO_UTF8(*N.Value)); return 1;});
    Register(L,"GetMinimapZoneText",[](lua_State* S)->int {const auto N=Names(S); lua_pushstring(S,TCHAR_TO_UTF8(*(N.Value.IsEmpty()?N.Key:N.Value))); return 1;});
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
    C->ConnectionManager->RequestQuestPois();
    C->ConnectionManager->UpdateCorpseMarkers();
    const auto& H=C->ConnectionManager->PacketHandler; auto& V=WowMapUi::View(C);
    const auto* Player=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    uint32 Explored=0,Quests=0;
    if(Player) {for(int32 I=0;I<128;++I) Explored=HashCombineFast(Explored,Player->GetField(PlayerField::BASE+0x037D+I)); for(int32 I=0;I<125;++I) Quests=HashCombineFast(Quests,Player->GetField(PlayerField::BASE+0x000A+I));}
    const double Now=FPlatformTime::Seconds();
    for(const auto& Q:H.QuestLog)
    {
        const auto* T=H.QuestTemplates.Find(Q.QuestId); Quests=HashCombineFast(Quests,T?T->Id:0); if(!T) continue;
        for(uint32 Id:T->CreatureId) if(Id && !(Id&0x80000000))
        {
            const auto* Name=H.CreatureNameCache.Find(Id); Quests=HashCombineFast(Quests,Name?GetTypeHash(*Name):0);
            if(!Name && V.ObjectiveNameRequests.FindRef(Id)<=Now) {V.ObjectiveNameRequests.Add(Id,Now+15); C->ConnectionManager->SendCreatureQuery(Id,0);}
        }
        for(uint32 Id:T->ItemId) if(Id) {const auto* Item=H.ItemTemplates.Find(Id); Quests=HashCombineFast(Quests,Item?GetTypeHash(Item->Name):0);}
    }
    const bool MapChanged=V.WorldRevision!=H.WorldStateRevision || V.PoiRevision!=H.MapPoiRevision || V.ExplorationHash!=Explored;
    const bool QuestChanged=V.QuestRevision!=H.QuestPoiRevision || V.QuestHash!=Quests;
    V.WorldRevision=H.WorldStateRevision; V.PoiRevision=H.MapPoiRevision; V.ExplorationHash=Explored; V.QuestRevision=H.QuestPoiRevision; V.QuestHash=Quests;
    if(MapChanged) C->EventSystem->FireEvent(TEXT("WORLD_MAP_UPDATE"));
    if(QuestChanged) C->EventSystem->FireEvent(TEXT("QUEST_POI_UPDATE"));
    const auto* A=Area(L); const auto* Z=Zone(A); if(!A || !Z) return;
    const uint32 WmoHash=WowMinimapUi::View(C).WmoHash;
    const bool AreaChanged=C->LastAreaId!=A->ID || C->LastWmoHash!=WmoHash,ZoneChanged=C->LastZoneId!=Z->ID;
    C->LastAreaId=A->ID; C->LastZoneId=Z->ID;
    C->LastWmoHash=WmoHash;
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
