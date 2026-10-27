#include "WowPacketHandler.h"

void FWowPacketHandler::HandleForcedReactions(FPacketReader& R)
{
    if(!R.CanRead(4)) return;
    const uint32 Count=R.ReadU32(); if(Count>4096 || R.Remaining()!=int32(Count)*8) return;
    TMap<uint32,uint8> Reactions;
    for(uint32 I=0;I<Count;++I)
    {
        const uint32 Faction=R.ReadU32(),Rank=R.ReadU32();
        if(!Faction || Rank>7 || Reactions.Contains(Faction)) return;
        Reactions.Add(Faction,uint8(Rank+1));
    }
    ForcedReactions=MoveTemp(Reactions);
}
