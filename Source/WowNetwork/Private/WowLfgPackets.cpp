#include "WowPacketHandler.h"
namespace
{
bool LfgText(FPacketReader& R,FString& S) {const int32 Start=R.Pos; S=R.ReadCString(); return R.Pos>Start && R.Data[R.Pos-1]==0 && R.Pos-Start<=256;}
bool Locks(FPacketReader& R,FWowLfgState::FLocks& Out)
{
    if(!R.CanRead(4)) return false; const uint32 Count=R.ReadU32(); if(Count>1024 || !R.CanRead(Count*8)) return false;
    for(uint32 I=0;I<Count;++I) {const uint32 Id=R.ReadU32()&0xFFFFFF,Reason=R.ReadU32(); if(!Id || !Reason || Out.Contains(Id)) return false; Out.Add(Id,Reason);} return true;
}
bool PartyLocks(FPacketReader& R,TMap<uint64,FWowLfgState::FLocks>& Out)
{
    if(!R.CanRead(1)) return false; const uint8 Count=R.ReadU8(); if(Count>40) return false;
    for(uint8 I=0;I<Count;++I) {if(!R.CanRead(8)) return false; const uint64 Guid=R.ReadU64(); FWowLfgState::FLocks Rows; if(!Guid || Out.Contains(Guid) || !Locks(R,Rows)) return false; Out.Add(Guid,MoveTemp(Rows));} return true;
}
bool Reward(FPacketReader& R,FWowLfgState::FReward& Out)
{
    if(!R.CanRead(18)) return false; const uint8 Done=R.ReadU8(); if(Done>1) return false; Out.Done=Done!=0;
    Out.Money=R.ReadU32(); Out.Experience=R.ReadU32(); Out.MoneyVariance=R.ReadU32(); Out.ExperienceVariance=R.ReadU32(); const uint8 Count=R.ReadU8(); if(Count>32 || !R.CanRead(Count*12)) return false;
    for(uint8 I=0;I<Count;++I) {FWowLfgState::FItem Item; Item.Id=R.ReadU32(); Item.Display=R.ReadU32(); Item.Count=R.ReadU32(); if(!Item.Id || !Item.Count) return false; Out.Items.Add(Item);} return true;
}
}
void FWowPacketHandler::RegisterLfgHandlers()
{
    Handlers.Add(0x36F,&FWowPacketHandler::HandleLfgPlayerInfo); Handlers.Add(0x372,&FWowPacketHandler::HandleLfgPartyInfo);
    Handlers.Add(0x367,&FWowPacketHandler::HandleLfgPlayerUpdate); Handlers.Add(0x368,&FWowPacketHandler::HandleLfgPartyUpdate);
    Handlers.Add(0x364,&FWowPacketHandler::HandleLfgJoinResult); Handlers.Add(0x365,&FWowPacketHandler::HandleLfgQueue); Handlers.Add(0x398,&FWowPacketHandler::HandleLfgDisabled);
}
void FWowPacketHandler::ResetLfgState()
{const uint32 Revision=Lfg.Revision+1,Info=Lfg.InfoRevision+1,Queue=Lfg.QueueRevision+1,Result=Lfg.ResultRevision+1; Lfg=FWowLfgState(); Lfg.Revision=Revision; Lfg.InfoRevision=Info; Lfg.QueueRevision=Queue; Lfg.ResultRevision=Result;}
void FWowPacketHandler::HandleLfgPlayerInfo(FPacketReader& R)
{
    if(!R.CanRead(1)) return; const uint8 Count=R.ReadU8(); if(Count>64) return; TMap<uint32,FWowLfgState::FReward> Rewards; FWowLfgState::FLocks Rows;
    for(uint8 I=0;I<Count;++I) {if(!R.CanRead(4)) return; const uint32 Id=R.ReadU32()&0xFFFFFF; FWowLfgState::FReward Item; if(!Id || Rewards.Contains(Id) || !Reward(R,Item)) return; Rewards.Add(Id,MoveTemp(Item));}
    if(!Locks(R,Rows) || R.Remaining()) return; Lfg.Rewards=MoveTemp(Rewards); Lfg.Locks=MoveTemp(Rows); Lfg.InfoReady=true; Lfg.Disabled=false; ++Lfg.InfoRevision;
}
void FWowPacketHandler::HandleLfgPartyInfo(FPacketReader& R)
{TMap<uint64,FWowLfgState::FLocks> Rows; if(!PartyLocks(R,Rows) || R.Remaining()) return; Lfg.PartyLocks=MoveTemp(Rows); ++Lfg.InfoRevision;}
void FWowPacketHandler::HandleLfgPlayerUpdate(FPacketReader& R) {ParseLfgUpdate(R,false);}
void FWowPacketHandler::HandleLfgPartyUpdate(FPacketReader& R) {ParseLfgUpdate(R,true);}
void FWowPacketHandler::ParseLfgUpdate(FPacketReader& R,bool Party)
{
    if(!R.CanRead(2)) return; FWowLfgState::FSelection Selection; Selection.Update=R.ReadU8(); const uint8 Has=R.ReadU8(); if(Has>1 || Selection.Update>16) return; Selection.Has=Has!=0;
    if(Has)
    {
        if(!R.CanRead(Party?8:4)) return; uint8 Joined=0,Queued=0;
        if(Party) {Joined=R.ReadU8(); Queued=R.ReadU8(); for(int32 I=0;I<5;++I) R.ReadU8();}
        else {Queued=R.ReadU8(); Joined=Queued; R.ReadU8(); R.ReadU8();}
        if(Joined>1 || Queued>1) return; Selection.Joined=Joined!=0; Selection.Queued=Queued!=0; const uint8 Count=R.ReadU8(); if(Count>50 || !R.CanRead(Count*4)) return;
        for(uint8 I=0;I<Count;++I) {const uint32 Id=R.ReadU32()&0xFFFFFF; if(!Id || Selection.Dungeons.Contains(Id)) return; Selection.Dungeons.Add(Id);}
        if(!LfgText(R,Selection.Comment)) return;
    }
    if(R.Remaining()) return; auto& Target=Party?Lfg.Party:Lfg.Player;
    Selection.QueuedAt=Selection.Queued?(Target.Queued && Target.QueuedAt>0?Target.QueuedAt:FPlatformTime::Seconds()):0;
    Target=MoveTemp(Selection); ++Lfg.Revision;
}
void FWowPacketHandler::HandleLfgJoinResult(FPacketReader& R)
{
    if(!R.CanRead(8)) return; const uint32 Result=R.ReadU32(),State=R.ReadU32(); if(Result>18 || State>8) return;
    TMap<uint64,FWowLfgState::FLocks> Rows; if((R.Remaining() && !PartyLocks(R,Rows)) || R.Remaining()) return;
    if(!Rows.IsEmpty()) {Lfg.PartyLocks=MoveTemp(Rows); ++Lfg.InfoRevision;}
    Lfg.Result=Result; Lfg.ResultState=State; ++Lfg.ResultRevision;
}
void FWowPacketHandler::HandleLfgQueue(FPacketReader& R)
{
    if(R.Remaining()!=31) return; FWowLfgState::FQueue Q; Q.Dungeon=R.ReadU32()&0xFFFFFF; if(!Q.Dungeon) return;
    Q.Average=int32(R.ReadU32()); Q.Wait=int32(R.ReadU32()); Q.TankWait=int32(R.ReadU32()); Q.HealerWait=int32(R.ReadU32()); Q.DamageWait=int32(R.ReadU32());
    Q.Tanks=R.ReadU8(); Q.Healers=R.ReadU8(); Q.Damage=R.ReadU8(); Q.Elapsed=R.ReadU32();
    if(Q.Tanks>1 || Q.Healers>1 || Q.Damage>3 || Q.Average<-1 || Q.Wait<-1 || Q.TankWait<-1 || Q.HealerWait<-1 || Q.DamageWait<-1) return;
    Q.At=FPlatformTime::Seconds(); Lfg.Queue=Q; ++Lfg.QueueRevision;
}
void FWowPacketHandler::HandleLfgDisabled(FPacketReader& R) {if(R.Remaining()) return; Lfg.Disabled=true; ++Lfg.Revision;}
