#include "WowPacketHandler.h"

FWowLootRoll* FWowPacketHandler::FindLootRoll(uint64 Guid,uint32 Slot)
{
    for(auto& Pair:LootRolls) if(Pair.Value.Guid==Guid && Pair.Value.Slot==Slot) return &Pair.Value;
    return nullptr;
}
void FWowPacketHandler::ResetLootState()
{
    const uint32 Revision=Loot.Revision+1; Loot=FWowLootState(); Loot.Revision=Revision;
    // UI contexts can survive a session transition. Keep their public IDs and
    // event sequence monotonic so an old frame cannot suppress a new roll.
    LootRolls.Reset(); LootRollEvents.Reset(); LootNotifications.Reset(); ++LootRollRevision;
    PendingLootGuid=MasterLootGuid=LastLootErrorGuid=0; MasterLootCandidates.Reset(); ++MasterLootRevision;
    LastLootError=0; ++LootErrorRevision;
}
void FWowPacketHandler::RecordLootRollEvent(FWowLootRollEvent Event)
{
    Event.Sequence=NextLootRollEvent++;
    if(LootRollEvents.Num()>=512) LootRollEvents.RemoveAt(0,LootRollEvents.Num()-511);
    LootRollEvents.Add(Event);
}
void FWowPacketHandler::RecordLootNotification(FWowLootNotification Notice)
{
    Notice.Sequence=NextLootNotification++;
    if(LootNotifications.Num()>=512) LootNotifications.RemoveAt(0,LootNotifications.Num()-511);
    LootNotifications.Add(Notice);
}
void FWowPacketHandler::HandleLootMoneyNotify(FPacketReader& R)
{
    if(R.Remaining()!=5) return;
    FWowLootNotification Notice; Notice.Money=true; Notice.Count=R.ReadU32(); const uint8 Sole=R.ReadU8();
    if(!Notice.Count || Sole>1 || !EntityManager.LocalPlayerGuid) return;
    Notice.Player=EntityManager.LocalPlayerGuid; Notice.Sole=Sole!=0; RecordLootNotification(Notice);
}
void FWowPacketHandler::HandleLootStartRoll(FPacketReader& R)
{
    if(R.Remaining()!=37) return; FWowLootRoll Roll;
    Roll.Guid=R.ReadU64(); Roll.Map=R.ReadU32(); Roll.Slot=R.ReadU32(); Roll.ItemId=R.ReadU32();
    Roll.RandomSuffix=R.ReadU32(); Roll.RandomProperty=int32(R.ReadU32()); Roll.Count=R.ReadU32();
    const uint32 Time=R.ReadU32(); Roll.VoteMask=R.ReadU8();
    if(!Roll.Guid || !Roll.ItemId || !Roll.Count || Roll.Slot>255 || !Time || Time>600000 || !Roll.VoteMask || (Roll.VoteMask&~15)) return;
    if(auto* Old=FindLootRoll(Roll.Guid,Roll.Slot))
    {
        if(Old->ItemId!=Roll.ItemId || Old->RandomSuffix!=Roll.RandomSuffix || Old->RandomProperty!=Roll.RandomProperty || Old->Finished) return;
        Roll.Id=Old->Id; Roll.Choice=Old->Choice; Roll.PendingChoice=Old->PendingChoice; Roll.Votes=Old->Votes; Roll.SourceLootGuid=Old->SourceLootGuid;
    }
    else
    {
        if(LootRolls.Num()>=256) for(auto I=LootRolls.CreateIterator();I;++I) if(I.Value().Finished || I.Value().MillisecondsLeft()==0) I.RemoveCurrent();
        if(LootRolls.Num()>=256 || !NextLootRollId) return;
        Roll.Id=NextLootRollId++; Roll.SourceLootGuid=PendingLootGuid;
    }
    Roll.EndsAt=FPlatformTime::Seconds()+double(Time)/1000;
    LootRolls.Add(Roll.Id,MoveTemp(Roll)); ++LootRollRevision;
}
void FWowPacketHandler::HandleLootRoll(FPacketReader& R)
{
    if(R.Remaining()!=35) return;
    const uint64 Guid=R.ReadU64(); const uint32 Slot=R.ReadU32(); const uint64 Player=R.ReadU64();
    const uint32 Item=R.ReadU32(),Suffix=R.ReadU32(); const int32 Property=int32(R.ReadU32());
    const uint8 Number=R.ReadU8(),Choice=R.ReadU8(),Auto=R.ReadU8();
    auto* Roll=Guid?FindLootRoll(Guid,Slot):nullptr;
    if(!Guid)
    {
        // AzerothCore deliberately sends an empty source GUID for normal votes.
        // Do not attribute an ambiguous vote to an unrelated concurrent drop.
        for(auto& Pair:LootRolls) if(!Pair.Value.Finished && Pair.Value.Slot==Slot && Pair.Value.ItemId==Item && Pair.Value.RandomSuffix==Suffix && Pair.Value.RandomProperty==Property)
        {if(Roll) return; Roll=&Pair.Value;}
    }
    if(!Roll || Roll->Finished || !Player || Item!=Roll->ItemId || Suffix!=Roll->RandomSuffix || Property!=Roll->RandomProperty || Number>128 || Choice>3 || Auto>1) return;
    const uint8 ActualChoice=(Choice==0 && Number==0)?1:Choice;
    Roll->Votes.Add(Player,ActualChoice);
    if(Player==EntityManager.LocalPlayerGuid) {Roll->Choice=ActualChoice; Roll->PendingChoice=-1;}
    RecordLootRollEvent({0,Roll->Id,Player,0,Number,ActualChoice,Auto});
    ++LootRollRevision;
}
void FWowPacketHandler::HandleLootRollWon(FPacketReader& R)
{
    if(R.Remaining()!=34) return;
    const uint64 Guid=R.ReadU64(); const uint32 Slot=R.ReadU32(),Item=R.ReadU32(),Suffix=R.ReadU32();
    const int32 Property=int32(R.ReadU32()); const uint64 Winner=R.ReadU64(); const uint8 Number=R.ReadU8(),Choice=R.ReadU8();
    auto* Roll=FindLootRoll(Guid,Slot);
    if(!Roll || Roll->Finished || Item!=Roll->ItemId || Suffix!=Roll->RandomSuffix || Property!=Roll->RandomProperty || !Winner || Number>100 || Choice<1 || Choice>3) return;
    Roll->Finished=true; Roll->Winner=Winner; Roll->WinnerRoll=Number; Roll->WinnerChoice=Choice; ++LootRollRevision;
    if(Loot.Open && Loot.Guid==Roll->SourceLootGuid) for(auto& I:Loot.Items) if(I.Index==Slot && I.ItemId==Item && I.RandomProperty==uint32(Property) && I.RandomSuffix==Suffix && !I.bLooted)
    {I.SlotType=Winner==EntityManager.LocalPlayerGuid?0:3; ++Loot.Revision;}
    RecordLootRollEvent({0,Roll->Id,Winner,1,Number,Choice,0});
}
void FWowPacketHandler::HandleLootAllPassed(FPacketReader& R)
{
    if(R.Remaining()!=24) return;
    const uint64 Guid=R.ReadU64(); const uint32 Slot=R.ReadU32(),Item=R.ReadU32();
    const int32 Property=int32(R.ReadU32()); const uint32 Suffix=R.ReadU32();
    auto* Roll=FindLootRoll(Guid,Slot);
    if(!Roll || Roll->Finished || Item!=Roll->ItemId || Suffix!=Roll->RandomSuffix || Property!=Roll->RandomProperty) return;
    Roll->Finished=true; ++LootRollRevision;
    if(Loot.Open && Loot.Guid==Roll->SourceLootGuid) for(auto& I:Loot.Items) if(I.Index==Slot && I.ItemId==Item && I.RandomProperty==uint32(Property) && I.RandomSuffix==Suffix && !I.bLooted)
    {I.SlotType=0; ++Loot.Revision;}
    RecordLootRollEvent({0,Roll->Id,0,2,0,0,0});
}
void FWowPacketHandler::HandleMasterLootList(FPacketReader& R)
{
    if(!R.CanRead(1)) return; const uint8 Count=R.ReadU8(); if(Count>40 || R.Remaining()!=Count*8) return;
    TArray<uint64> Candidates;
    for(int32 I=0;I<Count;++I) {const uint64 Guid=R.ReadU64(); if(!Guid || (Guid>>48) || Candidates.Contains(Guid)) return; Candidates.Add(Guid);}
    MasterLootGuid=PendingLootGuid?PendingLootGuid:Loot.Open?Loot.Guid:0;
    MasterLootCandidates=MoveTemp(Candidates); ++MasterLootRevision;
    for(uint64 Guid:MasterLootCandidates) if(!PlayerNameCache.Contains(Guid))
    {TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(&Guid),8); OnSendPacket.ExecuteIfBound(0x0050,Bytes);}
}
