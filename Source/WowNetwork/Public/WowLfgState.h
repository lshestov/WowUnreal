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
    struct FRoleMember {uint64 Guid=0; uint32 Roles=0; uint8 Level=0; bool Ready=false;};
    struct FRoleCheck {uint32 State=0; bool Initializing=false; TArray<uint32> Dungeons; TArray<FRoleMember> Members;} RoleCheck;
    FRoleMember Chosen;
    struct FChosenEvent {uint32 Revision=0; FRoleMember Member;}; TArray<FChosenEvent> ChosenEvents;
    struct FProposalMember {uint32 Roles=0; bool Self=false,InDungeon=false,SameGroup=false,Answered=false,Accepted=false;};
    struct FProposal {bool Valid=false,Silent=false; uint32 Dungeon=0,Id=0,Encounters=0; uint8 State=0; TArray<FProposalMember> Members; double At=0;} Proposal;
    struct FBoot {bool Progress=false,Voted=false,Agree=false; uint64 Victim=0; uint32 Votes=0,Agrees=0,Needed=0; double Deadline=0; FString Reason;} Boot;
    uint32 ContinueDungeon=0,TeleportError=0,RewardRandom=0,RewardSpecific=0;
    FReward FinalReward;
    uint32 RoleRevision=0,ChosenRevision=0,ProposalRevision=0,BootRevision=0,ContinueRevision=0,TeleportRevision=0,RewardRevision=0;
};
