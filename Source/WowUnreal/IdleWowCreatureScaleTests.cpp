#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Formats/Dbc/DbcStore.h"
#include "Mpq/MpqManager.h"
#include "WowAssetCache.h"
#include "WowCharacterBuilder.h"
#include "WowGameplayController.h"
#include "WowConnectionManager.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
template<typename T> void AppendScaleBytes(TArray<uint8>& Bytes, const T& Value)
{
    Bytes.Append(reinterpret_cast<const uint8*>(&Value), sizeof(T));
}
TArray<uint8> WolfQuery(uint32 Entry)
{
    TArray<uint8> Bytes;
    AppendScaleBytes(Bytes, Entry);
    const ANSICHAR Name[] = "Ragged Young Wolf";
    Bytes.Append(reinterpret_cast<const uint8*>(Name), sizeof(Name));
    Bytes.Append({0, 0, 0, 0, 0}); // alternate names, subtitle, icon name
    AppendScaleBytes(Bytes, uint32(0)); // type flags
    AppendScaleBytes(Bytes, uint32(1)); // beast
    AppendScaleBytes(Bytes, uint32(1)); // wolf family
    return Bytes;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowCreatureFamilyRecordsTest, "WowUnreal.Npc.FamilyScaleRecords",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowCreatureFamilyRecordsTest::RunTest(const FString&)
{
    TArray<uint8> Bytes;
    for (uint32 Header : {0x43424457u, 1u, 5u, 20u, 1u}) AppendScaleBytes(Bytes, Header);
    AppendScaleBytes(Bytes, uint32(1)); AppendScaleBytes(Bytes, .7f); AppendScaleBytes(Bytes, uint32(1));
    AppendScaleBytes(Bytes, 1.f); AppendScaleBytes(Bytes, uint32(60)); Bytes.Add(0);
    FDbcParser Parser;
    FCreatureFamilyDbc Families;
    if (!TestTrue(TEXT("Creature family size columns decode"), Parser.Parse(Bytes) && Families.Load(Parser))) return false;
    const auto* Family = Families.GetById(1);
    if (!TestNotNull(TEXT("Wolf family lookup"), Family)) return false;
    TestEqual(TEXT("Level below the first scaling level clamps to the family minimum"), Family->ModelScale(.4f, 0, false), .7f);
    TestEqual(TEXT("Scale interpolates by level without multiplying the display size"), Family->ModelScale(.55f, 2, false), .7f + .3f / 59, 1.e-6f);
    TestEqual(TEXT("High levels stop growing at the authored maximum"), Family->ModelScale(.4f, 80, false), 1.f);
    TestEqual(TEXT("Authored large wild models are not shrunk to the family floor"), Family->ModelScale(1.5f, 1, false), 1.5f);
    TestEqual(TEXT("Pets explicitly use family size even for a larger display"), Family->ModelScale(1.5f, 1, true), .7f);
    FCreatureFamilyDbcEntry Fixed = *Family;
    Fixed.MaxScaleLevel = Fixed.MinScaleLevel;
    TestEqual(TEXT("A single-level range has no division by zero"), Fixed.ModelScale(.4f, 80, false), .7f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowNpcFamilyScaleTest, "WowUnreal.Npc.FamilyModelScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowNpcFamilyScaleTest::RunTest(const FString&)
{
    FString DataPath;
    if (!FParse::Value(FCommandLine::Get(), TEXT("wowdata="), DataPath)) { AddError(TEXT("Supply -wowdata=<existing client Data>")); return false; }
    FMpqManager Mpq;
    if (!TestTrue(TEXT("Existing client MPQs open"), Mpq.Initialize(DataPath))) return false;
    auto& Dbc = FDbcStore::Get(); Dbc.LoadAll(Mpq); Dbc.LoadDeferred(Mpq);
    const auto* Family = Dbc.CreatureFamilies().GetById(1);
    if (!TestNotNull(TEXT("Original wolf family's scaling data loads before world entry"), Family)) return false;
    TestEqual(TEXT("Build 12340 wolf minimum is 0.7 at level one"), Family->ModelScale(.4f, 1, false), .7f, 1.e-6f);
    FWowAssetCache Cache;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Controller = World->SpawnActor<AWowGameplayController>();
    Controller->ConnectionManager = NewObject<UWowConnectionManager>(Controller);
    auto& Handler = Controller->ConnectionManager->PacketHandler;
    Handler.OnCreatureNameReceived.AddUObject(Controller, &AWowGameplayController::OnCreatureNameReceived);
    Handler.EntityManager.OnEntityUpdated.AddUObject(Controller, &AWowGameplayController::OnEntityUpdated);
    for (uint32 Display : {855u, 11416u, 31049u, 31048u})
    {
        const bool Young = Display == 855 || Display == 31049;
        const uint64 Guid = Display;
        const uint32 Entry = Display == 855 ? 705 : Display == 11416 ? 704 : Display == 31049 ? 299 : 69;
        auto& Entity = Handler.EntityManager.PromoteToTyped(Guid, WowTypeMask::OBJECT | WowTypeMask::UNIT);
        Entity.Entry = Entry; Entity.SetField(UnitField::DISPLAYID, Display);
        Entity.SetField(UnitField::HEALTH, 30); Entity.SetField(UnitField::LEVEL, Young ? 1 : 2);
        auto* Actor = FWowCharacterBuilder::SpawnCreatureByDisplayId(World, &Mpq, &Cache, Display, FVector::ZeroVector);
        if (!TestNotNull(TEXT("Original starting-zone wolf model spawns"), Actor)) continue;
        Controller->SpawnedEntityActors.Add(Guid, Actor);
        auto* Body = Actor->FindComponentByClass<USkeletalMeshComponent>();
        if (!TestNotNull(TEXT("Wolf has an animated body"), Body)) continue;
        auto* Bounds = NewObject<UBoxComponent>(Actor, TEXT("FamilyScaleSelection"));
        Bounds->SetupAttachment(Body); Bounds->SetBoxExtent(FVector(100)); Bounds->RegisterComponent();
        TestEqual(TEXT("Before template query, model has its authored display size"), float(Body->GetComponentScale().Z), Young ? .4f : .55f, 1.e-6f);
        Controller->NpcReferenceFeet.Add(Guid, 20); Controller->NpcGroundOffsets.Add(Guid, 10);
        Handler.HandlePacket(WowOpcode::SMSG_CREATURE_QUERY_RESPONSE, WolfQuery(Entry));
        const float Expected = Young ? .7f : .7f + .3f / 59;
        TestEqual(TEXT("Late template response applies the real family/level scale"), float(Body->GetComponentScale().Z), Expected, 1.e-6f);
        TestEqual(TEXT("Picking bounds follow the rendered body's family scale"), float(Bounds->GetScaledBoxExtent().Z), Expected * 100, 1.e-4f);
        TestFalse(TEXT("Changed size invalidates cached foot placement"), Controller->NpcReferenceFeet.Contains(Guid) || Controller->NpcGroundOffsets.Contains(Guid));
        Controller->ApplyUnitScale(Actor, Entity);
        TestEqual(TEXT("Repeated refresh does not compound scaling"), float(Body->GetComponentScale().Z), Expected, 1.e-6f);
        Entity.SetField(UnitField::LEVEL, 60);
        Controller->OnEntityUpdated(Entity);
        TestEqual(TEXT("A unit level update reapplies the family size"), float(Body->GetComponentScale().Z), 1.f, 1.e-6f);
        Entity.Scale = 2;
        Controller->OnEntityUpdated(Entity);
        TestEqual(TEXT("Server object scale remains an independent multiplier"), float(Body->GetComponentScale().Z), 2.f, 1.e-6f);
    }
    bool MetadataReadyAtNotification = false;
    Handler.OnCreatureNameReceived.AddLambda([&](uint32 Entry, const FString&, const FString&)
    { MetadataReadyAtNotification = Handler.CreatureFamilies.FindRef(Entry) == 1 && Handler.CreatureTypes.FindRef(Entry) == 1; });
    Handler.HandlePacket(WowOpcode::SMSG_CREATURE_QUERY_RESPONSE, WolfQuery(705));
    TestTrue(TEXT("Type and family are available to query-response callbacks"), MetadataReadyAtNotification);
    TArray<uint8> Missing; AppendScaleBytes(Missing, uint32(0x800002c1));
    Handler.HandlePacket(WowOpcode::SMSG_CREATURE_QUERY_RESPONSE, Missing);
    TestFalse(TEXT("Unknown templates do not introduce a high-bit family cache key"), Handler.CreatureFamilies.Contains(0x800002c1));
    Controller->Destroy(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
