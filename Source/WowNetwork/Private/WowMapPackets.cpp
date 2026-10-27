#include "WowConnectionManager.h"

void FWowPacketHandler::ResetMapState()
{
    ServerAreaMapId=MAX_uint32; ServerZoneId=ServerAreaId=0;
    QuestPois.Reset(); QuestPoiRequests.Reset(); GossipPoi=FGossipPoi(); WorldStates.Reset();
    CorpseMarker=FDeathMarker(); ReleaseMarker=FDeathMarker(); CorpseKnown=CorpseRequested=false; CorpseDeadline=0;
    ++WorldStateRevision; ++QuestPoiRevision; ++MapPoiRevision;
}
void UWowConnectionManager::RequestQuestPois()
{
    if(GetState()!=EWowSessionState::WorldInGame) return;
    auto& H=PacketHandler; TSet<uint32> Current; for(const auto& Q:H.QuestLog) if(Q.QuestId) Current.Add(Q.QuestId);
    for(auto It=H.QuestPois.CreateIterator();It;++It) if(!Current.Contains(It.Key())) It.RemoveCurrent();
    for(auto It=H.QuestPoiRequests.CreateIterator();It;++It) if(!Current.Contains(It.Key())) It.RemoveCurrent();
    TArray<uint32> Requests; const double Now=FPlatformTime::Seconds();
    for(uint32 Id:Current) if(!H.QuestPois.Contains(Id) && (!H.QuestPoiRequests.Contains(Id) || H.QuestPoiRequests[Id]<Now)) {Requests.Add(Id); if(Requests.Num()==25) break;}
    if(Requests.IsEmpty()) return;
    Requests.Sort(); const uint32 Count=Requests.Num(); TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Count),4);
    for(uint32 Id:Requests) {Data.Append(reinterpret_cast<const uint8*>(&Id),4); H.QuestPoiRequests.Add(Id,Now+15);}
    SendRawPacket(0x1E3,Data);
}
void FWowPacketHandler::HandleQuestPois(FPacketReader& R)
{
    if(!R.CanRead(4)) return; const uint32 Count=R.ReadU32(); if(Count>25) return;
    TMap<uint32,TArray<FQuestPoi>> Parsed; uint32 TotalPoints=0;
    for(uint32 I=0;I<Count;++I)
    {
        if(!R.CanRead(8)) return; const uint32 Quest=R.ReadU32(),PoiCount=R.ReadU32();
        if(!Quest || PoiCount>64 || Parsed.Contains(Quest)) return;
        auto& Rows=Parsed.Add(Quest); TSet<uint32> Indices;
        for(uint32 J=0;J<PoiCount;++J)
        {
            if(!R.CanRead(32)) return; FQuestPoi Poi; Poi.Id=R.ReadU32(); Poi.Objective=int32(R.ReadU32()); Poi.MapId=R.ReadU32(); Poi.AreaId=R.ReadU32(); Poi.Floor=R.ReadU32(); Poi.Priority=R.ReadU32(); Poi.Flags=R.ReadU32();
            const uint32 Points=R.ReadU32(); TotalPoints+=Points;
            if(Indices.Contains(Poi.Id) || Poi.Objective < -1 || Poi.Objective>31 || Points>4096 || TotalPoints>16384 || !R.CanRead(int32(Points)*8)) return;
            Indices.Add(Poi.Id); Poi.Points.Reserve(Points);
            for(uint32 P=0;P<Points;++P) {const int32 X=int32(R.ReadU32()),Y=int32(R.ReadU32()); if(FMath::Abs(int64(X))>20000 || FMath::Abs(int64(Y))>20000) return; Poi.Points.Add({X,Y});}
            Rows.Add(MoveTemp(Poi));
        }
    }
    if(R.Remaining()) return;
    bool Changed=false;
    for(auto& Pair:Parsed) if(QuestPoiRequests.Contains(Pair.Key) && QuestLog.ContainsByPredicate([&](const FWowQuestLogEntry& Q){return Q.QuestId==Pair.Key;}))
    {QuestPois.Add(Pair.Key,MoveTemp(Pair.Value)); QuestPoiRequests.Remove(Pair.Key); Changed=true;}
    if(Changed) ++QuestPoiRevision;
}
void FWowPacketHandler::HandleGossipPoi(FPacketReader& R)
{
    if(!R.CanRead(21)) return; FGossipPoi Poi; Poi.MapId=CurrentMapId; Poi.Flags=R.ReadU32(); Poi.Position.X=R.ReadFloat(); Poi.Position.Y=R.ReadFloat(); Poi.Icon=R.ReadU32(); R.ReadU32();
    int32 Length=0; while(Length<R.Remaining() && R.Data[R.Pos+Length]) ++Length;
    if(Length==R.Remaining() || Length>1024 || Poi.Position.ContainsNaN() || FMath::Abs(Poi.Position.X)>20000 || FMath::Abs(Poi.Position.Y)>20000) return;
    Poi.Name=R.ReadCString(); if(R.Remaining()) return; Poi.Present=true; GossipPoi=MoveTemp(Poi); ++MapPoiRevision;
}
void UWowConnectionManager::UpdateCorpseMarkers()
{
    if(GetState()!=EWowSessionState::WorldInGame) return;
    auto& H=PacketHandler; const auto* P=H.EntityManager.GetLocalPlayer(); if(!P) return;
    if(!P->IsDead() && !(P->GetField(PlayerField::FLAGS)&0x10))
    {
        if(H.CorpseMarker.Present || H.ReleaseMarker.Present) ++H.MapPoiRevision;
        H.CorpseMarker=FWowPacketHandler::FDeathMarker(); H.ReleaseMarker=FWowPacketHandler::FDeathMarker(); H.CorpseKnown=H.CorpseRequested=false; return;
    }
    if(H.CorpseKnown || (H.CorpseRequested && H.CorpseDeadline>FPlatformTime::Seconds())) return;
    H.CorpseRequested=true; H.CorpseDeadline=FPlatformTime::Seconds()+15; SendRawPacket(0x216,{});
}
void FWowPacketHandler::HandleCorpseQuery(FPacketReader& R)
{
    if(!R.CanRead(1)) return; const uint8 Found=R.ReadU8(); if(Found>1 || (Found && R.Remaining()!=24) || (!Found && R.Remaining())) return;
    FDeathMarker Marker;
    if(Found) {Marker.MapId=R.ReadU32(); Marker.Position={R.ReadFloat(),R.ReadFloat(),R.ReadFloat()}; Marker.CorpseMap=R.ReadU32(); R.ReadU32(); if(Marker.Position.ContainsNaN() || Marker.Position.GetAbsMax()>20000) return; Marker.Present=true;}
    if(!CorpseRequested) return; CorpseMarker=Marker; CorpseKnown=true; CorpseRequested=false; ++MapPoiRevision;
}
void FWowPacketHandler::HandleDeathRelease(FPacketReader& R)
{
    if(R.Remaining()!=16) return; FDeathMarker Marker; Marker.MapId=R.ReadU32(); Marker.Position={R.ReadFloat(),R.ReadFloat(),R.ReadFloat()};
    if(Marker.Position.ContainsNaN() || Marker.Position.GetAbsMax()>20000) return; Marker.Present=Marker.MapId!=MAX_uint32; ReleaseMarker=Marker; ++MapPoiRevision;
}
