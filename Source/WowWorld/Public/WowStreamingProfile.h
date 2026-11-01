#pragma once
#include "CoreMinimal.h"

/** Opt-in wall-clock scopes; disabled in ordinary play and identical in before/after runs. */
struct FWowStreamingProfile
{
    const TCHAR* Stage;
    double Started;
    static bool IsEnabled(){static const bool Enabled=FParse::Param(FCommandLine::Get(),TEXT("wowstreamprofile")); return Enabled;}
    explicit FWowStreamingProfile(const TCHAR* InStage):Stage(InStage),Started(0)
    {
        if(IsEnabled()) Started=FPlatformTime::Seconds();
    }
    ~FWowStreamingProfile()
    {
        if(Started) UE_LOG(LogTemp,Display,TEXT("StreamPerf stage=%s ms=%.3f frame=%llu"),Stage,(FPlatformTime::Seconds()-Started)*1000.,IsInGameThread()?GFrameCounter:MAX_uint64);
    }
};
