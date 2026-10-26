#include "WowConnectionManager.h"
#include "Net/WowWorldSocket.h"

namespace
{
bool ReadChannelText(FPacketReader& R,FString& Text,int32 Limit=255)
{
    int32 Length=0; while(Length<R.Remaining() && R.Data[R.Pos+Length]) ++Length;
    if(Length==R.Remaining() || Length>Limit) return false;
    Text=R.ReadCString(); return true;
}
void Text(TArray<uint8>& Data,const FString& Value) {const FTCHARToUTF8 Bytes(*Value); Data.Append(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length()); Data.Add(0);}
int32 FreeNumber(const FWowPacketHandler& H)
{
    for(int32 Number=4;Number<=10;++Number)
    {
        bool Used=false; for(const auto& C:H.Channels) Used|=C.Number==Number;
        for(const auto& Pair:H.ChannelJoins) Used|=Pair.Value.Number==Number;
        if(!Used) return Number;
    }
    return 0;
}
bool ValidChannel(const FString& Name)
{
    const FTCHARToUTF8 Bytes(*Name); if(Name.IsEmpty() || Bytes.Length()>=100 || FChar::IsDigit(Name[0])) return false;
    for(TCHAR C:Name) if(C<' ' || C=='|' || C==0x7F) return false;
    return true;
}
}
FWowChannel* FWowPacketHandler::FindChannel(const FString& Name) {return Channels.FindByPredicate([&](const FWowChannel& C){return C.Name.Equals(Name,ESearchCase::IgnoreCase);});}
const FWowChannel* FWowPacketHandler::FindChannel(const FString& Name) const {return Channels.FindByPredicate([&](const FWowChannel& C){return C.Name.Equals(Name,ESearchCase::IgnoreCase);});}
void FWowPacketHandler::ResetChannels() {Channels.Empty(); ChannelJoins.Empty(); ChannelNotices.Empty(); ++ChannelRevision;}

bool UWowConnectionManager::JoinChannel(const FString& Name,const FString& Password,uint32 Id,int32 Number,int32 Window)
{
    auto& H=PacketHandler; const FTCHARToUTF8 Secret(*Password);
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !ValidChannel(Name) || Secret.Length()>31 || Password.Contains(TEXT("\n")) || H.FindChannel(Name) || H.ChannelJoins.Contains(Name.ToLower()) || (!Id && Name.Contains(TEXT(" ")))) return false;
    if(Number<1 || Number>10) Number=FreeNumber(H); if(!Number) return false;
    for(const auto& Row:H.Channels) if(Row.Number==Number) return false;
    for(const auto& Pair:H.ChannelJoins) if(Pair.Value.Number==Number) return false;
    TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Id),4); Data.Add(0); Data.Add(0); Text(Data,Name); Text(Data,Password);
    H.ChannelJoins.Add(Name.ToLower(),{Number,FMath::Clamp(Window,1,10),Id,FPlatformTime::Seconds()+15});
    WorldSocket->SendPacket(0x0097,Data); return true;
}
bool UWowConnectionManager::LeaveChannel(const FString& Name)
{
    auto* Row=PacketHandler.FindChannel(Name); if(!WorldSocket || State!=EWowSessionState::WorldInGame || !Row || Row->Leaving) return false;
    TArray<uint8> Data={0,0,0,0}; Text(Data,Row->Name); WorldSocket->SendPacket(0x0098,Data); Row->Leaving=true; Row->RequestAt=FPlatformTime::Seconds(); return true;
}
bool UWowConnectionManager::RequestChannelRoster(const FString& Name,bool Refresh)
{
    auto* Row=PacketHandler.FindChannel(Name); if(!WorldSocket || State!=EWowSessionState::WorldInGame || !Row || Row->Leaving || Row->RosterPending || (Row->RosterKnown && !Refresh)) return false;
    TArray<uint8> Data; Text(Data,Row->Name); WorldSocket->SendPacket(0x03D2,Data); Row->RosterPending=true; Row->RequestAt=FPlatformTime::Seconds(); return true;
}
bool UWowConnectionManager::ChannelCommand(uint32 Opcode,const FString& Name,const FString& Argument)
{
    auto* Row=PacketHandler.FindChannel(Name); if(!WorldSocket || State!=EWowSessionState::WorldInGame || !Row || Opcode<0x009C || Opcode>0x00A8) return false;
    const bool HasArgument=Opcode!=0x009E && Opcode!=0x00A7 && Opcode!=0x00A8;
    const FTCHARToUTF8 Bytes(*Argument); if(HasArgument && ((Argument.IsEmpty() && Opcode!=0x009C) || Bytes.Length()>(Opcode==0x009C?31:127) || Argument.Contains(TEXT("|")))) return false;
    TArray<uint8> Data; Text(Data,Row->Name); if(HasArgument) Text(Data,Argument); WorldSocket->SendPacket(Opcode,Data); return true;
}
void FWowPacketHandler::HandleChannelNotify(FPacketReader& R)
{
    if(!R.CanRead(1)) return;
    FWowChannelNotice Notice; Notice.Type=R.ReadU8(); if(Notice.Type>35 || !ReadChannelText(R,Notice.Channel,100)) return;
    const auto Guid=[&](uint64& Value){if(!R.CanRead(8)) return false; Value=R.ReadU64(); return true;};
    switch(Notice.Type)
    {
    case 0: case 1: case 7: case 8: case 13: case 14: case 15: case 16: case 23: case 24: case 34: case 35: if(!Guid(Notice.Actor)) return; break;
    case 2: if(!R.CanRead(9)) return; Notice.Flags=R.ReadU8(); Notice.Id=R.ReadU32(); R.ReadU32(); if(R.Remaining() && !ReadChannelText(R,Notice.Player)) return; break;
    case 3: if(!R.CanRead(5)) return; Notice.Id=R.ReadU32(); R.ReadU8(); break;
    case 9: case 11: case 22: case 29: case 30: if(!ReadChannelText(R,Notice.Player,127)) return; break;
    case 12: if(!Guid(Notice.Actor) || !R.CanRead(2)) return; Notice.OldFlags=R.ReadU8(); Notice.Flags=R.ReadU8(); break;
    case 18: case 20: case 21: if(!Guid(Notice.Target) || !Guid(Notice.Actor)) return; break;
    }
    if(R.Remaining()) return;
    const FString Key=Notice.Channel.ToLower(); auto* Row=FindChannel(Notice.Channel);
    if(const auto* Pending=ChannelJoins.Find(Key)) {Notice.Number=Pending->Number; Notice.Window=Pending->Window; Notice.Id=Pending->Id;}
    if(Notice.Type==2)
    {
        const auto* Pending=ChannelJoins.Find(Key); Notice.Number=Pending?Pending->Number:(Row?Row->Number:FreeNumber(*this)); Notice.Window=Pending?Pending->Window:1;
        if(!Notice.Number) return;
        if(!Row) {FWowChannel New; New.Name=Notice.Channel; New.Number=Notice.Number; Channels.Add(MoveTemp(New)); Row=&Channels.Last();}
        Row->Id=Notice.Id; Row->Flags=Notice.Flags; Row->Leaving=false; ChannelJoins.Remove(Key);
    }
    if(Row)
    {
        Notice.Number=Row->Number; Notice.Id=Row->Id;
        if(Notice.Type==12) {if(auto* Member=Row->Members.FindByPredicate([&](const FWowChannelMember& M){return M.Guid==Notice.Actor;})) Member->Flags=Notice.Flags;}
        if(Notice.Type==8) {for(auto& Member:Row->Members) {Member.Flags&=~uint8(1); if(Member.Guid==Notice.Actor) Member.Flags|=1;}}
        if(Notice.Type==3 || ((Notice.Type==18 || Notice.Type==20) && Notice.Target==EntityManager.LocalPlayerGuid)) Channels.RemoveAll([&](const FWowChannel& C){return C.Name.Equals(Notice.Channel,ESearchCase::IgnoreCase);});
    }
    if(Notice.Type==4 || Notice.Type==19 || Notice.Type==25 || Notice.Type==26 || Notice.Type==27 || Notice.Type==32 || Notice.Type==33) ChannelJoins.Remove(Key);
    Notice.ReceivedAt=FPlatformTime::Seconds();
    if(ChannelNotices.Num()>=128) ChannelNotices.RemoveAt(0); ChannelNotices.Add(MoveTemp(Notice)); ++ChannelRevision;
}
void FWowPacketHandler::HandleChannelList(FPacketReader& R)
{
    if(!R.CanRead(1)) return; R.ReadU8(); FString Name; if(!ReadChannelText(R,Name,100) || !R.CanRead(5)) return;
    const uint8 Flags=R.ReadU8(); const uint32 Count=R.ReadU32(); if(Count>10000 || R.Remaining()!=int64(Count)*9) return;
    TArray<FWowChannelMember> Members; Members.Reserve(Count); TSet<uint64> Seen;
    for(uint32 I=0;I<Count;++I) {FWowChannelMember Member; Member.Guid=R.ReadU64(); Member.Flags=R.ReadU8(); if(!Member.Guid || Seen.Contains(Member.Guid)) return; Seen.Add(Member.Guid); Members.Add(Member);}
    if(auto* Row=FindChannel(Name)) {Row->Members=MoveTemp(Members); Row->Flags=Flags; Row->Count=Count; Row->RosterKnown=true; Row->RosterPending=false; ++ChannelRevision;}
}
void FWowPacketHandler::HandleChannelUserList(FPacketReader& R,bool Remove)
{
    if(!R.CanRead(Remove?13:14)) return;
    const uint64 Guid=R.ReadU64(); const uint8 MemberFlags=Remove?0:R.ReadU8(),Flags=R.ReadU8(); const uint32 Count=R.ReadU32(); FString Name;
    if(!ReadChannelText(R,Name,100) || R.Remaining() || Count>10000 || !Guid) return;
    if(auto* Row=FindChannel(Name))
    {
        Row->Flags=Flags; Row->Count=Count;
        if(Remove) Row->Members.RemoveAll([&](const FWowChannelMember& Member){return Member.Guid==Guid;});
        else if(auto* Member=Row->Members.FindByPredicate([&](const FWowChannelMember& M){return M.Guid==Guid;})) Member->Flags=MemberFlags;
        else Row->Members.Add({Guid,MemberFlags});
        ++ChannelRevision;
    }
}
