#pragma once
#include "CoreMinimal.h"
struct FWowPvpState
{
    struct FQueue {uint32 Type=0,Instance=0,Status=0,Map=0,Wait=0,Elapsed=0,Expiry=0; uint8 Arena=0,Min=0,Max=0,Team=0; bool Rated=false; double At=0;};
    FQueue Queues[3];
    struct FRewards {bool Won=false; uint32 Honor=0,Arena=0,LossHonor=0;};
    struct FList {uint64 Vendor=0; uint32 Type=0; uint8 Source=1; bool Random=false; FRewards Reward,RandomReward; TArray<uint32> Instances;} List;
    struct FScore {uint64 Guid=0; uint32 Blows=0,Kills=0,Deaths=0,Honor=0,Damage=0,Healing=0; uint8 Team=0; TArray<uint32> Extra;};
    struct FTeam {FString Name; uint32 Lost=0,Won=0,Skill=0;};
    FTeam Teams[2]; TArray<FScore> Scores; bool ScoreArena=false; int32 Winner=-1;
    struct FPosition {uint64 Guid=0; FVector2D World=FVector2D::ZeroVector;};
    TArray<FPosition> Positions,Flags;
    struct FArenaMember {uint64 Guid=0; FString Name; uint32 Rank=0,Games=0,Wins=0,SeasonGames=0,SeasonWins=0,Rating=0; uint8 Level=0,Class=0; bool Online=false;};
    struct FArena {uint32 Rating=0,Games=0,Wins=0,SeasonGames=0,SeasonWins=0,Rank=0,Size=0; TArray<FArenaMember> Members;};
    TMap<uint32,FArena> ArenaTeams;
    uint32 ArenaError=0,ArenaResultRevision=0;
    FString ArenaResultTeam,ArenaResultPlayer;
    struct FWorldQueue {uint32 Id=0,Zone=0,Status=0,Expires=0;};
    TMap<uint32,FWorldQueue> WorldQueues;
    struct FWorldEvent {uint32 Sequence=0,Id=0,Kind=0,Args[4]={};};
    TArray<FWorldEvent> WorldEvents;
    uint32 NextWorldEvent=0;
    FString Inviter,Invitation; int32 Result=0; uint32 Revision=1,ListRevision=0,ScoreRevision=0,PositionRevision=0,ArenaRevision=0,InviteRevision=0,ResultRevision=0;
};
