#include "Misc/AutomationTest.h"
#include "WowM2Cache.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowParsedCache,"WowUnreal.Performance.ParsedModelCacheBudgetAndOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowParsedCache::RunTest(const FString&)
{
    FWowM2Cache Cache(1024*1024); TSharedPtr<FM2Data> Pinned;
    for(int32 I=0;I<80;++I)
    {
        auto Model=MakeShared<FM2Data>(); Model->Vertices.SetNum(3000); Model->AnimationTracks.SetNum(1); Model->AnimationTracks[0].BoneTracks.SetNum(3);
        Model->AnimationTracks[0].BoneTracks[0].RotValues.SetNum(100); Model->Lights.SetNum(1); Model->Lights[0].DiffuseIntensity.Sequences.SetNum(1);
        Model->Lights[0].DiffuseIntensity.Interpolation=1;
        Model->Lights[0].DiffuseIntensity.Sequences[0].Times={0,100}; Model->Lights[0].DiffuseIntensity.Sequences[0].Values={2,4};
        if(I==0) Pinned=Model;
        Cache.Add(FString::FromInt(I),Model); const auto Stats=Cache.GetStats();
        if(Stats.Bytes>Stats.Budget || Stats.Entries>256){AddError(TEXT("Parsed cache exceeded its retained payload budget")); return false;}
    }
    TestTrue(TEXT("Repeated unique models evict old cache ownership"),Cache.GetStats().Evictions>70 && Cache.Find(TEXT("0"))==nullptr);
    TestTrue(TEXT("An active light/animation/effect's shared data survives eviction intact"),Pinned.IsValid() && Pinned->Vertices.Num()==3000 && Pinned->Lights[0].DiffuseIntensity.Sample(0,50,{},0)==3);
    auto Huge=MakeShared<FM2Data>(); Huge->Vertices.SetNum(20000); Cache.Add(TEXT("oversize"),Huge);
    TestTrue(TEXT("Oversized models can be used by a live owner without entering the cache"),!Cache.Find(TEXT("oversize")) && Cache.GetStats().Bytes<=Cache.GetStats().Budget && Huge->Vertices.Num()==20000);
    FWowM2Cache SmallEntries(16*1024*1024); auto Tiny=MakeShared<FM2Data>(); for(int32 I=0;I<900;++I) SmallEntries.Add(FString::FromInt(I),Tiny);
    TestEqual(TEXT("Metadata entries are bounded independently of payload size"),SmallEntries.GetStats().Entries,256);
    return true;
}
#endif
