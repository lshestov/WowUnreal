#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "StaticMeshResources.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/ItemDisplayInfoDbc.h"
#include "Mpq/MpqManager.h"
#include "WowAssetCache.h"
#include "WowCharacterBuilder.h"
#include "WowCharacterTexture.h"
#include "WowEquipmentManager.h"
#include "WowPlayerCharacter.h"
#include "WowSessionState.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
template<typename T> void EquipAppend(TArray<uint8>& Bytes, const T& Value) { Bytes.Append(reinterpret_cast<const uint8*>(&Value), sizeof(T)); }
TArray<uint8> TextureBytes(UTexture2D* Texture)
{
    TArray<uint8> Bytes;
    if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty()) return Bytes;
    auto& Bulk = Texture->GetPlatformData()->Mips[0].BulkData;
    const int64 Size = Bulk.GetBulkDataSize();
    if (Size <= 0 || Size > MAX_int32) return Bytes;
    if (const void* Pixels = Bulk.LockReadOnly()) Bytes.Append(static_cast<const uint8*>(Pixels), int32(Size));
    Bulk.Unlock();
    return Bytes;
}
TArray<UStaticMeshComponent*> EquipmentParts(AActor* Actor)
{
    TArray<UStaticMeshComponent*> Parts;
    Actor->GetComponents(Parts);
    Parts.RemoveAll([](const auto* Part) { return !Part->ComponentHasTag(TEXT("WowEquipment")); });
    return Parts;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowItemDisplayLayout, "WowUnreal.Equipment.WotlkDisplayLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowItemDisplayLayout::RunTest(const FString& Parameters)
{
    TArray<uint8> Strings = {0};
    auto String = [&Strings](const ANSICHAR* Text)
    {
        const uint32 Offset = Strings.Num();
        Strings.Append(reinterpret_cast<const uint8*>(Text), FCStringAnsi::Strlen(Text) + 1);
        return Offset;
    };
    TArray<uint32> Row;
    Row.Init(0, 25);
    Row[0] = 7541;
    Row[1] = String("Shoulder_Right.mdx"); Row[2] = String("Shoulder_Left.mdx");
    Row[7] = 3; Row[8] = 4; Row[9] = 5; Row[10] = 0xAABB;
    Row[13] = 99; Row[14] = 101;
    Row[15] = String("ArmWithoutNamingConvention"); Row[22] = String("BootTexture");
    Row[23] = 55; Row[24] = 66;
    TArray<uint8> Bytes;
    for (uint32 Header : {0x43424457u, 1u, 25u, 100u, uint32(Strings.Num())}) EquipAppend(Bytes, Header);
    for (uint32 Value : Row) EquipAppend(Bytes, Value);
    Bytes.Append(Strings);
    FDbcParser Parser;
    TestTrue(TEXT("25-column WotLK record"), Parser.Parse(Bytes));
    FItemDisplayInfoDbc Table;
    TestTrue(TEXT("Display table loads"), Table.Load(Parser));
    const auto* Item = Table.GetById(7541);
    if (!TestNotNull(TEXT("Item display lookup"), Item)) return false;
    TestEqual(TEXT("Exactly three geoset columns before flags"), Item->GeosetGroups[2], 5u);
    TestEqual(TEXT("Flags do not become a fake fourth geoset"), Item->Flags, 0xAABBu);
    TestEqual(TEXT("Helmet visibility column"), Item->HelmetGeosetVis[1], 101u);
    TestEqual(TEXT("First clothing column is 15"), Item->TextureOverlays[0], FString(TEXT("ArmWithoutNamingConvention")));
    TestEqual(TEXT("Last clothing column is 22"), Item->TextureOverlays[7], FString(TEXT("BootTexture")));
    TestEqual(TEXT("Item visual column"), Item->ItemVisual, 55u);
    TestEqual(TEXT("Particle color column"), Item->ParticleColorID, 66u);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowEquipmentModels, "WowUnreal.Equipment.MpqModelsAndTransfer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowEquipmentModels::RunTest(const FString& Parameters)
{
    FString DataPath;
    if (!FParse::Value(FCommandLine::Get(), TEXT("wowdata="), DataPath)) { AddError(TEXT("Supply -wowdata=<existing client Data>")); return false; }
    FMpqManager Mpq;
    if (!TestTrue(TEXT("Existing MPQs open"), Mpq.Initialize(DataPath))) return false;
    auto& Dbc = FDbcStore::Get(); Dbc.LoadAll(Mpq); Dbc.LoadDeferred(Mpq);
    FWowAssetCache Cache;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    FWowCharacterTexture::FCustomization Custom;
    TStrongObjectPtr<UTexture2D> Bare(FWowCharacterTexture::BuildCompositeTexture(&Mpq, &Cache, Custom));
    const auto Before = TextureBytes(Bare.Get());
    TestFalse(TEXT("Skin atlas has CPU pixels"), Before.IsEmpty());
    auto* Player = World->SpawnActor<AWowPlayerCharacter>();
    TArray<FWowCharacterEquipment> Items;
    for (const auto& Fixture : {FIntVector(9891, 4, 3), FIntVector(9892, 7, 6), FIntVector(10141, 8, 7), FIntVector(2380, 17, 15)})
    {
        FWowCharacterEquipment Item; Item.DisplayId = Fixture.X; Item.InventoryType = Fixture.Y; Item.Slot = Fixture.Z;
        Items.Add(Item);
    }
    Player->SetCharacterModelWithEquipment(World, &Mpq, &Cache, 1, 0, 0, 0, 0, 0, 0, &Items);
    UTexture* Clothed = nullptr;
    Player->GetMesh()->GetMaterial(0)->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseTexture")), Clothed);
    const auto After = TextureBytes(Cast<UTexture2D>(Clothed));
    TestTrue(TEXT("Clothed atlas preserves its dimensions"), !Before.IsEmpty() && Before.Num() == After.Num());
    int32 Changed = 0;
    for (int32 Byte = 0; Byte < FMath::Min(Before.Num(), After.Num()); ++Byte) if (Before[Byte] != After[Byte]) ++Changed;
    TestTrue(TEXT("Actual starter shirt, pants and boots change the skin atlas"), Changed > 1000);
    TestTrue(TEXT("Cached base skin is immutable"), TextureBytes(Bare.Get()) == Before);
    TestEqual(TEXT("Player retains weapon after temporary model is destroyed"), EquipmentParts(Player).Num(), 1);

    AActor* Npc = FWowCharacterBuilder::SpawnCreatureByDisplayId(World, &Mpq, &Cache, 2072, FVector(100, 200, 300));
    if (!TestNotNull(TEXT("Deputy Willem actual display model"), Npc)) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
    auto Parts = EquipmentParts(Npc);
    TestEqual(TEXT("Both shoulder components survive with different names"), Parts.Num(), 2);
    TSet<FName> Sockets;
    for (auto* Part : Parts)
    {
        Sockets.Add(Part->GetAttachSocketName());
        const UStaticMesh* Mesh = Part->GetStaticMesh();
        const auto* Render = Mesh ? Mesh->GetRenderData() : nullptr;
        if (!TestNotNull(TEXT("Equipment has actual render buffers"), Render)) continue;
        TestEqual(TEXT("Explicit cooked section slot"), Render->LODResources[0].Sections[0].MaterialIndex, 0);
        TestFalse(TEXT("Named material binding survives Game build"), Mesh->GetStaticMaterials()[0].MaterialSlotName.IsNone());
        TestEqual(TEXT("Equipment uses a cookable material"), Part->GetMaterial(0)->GetBaseMaterial()->GetPathName(), FString(TEXT("/Game/Materials/M_WowCharacter.M_WowCharacter")));
        UTexture* Texture = nullptr;
        TestTrue(TEXT("Shoulder has its actual BLP texture"), Part->GetMaterial(0)->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseTexture")), Texture) && Texture);
    }
    TestEqual(TEXT("Left and right shoulders follow different bones"), Sockets.Num(), 2);
    auto* NpcMesh = Npc->FindComponentByClass<USkeletalMeshComponent>();
    FWowEquipmentManager::CopyEquipment(NpcMesh, Player->GetMesh());
    FWowEquipmentManager::CopyEquipment(NpcMesh, Player->GetMesh());
    TestEqual(TEXT("Repeated transfer replaces equipment without duplication"), EquipmentParts(Player).Num(), 2);
    Npc->Destroy();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestEqual(TEXT("Transferred equipment survives source actor destruction and GC"), EquipmentParts(Player).Num(), 2);
    for (auto* Part : EquipmentParts(Player))
        TestTrue(TEXT("Transferred part remains registered on the player mesh"), Part->IsRegistered() && Part->GetAttachParent() == Player->GetMesh() && IsValid(Part->GetStaticMesh()) && IsValid(Part->GetMaterial(0)));
    FWowEquipmentManager::ClearEquipment(Player->GetMesh());
    TestEqual(TEXT("Rebuilding a naked model can clear prior equipment"), EquipmentParts(Player).Num(), 0);
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
