#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Coord/WowCoordinate.h"
#include "Engine/Texture2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/CreatureDisplayInfoExtraDbc.h"
#include "Mpq/MpqManager.h"
#include "WowAssetCache.h"
#include "WowCharacterBuilder.h"
#include "WowAnimationController.h"
#include "WowM2AnimInstance.h"
#include "WowPacketHandler.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
template<typename T> void Append(TArray<uint8>& Bytes, const T& Value) { Bytes.Append(reinterpret_cast<const uint8*>(&Value), sizeof(T)); }
void Point(TArray<uint8>& Bytes, float X, float Y, float Z) { Append(Bytes, X); Append(Bytes, Y); Append(Bytes, Z); }
TArray<uint8> MonsterHeader(uint8 Type)
{
    TArray<uint8> Bytes = {1, 1, 0}; // packed GUID 1, movement extension
    Point(Bytes, 100, 200, 10);
    Append(Bytes, uint32(7)); Append(Bytes, Type);
    return Bytes;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowNpcExtra, "WowUnreal.Npc.AppearanceRecords",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowNpcExtra::RunTest(const FString& Parameters)
{
    TArray<uint8> Bytes;
    for (uint32 Header : {0x43424457u, 1u, 21u, 84u, 10u}) Append(Bytes, Header);
    for (uint32 Field = 0; Field < 21; ++Field) Append(Bytes, Field == 20 ? 1u : 100u + Field);
    const uint8 Strings[] = {0,'s','k','i','n','.','b','l','p',0};
    Bytes.Append(Strings, UE_ARRAY_COUNT(Strings));
    FDbcParser Parser;
    TestTrue(TEXT("Valid client DBC record"), Parser.Parse(Bytes));
    FCreatureDisplayInfoExtraDbc Table;
    TestTrue(TEXT("Extra display table loads"), Table.Load(Parser));
    const auto* Extra = Table.GetById(100);
    if (!TestNotNull(TEXT("Extra appearance lookup"), Extra)) return false;
    TestEqual(TEXT("Race column"), Extra->Race, 101u);
    TestEqual(TEXT("Gender column"), Extra->Gender, 102u);
    TestEqual(TEXT("Hair customization column"), Extra->HairColor, 106u);
    TestEqual(TEXT("Chest item display"), Extra->ItemDisplay[3], 111u);
    TestEqual(TEXT("Cape item display"), Extra->ItemDisplay[10], 118u);
    TestEqual(TEXT("Baked NPC skin"), Extra->BakedTexture, FString(TEXT("skin.blp")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowNpcRoutes, "WowUnreal.Npc.ServerMovement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowNpcRoutes::RunTest(const FString& Parameters)
{
    FWowPacketHandler Handler;
    auto& Entity = Handler.EntityManager.GetOrCreate(1);
    auto Packet = MonsterHeader(0);
    Append(Packet, uint32(0)); Append(Packet, uint32(2000)); Append(Packet, uint32(2));
    Point(Packet, 110, 220, 10);
    const uint32 Packed = (12u & 0x7FF) | ((uint32(-20) & 0x7FF) << 11);
    Append(Packet, Packed);
    Handler.HandlePacket(WowOpcode::SMSG_MONSTER_MOVE, Packet);
    TestEqual(TEXT("Both points decoded"), Entity.Movement.SplineWaypoints.Num(), 2);
    TestTrue(TEXT("Packed waypoint is midpoint minus signed offset"), Entity.Movement.SplineWaypoints[0].Position.Equals(FVector(102, 215, 10)));

    Packet = MonsterHeader(4); // final angle still carries a movement route
    Append(Packet, 1.25f); Append(Packet, uint32(0x00200800));
    Append(Packet, uint8(0)); Append(Packet, uint32(5)); // animation transition
    Append(Packet, uint32(1000)); Append(Packet, 2.0f); Append(Packet, uint32(10)); // parabolic payload
    Append(Packet, uint32(1)); Point(Packet, 105, 200, 10);
    Handler.HandlePacket(WowOpcode::SMSG_MONSTER_MOVE, Packet);
    TestTrue(TEXT("Facing angle does not discard the route"), Entity.Movement.bHasActiveSpline);
    Entity.Movement.AdvanceSpline(1.1f);
    TestTrue(TEXT("Final angle applied on arrival"), FMath::IsNearlyEqual(Entity.Movement.Orientation, 1.25f));

    Packet = MonsterHeader(3); Append(Packet, uint64(99)); // facing target, not angle
    Append(Packet, uint32(0x00040000)); Append(Packet, uint32(1000)); Append(Packet, uint32(2));
    Point(Packet, 101, 200, 10); Point(Packet, 110, 200, 10);
    Handler.HandlePacket(WowOpcode::SMSG_MONSTER_MOVE, Packet);
    TestEqual(TEXT("64-bit facing target"), Entity.Movement.SplineFacingTarget, uint64(99));
    TestTrue(TEXT("Full spline points are not interpreted as packed offsets"), Entity.Movement.SplineWaypoints[1].Position.Equals(FVector(110, 200, 10)));
    Entity.Movement.AdvanceSpline(0.5f);
    TestTrue(TEXT("Unequal segments advance by distance rather than equal time per point"), Entity.Movement.Position.Equals(FVector(105, 200, 10), 0.001));
    TestTrue(TEXT("Server route speed drives gait"), FMath::IsNearlyEqual(Entity.Movement.GetSplineSpeed(), 10.0f));
    const auto Before = Entity.Movement;
    Handler.HandlePacket(WowOpcode::SMSG_MONSTER_MOVE, MonsterHeader(0));
    TestEqual(TEXT("Truncated packet preserves the current route"), Entity.Movement.SplineElapsed, Before.SplineElapsed);
    auto FinalFacing = Before;
    const FVector FacingTarget(110, 210, 10);
    FinalFacing.AdvanceSpline(0.6f, &FacingTarget);
    TestTrue(TEXT("Final target facing uses the target position after arrival"), FMath::IsNearlyEqual(FinalFacing.Orientation, float(PI / 2), 0.001f));
    Handler.HandlePacket(WowOpcode::SMSG_MONSTER_MOVE, MonsterHeader(1));
    TestFalse(TEXT("Stop clears movement"), Entity.Movement.bHasActiveSpline);
    FWowMovementInfo Cycle;
    Cycle.bHasActiveSpline = true; Cycle.SplineDuration = 1000; Cycle.SplineFlags = 0x00080000;
    Cycle.SplineWaypoints = {FWowSplineWaypoint(FVector(10, 0, 0)), FWowSplineWaypoint(FVector::ZeroVector)};
    Cycle.AdvanceSpline(1.25f);
    TestTrue(TEXT("Cyclic route remains active and wraps by elapsed duration"), Cycle.bHasActiveSpline && Cycle.Position.Equals(FVector(5, 0, 0), 0.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowNpcModels, "WowUnreal.Npc.MpqModels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowNpcModels::RunTest(const FString& Parameters)
{
    FString DataPath;
    if (!FParse::Value(FCommandLine::Get(), TEXT("wowdata="), DataPath)) { AddError(TEXT("Supply -wowdata=<existing client Data> for this integration test")); return false; }
    FMpqManager Mpq;
    if (!TestTrue(TEXT("Existing local MPQ data opens"), Mpq.Initialize(DataPath))) return false;
    auto& Dbc = FDbcStore::Get();
    Dbc.LoadAll(Mpq); Dbc.LoadDeferred(Mpq);
    TestEqual(TEXT("Bent Staff uses the original long two-handed swing"),int32(Dbc.ItemUi().MeleeSequences(35).Attack),19);
    TestEqual(TEXT("Battleworn Hammer uses the original two-handed swing"),int32(Dbc.ItemUi().MeleeSequences(2361).Attack),18);
    TestEqual(TEXT("Worn Shortsword uses the original one-handed swing"),int32(Dbc.ItemUi().MeleeSequences(25).Attack),17);
    FWowAssetCache Cache;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    for (uint32 Display : {3275u, 3276u, 31048u, 31049u})
    {
        const FVector Location(12345, 23456, 789);
        AActor* Actor = FWowCharacterBuilder::SpawnCreatureByDisplayId(World, &Mpq, &Cache, Display, Location, FRotator(0, 35, 0));
        if (!TestNotNull(TEXT("Actual NPC display model spawns"), Actor)) continue;
        TestTrue(TEXT("Root preserves server position"), Actor->GetActorLocation().Equals(Location, 0.01));
        TestTrue(TEXT("Root preserves server facing"), Actor->GetActorRotation().Equals(FRotator(0, 35, 0), 0.01));
        const auto* Mesh = Actor->FindComponentByClass<USkeletalMeshComponent>();
        if (!TestNotNull(TEXT("Skeletal NPC mesh"), Mesh)) continue;
        TArray<USkeletalMeshComponent*> Parts;
        Actor->GetComponents(Parts);
        for (const auto* Part : Parts)
            if (Part->GetName() == TEXT("HairMesh"))
                TestTrue(TEXT("Hair shares the animated body's basis and position"), Part->GetComponentTransform().Equals(Mesh->GetComponentTransform(), 0.001));
        TestNotNull(TEXT("NPC model has real geometry"), Mesh->GetSkeletalMeshAsset()->GetResourceForRendering());
        UTexture* Texture = nullptr;
        TestTrue(TEXT("NPC section has a resolved skin texture"), Mesh->GetMaterial(0)->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseTexture")), Texture) && Texture);
        if (Display == 31048)
        {
            TestTrue(TEXT("Northshire wolf uses the original small display on the full-sized Wolf M2"),
                FMath::IsNearlyEqual(double(Mesh->GetComponentScale().Z), .55, .001));
            const auto* Render = Mesh->GetSkeletalMeshAsset()->GetResourceForRendering();
            if (Render && !Render->LODRenderData.IsEmpty())
            {
                const auto& Positions = Render->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;
                FBox Bounds(ForceInit);
                if (Positions.GetVertexData()) for (uint32 I = 0; I < Positions.GetNumVertices(); ++I) Bounds += FVector(Positions.VertexPosition(I));
                TestTrue(TEXT("Visible wolf stands about one yard tall in reference pose"), Bounds.IsValid &&
                    FMath::IsNearlyEqual(Bounds.GetSize().Z * Mesh->GetComponentScale().Z / FWowCoordinate::SCALE, 1.03, .03));
            }
            TestEqual(TEXT("Diseased wolf has separate fur and body layers"), Mesh->GetNumMaterials(), 2);
            const TCHAR* Expected[] = {TEXT("WolfSkinDiseasedAlpha.blp"), TEXT("WolfSkinDiseased.blp")};
            for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(Expected); ++Slot)
            {
                UTexture* Bound = nullptr;
                const auto* Material = Mesh->GetMaterial(Slot);
                if (!TestTrue(TEXT("CreatureDisplayInfo replacement is bound to its M2 layer"), Material &&
                    Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseTexture")), Bound) &&
                    Bound && Bound->GetName().EndsWith(Expected[Slot]))) continue;
                const auto* Skin = Cast<UTexture2D>(Bound);
                TestTrue(TEXT("Original wolf skin has all nine sampling levels"), Skin && Skin->GetNumMips() == 9 &&
                    Skin->Filter == TF_Trilinear);
            }
        }
        auto* Controller = FWowCharacterBuilder::GetAnimationController(Actor);
        if (!TestNotNull(TEXT("NPC native animation controller"), Controller)) continue;
        TestTrue(TEXT("The actor owns the controller across garbage collection"),Actor->GetInstanceComponents().Contains(Controller));
        TestTrue(TEXT("NPC has an available walking clip"), Controller->PlayAnimationById(EWowAnimId::Walk));
        if (Display==31048)
        {
            auto* Runtime=Controller->GetRuntimeInstance();
            FWowMovementInfo Stationary;
            Controller->UpdateAnimationState(Stationary,true);
            TestEqual(TEXT("Wolf without ReadyUnarmed uses the authored Stand in combat"),Runtime->GetM2AnimationId(),0);
            TestTrue(TEXT("Wolf can bite from the fallback stance"),Controller->PlayAttackAnimation());
            TestEqual(TEXT("Bite uses the creature's unarmed clip"),Runtime->GetM2AnimationId(),16);
            Runtime->NativeUpdateAnimation(Runtime->GetM2Duration());
            Controller->UpdateAnimationState(Stationary,true);
            TestEqual(TEXT("Wolf resumes Stand when the bite ends, while combat is still active"),Runtime->GetM2AnimationId(),0);
            Controller->PlayAnimationById(EWowAnimId::Walk);
            TArray<FTransform> Walking,Corpse;
            Runtime->SampleLocalPose(Walking);
            Controller->UpdateDeathPose();
            Runtime->NativeUpdateAnimation(Runtime->GetM2Duration()+1.f);
            Controller->UpdateDeathPose();
            Runtime->SampleLocalPose(Corpse);
            TestEqual(TEXT("Wolf holds its authored Death clip"), Runtime->GetM2AnimationId(),1);
            TestEqual(TEXT("Wolf corpse stays on the final sample"), Runtime->GetM2Time(),Runtime->GetM2Duration());
            int32 Changed=0;
            for (int32 I=0;I<FMath::Min(Walking.Num(),Corpse.Num());++I)
                if (!Walking[I].Equals(Corpse[I],.01)) ++Changed;
            TestTrue(TEXT("Death changes the actual wolf skeleton"),Changed>2);
        }
        if (const auto* Info = Dbc.CreatureDisplayInfo().GetById(Display))
        {
            if (Info->ExtendedDisplayInfoID)
            {
                const auto* Extra = Dbc.CreatureDisplayInfoExtra().GetById(Info->ExtendedDisplayInfoID);
                TestNotNull(TEXT("Humanoid NPC customization resolved"), Extra);
                if (Extra) TestTrue(TEXT("Baked NPC outfit exists in local MPQs"), Mpq.FileExists(TEXT("Textures\\BakedNpcTextures\\") + Extra->BakedTexture));
            }
        }
    }
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
