#pragma once
#include "CoreMinimal.h"

/** Cooperative GT budget: an indivisible operation can overrun, then yields.
 * One terrain/LOD/WDL upload and at most eight object jobs, never an unbounded drain. */
struct FWowStreamingBudget
{
    static constexpr int32 MaxJobs=8;
    static constexpr double Seconds=.001;
    double Started;
    int32 Jobs=0,TerrainUploads=0;
    explicit FWowStreamingBudget(double Now):Started(Now){}
    static bool IsTerrain(int32 Kind){return Kind==0 || Kind==3 || Kind==4;}
    bool CanStart(int32 Kind,double Now) const
    {return Kind>=0 && Kind<6 && Jobs<MaxJobs && (!Jobs || Now-Started<Seconds) && (!IsTerrain(Kind) || TerrainUploads==0);}
    void Complete(int32 Kind){++Jobs; if(IsTerrain(Kind)) ++TerrainUploads;}
};
