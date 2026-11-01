#include "Misc/AutomationTest.h"
#include "WowSkyManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "WowWmoRenderer.h"
#include "WowWmoLighting.h"
#include "Materials/Material.h"
#include "Mpq/MpqManager.h"
#include "Formats/WmoParser.h"
#include "Formats/Dbc/LightDbc.h"
#include "Coord/WowCoordinate.h"

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
    int32 Interiors=0;
    for (uint32 I=0; I<Root.NumGroups; ++I)
    {
        Raw.Reset(); Mpq.ReadFile(FString::Printf(TEXT("WORLD\\WMO\\AZEROTH\\BUILDINGS\\NSABBEY\\NSABBEY_%03u.WMO"),I),Raw);
        const auto Group=FWmoParser::ParseGroup(Raw);
        for (const auto& Color:Group.VertexColors) if (Color.R<240 || Color.G<240 || Color.B<240) ++Colored;
        const auto Lighting=FWowWmoLighting::Prepare(Root,Group);
        if (!(Group.Flags&0x48) && !Lighting.IsEmpty())
        {
            ++Interiors;
            TestEqual(TEXT("Floor and surface lighting have one value per original vertex"),Lighting.Num(),Group.Vertices.Num());
            for(const auto& C:Lighting) if(!FMath::IsFinite(C.R+C.G+C.B+C.A) || C.A<0 || C.A>1) {AddError(TEXT("Invalid prepared WMO lighting")); break;}
        }
    }
    TestTrue(TEXT("Actual abbey has baked lighting colors to preserve"),Colored>100);
    TestTrue(TEXT("Actual abbey supplies local floor colours for units"),Interiors>0);
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightingDbcUnits,"WowUnreal.Lighting.DbcCoordinatesAndUnits",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FLightingDbcUnits::RunTest(const FString&)
{
    TArray<uint8> Bytes; Bytes.SetNumZeroed(20+120+1);
    const uint32 Header[]={0x43424457,2,15,60,1}; FMemory::Memcpy(Bytes.GetData(),Header,20);
    const auto U=[&](int32 Row,int32 Col,uint32 V){FMemory::Memcpy(Bytes.GetData()+20+Row*60+Col*4,&V,4);};
    const auto F=[&](int32 Row,int32 Col,float V){FMemory::Memcpy(Bytes.GetData()+20+Row*60+Col*4,&V,4);};
    U(0,0,1); F(0,6,100); U(0,7,1); // Global identity is the zero position, not the radius.
    U(1,0,2); U(1,7,1);
    F(1,2,(FWowCoordinate::MAP_ORIGIN+130)*36); F(1,3,80*36); F(1,4,(FWowCoordinate::MAP_ORIGIN+8900)*36);
    F(1,5,20*36); F(1,6,40*36);
    FDbcParser Parser; if(!TestTrue(TEXT("Fixture DBC parses"),Parser.Parse(Bytes))) return false;
    FLightDbc Lights; Lights.Load(Parser); const auto* L=Lights.GetById(2);
    if(!TestNotNull(TEXT("Local light loaded"),L)) return false;
    TestTrue(TEXT("DBC east/height/south editor coordinates become movement coordinates"),FVector(L->X,L->Y,L->Z).Equals(FVector(-8900,-130,80),.01));
    TestEqual(TEXT("Falloff start is in yards"),L->FalloffStart,20.f); TestEqual(TEXT("Falloff end is in yards"),L->FalloffEnd,40.f);
    TestTrue(TEXT("Global position is preserved even with nonzero falloff"),Lights.GetById(1)->IsGlobal());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightingWmoPortal,"WowUnreal.Lighting.WmoPortalColors",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FLightingWmoPortal::RunTest(const FString&)
{
    FWmoRootData Root; Root.GroupFlags={8}; Root.PortalVertices={{0,-2,-2},{0,2,-2},{0,2,2},{0,-2,2}};
    FWmoRootData::FPortal Portal; Portal.Count=4; Portal.Normal=FVector(1,0,0); Root.Portals.Add(Portal);
    FWmoRootData::FPortalRef Ref; Ref.Side=1; Root.PortalRefs.Add(Ref);
    FWmoGroupData Group; Group.PortalCount=1; Group.TransitionBatchCount=1;
    Group.Vertices={{2,0,0},{10,0,0}}; Group.VertexColors={FColor(200,100,60,0),FColor(200,100,60,0)};
    Group.Batches.SetNum(1); Group.Batches[0].VertexEnd=1;
    TestTrue(TEXT("Two yards inside an exterior portal retain 70% outdoor light"),FMath::IsNearlyEqual(FWowWmoLighting::PortalWeight(Root,Group,{2,0,0}),.7f));
    TestEqual(TEXT("Far from doorway uses baked interior"),FWowWmoLighting::PortalWeight(Root,Group,{10,0,0}),0.f);
    TestEqual(TEXT("Outside the portal polygon cannot leak sunlight sideways"),FWowWmoLighting::PortalWeight(Root,Group,{2,100,0}),0.f);
    const auto Colors=FWowWmoLighting::Prepare(Root,Group);
    TestTrue(TEXT("Transition floor alpha varies across one group"),Colors.Num()==2 && Colors[0].A>.69 && Colors[1].A==0);
    TestTrue(TEXT("Baked hue survives away from the door"),Colors[1].R>Colors[1].G && Colors[1].G>Colors[1].B);
    const auto Fill=FWowWmoLighting::UnitDiffuse(FLinearColor(.1f,.2f,.3f))+FWowWmoLighting::UnitAmbient(FLinearColor(.1f,.2f,.3f));
    TestTrue(TEXT("Original diffuse minimum and ambient cap retain the floor hue"),FMath::IsNearlyEqual(Fill.B,168.f/255.f+.3f,1.e-5f) && FMath::IsNearlyEqual(Fill.R/Fill.B,1.f/3.f,1.e-5f));
    Group.VertexColors.Pop(); TestTrue(TEXT("Incomplete MOCV falls back without indexing missing vertices"),FWowWmoLighting::Prepare(Root,Group).IsEmpty());
    return true;
}
#endif
