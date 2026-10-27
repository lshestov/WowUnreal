#include "WowPacketHandler.h"

namespace
{
bool Guid(FPacketReader& R,uint64& Out)
{
    if(!R.CanRead(1)) return false;
    const uint8 Mask=R.ReadU8(); int32 Bytes=0; for(int32 I=0;I<8;++I) Bytes+=(Mask>>I)&1; if(!R.CanRead(Bytes)) return false;
    Out=0; for(int32 I=0;I<8;++I) if(Mask&(1<<I)) Out|=uint64(R.ReadU8())<<(I*8); return true;
}
}
void FWowPacketHandler::ResetInspectState(uint64 Guid)
{const uint32 Revision=Inspect.Revision+1,Honor=Inspect.HonorRevision+1; Inspect=FInspectState(); Inspect.Guid=Guid; Inspect.Revision=Revision; Inspect.HonorRevision=Honor;}
void FWowPacketHandler::HandleInspectTalents(FPacketReader& R)
{
    FInspectState State; if(!Guid(R,State.Guid) || !State.Guid || State.Guid!=Inspect.Guid || !R.CanRead(6)) return;
    State.FreePoints=R.ReadU32(); const uint8 Count=R.ReadU8(); State.Active=R.ReadU8(); if(Count>2 || (Count?State.Active>=Count:State.Active!=0)) return;
    for(int32 G=0;G<Count;++G)
    {
        if(!R.CanRead(1)) return; FWowTalentGroup Group; const uint8 Nodes=R.ReadU8(); if(!R.CanRead(Nodes*5)) return; TSet<uint32> Seen;
        for(int32 I=0;I<Nodes;++I) {FWowTalentInfo T; T.TalentId=R.ReadU32(); T.Rank=R.ReadU8(); if(!T.TalentId || T.Rank>4 || Seen.Contains(T.TalentId)) return; Seen.Add(T.TalentId); Group.Talents.Add(T);}
        if(!R.CanRead(1)) return; const uint8 Glyphs=R.ReadU8(); if(Glyphs>6 || !R.CanRead(Glyphs*2)) return; for(int32 I=0;I<Glyphs;++I) Group.Glyphs[I]=R.ReadU16(); State.Groups.Add(MoveTemp(Group));
    }
    if(!R.CanRead(4)) return; const uint32 Slots=R.ReadU32(); if(Slots&~0x7FFFFu) return;
    for(int32 S=0;S<19;++S) if(Slots&(1<<S))
    {
        if(!R.CanRead(6)) return; FWowItemEntity Item; Item.Entry=R.ReadU32(); const uint16 Enchants=R.ReadU16(); if(!Item.Entry || (Enchants&~0xFFFu)) return;
        for(int32 E=0;E<12;++E) if(Enchants&(1<<E)) {if(!R.CanRead(2)) return; Item.SetField(ItemField::ENCHANTMENT_1_1+E*3,R.ReadU16());}
        if(!R.CanRead(2)) return; Item.SetField(ItemField::RANDOM_PROPERTIES_ID,uint32(int32(int16(R.ReadU16())))); uint64 Creator=0; if(!Guid(R,Creator) || !R.CanRead(4)) return;
        Item.SetField(ItemField::CREATOR,uint32(Creator)); Item.SetField(ItemField::CREATOR+1,uint32(Creator>>32)); Item.SetField(ItemField::PROPERTY_SEED,R.ReadU32()); Item.SetField(ItemField::STACK_COUNT,1); State.Items.Add(S+1,MoveTemp(Item));
    }
    if(R.Remaining()) return;
    State.Revision=Inspect.Revision+1; State.HonorRevision=Inspect.HonorRevision; State.HonorReady=Inspect.HonorReady; State.TodayKills=Inspect.TodayKills; State.YesterdayKills=Inspect.YesterdayKills; State.TodayHonor=Inspect.TodayHonor; State.YesterdayHonor=Inspect.YesterdayHonor; State.LifetimeKills=Inspect.LifetimeKills; for(int32 I=0;I<3;++I) State.Arena[I]=Inspect.Arena[I]; State.Ready=true; Inspect=MoveTemp(State);
}
void FWowPacketHandler::HandleInspectHonor(FPacketReader& R)
{
    if(R.Remaining()!=25) return; const uint64 Guid=R.ReadU64(); if(!Guid || Guid!=Inspect.Guid) return;
    R.ReadU8(); const uint32 Kills=R.ReadU32(); Inspect.TodayKills=Kills&0xFFFF; Inspect.YesterdayKills=Kills>>16;
    Inspect.TodayHonor=R.ReadU32(); Inspect.YesterdayHonor=R.ReadU32(); Inspect.LifetimeKills=R.ReadU32(); Inspect.HonorReady=true; ++Inspect.HonorRevision;
}
void FWowPacketHandler::HandleInspectArena(FPacketReader& R)
{
    if(R.Remaining()!=33 || R.ReadU64()!=Inspect.Guid) return; const uint8 Slot=R.ReadU8(); if(Slot>=3) return;
    FInspectState::FArena A; A.Id=R.ReadU32(); A.Rating=R.ReadU32(); A.Games=R.ReadU32(); A.Wins=R.ReadU32(); A.Played=R.ReadU32(); A.Personal=R.ReadU32();
    if(!A.Id || A.Wins>A.Games) return; Inspect.Arena[Slot]=A; ++Inspect.HonorRevision;
    if(!ArenaIdentities.Contains(A.Id)) {TArray<uint8> B; B.Append(reinterpret_cast<const uint8*>(&A.Id),4); OnSendPacket.ExecuteIfBound(0x34B,B);}
}
void FWowPacketHandler::HandleArenaIdentity(FPacketReader& R)
{
    if(!R.CanRead(4)) return; FArenaIdentity A; A.Id=R.ReadU32(); const int32 Start=R.Pos; A.Name=R.ReadCString(); if(!A.Id || R.Pos<=Start || R.Data[R.Pos-1]!=0 || R.Pos-Start>128 || R.Remaining()!=24) return;
    A.Size=R.ReadU32(); if(A.Size!=2 && A.Size!=3 && A.Size!=5) return; A.Background=R.ReadU32(); A.Emblem=R.ReadU32(); A.EmblemColor=R.ReadU32(); A.Border=R.ReadU32(); A.BorderColor=R.ReadU32();
    if(ArenaIdentities.Num()>=128 && !ArenaIdentities.Contains(A.Id)) return; ArenaIdentities.Add(A.Id,MoveTemp(A)); ++Inspect.HonorRevision;
}
