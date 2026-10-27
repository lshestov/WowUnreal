#include "WowPacketHandler.h"
namespace
{
bool Text(FPacketReader& R,FString& S) {const int32 Start=R.Pos; S=R.ReadCString(); return R.Pos>Start && R.Data[R.Pos-1]==0 && R.Pos-Start<=256;}
}
void FWowPacketHandler::RegisterPvpHandlers()
{
    Handlers.Add(0x2D4,&FWowPacketHandler::HandleBattlefieldStatus); Handlers.Add(0x23D,&FWowPacketHandler::HandleBattlefieldList);
    Handlers.Add(0x2E0,&FWowPacketHandler::HandlePvpScores); Handlers.Add(0x2E9,&FWowPacketHandler::HandlePvpPositions); Handlers.Add(0x2E8,&FWowPacketHandler::HandlePvpResult);
    Handlers.Add(0x34E,&FWowPacketHandler::HandleArenaRoster); Handlers.Add(0x35B,&FWowPacketHandler::HandleArenaStats); Handlers.Add(0x350,&FWowPacketHandler::HandleArenaInvite);
    Handlers.Add(0x349,&FWowPacketHandler::HandleArenaResult); Handlers.Add(0x357,&FWowPacketHandler::HandleArenaEvent);
    Handlers.Add(0x4DE,&FWowPacketHandler::HandleWorldPvpEntry); Handlers.Add(0x4E1,&FWowPacketHandler::HandleWorldPvpQueueInvite); Handlers.Add(0x4E4,&FWowPacketHandler::HandleWorldPvpQueued); Handlers.Add(0x4E0,&FWowPacketHandler::HandleWorldPvpEntered); Handlers.Add(0x4E6,&FWowPacketHandler::HandleWorldPvpLeft);
}
void FWowPacketHandler::ResetPvpState() {const uint32 Revision=Pvp.Revision+1,Sequence=Pvp.NextWorldEvent; Pvp=FWowPvpState(); Pvp.Revision=Revision; Pvp.NextWorldEvent=Sequence;}
void FWowPacketHandler::HandleBattlefieldStatus(FPacketReader& R)
{
    if(!R.CanRead(12)) return; const uint32 Slot=R.ReadU32(); if(Slot>=3) return; const uint64 Identity=R.ReadU64(); FWowPvpState::FQueue Q;
    if(Identity)
    {
        Q.Arena=Identity&255; Q.Type=(Identity>>16)&0xFFFFFFFFu; if(!Q.Type || (Q.Arena!=0 && Q.Arena!=2 && Q.Arena!=3 && Q.Arena!=5) || !R.CanRead(11)) return;
        Q.Min=R.ReadU8(); Q.Max=R.ReadU8(); Q.Instance=R.ReadU32(); const uint8 Rated=R.ReadU8(); if(Rated>1 || Q.Max<Q.Min) return; Q.Rated=Rated!=0; Q.Status=R.ReadU32();
        if(Q.Status==1) {if(R.Remaining()!=8) return; Q.Wait=R.ReadU32(); Q.Elapsed=R.ReadU32();}
        else if(Q.Status==2 || Q.Status==3)
        {if(R.Remaining()!=(Q.Status==2?16:21)) return; Q.Map=R.ReadU32(); R.ReadU64(); Q.Expiry=R.ReadU32(); if(Q.Status==3) {Q.Elapsed=R.ReadU32(); Q.Team=R.ReadU8(); if(Q.Team>1) return;}}
        else return;
    }
    if(R.Remaining()) return; Q.At=FPlatformTime::Seconds(); const bool WasActive=Pvp.Queues[Slot].Status==3; Pvp.Queues[Slot]=Q; ++Pvp.Revision;
    if(WasActive && Q.Status!=3) {Pvp.Scores.Reset(); Pvp.Positions.Reset(); Pvp.Flags.Reset(); Pvp.Winner=-1; ++Pvp.ScoreRevision; ++Pvp.PositionRevision;}
}
void FWowPacketHandler::HandleBattlefieldList(FPacketReader& R)
{
    if(!R.CanRead(17)) return; FWowPvpState::FList List; List.Vendor=R.ReadU64(); List.Source=R.ReadU8(); List.Type=R.ReadU32(); R.ReadU8(); R.ReadU8();
    const auto Rewards=[&](FWowPvpState::FRewards& V) {if(!R.CanRead(13)) return false; const uint8 Won=R.ReadU8(); if(Won>1) return false; V.Won=Won!=0; V.Honor=R.ReadU32(); V.Arena=R.ReadU32(); V.LossHonor=R.ReadU32(); return true;};
    if(!List.Type || List.Source>1 || !Rewards(List.Reward) || !R.CanRead(1)) return; const uint8 Random=R.ReadU8(); if(Random>1) return; List.Random=Random!=0;
    if(List.Random && !Rewards(List.RandomReward)) return; if(!R.CanRead(4)) return; const uint32 Count=R.ReadU32(); if(Count>1000 || R.Remaining()!=int32(Count*4)) return;
    TSet<uint32> Seen; for(uint32 I=0;I<Count;++I) {const uint32 Instance=R.ReadU32(); if(!Instance || Seen.Contains(Instance)) return; Seen.Add(Instance); List.Instances.Add(Instance);}
    Pvp.List=MoveTemp(List); ++Pvp.ListRevision;
    if(!Pvp.List.Source && Pvp.List.Vendor && GossipMenu.Guid==Pvp.List.Vendor) {GossipMenu=FWowGossipMenu(); OnGossipClosed.Broadcast();}
}
void FWowPacketHandler::HandlePvpScores(FPacketReader& R)
{
    if(!R.CanRead(1)) return; const uint8 Arena=R.ReadU8(); if(Arena>1) return; FWowPvpState::FTeam Teams[2];
    if(Arena) {if(!R.CanRead(24)) return; for(auto& T:Teams) {T.Lost=R.ReadU32(); T.Won=R.ReadU32(); T.Skill=R.ReadU32();} for(auto& T:Teams) if(!Text(R,T.Name)) return;}
    if(!R.CanRead(1)) return; const uint8 Ended=R.ReadU8(); if(Ended>1) return; int32 Winner=-1; if(Ended) {if(!R.CanRead(1)) return; Winner=R.ReadU8(); if(Winner>2) return;}
    if(!R.CanRead(4)) return; const uint32 Count=R.ReadU32(); if(Count>160) return; TArray<FWowPvpState::FScore> Scores; TSet<uint64> Seen;
    for(uint32 I=0;I<Count;++I)
    {
        if(!R.CanRead(Arena?25:36)) return; FWowPvpState::FScore S; S.Guid=R.ReadU64(); S.Blows=R.ReadU32(); if(!S.Guid || Seen.Contains(S.Guid)) return; Seen.Add(S.Guid);
        if(Arena) {S.Team=R.ReadU8(); if(S.Team>1) return;} else {S.Kills=R.ReadU32(); S.Deaths=R.ReadU32(); S.Honor=R.ReadU32();}
        S.Damage=R.ReadU32(); S.Healing=R.ReadU32(); const uint32 Extra=R.ReadU32(); if(Extra>8 || !R.CanRead(Extra*4)) return;
        for(uint32 J=0;J<Extra;++J) S.Extra.Add(R.ReadU32()); Scores.Add(MoveTemp(S));
    }
    if(R.Remaining()) return; Pvp.ScoreArena=Arena!=0; Pvp.Teams[0]=MoveTemp(Teams[0]); Pvp.Teams[1]=MoveTemp(Teams[1]); Pvp.Winner=Winner; Pvp.Scores=MoveTemp(Scores); ++Pvp.ScoreRevision;
}
void FWowPacketHandler::HandlePvpPositions(FPacketReader& R)
{
    TArray<FWowPvpState::FPosition> Players,Flags;
    const auto Read=[&](TArray<FWowPvpState::FPosition>& Out,uint32 Limit) {if(!R.CanRead(4)) return false; const uint32 Count=R.ReadU32(); if(Count>Limit || !R.CanRead(Count*16)) return false; for(uint32 I=0;I<Count;++I) {FWowPvpState::FPosition P; P.Guid=R.ReadU64(); const float X=R.ReadFloat(),Y=R.ReadFloat(); if(!P.Guid || !FMath::IsFinite(X) || !FMath::IsFinite(Y)) return false; P.World={X,Y}; Out.Add(P);} return true;};
    if(!Read(Players,160) || !Read(Flags,2) || R.Remaining()) return; Pvp.Positions=MoveTemp(Players); Pvp.Flags=MoveTemp(Flags); ++Pvp.PositionRevision;
}
void FWowPacketHandler::HandlePvpResult(FPacketReader& R)
{if(R.Remaining()!=4 && R.Remaining()!=12) return; const int32 Result=int32(R.ReadU32()); if(R.CanRead(8)) R.ReadU64(); Pvp.Result=Result; ++Pvp.ResultRevision;}
void FWowPacketHandler::HandleArenaRoster(FPacketReader& R)
{
    if(!R.CanRead(13)) return; const uint32 Id=R.ReadU32(); const uint8 Extra=R.ReadU8(); const uint32 Count=R.ReadU32(),Size=R.ReadU32(); if(!Id || Extra>1 || (Size!=2 && Size!=3 && Size!=5) || Count>Size*2) return;
    TArray<FWowPvpState::FArenaMember> Members; TSet<uint64> Seen;
    for(uint32 I=0;I<Count;++I)
    {
        if(!R.CanRead(9)) return; FWowPvpState::FArenaMember M; M.Guid=R.ReadU64(); const uint8 Online=R.ReadU8(); if(!M.Guid || Online>1 || Seen.Contains(M.Guid) || !Text(R,M.Name) || !R.CanRead(26+(Extra?8:0))) return;
        Seen.Add(M.Guid); M.Online=Online!=0; M.Rank=R.ReadU32(); M.Level=R.ReadU8(); M.Class=R.ReadU8(); if(M.Rank>1 || M.Level>80 || !M.Class || M.Class>11) return;
        M.Games=R.ReadU32(); M.Wins=R.ReadU32(); M.SeasonGames=R.ReadU32(); M.SeasonWins=R.ReadU32(); M.Rating=R.ReadU32(); if(Extra) {R.ReadFloat(); R.ReadFloat();} if(M.Wins>M.Games || M.SeasonWins>M.SeasonGames) return; Members.Add(MoveTemp(M));
    }
    if(R.Remaining() || (Pvp.ArenaTeams.Num()>=128 && !Pvp.ArenaTeams.Contains(Id))) return; auto& Team=Pvp.ArenaTeams.FindOrAdd(Id); Team.Size=Size; Team.Members=MoveTemp(Members); ++Pvp.ArenaRevision;
}
void FWowPacketHandler::HandleArenaStats(FPacketReader& R)
{
    if(R.Remaining()!=28) return; const uint32 Id=R.ReadU32(); if(!Id || (Pvp.ArenaTeams.Num()>=128 && !Pvp.ArenaTeams.Contains(Id))) return; auto Team=Pvp.ArenaTeams.FindRef(Id);
    Team.Rating=R.ReadU32(); Team.Games=R.ReadU32(); Team.Wins=R.ReadU32(); Team.SeasonGames=R.ReadU32(); Team.SeasonWins=R.ReadU32(); Team.Rank=R.ReadU32(); if(Team.Wins>Team.Games || Team.SeasonWins>Team.SeasonGames) return; Pvp.ArenaTeams.Add(Id,MoveTemp(Team)); ++Pvp.ArenaRevision;
}
void FWowPacketHandler::HandleArenaInvite(FPacketReader& R)
{FString Inviter,Name; if(!Text(R,Inviter) || !Text(R,Name) || R.Remaining() || Inviter.IsEmpty() || Name.IsEmpty()) return; Pvp.Inviter=MoveTemp(Inviter); Pvp.Invitation=MoveTemp(Name); ++Pvp.InviteRevision;}
void FWowPacketHandler::HandleArenaResult(FPacketReader& R)
{if(!R.CanRead(4)) return; const uint32 Action=R.ReadU32(); FString Team,Name; if(Action>14 || !Text(R,Team) || !Text(R,Name) || R.Remaining()!=4) return; const uint32 Error=R.ReadU32(); if(Error>30) return; Pvp.ArenaError=Error; Pvp.ArenaResultTeam=MoveTemp(Team); Pvp.ArenaResultPlayer=MoveTemp(Name); ++Pvp.ArenaResultRevision;}
void FWowPacketHandler::HandleArenaEvent(FPacketReader& R)
{
    if(!R.CanRead(2)) return; const uint8 Kind=R.ReadU8(),Count=R.ReadU8(); if(Kind<3 || Kind>8 || Count>3) return; for(int32 I=0;I<Count;++I) {FString S; if(!Text(R,S)) return;} if(R.Remaining()==8) R.ReadU64(); if(R.Remaining()) return;
    ++Pvp.ArenaRevision;
    // Member/captain changes contain names, not a replacement roster. Refresh
    // it on the server event so the stock details panel never retains old rows.
    if(const auto* Player=EntityManager.GetLocalPlayer()) for(int32 I=0;I<3;++I) if(const uint32 Id=Player->GetField(UnitField::END+0x454+I*7))
    {TArray<uint8> B; B.Append(reinterpret_cast<const uint8*>(&Id),4); OnSendPacket.ExecuteIfBound(0x34B,B); OnSendPacket.ExecuteIfBound(0x34D,B);}
}
void FWowPacketHandler::AddWorldPvpEvent(uint32 Id,uint32 Kind,uint32 A,uint32 B,uint32 C,uint32 D)
{if(Pvp.WorldEvents.Num()>=32) Pvp.WorldEvents.RemoveAt(0); Pvp.WorldEvents.Add({++Pvp.NextWorldEvent,Id,Kind,{A,B,C,D}}); ++Pvp.Revision;}
void FWowPacketHandler::HandleWorldPvpEntry(FPacketReader& R)
{if(R.Remaining()!=12) return; FWowPvpState::FWorldQueue Q; Q.Id=R.ReadU32(); Q.Zone=R.ReadU32(); Q.Expires=R.ReadU32(); Q.Status=2; if(!Q.Id || (Pvp.WorldQueues.Num()>=8 && !Pvp.WorldQueues.Contains(Q.Id))) return; Pvp.WorldQueues.Add(Q.Id,Q); AddWorldPvpEvent(Q.Id,0);}
void FWowPacketHandler::HandleWorldPvpQueueInvite(FPacketReader& R)
{if(R.Remaining()!=5) return; const uint32 Id=R.ReadU32(); const uint8 Warmup=R.ReadU8(); if(!Id || Warmup>1) return; AddWorldPvpEvent(Id,1,Warmup);}
void FWowPacketHandler::HandleWorldPvpQueued(FPacketReader& R)
{if(R.Remaining()!=11) return; const uint32 Id=R.ReadU32(),Zone=R.ReadU32(); const uint8 Accepted=R.ReadU8(),Login=R.ReadU8(),Warmup=R.ReadU8(); if(!Id || Accepted>1 || Login>1 || Warmup>1 || (Pvp.WorldQueues.Num()>=8 && !Pvp.WorldQueues.Contains(Id))) return; if(Accepted) Pvp.WorldQueues.Add(Id,{Id,Zone,1,0}); AddWorldPvpEvent(Id,2,Accepted,Warmup,ServerZoneId==Zone,Login);}
void FWowPacketHandler::HandleWorldPvpEntered(FPacketReader& R)
{if(R.Remaining()!=7) return; const uint32 Id=R.ReadU32(); const uint8 A=R.ReadU8(),B=R.ReadU8(),ClearAfk=R.ReadU8(); if(!Id || A>1 || B>1 || ClearAfk>1) return; Pvp.WorldQueues.Remove(Id); AddWorldPvpEvent(Id,3,ClearAfk);}
void FWowPacketHandler::HandleWorldPvpLeft(FPacketReader& R)
{if(R.Remaining()!=7) return; const uint32 Id=R.ReadU32(); const uint8 Reason=R.ReadU8(),Status=R.ReadU8(),Relocated=R.ReadU8(); if(!Id || Relocated>1 || Status>3 || Reason>16) return; Pvp.WorldQueues.Remove(Id); AddWorldPvpEvent(Id,4,Reason,Relocated,Status);}
