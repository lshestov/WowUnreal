#include "Misc/AutomationTest.h"
#include "Formats/M2Parser.h"
#include "Formats/WmoParser.h"
#include "Formats/AdtParser.h"
#include "WowM2Lighting.h"
#include "WowM2LightSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/SceneComponent.h"
#include "Mpq/MpqManager.h"
#include "Coord/WowCoordinate.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM2LightDataAudit,"WowUnreal.Lighting.M2AuthoredLightData",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FM2LightDataAudit::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) {AddError(TEXT("Supply existing -wowdata")); return false;}
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    TSet<FString> Models; TMap<FString,FVector> Placements; int32 Referenced=0;
    for(const TCHAR* Building:{TEXT("NSABBEY"),TEXT("GOLDSHIREINN"),TEXT("STORMWIND")})
    {
        const FString Wmo=FString::Printf(TEXT("World\\WMO\\Azeroth\\Buildings\\%s\\%s.wmo"),Building,Building);
        TArray<uint8> Raw; if(!TestTrue(TEXT("Original WMO root readable"),Mpq.ReadFile(Wmo,Raw))) continue;
        const auto Root=FWmoParser::ParseRoot(Raw);
        TestTrue(TEXT("Original WMO doodad placements retained"),!Root.Doodads.IsEmpty());
        for(const auto& Set:Root.DoodadSets) TestTrue(TEXT("Doodad set stays inside MODD"),uint64(Set.First)+Set.Count<=uint64(Root.Doodads.Num()));
        for(const auto& D:Root.Doodads) if(!D.Path.IsEmpty())
        {
            TestTrue(TEXT("MODD transform is finite and usable"),!D.Position.ContainsNaN() && D.Rotation.IsNormalized() && FMath::IsFinite(D.Scale) && D.Scale>0);
            Models.Add(D.Path.ToLower());
        }
        for(uint32 I=0;I<Root.NumGroups;++I)
        {
            Raw.Reset(); if(!Mpq.ReadFile(Wmo.LeftChop(4)+FString::Printf(TEXT("_%03u.wmo"),I),Raw)) continue;
            const auto Group=FWmoParser::ParseGroup(Raw);
            for(uint16 Ref:Group.DoodadRefs) {TestTrue(TEXT("Group doodad reference stays inside MODD"),Root.Doodads.IsValidIndex(Ref)); ++Referenced;}
        }
    }
    for(const FIntPoint Center:{FIntPoint(32,48),FIntPoint(32,49),FIntPoint(31,51)})
        for(int32 X=Center.X-1;X<=Center.X+1;++X) for(int32 Y=Center.Y-1;Y<=Center.Y+1;++Y)
        {
            TArray<uint8> Raw; if(!Mpq.ReadFile(FString::Printf(TEXT("World\\Maps\\Azeroth\\Azeroth_%d_%d.adt"),X,Y),Raw)) continue;
            const auto Adt=FAdtParser::Parse(Raw);
            for(const auto& Model:Adt.DoodadPaths) Models.Add(Model.ToLower());
            for(const auto& D:Adt.DoodadPlacements) if(Adt.DoodadPaths.IsValidIndex(D.NameIndex))
                Placements.FindOrAdd(Adt.DoodadPaths[D.NameIndex].ToLower(),FWowCoordinate::UEToWow(FWowCoordinate::AdtToUE(D.Position.X,D.Position.Y,D.Position.Z)));
        }
    int32 PointLights=0,AnimatedLights=0,ModelsWithLights=0;
    for(const auto& Model:Models)
    {
        TArray<uint8> Raw; if(!Mpq.ReadFile(Model,Raw) || !FM2Parser::HasLightTable(Raw)) continue;
        TArray<uint8> Skin; Mpq.ReadFile(Model.LeftChop(3)+TEXT("00.skin"),Skin);
        const auto Data=FM2Parser::Parse(Raw,Skin); uint32 Count=0; FMemory::Memcpy(&Count,Raw.GetData()+264,4);
        TestEqual(TEXT("Every authored light is parsed"),Data.Lights.Num(),int32(Count));
        ++ModelsWithLights;
        if(const auto* P=Placements.Find(Model)) AddInfo(FString::Printf(TEXT("M2LightPlacement path=%s position=%s"),*Model,*P->ToString()));
        for(const auto& L:Data.Lights)
        {
            if(L.Type==1) ++PointLights;
            const auto& Durations=Data.GlobalSequenceDurations;
            const float Intensity=L.DiffuseIntensity.Sample(0,0,Durations,1),End=L.AttenuationEnd.Sample(0,0,Durations,0);
            TestTrue(TEXT("Original initial intensity/range are finite"),FMath::IsFinite(Intensity) && FMath::IsFinite(End));
            if(L.DiffuseIntensity.GlobalSequence>=0 || (L.DiffuseIntensity.Sequences.Num() && L.DiffuseIntensity.Sequences[0].Values.Num()>1)) ++AnimatedLights;
            AddInfo(FString::Printf(TEXT("M2LightAudit path=%s type=%u bone=%d position=%s intensity=%.4f attenuationEnd=%.4f global=%d"),
                *Model,L.Type,L.Bone,*L.Position.ToString(),Intensity,End,L.DiffuseIntensity.GlobalSequence));
        }
    }
    AddInfo(FString::Printf(TEXT("M2LightAudit models=%d litModels=%d points=%d animated=%d groupRefs=%d"),Models.Num(),ModelsWithLights,PointLights,AnimatedLights,Referenced));
    TestTrue(TEXT("Real WMO group doodads are referenced"),Referenced>0);
    TestTrue(TEXT("Actual lantern/torch models contain point light records"),PointLights>0);
    TestTrue(TEXT("Actual models retain authored animated intensity tracks"),AnimatedLights>0);
    const FVector Model(2,1,3); const FQuat Rotation(FVector::UpVector,PI/2);
    TestTrue(TEXT("Model rotation and position use the same handedness"),FWowCoordinate::ModelRotationToUE(Rotation).RotateVector(FWowCoordinate::ModelToUE(Model)).Equals(FWowCoordinate::ModelToUE(Rotation.RotateVector(Model)),.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM2LightClocks,"WowUnreal.Lighting.M2LightClocksAndBones",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FM2LightClocks::RunTest(const FString&)
{
    FM2Data M; M.GlobalSequenceDurations={1000}; M.Animations.SetNum(1); M.Animations[0].Length=1000;
    M.Bones.SetNum(2); M.Bones[1].ParentBone=0; M.Bones[1].PivotPoint=FVector(1,0,0);
    M.LightBoneTracks.SetNum(2);
    auto& Translation=M.LightBoneTracks[0].Translation; Translation.GlobalSequence=0; Translation.Interpolation=1; Translation.Sequences.SetNum(1);
    Translation.Sequences[0].Times={0,1000}; Translation.Sequences[0].Values={FVector3f::ZeroVector,FVector3f(2,0,0)};
    auto& Rotation=M.LightBoneTracks[1].Rotation; Rotation.Sequences.SetNum(1); Rotation.Sequences[0].Times={0};
    Rotation.Sequences[0].Values={FQuat4f(FVector3f::UpVector,PI/2)};
    M.Lights.SetNum(1); auto& L=M.Lights[0]; L.Bone=1; L.Position=FVector(2,0,0);
    L.DiffuseColor.Sequences.SetNum(1); L.DiffuseColor.Sequences[0].Times={0}; L.DiffuseColor.Sequences[0].Values={FVector3f(1,0,0)};
    L.DiffuseIntensity.Interpolation=1; L.DiffuseIntensity.Sequences.SetNum(1); L.DiffuseIntensity.Sequences[0].Times={0,1000}; L.DiffuseIntensity.Sequences[0].Values={0,2};
    L.Visibility.Sequences.SetNum(1); L.Visibility.Sequences[0].Times={0,500}; L.Visibility.Sequences[0].Values={1,0};
    const auto S=FWowM2Lighting::Sample(M,0,250,500);
    TestTrue(TEXT("Parent translation and child pivot rotation use model coordinates"),S.Position.Equals(FVector(100,200,0),.01));
    TestTrue(TEXT("Authored intensity uses the clip clock"),FMath::IsNearlyEqual(S.Diffuse.R,.5f,1.e-5f) && S.Visible);
    const auto Later=FWowM2Lighting::Sample(M,0,1250,750);
    TestTrue(TEXT("Clip looping does not reset global bone tracks"),Later.Position.Equals(FVector(100,250,0),.01) && FMath::IsNearlyEqual(Later.Diffuse.R,.5f,1.e-5f));
    TestFalse(TEXT("Authored byte visibility switches off the light"),FWowM2Lighting::Sample(M,0,750,500).Visible);
    TestEqual(TEXT("Authored falloff keeps full intensity at the start"),FWowM2Lighting::Attenuation(2,2,10),1.f);
    TestEqual(TEXT("Authored falloff ends at its radius"),FWowM2Lighting::Attenuation(10,2,10),0.f);
    TestTrue(TEXT("Original default attenuation is retained when the range is absent"),FMath::IsNearlyEqual(FWowM2Lighting::Attenuation(10,0,0),.1f,1.e-5f));
    TestTrue(TEXT("Default renderer radius cuts off below one percent"),FWowM2Lighting::Attenuation(S.Radius,0,0)<=.01001f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM2LightLifetime,"WowUnreal.Lighting.M2StreamingLifetime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FM2LightLifetime::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Lights=World->GetSubsystem<UWowM2LightSubsystem>();
    if(!TestNotNull(TEXT("Streaming light subsystem exists"),Lights)) {World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false;}
    auto Model=MakeShared<FM2Data>(); Model->Lights.SetNum(1);
    for(int32 Cycle=0;Cycle<3;++Cycle)
    {
        auto* Owner=World->SpawnActor<AActor>(); auto* Anchor=NewObject<USceneComponent>(Owner);
        Owner->SetRootComponent(Anchor); Anchor->RegisterComponent();
        const uint32 Id=Lights->RegisterModel(Anchor,Model);
        TestTrue(TEXT("Reload registers a new source"),Id!=0 && Lights->GetSourceCount()==1);
        Owner->Destroy(); Lights->Tick(.1f);
        TestEqual(TEXT("Unloading the owner removes every source"),Lights->GetSourceCount(),0);
        TestEqual(TEXT("No light survives its streamed owner"),Lights->GetActiveCount(),0);
    }
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return true;
}
#endif
