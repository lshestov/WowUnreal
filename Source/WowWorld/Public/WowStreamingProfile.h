#pragma once
#include "CoreMinimal.h"

/** Opt-in wall-clock scopes; disabled in ordinary play and identical in before/after runs. */
struct FWowStreamingProfile
{
    const TCHAR* Stage;
    double Started;
    explicit FWowStreamingProfile(const TCHAR* InStage):Stage(InStage),Started(0)
    {
        static const bool Enabled=FParse::Param(FCommandLine::Get(),TEXT("wowstreamprofile"));
        if(Enabled) Started=FPlatformTime::Seconds();
    }
    ~FWowStreamingProfile()
    {
        if(Started) UE_LOG(LogTemp,Display,TEXT("StreamPerf stage=%s ms=%.3f"),Stage,(FPlatformTime::Seconds()-Started)*1000.);
    }
};
