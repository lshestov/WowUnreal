#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/SceneComponent.h"
#include "WowWorldManager.h"
#include "Coord/WowCoordinate.h"
#include "WowWmoRenderer.h"
#include "Formats/WmoParser.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowWdlWorker,"WowUnreal.World.WdlWorkerBounds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowWdlWorker::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    auto* World=UWorld::CreateWorld(EWorldType::Game,false); GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Manager=World->SpawnActor<AWowWorldManager>(); auto* Focus=World->SpawnActor<AActor>();
    auto* Root=NewObject<USceneComponent>(Focus); Focus->SetRootComponent(Root); Root->RegisterComponent(); Focus->SetActorLocation(FWowCoordinate::WowToUE(-8930,-117,87));
    Manager->FollowStreamingActor(Focus); Manager->MapName=TEXT("Azeroth"); Manager->MpqManager=MakeUnique<FMpqManager>();
    if(!Manager->MpqManager->Initialize(Path)){World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false;}
    Manager->WdtData=MakeUnique<FWdtData>(); Manager->WdtData->bUseBigAlpha=true;
    for(int32 I=0;I<5;++I){FPendingTileLoad P; P.TX=32+I; P.TY=48; P.Wdl=true; Manager->PendingLoads.Add(MoveTemp(P));}
    Manager->StartPendingTileLoads();
    TestEqual(TEXT("WDL transition reads share the four-result slot limit"),Manager->GetPreparedTileCount(),4);
    bool Actual=false;
    for(auto& P:Manager->PendingLoads) if(P.Future.IsValid())
    {
        auto Result=P.Future.Get(); TestFalse(TEXT("Transition readers never prepare full terrain render/alpha/collision geometry"),Result.Geometry.IsValid());
        if(P.TX==32){Actual=Result.Data.IsValid() && Result.Data->bIsValid && Result.Data->Chunks[0].AreaId>0;}
    }
    TestTrue(TEXT("The source ADT, including real area/edge heights, crosses the worker handoff"),Actual);
    Manager->PendingLoads.Empty();
    FPendingTileLoad Tile; Tile.TX=32; Tile.TY=48; Tile.Wdl=true; Manager->PendingLoads.Add(MoveTemp(Tile)); Manager->StartPendingTileLoads();
    TArray<uint8> Raw; const FString Wmo=TEXT("World\\Wmo\\Azeroth\\Buildings\\NSabbey\\NSabbey.wmo");
    if(!Manager->MpqManager->ReadFile(Wmo,Raw)){AddError(TEXT("Original WMO root unavailable")); return false;}
    auto WmoRoot=MakeShared<FWmoRootData,ESPMode::ThreadSafe>(FWmoParser::ParseRoot(Raw));
    for(int32 I=0;I<4;++I){FPendingWmoLoad J; J.Actor=Focus; J.Root=WmoRoot; J.Path=Wmo; J.Mpq=Manager->MpqManager.Get(); J.Order={0}; Manager->PendingWmoLoads.Add(MoveTemp(J));}
    Manager->StartPendingWmoReads(); TestEqual(TEXT("WMO and terrain readers share four active/ready slots"),Manager->GetPreparedTileCount(),4);
    Focus->Destroy(); Manager->UpdatePendingWmoLoads();
    TestTrue(TEXT("Cancelled actors retire their futures without releasing active slot accounting"),Manager->PendingWmoLoads.IsEmpty() && Manager->RetiredWmoReads.Num()==3 && Manager->GetPreparedTileCount()==4);
    for(auto& F:Manager->RetiredWmoReads){auto R=F.Get(); TestTrue(TEXT("Cancelled worker finishes immutable source geometry/lighting without touching a dead actor"),R && R->Data && R->RenderData && R->Lighting.Num()==R->Data->Vertices.Num());}
    Manager->RetiredWmoReads.Empty(); for(auto& P:Manager->PendingLoads) if(P.Future.IsValid()) P.Future.Get(); Manager->PendingLoads.Empty();
    Manager->AssetCache=MakeUnique<FWowAssetCache>(); FAdtWmoPlacement Placement;
    auto* Building=FWowWmoRenderer::SpawnWmo(World,Wmo,Placement,Manager->MpqManager.Get(),Manager->AssetCache.Get(),nullptr,&WmoRoot.Get(),0,0);
    FPendingWmoLoad Sequence; Sequence.Actor=Building; Sequence.Root=WmoRoot; Sequence.Path=Wmo; Sequence.Placement=Placement; Sequence.Mpq=Manager->MpqManager.Get(); Sequence.Cache=Manager->AssetCache.Get(); Sequence.Order={0,1}; Manager->PendingWmoLoads.Add(MoveTemp(Sequence));
    Manager->StartPendingWmoReads(); Manager->PendingWmoLoads[0].Future.Wait(); Manager->UpdatePendingWmoLoads();
    TestTrue(TEXT("Consuming one prepared group releases its future so the next group cannot reuse a consumed result"),Manager->PendingWmoLoads.Num()==1 && Manager->PendingWmoLoads[0].Next==1 && !Manager->PendingWmoLoads[0].Future.IsValid());
    Manager->StartPendingWmoReads();
    if(Manager->PendingWmoLoads[0].Future.IsValid()) {Manager->PendingWmoLoads[0].Future.Wait(); Manager->UpdatePendingWmoLoads();}
    TestTrue(TEXT("The second original WMO group completes through its own worker handoff"),Manager->PendingWmoLoads.IsEmpty());
    if(Building) Building->Destroy();
    Manager->Destroy(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
