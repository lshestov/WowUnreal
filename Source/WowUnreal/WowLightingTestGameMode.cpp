#include "WowLightingTestGameMode.h"
#include "WowWorldManager.h"
#include "WowSkyManager.h"
#include "WowWmoArea.h"
#include "WowM2LightSubsystem.h"
#include "WowDoodadManager.h"
#include "WowCharacterBuilder.h"
#include "Coord/WowCoordinate.h"
#include "Formats/Dbc/DbcStore.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "RHI.h"

DEFINE_LOG_CATEGORY_STATIC(LogLightingCapture,Log,All);
namespace
{
FVector Vector(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,const FVector& Default=FVector::ZeroVector)
{
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
    return O->TryGetArrayField(Key,A) && A->Num()==3?FWowCoordinate::WowToUE(FVector((*A)[0]->AsNumber(),(*A)[1]->AsNumber(),(*A)[2]->AsNumber())):Default;
}
double Number(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,double Default)
{double V=Default; O->TryGetNumberField(Key,V); return V;}
TSharedPtr<FJsonObject> Stats(TArray<double> Values)
{
    auto O=MakeShared<FJsonObject>(); Values.Sort(); double Sum=0; for(double V:Values) Sum+=V;
    O->SetNumberField(TEXT("samples"),Values.Num());
    if(Values.Num())
    {
        O->SetNumberField(TEXT("mean"),Sum/Values.Num()); O->SetNumberField(TEXT("median"),Values[Values.Num()/2]);
        O->SetNumberField(TEXT("p95"),Values[FMath::Min(Values.Num()-1,FMath::FloorToInt(Values.Num()*.95))]);
        O->SetNumberField(TEXT("max"),Values.Last()); O->SetNumberField(TEXT("zeroSamples"),Values.FilterByPredicate([](double V){return FMath::Abs(V)<1.e-7;}).Num());
    }
    return O;
}
}
AWowLightingTestGameMode::AWowLightingTestGameMode()
{PrimaryActorTick.bCanEverTick=true; HUDClass=nullptr;}
void AWowLightingTestGameMode::SetupTestScene(UWorld* World,AWowWorldManager* WM)
{
    Started=FPlatformTime::Seconds();
    FString Spec,Text;
    if(!FParse::Value(FCommandLine::Get(),TEXT("lightingsteps="),Spec) || !FParse::Value(FCommandLine::Get(),TEXT("lightingoutput="),Output) || !FFileHelper::LoadFileToString(Text,*Spec))
    {Finish(false,TEXT("Supply readable -lightingsteps and -lightingoutput")); return;}
    TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root) || !Root.IsValid()) {Finish(false,TEXT("Invalid spec")); return;}
    const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;
    if(!Root->TryGetArrayField(TEXT("cases"),Entries) || Entries->IsEmpty()) {Finish(false,TEXT("Empty cases")); return;}
    Cases=*Entries;
    // Match the game's fixed exposure/AO; no gallery lights or synthetic floor.
    for(TActorIterator<APostProcessVolume> It(World);It;++It)
    {
        auto& S=It->Settings; S.bOverride_AmbientOcclusionIntensity=true; S.AmbientOcclusionIntensity=.85f;
        S.bOverride_AmbientOcclusionRadius=true; S.AmbientOcclusionRadius=75;
        S.bOverride_AmbientOcclusionRadiusInWS=true; S.AmbientOcclusionRadiusInWS=true;
        S.bOverride_AmbientOcclusionQuality=true; S.AmbientOcclusionQuality=60;
    }
    if(!WM || !WM->GetMpqManager()) {Finish(false,TEXT("Missing world manager")); return;}
    WM->SelectMap(uint32(Number(Root,TEXT("map"),0)));
    const FVector Start=Vector(Cases[0]->AsObject(),TEXT("position"));
    if(auto* PC=UGameplayStatics::GetPlayerController(this,0); PC && PC->GetPawn()) PC->GetPawn()->SetActorLocation(Start);
    Sky=World->SpawnActor<AWowSkyManager>(); Sky->CurrentMapId=uint32(Number(Root,TEXT("map"),0)); Sky->TimeSpeed=0;
    if(Number(Root,TEXT("legacyLoginSun"),0)>0)
    {
        auto* Light=World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(),FTransform(FRotator(-45,30,0),FVector::ZeroVector));
        Light->GetLightComponent()->SetIntensity(3.14159f); Light->GetLightComponent()->SetLightColor(FLinearColor(1,.95,.85));
    }
    if(Root->HasField(TEXT("skyTickInterval"))) Sky->SetActorTickInterval(Number(Root,TEXT("skyTickInterval"),0));
    WM->EnableTerrainStreaming(); WM->LoadTilesAroundPosition(Start,2);
    UE_LOG(LogLightingCapture,Display,TEXT("LightingCapture loading map=%s cases=%d"),*WM->GetMapName(),Cases.Num());
}
void AWowLightingTestGameMode::StartCase()
{
    if(++Index>=Cases.Num()) {Finish(true); return;}
    const auto C=Cases[Index]->AsObject();
    FString Command; if(C->TryGetStringField(TEXT("command"),Command)) GEngine->Exec(GetWorld(),*Command);
    if(Fixture) {Fixture->Destroy(); Fixture=nullptr;}
    FString ModelPath;
    if(C->TryGetStringField(TEXT("model"),ModelPath))
    {
        const auto Model=FWowDoodadManager::GetOrParseM2(ModelPath,CachedWorldManager->GetMpqManager());
        auto* Mesh=FWowDoodadManager::GetOrCreateStaticMesh(ModelPath,CachedWorldManager->GetMpqManager(),CachedWorldManager->GetAssetCache());
        if(!Model || !Mesh) {Finish(false,TEXT("Cannot load authored fixture")); return;}
        auto* Actor=GetWorld()->SpawnActor<AStaticMeshActor>(); Fixture=Actor;
        Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable); Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
        Actor->SetActorLocation(Vector(C,TEXT("modelPosition"))); Actor->SetActorRotation(FRotator(0,Number(C,TEXT("modelYaw"),0),0));
        Actor->SetActorScale3D(FVector(Number(C,TEXT("modelScale"),1)));
        GetWorld()->GetSubsystem<UWowM2LightSubsystem>()->RegisterModel(Actor->GetStaticMeshComponent(),Model);
    }
    From=Vector(C,TEXT("position")); To=Vector(C,TEXT("end"),From); Target=Vector(C,TEXT("target"),From+FVector(100,0,0));
    Sky->TimeOfDay=Number(C,TEXT("time"),540); Sky->TimeSpeed=Number(C,TEXT("timeSpeed"),0);
    auto* PC=UGameplayStatics::GetPlayerController(this,0);
    PC->GetPawn()->SetActorLocation(From); PC->SetControlRotation((Target-From).Rotation());
    const FVector P=Vector(C,TEXT("subject"),From-FVector(0,0,170));
    if(!Subject) Subject=FWowCharacterBuilder::SpawnCharacter(GetWorld(),CachedWorldManager->GetMpqManager(),CachedWorldManager->GetAssetCache(),
        FWowCharacterBuilder::ERace::Human,FWowCharacterBuilder::EGender::Male,P,FRotator(0,180,0));
    if(Subject) Subject->SetActorLocation(P);
    CachedWorldManager->FollowStreamingActor(PC->GetPawn());
    PhaseStarted=FPlatformTime::Seconds(); NextShot=Number(C,TEXT("settle"),3)+(Number(C,TEXT("shots"),1)>1?.5:Number(C,TEXT("duration"),3));
    Profile.Reset(); Shot=0; PreviousSun=FVector::ZeroVector;
    UE_LOG(LogLightingCapture,Display,TEXT("LightingCapture case=%s time=%.1f from=%s to=%s"),*C->GetStringField(TEXT("name")),Sky->TimeOfDay,*From.ToString(),*To.ToString());
}
void AWowLightingTestGameMode::Tick(float DT)
{
    Super::Tick(DT); if(Finished) return;
    const double Now=FPlatformTime::Seconds();
    if(Now-Started>600) {Finish(false,TEXT("600 second timeout")); return;}
    if(!Ready)
    {
        if(!CachedWorldManager || !CachedWorldManager->IsInitialLoadComplete() || !FDbcStore::Get().IsDeferredLoaded()) return;
        Ready=true; Sky->LoadLightZones(Sky->CurrentMapId); StartCase(); return;
    }
    if(Index<0 || Index>=Cases.Num()) return;
    const auto C=Cases[Index]->AsObject(); const double Elapsed=Now-PhaseStarted,Settle=Number(C,TEXT("settle"),3),Duration=Number(C,TEXT("duration"),3);
    auto* PC=UGameplayStatics::GetPlayerController(this,0); APawn* Pawn=PC?PC->GetPawn():nullptr; if(!Pawn) {Finish(false,TEXT("No camera")); return;}
    const FVector P=FMath::Lerp(From,To,FMath::Clamp((Elapsed-Settle)/FMath::Max(.01,Duration),0.,1.));
    Pawn->SetActorLocation(P); PC->SetControlRotation((Target-P).Rotation());
    if(Elapsed>=Settle && Elapsed<=Settle+Duration)
    {
        Profile.FindOrAdd(TEXT("frameMs")).Add(DT*1000.);
        Profile.FindOrAdd(TEXT("gameThreadMs")).Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
        Profile.FindOrAdd(TEXT("renderThreadMs")).Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
        Profile.FindOrAdd(TEXT("gpuMs")).Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
        if(auto* Lights=GetWorld()->GetSubsystem<UWowM2LightSubsystem>()) Profile.FindOrAdd(TEXT("m2LightTickMs")).Add(Lights->GetTickMilliseconds());
        TInlineComponentArray<UDirectionalLightComponent*> Directions(Sky);
        for(auto* Light:Directions) if(Light->GetName()==TEXT("SunLight"))
        {
            const FVector Sun=Light->GetForwardVector();
            if(!PreviousSun.IsZero()) Profile.FindOrAdd(TEXT("sunStepDegrees")).Add(FMath::RadiansToDegrees(FMath::Atan2(FVector::CrossProduct(Sun,PreviousSun).Length(),FVector::DotProduct(Sun,PreviousSun))));
            PreviousSun=Sun;
        }
    }
    if(Shot<Number(C,TEXT("shots"),1) && Elapsed>=NextShot)
    {
        const FString Name=C->GetStringField(TEXT("name"));
        const FString Path=FPaths::Combine(Output,FString::Printf(TEXT("%s-%02d.png"),*Name,Shot));
        IFileManager::Get().MakeDirectory(*Output,true); FScreenshotRequest::RequestScreenshot(Path,false,false);
        UE_LOG(LogLightingCapture,Display,TEXT("LightingCapture shot=%s camera=%s"),*Path,*P.ToString());
        ++Shot; NextShot+=Number(C,TEXT("interval"),.25);
    }
    if(Shot>=Number(C,TEXT("shots"),1) && Elapsed>=FMath::Max(NextShot+.25,Settle+Duration))
    {
        auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("name"),C->GetStringField(TEXT("name"))); Row->SetNumberField(TEXT("time"),Sky->TimeOfDay);
        auto Metrics=MakeShared<FJsonObject>(); for(const auto& Pair:Profile) Metrics->SetObjectField(Pair.Key,Stats(Pair.Value)); Row->SetObjectField(TEXT("profile"),Metrics);
        Row->SetNumberField(TEXT("tiles"),CachedWorldManager->GetLoadedTileCount()); Row->SetNumberField(TEXT("wmoGroups"),CachedWorldManager->GetActiveWmoGroupCount());
        Row->SetNumberField(TEXT("pendingWmoGroups"),CachedWorldManager->GetPendingWmoGroupCount());
        const auto L=UWowWmoAreaComponent::Locate(GetWorld(),Subject?Subject->GetActorLocation():P,0);
        Row->SetBoolField(TEXT("indoors"),L.Indoors); Row->SetNumberField(TEXT("group"),L.Group);
        Row->SetStringField(TEXT("floorLight"),L.Lighting.ToString()); Row->SetNumberField(TEXT("outdoorLight"),L.OutdoorLight);
        int32 Points=0,ShadowCasters=0,Directions=0; TArray<TSharedPtr<FJsonValue>> Authored;
        for(TActorIterator<AActor> It(GetWorld());It;++It)
        {
            TInlineComponentArray<UPointLightComponent*> Lights(*It); for(auto* Light:Lights) if(Light->IsVisible())
            {
                ++Points;
                if(Light->ComponentHasTag(TEXT("WowAuthoredM2Light")))
                {
                    auto Entry=MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("position"),FWowCoordinate::UEToWow(Light->GetComponentLocation()).ToString());
                    Entry->SetNumberField(TEXT("radiusYards"),Light->AttenuationRadius/100); Entry->SetNumberField(TEXT("intensity"),Light->Intensity);
                    Entry->SetBoolField(TEXT("falloffMaterial"),Light->LightFunctionMaterial!=nullptr); Authored.Add(MakeShared<FJsonValueObject>(Entry));
                }
            }
            TInlineComponentArray<UDirectionalLightComponent*> Directional(*It); Directions+=Directional.Num();
            TInlineComponentArray<UPrimitiveComponent*> Meshes(*It); for(auto* Mesh:Meshes) if(Mesh->CastShadow && Mesh->GetName().StartsWith(TEXT("WmoGroup_"))) ++ShadowCasters;
        }
        Row->SetNumberField(TEXT("pointLights"),Points); Row->SetNumberField(TEXT("wmoShadowCasters"),ShadowCasters);
        Row->SetNumberField(TEXT("directionalLights"),Directions); Row->SetArrayField(TEXT("authoredLights"),Authored);
        if(auto* Lights=GetWorld()->GetSubsystem<UWowM2LightSubsystem>())
        {
            Row->SetNumberField(TEXT("m2Sources"),Lights->GetSourceCount()); Row->SetNumberField(TEXT("m2Active"),Lights->GetActiveCount());
            Row->SetNumberField(TEXT("m2Pending"),Lights->GetPendingCount()); Row->SetNumberField(TEXT("m2MaxLoadMs"),Lights->GetMaxLoadMilliseconds());
        }
        Results.Add(MakeShared<FJsonValueObject>(Row)); StartCase();
    }
}
void AWowLightingTestGameMode::Finish(bool Passed,const FString& Reason)
{
    Finished=true; auto Report=MakeShared<FJsonObject>(); Report->SetBoolField(TEXT("passed"),Passed); Report->SetStringField(TEXT("failure"),Reason);
    Report->SetBoolField(TEXT("networkLogin"),false); Report->SetArrayField(TEXT("cases"),Results);
    FString Text; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
    if(!Output.IsEmpty()) {IFileManager::Get().MakeDirectory(*Output,true); FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Output,TEXT("runtime.json")));}
    UE_LOG(LogLightingCapture,Display,TEXT("LightingCapture %s cases=%d reason=%s"),Passed?TEXT("PASS"):TEXT("FAIL"),Results.Num(),*Reason);
    FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
}
