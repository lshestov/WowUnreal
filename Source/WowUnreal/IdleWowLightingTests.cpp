#include "Misc/AutomationTest.h"
#include "WowSkyManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "WowWmoRenderer.h"
#include "Materials/Material.h"
#include "Mpq/MpqManager.h"
#include "Formats/WmoParser.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightingSunCycle,"WowUnreal.Lighting.DayNightAndShadows",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FLightingSunCycle::RunTest(const FString&)
{
    TestTrue(TEXT("Sunrise at the horizon"),FMath::Abs(AWowSkyManager::SunRotationForTime(360).Pitch)<.01);
    TestTrue(TEXT("Noon sun points down"),AWowSkyManager::SunRotationForTime(720).Pitch<-89);
    TestTrue(TEXT("Midnight sun is below the horizon"),AWowSkyManager::SunRotationForTime(0).Pitch>89);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Sky=World->SpawnActor<AWowSkyManager>();
    Sky->DispatchBeginPlay();
    TArray<UDirectionalLightComponent*> Lights; Sky->GetComponents(Lights);
    TestEqual(TEXT("Sun and moon are separate lights"),Lights.Num(),2);
    for (auto* Light:Lights)
    {
        TestTrue(TEXT("Runtime geometry uses movable shadows"),Light->Mobility==EComponentMobility::Movable && Light->CastShadows);
        TestTrue(TEXT("Contact shadow length is bounded in world units"),Light->ContactShadowLengthInWS && Light->ContactShadowLength>0 && Light->ContactShadowLength<=100);
        TestTrue(TEXT("Cascades stay local to the player"),Light->DynamicShadowDistanceMovableLight<=15000 && Light->DynamicShadowCascades<=3);
    }
    Sky->TimeOfDay=0; Sky->TimeSpeed=0; Sky->Tick(.1f);
    for (auto* Light:Lights) if (Light->GetName()==TEXT("SunLight")) TestFalse(TEXT("Sun does not illuminate the world at midnight"),Light->IsVisible());
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightingWmoBake,"WowUnreal.Lighting.WmoBakedColors",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FLightingWmoBake::RunTest(const FString&)
{
    auto* Mat=LoadObject<UMaterial>(nullptr,TEXT("/Game/Materials/M_WowWmo"));
    if (!TestNotNull(TEXT("Cookable WMO lighting material"),Mat)) return false;
    TestTrue(TEXT("World surfaces receive dynamic lighting"),Mat->GetShadingModels().HasShadingModel(MSM_DefaultLit));
    float Value=1;
    TestTrue(TEXT("Groups without baked colors retain their albedo"),Mat->GetScalarParameterValue(FMaterialParameterInfo(TEXT("BakedLightStrength")),Value) && Value==0);
    FString Path; if (!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) { AddError(TEXT("Supply existing -wowdata")); return false; }
    FMpqManager Mpq; if (!Mpq.Initialize(Path)) return false;
    TArray<uint8> Raw; Mpq.ReadFile(TEXT("WORLD\\WMO\\AZEROTH\\BUILDINGS\\NSABBEY\\NSABBEY.WMO"),Raw);
    const auto Root=FWmoParser::ParseRoot(Raw);
    int32 Colored=0;
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Actor=World->SpawnActor<AActor>(); auto* Scene=NewObject<USceneComponent>(Actor);
    Actor->SetRootComponent(Scene); Scene->RegisterComponent();
    int32 InteriorFills=0;
    for (uint32 I=0; I<Root.NumGroups; ++I)
    {
        Raw.Reset(); Mpq.ReadFile(FString::Printf(TEXT("WORLD\\WMO\\AZEROTH\\BUILDINGS\\NSABBEY\\NSABBEY_%03u.WMO"),I),Raw);
        const auto Group=FWmoParser::ParseGroup(Raw);
        for (const auto& Color:Group.VertexColors) if (Color.R<240 || Color.G<240 || Color.B<240) ++Colored;
        if (auto* Light=FWowWmoRenderer::AddInteriorFill(Scene,Group))
        {
            ++InteriorFills;
            TestTrue(TEXT("Interior fill affects characters without flattening world shadows"),!Light->LightingChannels.bChannel0 && Light->LightingChannels.bChannel1 && !Light->CastShadows);
            TestTrue(TEXT("Fill radius and draw distance are bounded"),Light->AttenuationRadius<=3500 && Light->MaxDrawDistance==10000);
        }
    }
    TestTrue(TEXT("Actual abbey has baked lighting colors to preserve"),Colored>100);
    TestTrue(TEXT("Actual abbey supplies local ambient for units"),InteriorFills>0);
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
