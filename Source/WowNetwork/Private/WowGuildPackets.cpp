#include "WowPacketHandler.h"

namespace
{
bool Text(FPacketReader& R,FString& Out,int32 Max)
{
    const int32 Start=R.Pos; Out=R.ReadCString();
    return R.Pos>Start && R.Data[R.Pos-1]==0 && R.Pos-Start<=Max+1;
}
}
void FWowPacketHandler::ResetGuildState()
{
    GuildRoster.Reset(); GuildRankAccess.Reset(); Guilds.Reset(); GuildQueries.Reset(); GuildEvents.Reset(); GuildLogs.Reset();
    GuildName.Empty(); GuildMotd.Empty(); GuildInfoText.Empty(); GuildInviter.Empty(); GuildInvitation.Empty();
    GuildCanRepair=GuildRepairRequested=false; GuildRepairId=0; GuildWithdrawMoney=0; GuildBankTabs=0; GuildBankTabNames.Reset(); GuildBankMemberAccess=FWowGuildRankAccess();
    ++GuildRevision; ++GuildLogRevision; ++GuildInviteRevision; ++GuildResultRevision;
    const uint32 Revision=Tabard.Revision+1,Result=Tabard.ResultRevision+1; Tabard=FTabardState(); Tabard.Revision=Revision; Tabard.ResultRevision=Result;
}
void FWowPacketHandler::HandleGuildQuery(FPacketReader& R)
{
    if(!R.CanRead(4)) return;
    FGuildMetadata G; G.Id=R.ReadU32(); if(!G.Id || !Text(R,G.Name,127) || G.Name.IsEmpty()) return;
    for(int32 I=0;I<10;++I) {FString Name; if(!Text(R,Name,127)) return; G.Ranks.Add(Name);}
    if(R.Remaining()!=24) return;
    for(uint32& Value:G.Emblem) Value=R.ReadU32();
    const uint32 Count=R.ReadU32(); if(Count<1 || Count>10) return; G.Ranks.SetNum(Count);
    const uint32 Id=G.Id; Guilds.Add(Id,MoveTemp(G)); GuildQueries.Remove(Id); ++GuildRevision;
    const auto* P=EntityManager.GetLocalPlayer(); if(P && P->GetField(PlayerField::GUILD_ID)==Id) GuildName=Guilds[Id].Name;
}
void FWowPacketHandler::HandleGuildRoster(FPacketReader& R)
{
    if(!R.CanRead(4)) return;
    const uint32 Count=R.ReadU32(); if(Count>5000) return;
    FString Motd,Info; if(!Text(R,Motd,2047) || !Text(R,Info,8191) || !R.CanRead(4)) return;
    const uint32 RankCount=R.ReadU32(); if(RankCount<1 || RankCount>10 || !R.CanRead(RankCount*56)) return;
    TArray<FWowGuildRankAccess> Ranks;
    for(uint32 I=0;I<RankCount;++I)
    {
        FWowGuildRankAccess Rank; Rank.Rights=R.ReadU32(); Rank.GoldPerDay=R.ReadU32();
        for(int32 T=0;T<6;++T) {Rank.TabRights[T]=R.ReadU32(); Rank.TabSlots[T]=R.ReadU32();} Ranks.Add(Rank);
    }
    TArray<FWowGuildMember> Rows; TSet<uint64> Seen;
    for(uint32 I=0;I<Count;++I)
    {
        if(!R.CanRead(9)) return;
        FWowGuildMember M; M.Guid=R.ReadU64(); M.Status=R.ReadU8();
        if(!M.Guid || Seen.Contains(M.Guid) || (M.Status&~7) || !Text(R,M.Name,48) || M.Name.IsEmpty() || !R.CanRead(11)) return;
        Seen.Add(M.Guid); const uint32 Rank=R.ReadU32(); if(Rank>=RankCount) return;
        M.RankId=uint8(Rank); M.Level=R.ReadU8(); M.Class=R.ReadU8(); M.Gender=R.ReadU8(); M.ZoneId=R.ReadU32();
        if(!M.Status) {if(!R.CanRead(4)) return; M.DaysOffline=R.ReadFloat(); if(!FMath::IsFinite(M.DaysOffline) || M.DaysOffline<0) return;}
        if(!Text(R,M.PublicNote,1023) || !Text(R,M.OfficerNote,1023)) return;
        Rows.Add(MoveTemp(M));
    }
    if(R.Remaining()) return;
    GuildRoster=MoveTemp(Rows); GuildRankAccess=MoveTemp(Ranks); GuildMotd=MoveTemp(Motd); GuildInfoText=MoveTemp(Info);
    GuildCanRepair=false;
    for(const auto& M:GuildRoster)
    {
        PlayerNameCache.Add(M.Guid,M.Name);
        if(M.Guid==EntityManager.LocalPlayerGuid) GuildCanRepair=M.RankId==0 || (GuildRankAccess[M.RankId].Rights&0x40000)!=0;
    }
    ++GuildRevision; OnGuildRosterUpdated.Broadcast(); if(Merchant.Open) OnVendorUpdated.Broadcast();
}
void FWowPacketHandler::HandleGuildEvent(FPacketReader& R)
{
    if(!R.CanRead(2)) return;
    FGuildEvent E; E.Type=R.ReadU8(); const uint8 Count=R.ReadU8(); if(E.Type>19 || Count>3) return;
    for(int32 I=0;I<Count;++I) {FString Value; if(!Text(R,Value,2047)) return; E.Text.Add(Value);}
    if(E.Type==3 || E.Type==4 || E.Type==12 || E.Type==13) {if(!R.CanRead(8)) return; E.Guid=R.ReadU64();}
    if(R.Remaining()) return;
    if(E.Type==2 && E.Text.Num()==1) GuildMotd=E.Text[0];
    if(E.Type==9 || E.Type==10 || E.Type==11) GuildQueries.Reset();
    E.Sequence=NextGuildEvent++; if(GuildEvents.Num()>=256) GuildEvents.RemoveAt(0); GuildEvents.Add(MoveTemp(E)); ++GuildRevision;
}
void FWowPacketHandler::HandleGuildInvite(FPacketReader& R)
{
    FString Inviter,Guild; if(!Text(R,Inviter,48) || !Text(R,Guild,127) || R.Remaining() || Inviter.IsEmpty() || Guild.IsEmpty()) return;
    GuildInviter=Inviter; GuildInvitation=Guild; ++GuildInviteRevision;
}
void FWowPacketHandler::HandleGuildResult(FPacketReader& R)
{
    if(!R.CanRead(4)) return;
    const uint32 Command=R.ReadU32(); FString Name;
    if(!Text(R,Name,127) || R.Remaining()!=4) return;
    const uint32 Code=R.ReadU32(); if(Command>25 || Code>50) return;
    GuildResultCommand=Command; GuildResultName=Name; GuildResultCode=Code; ++GuildResultRevision;
}
void FWowPacketHandler::HandleGuildLog(FPacketReader& R)
{
    if(!R.CanRead(1)) return;
    const uint8 Count=R.ReadU8(); TArray<FGuildLog> Logs;
    for(int32 I=0;I<Count;++I)
    {
        if(!R.CanRead(9)) return;
        FGuildLog E; E.Type=R.ReadU8(); E.First=R.ReadU64(); if(E.Type<1 || E.Type>6 || !E.First) return;
        if(E.Type!=2 && E.Type!=6) {if(!R.CanRead(8)) return; E.Second=R.ReadU64();}
        if(E.Type==3 || E.Type==4) {if(!R.CanRead(1)) return; E.Rank=R.ReadU8(); if(E.Rank>=10) return;}
        if(!R.CanRead(4)) return; E.Seconds=R.ReadU32(); Logs.Add(E);
    }
    if(R.Remaining()) return; GuildLogs=MoveTemp(Logs); ++GuildLogRevision;
}
void FWowPacketHandler::HandleGuildPermissions(FPacketReader& R)
{
    if(R.Remaining()!=61) return;
    const uint32 Rank=R.ReadU32(),Rights=R.ReadU32(),Gold=R.ReadU32(); const uint8 Tabs=R.ReadU8(); if(Rank>=10 || Tabs>6) return;
    FWowGuildRankAccess Access; Access.Rights=Rights; Access.GoldPerDay=Gold;
    for(int32 T=0;T<6;++T) {Access.TabRights[T]=R.ReadU32(); Access.TabSlots[T]=R.ReadU32();}
    GuildBankTabs=Tabs; GuildBankMemberAccess=Access; GuildCanRepair=Rank==0 || (Rights&0x40000)!=0; ++GuildRevision;
    // Remaining per-member withdrawals differ from rank limits; the roster owns editable rank access.
}
void FWowPacketHandler::HandleGuildBankMetadata(FPacketReader& R)
{
    if(!R.CanRead(14)) return; R.ReadU64(); const uint8 Tab=R.ReadU8(); R.ReadU32(); const uint8 Full=R.ReadU8(); if(Tab>=6 || Full>1) return;
    TArray<FGuildBankTabName> Names;
    if(!Tab && Full)
    {
        if(!R.CanRead(1)) return; const uint8 Count=R.ReadU8(); if(Count>6) return;
        for(int32 I=0;I<Count;++I) {FGuildBankTabName N; if(!Text(R,N.Name,127) || !Text(R,N.Icon,255)) return; Names.Add(MoveTemp(N));}
    }
    if(!R.CanRead(1)) return; const uint8 Count=R.ReadU8(); if(Count>98) return; TSet<uint8> Slots;
    for(int32 I=0;I<Count;++I)
    {
        if(!R.CanRead(5)) return; const uint8 Slot=R.ReadU8(); const uint32 Item=R.ReadU32(); if(Slot>=98 || Slots.Contains(Slot)) return; Slots.Add(Slot);
        if(Item)
        {
            if(!R.CanRead(8)) return; R.ReadU32(); const int32 Property=int32(R.ReadU32()); if(Property) {if(!R.CanRead(4)) return; R.ReadU32();}
            if(!R.CanRead(10)) return; R.ReadU32(); R.ReadU32(); R.ReadU8(); const uint8 Sockets=R.ReadU8(); if(Sockets>4 || !R.CanRead(Sockets*5)) return;
            for(int32 S=0;S<Sockets;++S) {if(R.ReadU8()>3) return; R.ReadU32();}
        }
    }
    if(R.Remaining()) return; if(!Tab && Full) {GuildBankTabNames=MoveTemp(Names); GuildBankTabs=uint8(GuildBankTabNames.Num()); ++GuildRevision;}
}
void FWowPacketHandler::HandleTabardOpen(FPacketReader& R)
{if(R.Remaining()!=8) return; const uint64 Guid=R.ReadU64(); if(!Guid) return; Tabard.Vendor=Guid; Tabard.Open=true; ++Tabard.Revision;}
void FWowPacketHandler::HandleTabardResult(FPacketReader& R)
{if(R.Remaining()!=4 || !Tabard.Pending) return; const uint32 Code=R.ReadU32(); if(Code>6) return; Tabard.Error=Code; Tabard.Pending=false; Tabard.HasResult=true; ++Tabard.ResultRevision; ++Tabard.Revision;}
