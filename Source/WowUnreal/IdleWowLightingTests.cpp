#include "Misc/AutomationTest.h"
#include "WowSkyManager.h"
#include "WowWorldManager.h"
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/TextureCube.h"
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightingRealmClock, "WowUnreal.Lighting.RealmClock", EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FLightingRealmClock::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Sky = World->SpawnActor<AWowSkyManager>();
    auto* Controller = World->SpawnActor<AWowGameplayController>();
    Controller->ConnectionManager = NewObject<UWowConnectionManager>(Controller);
    auto& Handler = Controller->ConnectionManager->PacketHandler;
    // Packed date containing 23:59, two game minutes per second, realm TZ offset 3.
    TArray<uint8> Data = {0xfb, 0xc5, 0xe2, 0x18, 0, 0, 0, 0x40, 3, 0, 0, 0};
    Handler.HandlePacket(WowOpcode::SMSG_LOGIN_SETTIMESPEED, Data);
    if (TestTrue(TEXT("Wire time is cached before the UI binds"), Handler.RealmClock.IsSet()))
    {
        const auto Clock = Handler.RealmClock.GetValue();
        TestEqual(TEXT("Use realm-local hours and minutes"), Clock.MinuteOfDay, 1439);
        TestEqual(TEXT("Use server speed and wrap around midnight"), Clock.MinutesAt(Clock.ReceivedAt + 3.), 5.);
        Handler.HandlePacket(WowOpcode::SMSG_LOGIN_SETTIMESPEED, {0, 0, 0, 0});
        TestEqual(TEXT("A truncated update preserves the previous clock"), Handler.RealmClock->ReceivedAt, Clock.ReceivedAt);
        const double BeforeBind = Clock.MinutesAt(FPlatformTime::Seconds());
        Controller->BindEntityEvents();
        const double AfterBind = Clock.MinutesAt(FPlatformTime::Seconds());
        const double ReplayOffset = FMath::Fmod(Sky->TimeOfDay - BeforeBind + 1440., 1440.);
        const double BindDuration = FMath::Fmod(AfterBind - BeforeBind + 1440., 1440.);
        TestTrue(TEXT("Late binding replays the cached time into the sky"),
            ReplayOffset <= BindDuration + .00001 && Sky->TimeSpeed == 2.f);
        Handler.ResetRealmClock();
        TestTrue(TEXT("Session reset clears cached time and restores the fallback"),
            !Handler.RealmClock.IsSet() && Sky->TimeOfDay == GetDefault<AWowSkyManager>()->TimeOfDay);
        Data[0] = 0x43; Data[1] = 2; // 09:03
        Handler.HandlePacket(WowOpcode::SMSG_LOGIN_SETTIMESPEED, Data);
        TestTrue(TEXT("A fresh clock reaches an already-bound sky"),
            Sky->TimeOfDay >= 543. && Sky->TimeOfDay <= Handler.RealmClock->MinutesAt(FPlatformTime::Seconds()));
        Sky->FreezeTime(630.);
        Handler.HandlePacket(WowOpcode::SMSG_LOGIN_SETTIMESPEED, Data);
        TestTrue(TEXT("Explicit capture time survives later packets"), Sky->TimeOfDay == 630. && Sky->TimeSpeed == 0.f);
    }
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightingDataLifecycle,"WowUnreal.Lighting.LoginAndMapLighting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FLightingDataLifecycle::RunTest(const FString&)
{
    FString Path;
    if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) {AddError(TEXT("Supply existing -wowdata")); return false;}
    auto& Dbc=FDbcStore::Get();
    if(!Dbc.IsLoaded())
    {
        FMpqManager Mpq;
        if(!Mpq.Initialize(Path) || !Dbc.LoadAll(Mpq)) return false;
    }
    if(!TestTrue(TEXT("Original client lighting tables are present"),Dbc.Lights().Num()>0 && Dbc.LightIntParams().Num()>0 && Dbc.LightFloatParams().Num()>0)) return false;
    TestTrue(TEXT("Coldridge inherits Dun Morogh's character ambient"), FMath::IsNearlyEqual(Dbc.AreaTable().GetCharacterAmbientScale(132), 1.6f));
    TestEqual(TEXT("Elwynn uses base character ambient"), Dbc.AreaTable().GetCharacterAmbientScale(12), 1.f);
    TestEqual(TEXT("An unknown area uses base character ambient"), Dbc.AreaTable().GetCharacterAmbientScale(0), 1.f);
    // Reproduce the real login ordering: sky begins first, essential tables
    // become available afterwards. Preserve the shared DBC state for all tests.
    auto SavedLights=MoveTemp(Dbc.Lights()); Dbc.Lights()=FLightDbc();
    auto SavedInts=MoveTemp(Dbc.LightIntParams()); Dbc.LightIntParams()=FLightIntParamsDbc();
    auto SavedFloats=MoveTemp(Dbc.LightFloatParams()); Dbc.LightFloatParams()=FLightFloatParamsDbc();
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Sky=World->SpawnActor<AWowSkyManager>();
    Sky->TimeOfDay=630; Sky->TimeSpeed=0; Sky->DispatchBeginPlay();
    TestFalse(TEXT("Early login starts with a fallback until tables exist"),Sky->bLightTablesReady || Sky->bHasDbcLights || Sky->bHasFloatParams);
    Dbc.Lights()=MoveTemp(SavedLights); Dbc.LightIntParams()=MoveTemp(SavedInts); Dbc.LightFloatParams()=MoveTemp(SavedFloats);
    Sky->Tick(.1f);
    TestTrue(TEXT("Next sky update adopts the loaded colour and fog bands"),Sky->bLightTablesReady && Sky->bHasDbcLights && Sky->bHasFloatParams);
    TestEqual(TEXT("Late loading retains the Eastern Kingdoms bands"),Sky->MapLights.Num(),Dbc.Lights().GetByMap(0).Num());
    TestEqual(TEXT("Lighting refresh preserves the game clock"),Sky->TimeOfDay,630.);
    TestTrue(TEXT("Ambient uses a uniform source across both hemispheres"),
        Sky->SkyLight->SourceType == SLS_SpecifiedCubemap && Sky->SkyLight->Cubemap &&
        Sky->SkyLight->Cubemap->GetSizeX() == 1 && Sky->SkyLight->Cubemap->GetSizeY() == 1 &&
        !Sky->SkyLight->bRealTimeCapture && !Sky->SkyLight->bLowerHemisphereIsBlack);
    for (const double Minutes : {0., 360., 720., 1080., 1286.})
    {
        Sky->TimeOfDay = Minutes;
        Sky->Tick(.11f);
        const FLinearColor Ambient = Sky->BlendZoneColor(AWowSkyManager::LP_GlobalAmbient, FVector::ZeroVector);
        const FLinearColor Diffuse = Sky->BlendZoneColor(AWowSkyManager::LP_GlobalDiffuse, FVector::ZeroVector);
        const float Height = float(-Sky->SunLight->GetForwardVector().Z);
        const auto ExpectedGround = (Ambient + Diffuse * Height).ToFColor(false);
        const auto RenderedGround = Sky->SkyLight->GetLightColor() + Sky->SunLight->GetLightColor() * (Height * Sky->SunLight->Intensity / UE_PI);
        TestTrue(TEXT("Authored ambient reaches the light without a second time-of-day multiplier"),
            Sky->SkyLight->GetLightColor().ToFColorSRGB() == Ambient.ToFColor(false) && Sky->SkyLight->Intensity == 1.f);
        const FColor Ground = RenderedGround.ToFColorSRGB();
        TestTrue(TEXT("Day and night reproduce stock ground irradiance within component quantization"),
            Sky->SunLight->IsVisible() && !Sky->MoonLight->IsVisible() && FMath::Abs(int(Ground.R)-ExpectedGround.R)<=2 &&
            FMath::Abs(int(Ground.G)-ExpectedGround.G)<=2 && FMath::Abs(int(Ground.B)-ExpectedGround.B)<=2);
        const auto Aim = Sky->SunLight->GetComponentRotation();
        const float Opacity = Sky->BlendZoneColor(AWowSkyManager::LP_ShadowOpacity, FVector::ZeroVector).R;
        const auto StockShadow = (ExpectedGround.ReinterpretAsLinear() * FMath::Lerp(FLinearColor::White, Ambient / 3.f, Opacity)).ToFColor(false);
        const float ShadowLuma = FMath::Lerp(RenderedGround.GetLuminance(), Sky->SkyLight->GetLightColor().GetLuminance(), Sky->SunLight->ShadowAmount);
        TestTrue(TEXT("Cast shadow strength reproduces the stock attenuation above the ambient floor"),
            FMath::IsNearlyEqual(ShadowLuma, FMath::Max(FLinearColor::FromSRGBColor(StockShadow).GetLuminance(), Sky->SkyLight->GetLightColor().GetLuminance()), .015f));
        TestTrue(TEXT("The world light stays low and uses a fixed azimuth"),
            Aim.Pitch >= -37.01 && Aim.Pitch <= -19.99 && FMath::IsNearlyEqual(Aim.Yaw, 135., .01));
    }
    Sky->TimeOfDay = 630.;
    auto* Manager=World->SpawnActor<AWowWorldManager>();
    TestTrue(TEXT("Selecting Kalimdor succeeds"),Manager->SelectMap(1));
    TestEqual(TEXT("Sky follows the selected terrain map"),Sky->CurrentMapId,1);
    TestEqual(TEXT("Kalimdor gets its own lighting table"),Sky->MapLights.Num(),Dbc.Lights().GetByMap(1).Num());
    for(const auto* Light:Sky->MapLights) if(Light->MapID!=1) {AddError(TEXT("Previous map lighting leaked through transfer")); break;}
    TestEqual(TEXT("Map transfer does not reset the game clock"),Sky->TimeOfDay,630.);
    // A same-map selection used to return before refreshing an early sky.
    Sky->LoadLightZones(999999);
    TestFalse(TEXT("Unknown map has a fallback"),Sky->bHasDbcLights);
    TestTrue(TEXT("Reselecting the current map succeeds"),Manager->SelectMap(1));
    TestTrue(TEXT("Same-map login restores authored lighting"),Sky->bHasDbcLights && Sky->CurrentMapId==1);
    TestTrue(TEXT("Returning to Eastern Kingdoms succeeds"),Manager->SelectMap(0));
    TestEqual(TEXT("Return transfer restores the original map"),Sky->CurrentMapId,0);
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightingSunCycle,"WowUnreal.Lighting.DayNightAndShadows",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FLightingSunCycle::RunTest(const FString&)
{
    TestTrue(TEXT("Sunrise at the horizon"),FMath::Abs(AWowSkyManager::SunRotationForTime(360).Pitch)<.01);
    TestTrue(TEXT("Noon sun points down"),AWowSkyManager::SunRotationForTime(720).Pitch<-89);
    TestTrue(TEXT("Midnight sun is below the horizon"),AWowSkyManager::SunRotationForTime(0).Pitch>89);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Sky=World->SpawnActor<AWowSkyManager>();
    TestEqual(TEXT("Shadow direction is updated every frame"),Sky->PrimaryActorTick.TickInterval,0.f);
    TestTrue(TEXT("Default clock advances one game minute per real minute"),FMath::IsNearlyEqual(Sky->TimeSpeed,1.f/60.f));
    Sky->DispatchBeginPlay();
    Sky->LoadLightZones(999999); // This path exercises the fallback when a map has no authored bands.
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
    Sky->TimeOfDay=1200; Sky->TimeSpeed=1.f/60.f;
    const double Before=Sky->TimeOfDay;
    for(int32 I=0;I<600;++I) Sky->Tick(1.f/600.f);
    TestTrue(TEXT("Realtime clock retains increments at high frame rates"),FMath::Abs(Sky->TimeOfDay-Before-1./60.)<1.e-7);
    TestEqual(TEXT("Horizon has no below-ground direct light"),AWowSkyManager::HorizonWeight(0),0.f);
    TestEqual(TEXT("Daylight retains full shadowed light"),AWowSkyManager::HorizonWeight(8),1.f);
    TestTrue(TEXT("Old five-degree cutoff is now continuous"),FMath::Abs(AWowSkyManager::HorizonWeight(5.001f)-AWowSkyManager::HorizonWeight(4.999f))<.001f);
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
