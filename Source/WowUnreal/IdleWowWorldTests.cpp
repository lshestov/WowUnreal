#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"
#include "WowUpdateFields.h"
#include "WowWorldManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Components/SceneComponent.h"
#include "Coord/WowCoordinate.h"
#include "TimerManager.h"
#include "WowWmoRenderer.h"
#include "WowTerrainTile.h"
#include "Formats/WmoParser.h"
#include "Formats/WdtParser.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowGlobalWmo,"WowUnreal.World.GlobalWmoMaps",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowGlobalWmo::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    TArray<uint8> Bytes; if(!Mpq.ReadFile(TEXT("World\\Maps\\StormwindJail\\StormwindJail.wdt"),Bytes)) return false;
    const auto Stockade=FWdtParser::Parse(Bytes);
    TestTrue(TEXT("Original global MODF retains names, zero-origin placement and doodad set"),Stockade.bIsValid && (Stockade.Flags&1) && Stockade.bHasGlobalWmoPlacement && Stockade.GlobalWmoPlacement.Position.IsZero() && Stockade.GlobalWmoPlacement.DoodadSet==0 && Stockade.GlobalWmoPath.EndsWith(TEXT("StormwindJail.wmo")));
    for(int32 N=0;N<Bytes.Num();++N) {const auto Cut=FWdtParser::Parse(TArray<uint8>(Bytes.GetData(),N)); if(Cut.bIsValid) {AddError(TEXT("Truncated global WDT published a usable placement")); break;}}
    auto Extra=Bytes; Extra.Add(0); TestFalse(TEXT("Incomplete trailing chunks cannot publish a WDT"),FWdtParser::Parse(Extra).bIsValid);
    TestTrue(TEXT("Global MODF zero origin matches the server's centered map coordinates"),FWowCoordinate::AdtToUE(FWowCoordinate::MAP_ORIGIN,0,FWowCoordinate::MAP_ORIGIN).Equals(FWowCoordinate::WowToUE(0,0,0)) && FWowCoordinate::WdtWmoToAdt(FVector::ZeroVector)==FVector(FWowCoordinate::MAP_ORIGIN,0,FWowCoordinate::MAP_ORIGIN));
    if(!Mpq.ReadFile(TEXT("World\\Maps\\DeadminesInstance\\DeadminesInstance.wdt"),Bytes)) return false; const auto Terrain=FWdtParser::Parse(Bytes); TestTrue(TEXT("Terrain dungeons keep their existing tile path"),Terrain.bIsValid && !(Terrain.Flags&1) && Terrain.GlobalWmoPath.IsEmpty());
    auto& D=FDbcStore::Get(); if(!D.IsLoaded()) D.LoadAll(Mpq);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false); if(!World) return false; GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Manager=World->SpawnActor<AWowWorldManager>(); if(!Manager) {World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false;}
    Manager->SelectMap(34); Manager->MpqManager=MakeUnique<FMpqManager>(); Manager->MpqManager->Initialize(Path); Manager->AssetCache=MakeUnique<FWowAssetCache>(); Manager->WdtData=MakeUnique<FWdtData>(Stockade);
    TestFalse(TEXT("A global map is not ready before its model exists"),Manager->IsInitialLoadComplete());
    TestTrue(TEXT("Global map schedules model groups without inventing ADT tiles"),Manager->LoadGlobalWmo(FWowCoordinate::WowToUE(48,0,-16)) && Manager->LoadedTiles.IsEmpty() && Manager->InitialTilesQueued==0 && Manager->PendingWmoLoads.Num()==1);
    auto* Building=Manager->GlobalWmo.Get(); TestTrue(TEXT("The collision loading gate remains closed while model groups are pending"),Building && !Manager->IsInitialLoadComplete() && Manager->GetPendingWmoGroupCount()>0);
    TestTrue(TEXT("Repeated load requests reuse the model and bounded group job"),Manager->LoadGlobalWmo(FVector::ZeroVector) && Manager->GlobalWmo==Building && Manager->PendingWmoLoads.Num()==1);
    TestTrue(TEXT("Map transfer destroys the previous global building and pending work"),Manager->SelectMap(0) && !Manager->GlobalWmo && Manager->PendingWmoLoads.IsEmpty() && Building->IsActorBeingDestroyed());
    Manager->Destroy(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowLargeWmo,"WowUnreal.World.LargeWmoStreaming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowLargeWmo::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    TArray<uint8> Bytes; if(!TestTrue(TEXT("Original Stormwind root is available from MPQ"),Mpq.ReadFile(TEXT("World\\WMO\\Azeroth\\Buildings\\Stormwind\\Stormwind.wmo"),Bytes))) return false;
    const auto City=FWmoParser::ParseRoot(Bytes); TestTrue(TEXT("The city is larger than the old 100-group skip and retains every MOGI bounding box"),City.bIsValid && City.NumGroups==286 && City.GroupBounds.Num()==286);
    const FVector Position=City.GroupBounds.IsEmpty()?FVector::ZeroVector:City.GroupBounds.Last().GetCenter(); const auto Order=FWowWmoRenderer::GroupLoadOrder(City,Position); TSet<int32> Unique(Order);
    TestTrue(TEXT("Every city room is scheduled once, with a containing room first"),Order.Num()==286 && Unique.Num()==286 && City.GroupBounds[Order[0]].ComputeSquaredDistanceToPoint(Position)==0);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false); if(!World) return false; GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Manager=World->SpawnActor<AWowWorldManager>(); if(!Manager) {World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false;}
    Manager->InitialTilesQueued=Manager->InitialTilesLoaded=1; TestFalse(TEXT("Terrain alone cannot reveal the player before building collision was scanned"),Manager->IsInitialLoadComplete());
    Manager->InitialObjectTiles=1; FPendingWmoLoad Job; Job.Order={0,1}; Job.Actor=World->SpawnActor<AActor>(); Manager->PendingWmoLoads.Add(Job);
    TestFalse(TEXT("A city job holds the login loading gate until collision is ready"),Manager->IsInitialLoadComplete());
    Job.Actor->Destroy(); Manager->UpdatePendingWmoLoads(); TestTrue(TEXT("Destroyed jobs are cancelled without loading or retaining stale geometry"),Manager->PendingWmoLoads.IsEmpty() && Manager->IsInitialLoadComplete());
    Manager->Destroy(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowServerMapSelection,"WowUnreal.World.ServerMapSelection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowServerMapSelection::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false; auto& D=FDbcStore::Get(); if(!D.IsLoaded()) D.LoadAll(Mpq);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false); if(!World) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Manager=World->SpawnActor<AWowWorldManager>(); if(!Manager) {World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false;}
    TestTrue(TEXT("A Kalimdor login selects the server's map instead of the default Azeroth terrain"),Manager->SelectMap(1) && Manager->GetMapName()==TEXT("Kalimdor"));
    Manager->bStreamingEnabled=true;
    TestTrue(TEXT("An identical map keeps its existing stream"),Manager->SelectMap(1) && Manager->bStreamingEnabled);
    auto* Tile=World->SpawnActor<AWowTerrainTile>(); auto* Building=World->SpawnActor<AActor>();
    if(!Tile || !Building) {World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false;}
    Tile->SpawnedWmos.Add(123,Building); Manager->LoadedTiles.Add(Manager->TileKey(30,30),Tile);
    TestTrue(TEXT("Another continent resets streaming before new tiles are queued"),Manager->SelectMap(0) && Manager->GetMapName()==TEXT("Azeroth") && !Manager->bStreamingEnabled && Manager->GetInitialTilesQueued()==0);
    TestTrue(TEXT("Switching maps destroys the old tile's separately spawned WMO actors"),Building->IsActorBeingDestroyed() && Tile->IsActorBeingDestroyed() && Manager->LoadedTiles.IsEmpty());
    TestTrue(TEXT("Unknown map IDs cannot replace the current terrain"),!Manager->SelectMap(MAX_uint32) && Manager->GetMapName()==TEXT("Azeroth"));
    Manager->Destroy(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowDeathLifetimeTest, "WowUnreal.World.DeathPreservesEntityUntilServerDestroy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowDeathLifetimeTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Test world"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    AWowGameplayController* Controller = World->SpawnActor<AWowGameplayController>();
    if (!TestNotNull(TEXT("Gameplay controller"), Controller))
    {
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
        return false;
    }
    Controller->ConnectionManager = NewObject<UWowConnectionManager>(Controller);
    Controller->BindEntityEvents();
    FWowEntityManager& Entities = Controller->ConnectionManager->PacketHandler.EntityManager;
    Entities.LocalPlayerGuid = 1;
    constexpr uint64 NpcGuid = 0xF130000001000123ULL;
    FWowEntity& Npc = Entities.PromoteToTyped(NpcGuid, WowTypeMask::OBJECT | WowTypeMask::UNIT);
    Npc.SetField(UnitField::HEALTH, 50);
    Npc.SetField(UnitField::MAXHEALTH, 50);
    Entities.OnEntityUpdated.Broadcast(Npc);
    AActor* Model=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(Model); Model->SetRootComponent(Root); Root->RegisterComponent();
    Model->SetActorLocation(FVector(999,999,999)); Controller->SpawnedEntityActors.Add(NpcGuid,Model); Npc.Movement.Position=FVector(1,2,3);

    // Reproduce the fatal path: a positive-to-zero health update invokes death
    // handlers while OnEntityUpdated still holds a reference to the same object.
    Npc.SetField(UnitField::HEALTH, 0);
    Entities.OnEntityUpdated.Broadcast(Npc);
    TestTrue(TEXT("Death preserves the same server entity"), Entities.Find(NpcGuid) == &Npc);
    TestTrue(TEXT("The rendered corpse uses the current server position, not a stopped visual interpolation"),Model->GetActorLocation().Equals(FWowCoordinate::WowToUE(Npc.Movement.Position),.01));
    Npc.Movement.Position=FVector(1.5,2.5,3); Entities.OnEntityUpdated.Broadcast(Npc);
    TestTrue(TEXT("A final server stop correction still reaches a dead actor"),Model->GetActorLocation().Equals(FWowCoordinate::WowToUE(Npc.Movement.Position),.01));
    World->GetTimerManager().Tick(61.f);
    TestFalse(TEXT("A local minute-long timeout cannot remove a still-authoritative corpse"),Model->IsActorBeingDestroyed());
    if (FWowEntity* Corpse = Entities.Find(NpcGuid))
    {
        TestEqual(TEXT("Corpse can receive later updates"), Corpse->GetHealth(), 0);
        Corpse->SetField(UnitField::HEALTH, 50);
        Entities.OnEntityUpdated.Broadcast(*Corpse);
        TestEqual(TEXT("Server can restore health on the same entity"), Corpse->GetHealth(), 50);
    }
    Npc.SetField(UnitField::HEALTH,0); Entities.OnEntityUpdated.Broadcast(Npc);
    Controller->TargetGuid=NpcGuid;
    Entities.Remove(NpcGuid);
    TestNull(TEXT("Explicit server removal still removes the entity"), Entities.Find(NpcGuid));
    TestTrue(TEXT("Server removal also destroys the corpse actor and clears its dead marker"),Model->IsActorBeingDestroyed() && !Controller->SpawnedEntityActors.Contains(NpcGuid) && !Controller->DeadEntityGuids.Contains(NpcGuid));
    TestEqual(TEXT("A removed corpse also clears its stale target frame"),Controller->TargetGuid,uint64(0));
    Controller->Destroy();
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}
#endif
