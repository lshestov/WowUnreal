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
    Handlers.Add(0x363,&FWowPacketHandler::HandleLfgRoles); Handlers.Add(0x2BB,&FWowPacketHandler::HandleLfgRoleChosen); Handlers.Add(0x361,&FWowPacketHandler::HandleLfgProposal);
    Handlers.Add(0x36D,&FWowPacketHandler::HandleLfgBoot); Handlers.Add(0x293,&FWowPacketHandler::HandleLfgContinue); Handlers.Add(0x200,&FWowPacketHandler::HandleLfgTeleport); Handlers.Add(0x1FF,&FWowPacketHandler::HandleLfgReward);
}
void FWowPacketHandler::ResetLfgState()
{
    FWowLfgState Fresh; Fresh.Revision=Lfg.Revision+1; Fresh.InfoRevision=Lfg.InfoRevision+1; Fresh.QueueRevision=Lfg.QueueRevision+1; Fresh.ResultRevision=Lfg.ResultRevision+1;
    Fresh.RoleRevision=Lfg.RoleRevision+1; Fresh.ChosenRevision=Lfg.ChosenRevision+1; Fresh.ProposalRevision=Lfg.ProposalRevision+1; Fresh.BootRevision=Lfg.BootRevision+1; Fresh.ContinueRevision=Lfg.ContinueRevision+1; Fresh.TeleportRevision=Lfg.TeleportRevision+1; Fresh.RewardRevision=Lfg.RewardRevision+1; Lfg=MoveTemp(Fresh);
}
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
    Target=MoveTemp(Selection);
    if((Target.Update==7 || Target.Update==8 || Target.Update==9 || Target.Update==10) && Lfg.Proposal.Valid && Lfg.Proposal.State==0) {Lfg.Proposal.State=Target.Update==10?2:1; Lfg.Proposal.At=FPlatformTime::Seconds(); ++Lfg.ProposalRevision;}
    if((Target.Update==4 || Target.Update==6 || Target.Update==7) && Lfg.RoleCheck.State==2) {Lfg.RoleCheck.State=Target.Update==6?6:5; ++Lfg.RoleRevision;}
    ++Lfg.Revision;
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
void FWowPacketHandler::HandleLfgRoles(FPacketReader& R)
{
    if(!R.CanRead(6)) return; FWowLfgState::FRoleCheck Check; Check.State=R.ReadU32(); const uint8 Initial=R.ReadU8(),Count=R.ReadU8();
    if(Check.State>6 || Initial>1 || Count>50 || !R.CanRead(Count*4+1)) return; Check.Initializing=Initial!=0;
    for(uint8 I=0;I<Count;++I) {const uint32 Id=R.ReadU32()&0xFFFFFF; if(!Id || Check.Dungeons.Contains(Id)) return; Check.Dungeons.Add(Id);}
    const uint8 Members=R.ReadU8(); if(Members>5 || !R.CanRead(Members*14)) return; TSet<uint64> Guids;
    for(uint8 I=0;I<Members;++I) {FWowLfgState::FRoleMember M; M.Guid=R.ReadU64(); const uint8 Ready=R.ReadU8(); M.Roles=R.ReadU32(); M.Level=R.ReadU8(); if(!M.Guid || Guids.Contains(M.Guid) || Ready>1 || (M.Roles&~15u) || M.Level>80) return; M.Ready=Ready!=0; Guids.Add(M.Guid); Check.Members.Add(M);}
    if(R.Remaining()) return; Lfg.RoleCheck=MoveTemp(Check); ++Lfg.RoleRevision; ++Lfg.Revision;
}
void FWowPacketHandler::HandleLfgRoleChosen(FPacketReader& R)
{
    if(R.Remaining()!=13) return; FWowLfgState::FRoleMember M; M.Guid=R.ReadU64(); const uint8 Ready=R.ReadU8(); M.Roles=R.ReadU32(); if(!M.Guid || Ready>1 || (M.Roles&~15u)) return; M.Ready=Ready!=0;
    Lfg.Chosen=M; ++Lfg.ChosenRevision; if(Lfg.ChosenEvents.Num()>=64) Lfg.ChosenEvents.RemoveAt(0); Lfg.ChosenEvents.Add({Lfg.ChosenRevision,M});
}
void FWowPacketHandler::HandleLfgProposal(FPacketReader& R)
{
    if(!R.CanRead(15)) return; FWowLfgState::FProposal P; P.Dungeon=R.ReadU32()&0xFFFFFF; P.State=R.ReadU8(); P.Id=R.ReadU32(); P.Encounters=R.ReadU32(); const uint8 Silent=R.ReadU8(),Count=R.ReadU8();
    if(!P.Dungeon || !P.Id || P.State>2 || Silent>1 || Count<1 || Count>5 || !R.CanRead(Count*9)) return; P.Silent=Silent!=0; int32 Self=0;
    for(uint8 I=0;I<Count;++I) {FWowLfgState::FProposalMember M; M.Roles=R.ReadU32(); uint8 Flags[5]; for(auto& F:Flags) {F=R.ReadU8(); if(F>1) return;} if((M.Roles&~15u) || !(M.Roles&14) || (Flags[4] && !Flags[3])) return; M.Self=Flags[0]!=0; M.InDungeon=Flags[1]!=0; M.SameGroup=Flags[2]!=0; M.Answered=Flags[3]!=0; M.Accepted=Flags[4]!=0; Self+=M.Self?1:0; P.Members.Add(M);}
    if(R.Remaining() || Self!=1) return; P.Valid=true; P.At=FPlatformTime::Seconds(); Lfg.Proposal=MoveTemp(P); ++Lfg.ProposalRevision; ++Lfg.Revision;
}
void FWowPacketHandler::HandleLfgBoot(FPacketReader& R)
{
    if(!R.CanRead(27)) return; FWowLfgState::FBoot B; const uint8 Progress=R.ReadU8(),Voted=R.ReadU8(),Agree=R.ReadU8(); if(Progress>1 || Voted>1 || Agree>1 || (Agree && !Voted)) return;
    B.Progress=Progress!=0; B.Voted=Voted!=0; B.Agree=Agree!=0; B.Victim=R.ReadU64(); B.Votes=R.ReadU32(); B.Agrees=R.ReadU32(); const uint32 Left=R.ReadU32(); B.Needed=R.ReadU32();
    if(!B.Victim || B.Votes>5 || B.Agrees>B.Votes || B.Needed>5 || !LfgText(R,B.Reason) || R.Remaining()) return;
    B.Deadline=FPlatformTime::Seconds()+(B.Progress?FMath::Min(Left,120u):0); Lfg.Boot=MoveTemp(B); ++Lfg.BootRevision;
}
void FWowPacketHandler::HandleLfgContinue(FPacketReader& R)
{if(R.Remaining()!=4) return; const uint32 Id=R.ReadU32()&0xFFFFFF; if(!Id) return; Lfg.ContinueDungeon=Id; ++Lfg.ContinueRevision;}
void FWowPacketHandler::HandleLfgTeleport(FPacketReader& R)
{if(R.Remaining()!=4) return; const uint32 Error=R.ReadU32(); if(Error>8) return; Lfg.TeleportError=Error; ++Lfg.TeleportRevision;}
void FWowPacketHandler::HandleLfgReward(FPacketReader& R)
{
    if(!R.CanRead(13)) return; const uint32 Random=R.ReadU32()&0xFFFFFF,Specific=R.ReadU32()&0xFFFFFF; const uint8 Done=R.ReadU8(); const uint32 Count=R.ReadU32();
    if(!Random || !Specific || Done>1 || Count!=1) return; FWowLfgState::FReward RewardData;
    // Reward() consumes the leading done flag; the final-reward packet inserts a count after it.
    TArray<uint8> Tail; Tail.Add(Done); Tail.Append(R.Data+R.Pos,R.Remaining()); FPacketReader Reader(Tail); if(!Reward(Reader,RewardData) || Reader.Remaining()) return;
    Lfg.RewardRandom=Random; Lfg.RewardSpecific=Specific; Lfg.FinalReward=MoveTemp(RewardData); ++Lfg.RewardRevision;
    // Completion is authoritative even when no separate party update follows.
    if(GroupInfo.IsLfgGroup() && (GroupInfo.LfgDungeon&0xFFFFFF)==Specific)
    {
        GroupInfo.LfgState=2; ++GroupRevision;
        Lfg.Player.Queued=false; Lfg.Party.Queued=false; Lfg.ContinueDungeon=0; ++Lfg.Revision;
    }
}
