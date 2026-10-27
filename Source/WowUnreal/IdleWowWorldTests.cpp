#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"
#include "WowUpdateFields.h"
#include "WowWorldManager.h"
#include "Formats/Dbc/DbcStore.h"

#if WITH_DEV_AUTOMATION_TESTS
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
    TestTrue(TEXT("Another continent resets streaming before new tiles are queued"),Manager->SelectMap(0) && Manager->GetMapName()==TEXT("Azeroth") && !Manager->bStreamingEnabled && Manager->GetInitialTilesQueued()==0);
    TestTrue(TEXT("Unknown map IDs cannot replace the current terrain"),!Manager->SelectMap(MAX_uint32) && Manager->GetMapName()==TEXT("Azeroth"));
    Manager->Destroy(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowDeathLifetimeTest, "WowUnreal.World.DeathPreservesEntityUntilServerDestroy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowDeathLifetimeTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Test world"), World)) return false;
    AWowGameplayController* Controller = World->SpawnActor<AWowGameplayController>();
    if (!TestNotNull(TEXT("Gameplay controller"), Controller))
    {
        World->DestroyWorld(false);
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

    // Reproduce the fatal path: a positive-to-zero health update invokes death
    // handlers while OnEntityUpdated still holds a reference to the same object.
    Npc.SetField(UnitField::HEALTH, 0);
    Entities.OnEntityUpdated.Broadcast(Npc);
    TestTrue(TEXT("Death preserves the same server entity"), Entities.Find(NpcGuid) == &Npc);
    if (FWowEntity* Corpse = Entities.Find(NpcGuid))
    {
        TestEqual(TEXT("Corpse can receive later updates"), Corpse->GetHealth(), 0);
        Corpse->SetField(UnitField::HEALTH, 50);
        Entities.OnEntityUpdated.Broadcast(*Corpse);
        TestEqual(TEXT("Server can restore health on the same entity"), Corpse->GetHealth(), 50);
    }
    Entities.Remove(NpcGuid);
    TestNull(TEXT("Explicit server removal still removes the entity"), Entities.Find(NpcGuid));
    Controller->Destroy();
    World->DestroyWorld(false);
    return true;
}
#endif
