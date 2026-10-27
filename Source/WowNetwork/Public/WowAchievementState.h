#pragma once
#include "CoreMinimal.h"
struct FWowAchievementState
{
    struct FProgress {uint64 Counter=0,Player=0; uint32 Flags=0,Date=0,Elapsed=0,CreationElapsed=0; double At=0;};
    struct FSnapshot {bool Ready=false; TMap<uint32,uint32> Completed; TMap<uint32,FProgress> Progress;};
    FSnapshot Own,Comparison;
    uint64 CompareGuid=0;
    uint32 Revision=1,DataRevision=0,ComparisonRevision=0;
    TArray<uint32> Earned;
};
