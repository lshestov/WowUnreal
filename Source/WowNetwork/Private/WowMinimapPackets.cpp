#include "WowConnectionManager.h"

void FWowPacketHandler::HandleMinimapPing(FPacketReader& R)
{
    if(R.Remaining()!=16) return; FMinimapPing Ping; Ping.Guid=R.ReadU64(); Ping.Map=CurrentMapId; Ping.Position.X=R.ReadFloat(); Ping.Position.Y=R.ReadFloat();
    if(!Ping.Guid || (Ping.Guid!=EntityManager.LocalPlayerGuid && !GroupInfo.FindMember(Ping.Guid)) || Ping.Position.ContainsNaN() || Ping.Position.GetAbsMax()>20000) return;
    Ping.At=FPlatformTime::Seconds(); MinimapPing=Ping; ++MinimapPingRevision;
}

void FWowPacketHandler::HandleQuestgiverStatuses(FPacketReader& R)
{
    if(!R.CanRead(4)) return; const uint32 Count=R.ReadU32(); if(Count>4096 || R.Remaining()!=int32(Count)*9) return;
    TMap<uint64,uint8> Statuses;
    for(uint32 I=0;I<Count;++I) {const uint64 Guid=R.ReadU64(); const uint8 Status=R.ReadU8(); if(!Guid || Status>10 || Statuses.Contains(Guid)) return; Statuses.Add(Guid,Status);}
    QuestGiverStatuses=MoveTemp(Statuses); ++QuestGiverStatusRevision;
}
void UWowConnectionManager::RequestGameObjectTemplate(uint32 Entry,uint64 Guid)
{
    auto& H=PacketHandler; const double Now=FPlatformTime::Seconds();
    if(!Entry || GetState()!=EWowSessionState::WorldInGame || H.GameObjectTemplates.Contains(Entry) || H.MissingGameObjects.Contains(Entry) || H.GameObjectQueries.FindRef(Entry)>Now) return;
    H.GameObjectQueries.Add(Entry,Now+15); TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Entry),4); Data.Append(reinterpret_cast<const uint8*>(&Guid),8); SendRawPacket(0x05E,Data);
}
void FWowPacketHandler::HandleGameObjectQueryResponse(FPacketReader& R)
{
    if(!R.CanRead(4)) return; const uint32 Entry=R.ReadU32();
    if(Entry&0x80000000) {if(!R.Remaining() && GameObjectQueries.Contains(Entry&0x7fffffff)) {GameObjectQueries.Remove(Entry&0x7fffffff); MissingGameObjects.Add(Entry&0x7fffffff);} return;}
    if(!GameObjectQueries.Contains(Entry) || !R.CanRead(8)) return;
    FGameObjectTemplate T; T.Entry=Entry; T.Type=R.ReadU32(); T.Display=R.ReadU32(); if(T.Type>35) return;
    const auto ReadText=[&](FString* Out)->bool {int32 N=0; while(N<R.Remaining() && R.Data[R.Pos+N]) ++N; if(N==R.Remaining() || N>4096) return false; const FString Text=R.ReadCString(); if(Out) *Out=Text; return true;};
    if(!ReadText(&T.Name)) return; for(int32 I=0;I<6;++I) if(!ReadText(nullptr)) return;
    if(R.Remaining()!=24*4+4+6*4) return; for(uint32& Value:T.Data) Value=R.ReadU32(); T.Scale=R.ReadFloat();
    if(!FMath::IsFinite(T.Scale) || T.Scale<=0 || T.Scale>1000) return; R.Skip(6*4);
    GameObjectTemplates.Add(Entry,MoveTemp(T)); GameObjectQueries.Remove(Entry);
}
