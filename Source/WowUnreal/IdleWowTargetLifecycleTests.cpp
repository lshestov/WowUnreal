#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowUpdateFields.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FTargetWire
{
    TArray<uint8> Data;
    template<typename T> FTargetWire& Add(T Value)
    { Data.Append(reinterpret_cast<const uint8*>(&Value),sizeof(Value)); return *this; }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowTargetLifecycleTest,"WowUnreal.World.TargetDeathAndLootLifecycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowTargetLifecycleTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Test world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Controller=World->SpawnActor<AWowGameplayController>();
    Controller->ConnectionManager=NewObject<UWowConnectionManager>(Controller);
    Controller->BindEntityEvents();
    auto& Connection=*Controller->ConnectionManager;
    auto& H=Connection.PacketHandler;
    constexpr uint64 Corpse=0xF130000006000101ULL,Other=0xF130000006000102ULL,Player=2;
    H.EntityManager.LocalPlayerGuid=1;
    for(const uint64 Guid:{Corpse,Other,Player})
    {
        auto& Unit=H.EntityManager.PromoteToTyped(Guid,WowTypeMask::OBJECT|WowTypeMask::UNIT|(Guid==Player?WowTypeMask::PLAYER:0));
        Unit.SetField(UnitField::HEALTH,50); Unit.SetField(UnitField::MAXHEALTH,50);
    }
    auto Health=[&](uint64 Guid,uint32 Value)
    {
        FTargetWire W; W.Add(uint32(1)).Add(uint8(0)).Add(uint8(0xff)).Add(Guid);
        const uint8 Blocks=UnitField::HEALTH/32+1; W.Add(Blocks);
        for(uint8 I=0;I<Blocks;++I) W.Add(uint32(I==UnitField::HEALTH/32?1u<<(UnitField::HEALTH%32):0));
        W.Add(Value); H.HandlePacket(WowOpcode::SMSG_UPDATE_OBJECT,W.Data);
    };
    auto Selected=[&](uint64 Guid)
    { return Controller->TargetGuid==Guid && uint64(Connection.GetTargetGuid())==Guid; };
    auto OpenLoot=[&](uint32 Gold,bool Item)
    {
        FTargetWire W; W.Add(Corpse).Add(uint8(1)).Add(Gold).Add(uint8(Item?1:0));
        if(Item) W.Add(uint8(0)).Add(uint32(117)).Add(uint32(1)).Add(uint32(0)).Add(uint32(0)).Add(uint32(0)).Add(uint8(0));
        H.HandlePacket(WowOpcode::SMSG_LOOT_RESPONSE,W.Data);
    };
    auto Release=[&](uint64 Guid,uint8 Success)
    { FTargetWire W; W.Add(Guid).Add(Success); H.HandlePacket(WowOpcode::SMSG_LOOT_RELEASE_RESPONSE,W.Data); };

    Controller->SetTarget(Corpse); Health(Corpse,0);
    TestTrue(TEXT("Authoritative creature death clears both selection views"),Selected(0));
    TestTrue(TEXT("Deselecting preserves the corpse for loot"),H.EntityManager.Find(Corpse) && H.EntityManager.Find(Corpse)->GetHealth()==0);
    Controller->SetTarget(Corpse); Health(Corpse,0);
    TestTrue(TEXT("A deliberate corpse selection survives repeated zero-health updates"),Selected(Corpse));
    Health(Corpse,50); Controller->SetTarget(0); Connection.SendSetSelection(Corpse); Health(Corpse,0);
    TestTrue(TEXT("Death also clears a fresh Lua selection before controller synchronization"),Selected(0));
    Controller->SetTarget(Player); Health(Player,0);
    TestTrue(TEXT("A dead player remains selected for resurrection"),Selected(Player));

    Health(Corpse,50); Controller->SetTarget(Corpse);
    Connection.SendSetSelection(Other); Health(Corpse,0);
    TestEqual(TEXT("Death cannot overwrite a newer Lua selection before controller synchronization"),uint64(Connection.GetTargetGuid()),Other);
    Controller->SetTarget(Corpse);
    int32 Completed=0; uint64 CompletedGuid=0;
    H.OnLootCompleted.AddLambda([&](uint64 Guid){++Completed; CompletedGuid=Guid;});
    OpenLoot(17,true); Release(Corpse,1);
    TestTrue(TEXT("Closing an uncollected corpse preserves selection"),Selected(Corpse) && Completed==0);
    OpenLoot(17,true);
    H.HandlePacket(WowOpcode::SMSG_LOOT_CLEAR_MONEY,{});
    Release(Corpse,1);
    TestTrue(TEXT("Uncollected items still prevent completion after collecting money"),Selected(Corpse) && Completed==0);
    OpenLoot(17,true);
    H.HandlePacket(WowOpcode::SMSG_LOOT_REMOVED,{0});
    Release(Corpse,1);
    TestTrue(TEXT("Uncollected money still prevents completion after collecting items"),Selected(Corpse) && Completed==0);
    OpenLoot(17,true);
    H.HandlePacket(WowOpcode::SMSG_LOOT_REMOVED,{0});
    H.HandlePacket(WowOpcode::SMSG_LOOT_CLEAR_MONEY,{});
    TestTrue(TEXT("The final slot acknowledgement alone waits for the server release"),Selected(Corpse) && H.Loot.Open);
    Release(Other,1); Release(Corpse,0); Release(Corpse,2);
    TestTrue(TEXT("Stale, denied and malformed releases cannot clear the corpse"),Selected(Corpse) && H.Loot.Open && Completed==0);
    Release(Corpse,1);
    TestTrue(TEXT("Exhausted loot acknowledgement clears the exact corpse selection"),Selected(0) && !H.Loot.Open && Completed==1 && CompletedGuid==Corpse);
    Controller->SetTarget(Corpse); Release(Corpse,1);
    TestTrue(TEXT("A duplicate release cannot clear a newly selected corpse"),Selected(Corpse) && Completed==1);
    OpenLoot(0,false); Connection.SendSetSelection(Other); Release(Corpse,1);
    TestEqual(TEXT("Delayed loot completion never clears a different new selection"),uint64(Connection.GetTargetGuid()),Other);
    Controller->SetTarget(Corpse); OpenLoot(0,false); Health(Corpse,50); Release(Corpse,1);
    TestTrue(TEXT("A resurrected creature is not deselected by old loot completion"),Selected(Corpse));

    Controller->Destroy(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
