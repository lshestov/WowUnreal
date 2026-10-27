#pragma once
#include "CoreMinimal.h"
struct FWowLfgState
{
    using FLocks=TMap<uint32,uint32>;
    struct FItem {uint32 Id=0,Display=0,Count=0;};
    struct FReward {bool Done=false; uint32 Money=0,Experience=0,MoneyVariance=0,ExperienceVariance=0; TArray<FItem> Items;};
    TMap<uint32,FReward> Rewards;
    FLocks Locks;
    TMap<uint64,FLocks> PartyLocks;
    bool InfoReady=false,Disabled=false;
    struct FSelection {uint8 Update=0; bool Has=false,Joined=false,Queued=false; FString Comment; TArray<uint32> Dungeons; double QueuedAt=0;} Player,Party;
    struct FQueue {uint32 Dungeon=0,Elapsed=0; int32 Average=-1,Wait=-1,TankWait=-1,HealerWait=-1,DamageWait=-1; uint8 Tanks=1,Healers=1,Damage=3; double At=0;} Queue;
    uint32 Result=0,ResultState=0;
    uint32 Revision=1,InfoRevision=0,QueueRevision=0,ResultRevision=0;
};
