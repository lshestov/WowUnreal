#include "WowPerformanceTestGameMode.h"
#include "WowWorldManager.h"
#include "WowTerrainTile.h"
#include "WowSkyManager.h"
#include "WowM2LightSubsystem.h"
#include "WowWmoArea.h"
#include "WowAssetCache.h"
#include "WowDoodadManager.h"
#include "WowCharacterBuilder.h"
#include "Coord/WowCoordinate.h"
#include "Formats/Dbc/DbcStore.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformMemory.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectArray.h"
#include "UObject/GarbageCollection.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "UnrealClient.h"
#include "RHI.h"
#include "RHIStats.h"
#include "RenderTimer.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPerformance,Log,All);
namespace
{
double Number(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,double Default)
{double V=Default; O->TryGetNumberField(Key,V); return V;}
FVector Position(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,const FVector& Default=FVector::ZeroVector)
{
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
    return O->TryGetArrayField(Key,A) && A->Num()==3?FWowCoordinate::WowToUE(FVector((*A)[0]->AsNumber(),(*A)[1]->AsNumber(),(*A)[2]->AsNumber())):Default;
}
TSharedPtr<FJsonObject> Distribution(TArray<double> V)
{
    auto O=MakeShared<FJsonObject>(); O->SetNumberField(TEXT("samples"),V.Num()); if(V.IsEmpty()) return O;
    V.Sort(); double Sum=0; int32 Hitches=0; for(double N:V){Sum+=N; if(N>50) ++Hitches;}
    const auto Q=[&](double P){return V[FMath::Clamp(FMath::CeilToInt(V.Num()*P)-1,0,V.Num()-1)];};
    O->SetNumberField(TEXT("mean"),Sum/V.Num()); O->SetNumberField(TEXT("median"),Q(.5));
    O->SetNumberField(TEXT("p95"),Q(.95)); O->SetNumberField(TEXT("p99"),Q(.99)); O->SetNumberField(TEXT("max"),V.Last());
    O->SetNumberField(TEXT("above50"),Hitches); return O;
}
TSharedPtr<FJsonObject> Profile(const TMap<FString,TArray<double>>& Map)
{auto O=MakeShared<FJsonObject>(); for(const auto& P:Map) O->SetObjectField(P.Key,Distribution(P.Value)); return O;}
}
AWowPerformanceTestGameMode::AWowPerformanceTestGameMode()
{PrimaryActorTick.bCanEverTick=true; HUDClass=nullptr;}
void AWowPerformanceTestGameMode::SetupTestScene(UWorld* World,AWowWorldManager* WM)
{
    Started=Previous=FPlatformTime::Seconds(); FString Spec,Text;
    if(!FParse::Value(FCommandLine::Get(),TEXT("perfsteps="),Spec) || !FParse::Value(FCommandLine::Get(),TEXT("perfoutput="),Output) || !FFileHelper::LoadFileToString(Text,*Spec))
    {Finish(false,TEXT("Supply readable perfsteps/perfoutput")); return;}
    TSharedPtr<FJsonObject> Root; const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("steps"),Entries) || Entries->IsEmpty())
    {Finish(false,TEXT("Invalid route")); return;}
    Steps=*Entries;
    FCoreUObjectDelegates::GetPostGarbageCollect().AddWeakLambda(this,[this](){++GarbageCollections;});
    Frames=TEXT("phase,kind,elapsedSeconds,wallFrameMs,gameThreadMs,renderThreadMs,gpuMs,drawCalls,pendingTiles,pendingWmoGroups,pendingDoodadTiles,pendingLod1,preparedSlots\n");
    Sky=World->SpawnActor<AWowSkyManager>(); Sky->TimeOfDay=540; Sky->TimeSpeed=0;
    StartStep();
}
void AWowPerformanceTestGameMode::StartStep()
{
    if(++Index>=Steps.Num()){Finish(true); return;}
    const auto Step=Steps[Index]->AsObject(); auto* WM=CachedWorldManager.Get();
    const uint32 Map=uint32(Number(Step,TEXT("map"),0));
    if(!WM || !WM->SelectMap(Map)){Finish(false,TEXT("Map not available")); return;}
    Sky->CurrentMapId=Map; Sky->LoadLightZones(Map);
    From=Position(Step,TEXT("position")); To=Position(Step,TEXT("end"),From); Target=Position(Step,TEXT("target"),To+FVector(100,0,0));
    auto* PC=UGameplayStatics::GetPlayerController(this,0); if(!PC || !PC->GetPawn()){Finish(false,TEXT("No camera")); return;}
    PC->GetPawn()->SetActorLocation(From); PC->SetControlRotation((Target-From).Rotation());
    WM->FollowStreamingActor(PC->GetPawn()); WM->EnableTerrainStreaming();
    if(Number(Step,TEXT("initial"),0)>0) WM->LoadTilesAroundPosition(From,2);
    Ordinary.Reset(); Capture.Reset(); PhaseStarted=FPlatformTime::Seconds(); IdleSince=ShotAt=0; ShotRequested=false;
    DiagnosticCollected=false; BeforeCollectMiB=0;
    UE_LOG(LogWorldPerformance,Display,TEXT("WorldPerformance step=%s map=%u"),*Step->GetStringField(TEXT("name")),Map);
}
TSharedPtr<FJsonObject> AWowPerformanceTestGameMode::Snapshot() const
{
    auto O=MakeShared<FJsonObject>(); const auto Mem=FPlatformMemory::GetStats();
    O->SetNumberField(TEXT("workingSetMiB"),Mem.UsedPhysical/1048576.); O->SetNumberField(TEXT("privateMiB"),Mem.UsedVirtual/1048576.);
    O->SetNumberField(TEXT("uobjects"),GUObjectArray.GetObjectArrayNumMinusAvailable());
    int32 Tiles=0,Sections=0,Slots=0,DoodadInstances=0,WmoCasters=0,Reused=0; uint64 AlphaBytes=0;
    TSet<const UTexture2D*> AlphaAtlases;
    TSet<const UStaticMesh*> Meshes; TSet<const UMaterialInterface*> Materials;
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        if(It->IsActorBeingDestroyed()) continue;
        if(const auto* Tile=Cast<AWowTerrainTile>(*It); Tile && Tile->WasMeshReused()) ++Reused;
        TInlineComponentArray<UStaticMeshComponent*> Components(*It);
        for(auto* C:Components)
        {
            const UStaticMesh* M=C->GetStaticMesh(); if(!M) continue; Meshes.Add(M);
            if(const auto* ISM=Cast<UInstancedStaticMeshComponent>(C)) DoodadInstances+=ISM->GetInstanceCount();
            if(C->GetName().StartsWith(TEXT("WmoGroup_")) && C->CastShadow) ++WmoCasters;
            if(It->IsA<AWowTerrainTile>() && C->GetName()==TEXT("TerrainMesh"))
            {
                ++Tiles; Slots+=M->GetStaticMaterials().Num();
                if(M->GetRenderData() && !M->GetRenderData()->LODResources.IsEmpty()) Sections+=M->GetRenderData()->LODResources[0].Sections.Num();
                for(int32 I=0;I<C->GetNumMaterials();++I) Materials.Add(C->GetMaterial(I));
                if(auto* Material=C->GetMaterial(0))
                {UTexture* Texture=nullptr; if(Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("AlphaAtlas")),Texture)) if(const auto* Alpha=Cast<UTexture2D>(Texture)) AlphaAtlases.Add(Alpha);}
            }
        }
    }
    const auto Stats=CachedWorldManager->GetAssetCache()->GetStats();
    for(const auto* Alpha:AlphaAtlases) AlphaBytes+=Alpha->CalcTextureMemorySizeEnum(TMC_AllMips);
    const auto WorldCache=FWowDoodadManager::GetParsedCacheStats(),CharCache=FWowCharacterBuilder::GetParsedCacheStats();
    O->SetNumberField(TEXT("worldParsedM2MiB"),WorldCache.Bytes/1048576.); O->SetNumberField(TEXT("worldParsedM2Entries"),WorldCache.Entries); O->SetNumberField(TEXT("worldParsedM2Evictions"),WorldCache.Evictions);
    O->SetNumberField(TEXT("characterParsedM2MiB"),CharCache.Bytes/1048576.); O->SetNumberField(TEXT("characterParsedM2Entries"),CharCache.Entries);
    O->SetNumberField(TEXT("terrainAlphaAtlasMiB"),AlphaBytes/1048576.); O->SetNumberField(TEXT("sourceTextureMipMiB"),Stats.EstimatedTextureMemory/1048576.);
    O->SetNumberField(TEXT("doodadInstances"),DoodadInstances); O->SetNumberField(TEXT("reusedTerrainTiles"),Reused); O->SetNumberField(TEXT("garbageCollections"),GarbageCollections);
    O->SetBoolField(TEXT("diagnosticCollection"),DiagnosticCollected); O->SetNumberField(TEXT("beforeCollectPrivateMiB"),BeforeCollectMiB);
    O->SetNumberField(TEXT("peakPrivateMiB"),Mem.PeakUsedVirtual/1048576.);
    O->SetNumberField(TEXT("tiles"),Tiles); O->SetNumberField(TEXT("terrainSections"),Sections); O->SetNumberField(TEXT("terrainMaterialSlots"),Slots);
    O->SetNumberField(TEXT("terrainUniqueMaterials"),Materials.Num()); O->SetNumberField(TEXT("uniqueStaticMeshes"),Meshes.Num());
    O->SetNumberField(TEXT("wmoShadowCasters"),WmoCasters); O->SetNumberField(TEXT("wmoGroups"),CachedWorldManager->GetActiveWmoGroupCount());
    O->SetNumberField(TEXT("pendingTiles"),CachedWorldManager->GetPendingTileCount()); O->SetNumberField(TEXT("pendingWmoGroups"),CachedWorldManager->GetPendingWmoGroupCount());
    O->SetNumberField(TEXT("pendingDoodadTiles"),CachedWorldManager->GetPendingDoodadJobs()); O->SetNumberField(TEXT("pendingLod1"),CachedWorldManager->GetPendingLod1Count());
    O->SetNumberField(TEXT("lod1Tiles"),CachedWorldManager->GetLod1TileCount()); O->SetNumberField(TEXT("wdlTiles"),CachedWorldManager->GetWdlTileCount());
    O->SetNumberField(TEXT("preparedSlots"),CachedWorldManager->GetPreparedTileCount());
    O->SetNumberField(TEXT("missingTerrainCoverage"),CachedWorldManager->GetMissingTerrainCoverage());
    O->SetNumberField(TEXT("cachedTextures"),Stats.TextureCount); O->SetNumberField(TEXT("cachedMeshes"),Stats.MeshCount);
    O->SetNumberField(TEXT("estimatedAssetMiB"),Stats.TotalEstimatedMemory()/1048576.);
    if(auto* L=GetWorld()->GetSubsystem<UWowM2LightSubsystem>())
    {O->SetNumberField(TEXT("lightSources"),L->GetSourceCount()); O->SetNumberField(TEXT("lightPending"),L->GetPendingCount());}
    const auto P=FWowCoordinate::UEToWow(To); const auto Room=UWowWmoAreaComponent::Locate(GetWorld(),To,0);
    O->SetStringField(TEXT("position"),P.ToString()); O->SetNumberField(TEXT("area"),CachedWorldManager->GetAreaAt(To)); O->SetBoolField(TEXT("indoors"),Room.Indoors);
    return O;
}
void AWowPerformanceTestGameMode::Tick(float DT)
{
    Super::Tick(DT); if(Finished || Index<0 || Index>=Steps.Num()) return;
    const double Now=FPlatformTime::Seconds(),WallMs=(Now-Previous)*1000.; Previous=Now;
    if(Now-Started>1200){Finish(false,TEXT("1200 second route timeout")); return;}
    const auto Step=Steps[Index]->AsObject(); const FString Name=Step->GetStringField(TEXT("name"));
    auto& Samples=DiagnosticFrames>0?Capture:Ordinary;
    const double Game=FPlatformTime::ToMilliseconds(GGameThreadTime),Render=FPlatformTime::ToMilliseconds(GRenderThreadTime),Gpu=FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
    Samples.FindOrAdd(TEXT("wallFrameMs")).Add(WallMs); Samples.FindOrAdd(TEXT("gameThreadMs")).Add(Game);
    Samples.FindOrAdd(TEXT("renderThreadMs")).Add(Render); Samples.FindOrAdd(TEXT("gpuMs")).Add(Gpu); Samples.FindOrAdd(TEXT("drawCalls")).Add(GNumDrawCallsRHI[0]);
    Samples.FindOrAdd(TEXT("presentMs")).Add(FPlatformTime::ToMilliseconds(GSwapBufferTime));
    Frames+=FString::Printf(TEXT("%s,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,%d,%d,%d\n"),*Name,DiagnosticFrames>0?TEXT("diagnostic"):TEXT("ordinary"),Now-PhaseStarted,WallMs,Game,Render,Gpu,GNumDrawCallsRHI[0],CachedWorldManager->GetPendingTileCount(),CachedWorldManager->GetPendingWmoGroupCount(),CachedWorldManager->GetPendingDoodadJobs(),CachedWorldManager->GetPendingLod1Count(),CachedWorldManager->GetPreparedTileCount());
    if(DiagnosticFrames>0) --DiagnosticFrames;
    const double Elapsed=Now-PhaseStarted,Duration=Number(Step,TEXT("duration"),8);
    auto* PC=UGameplayStatics::GetPlayerController(this,0); APawn* Pawn=PC?PC->GetPawn():nullptr;
    if(!Pawn){Finish(false,TEXT("Camera lost")); return;}
    const FVector P=FMath::Lerp(From,To,FMath::Clamp(Elapsed/FMath::Max(.01,Duration),0.,1.));
    Pawn->SetActorLocation(P); PC->SetControlRotation((Target-P).Rotation());
    const bool Ready=(!Number(Step,TEXT("initial"),0) || CachedWorldManager->IsInitialLoadComplete()) && CachedWorldManager->IsStreamingWorkIdle() && FDbcStore::Get().IsDeferredLoaded();
    if(Ready){if(!IdleSince) IdleSince=Now;} else IdleSince=0;
    if(!ShotRequested && Elapsed>=Duration && IdleSince && Now-IdleSince>=Number(Step,TEXT("settle"),2))
    {
        if(Number(Step,TEXT("collect"),0)>0 && !DiagnosticCollected)
        {
            BeforeCollectMiB=FPlatformMemory::GetStats().UsedVirtual/1048576.; DiagnosticCollected=true; DiagnosticFrames=4;
            CollectGarbage(RF_NoFlags); IdleSince=0; return;
        }
        auto Row=Snapshot(); Row->SetStringField(TEXT("name"),Name); Row->SetNumberField(TEXT("secondsToSettled"),Elapsed); Row->SetObjectField(TEXT("ordinary"),Profile(Ordinary));
        Results.Add(MakeShared<FJsonValueObject>(Row));
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Output,Name+TEXT(".png")),false,false);
        DiagnosticFrames=4; ShotRequested=true; ShotAt=Now;
        UE_LOG(LogWorldPerformance,Display,TEXT("WorldPerformance settled=%s seconds=%.3f tiles=%d"),*Name,Elapsed,CachedWorldManager->GetLoadedTileCount());
    }
    if(ShotRequested && Now-ShotAt>=.5 && DiagnosticFrames==0)
    {
        Results.Last()->AsObject()->SetObjectField(TEXT("diagnostic"),Profile(Capture));
        // Step setup/snapshot cost is explicitly kept out of ordinary frame samples.
        DiagnosticFrames=4; StartStep();
    }
}
void AWowPerformanceTestGameMode::Finish(bool Passed,const FString& Reason)
{
    Finished=true; auto Report=MakeShared<FJsonObject>(); Report->SetBoolField(TEXT("passed"),Passed); Report->SetStringField(TEXT("failure"),Reason);
    Report->SetBoolField(TEXT("networkLogin"),false); Report->SetArrayField(TEXT("steps"),Results);
    FString Text; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));
    if(!Output.IsEmpty())
    {IFileManager::Get().MakeDirectory(*Output,true); FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Output,TEXT("runtime.json"))); FFileHelper::SaveStringToFile(Frames,*FPaths::Combine(Output,TEXT("frames.csv")));}
    UE_LOG(LogWorldPerformance,Display,TEXT("WorldPerformance %s steps=%d reason=%s"),Passed?TEXT("PASS"):TEXT("FAIL"),Results.Num(),*Reason);
    FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
}
