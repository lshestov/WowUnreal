#include "WowPacketHandler.h"

namespace
{
bool Text(FPacketReader& R,FString& Value)
{
    const int32 Start=R.Pos; Value=R.ReadCString();
    return R.Pos>Start && R.Pos-Start<=256 && R.Data[R.Pos-1]==0;
}
bool Group(FPacketReader& R,FWowLfgState::FRaidGroup& Row)
{
    if(!R.CanRead(12)) return false; Row.Guid=R.ReadU64(); const uint32 Flags=R.ReadU32();
    if(!Row.Guid || (Flags&~0x92u)) return false;
    if((Flags&2) && !Text(R,Row.Comment)) return false;
    if(Flags&16) {if(!R.CanRead(3)) return false; Row.Tanks=R.ReadU8(); Row.Healers=R.ReadU8(); Row.Damage=R.ReadU8(); if(Row.Tanks>40 || Row.Healers>40 || Row.Damage>40) return false;}
    if(Flags&128) {if(!R.CanRead(12)) return false; Row.Instance=R.ReadU64(); Row.Encounters=R.ReadU32();}
    return true;
}
bool Player(FPacketReader& R,FWowLfgState::FRaidPlayer& Row)
{
    if(!R.CanRead(12)) return false; Row.Guid=R.ReadU64(); const uint32 Flags=R.ReadU32();
    if(!Row.Guid || (Flags&~0xffu)) return false;
    if(Flags&1)
    {
        if(!R.CanRead(86)) return false; Row.Level=R.ReadU8(); Row.Class=R.ReadU8(); Row.Race=R.ReadU8();
        for(int I=0;I<3;++I) R.ReadU8(); for(int I=0;I<6;++I) R.ReadU32();
        const float Mp5=R.ReadFloat(),CombatMp5=R.ReadFloat();
        for(int I=0;I<4;++I) R.ReadU32(); const uint32 Online=R.ReadU32(); Row.ItemLevel=R.ReadFloat();
        for(int I=0;I<6;++I) R.ReadU32();
        if(!Row.Level || Row.Class>11 || Online>1 || !FMath::IsFinite(Mp5) || !FMath::IsFinite(CombatMp5) || !FMath::IsFinite(Row.ItemLevel)) return false;
        Row.Online=Online!=0;
    }
    if((Flags&2) && !Text(R,Row.Comment)) return false;
    Row.Leader=false; if(Flags&4) {if(!R.CanRead(1)) return false; const uint8 Leader=R.ReadU8(); if(Leader>1) return false; Row.Leader=Leader!=0;}
    Row.Group=0; if(Flags&8) {if(!R.CanRead(8)) return false; Row.Group=R.ReadU64();}
    if(Flags&16) {if(!R.CanRead(1)) return false; Row.Roles=R.ReadU8(); if(Row.Roles&~15u) return false;}
    if(Flags&32) {if(!R.CanRead(4)) return false; Row.Area=R.ReadU32();}
    if(Flags&64) {if(!R.CanRead(1)) return false; Row.Status=R.ReadU8();}
    if(Flags&128) {if(!R.CanRead(12)) return false; Row.Instance=R.ReadU64(); Row.Encounters=R.ReadU32();}
    return true;
}
}
void FWowPacketHandler::HandleLfrList(FPacketReader& R)
{
    if(!R.CanRead(9)) return; const uint32 Type=R.ReadU32(),Dungeon=R.ReadU32()&0xffffff; const uint8 Difference=R.ReadU8();
    if(Type!=2 || !Dungeon || Dungeon!=Lfg.Raid.SearchDungeon || Difference>1 || (Difference && !Lfg.Raid.Ready)) return;
    auto Next=Lfg.Raid; if(!Difference) {Next.Groups.Reset(); Next.Players.Reset();}
    if(Difference)
    {
        if(!R.CanRead(4)) return; const uint32 Deleted=R.ReadU32(); if(Deleted>10000 || !R.CanRead(Deleted*8)) return;
        for(uint32 I=0;I<Deleted;++I) {const uint64 Guid=R.ReadU64(); Next.Groups.Remove(Guid); Next.Players.Remove(Guid);}
    }
    if(!R.CanRead(8)) return; const uint32 Groups=R.ReadU32(); R.ReadU32(); if(Groups>2000) return; TSet<uint64> SeenGroups,SeenPlayers;
    for(uint32 I=0;I<Groups;++I)
    {
        if(!R.CanRead(8)) return; uint64 Guid=0; FMemory::Memcpy(&Guid,R.Data+R.Pos,8);
        auto Row=Next.Groups.FindRef(Guid); if(!Group(R,Row) || SeenGroups.Contains(Row.Guid)) return;
        SeenGroups.Add(Row.Guid); Next.Groups.Add(Row.Guid,MoveTemp(Row));
    }
    if(!R.CanRead(8)) return; const uint32 Players=R.ReadU32(); R.ReadU32(); if(Players>5000) return;
    for(uint32 I=0;I<Players;++I)
    {
        if(!R.CanRead(8)) return; uint64 Guid=0; FMemory::Memcpy(&Guid,R.Data+R.Pos,8);
        auto Row=Next.Players.FindRef(Guid); if(!Player(R,Row) || SeenPlayers.Contains(Row.Guid)) return;
        SeenPlayers.Add(Row.Guid); Next.Players.Add(Row.Guid,MoveTemp(Row));
    }
    if(R.Remaining() || Next.Groups.Num()>2000 || Next.Players.Num()>5000) return;
    Next.Ready=true; ++Next.Revision; Lfg.Raid=MoveTemp(Next);
}
void FWowPacketHandler::HandleLfrSearchActive(FPacketReader& R)
{
    if(R.Remaining()!=1) return; const uint8 Active=R.ReadU8(); if(Active>1) return;
    Lfg.Raid.Listed=Active!=0; ++Lfg.Raid.Revision;
}
