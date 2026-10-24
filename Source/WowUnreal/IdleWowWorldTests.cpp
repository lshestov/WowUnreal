#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"
#include "WowUpdateFields.h"

#if WITH_DEV_AUTOMATION_TESTS
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
