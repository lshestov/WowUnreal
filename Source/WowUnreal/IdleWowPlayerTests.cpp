#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "WowSkeletalMeshBuilder.h"
#include "Formats/M2Types.h"
#include "WowPlayerCharacter.h"
#include "WowTerrainTile.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowRuntimePlayerMesh, "WowUnreal.Player.RuntimeSkeletalMesh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowRuntimePlayerMesh::RunTest(const FString& Parameters)
{
    FM2Data Data;
    Data.Bones.SetNum(1);
    Data.Bones[0].ParentBone = -1;
    Data.Bones[0].PivotPoint = FVector::ZeroVector;
    Data.Vertices.SetNum(3);
    Data.Vertices[0].Position = FVector::ZeroVector;
    Data.Vertices[1].Position = FVector(1, 0, 0);
    Data.Vertices[2].Position = FVector(0, 0, 2);
    for (auto& Vertex : Data.Vertices)
    {
        Vertex.Normal = FVector(0, 1, 0);
        Vertex.TexCoord = FVector2D::ZeroVector;
        FMemory::Memzero(Vertex.BoneWeights);
        FMemory::Memzero(Vertex.BoneIndices);
        Vertex.BoneWeights[0] = 255;
    }
    Data.Indices = {0, 1, 2};
    Data.BoundingBox = FBox(FVector::ZeroVector, FVector(1, 1, 2));
    USkeleton* Skeleton = FWowSkeletalMeshBuilder::CreateSkeleton(Data, TEXT("PlayerTest"));
    USkeletalMesh* Mesh = FWowSkeletalMeshBuilder::CreateSkeletalMesh(Data, Skeleton, TEXT("PlayerTest"), nullptr, nullptr, nullptr, nullptr, true);
    if (!TestNotNull(TEXT("Runtime mesh"), Mesh)) return false;
    const auto* Render = Mesh->GetResourceForRendering();
    if (!TestNotNull(TEXT("Actual render data, not an empty skeletal shell"), Render)) return false;
    TestEqual(TEXT("One resident LOD"), Render->LODRenderData.Num(), 1);
    const auto& Lod = Render->LODRenderData[0];
    TestEqual(TEXT("Visible vertices"), Lod.GetNumVertices(), 3u);
    TestEqual(TEXT("Vertex skin weights"), Lod.SkinWeightVertexBuffer.GetNumVertices(), 3u);
    TestEqual(TEXT("Triangle material section"), Lod.RenderSections[0].NumTriangles, 1u);
    TestEqual(TEXT("Root-bone mapping"), Lod.RenderSections[0].BoneMap.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowPlayerCamera, "WowUnreal.Player.LoginCamera",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowPlayerCamera::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Test world"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Player = World->SpawnActor<AWowPlayerCharacter>();
    auto* Controller = World->SpawnActor<APlayerController>();
    Controller->Possess(Player);
    const FVector Feet(100, 200, 300);
    Player->ApplyLoginSpawn(Feet, 0.0f);
    const float HalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    TestEqual(TEXT("Spawn is feet plus capsule, without a growing altitude offset"), Player->GetActorLocation().Z, Feet.Z + HalfHeight);
    TestEqual(TEXT("Normal third-person distance"), Player->CameraBoom->TargetArmLength, 400.0f);
    TestTrue(TEXT("Normal camera pitch"), FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(Controller->GetControlRotation().Pitch, -20.0), 0.01));
    Player->ApplyLoginSpawn(Feet, 0.0f);
    TestEqual(TEXT("Repeated spawn does not accumulate height"), Player->GetActorLocation().Z, Feet.Z + HalfHeight);
    Player->ApplyLoginSpawn(Feet,1.25f);
    TestTrue(TEXT("Server facing survives the movement coordinate round trip"),FMath::IsNearlyEqual(-FMath::DegreesToRadians(Player->GetActorRotation().Yaw),1.25,.001));
    TestTrue(TEXT("Camera and player use the same converted facing"),FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(Controller->GetControlRotation().Yaw,Player->GetActorRotation().Yaw),.001));
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowGroundCollision, "WowUnreal.Player.RuntimeTerrainFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowGroundCollision::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Test world"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto Data = MakeUnique<FAdtData>();
    Data->bIsValid = true;
    for (auto& Chunk : Data->Chunks) Chunk.Holes = MAX_uint16;
    auto& Ground = Data->Chunks[136];
    Ground.Holes = 0;
    Ground.IndexX = 8; Ground.IndexY = 8; Ground.WorldZ = 10.0f;
    for (FVector& Normal : Ground.Normals) Normal = FVector(0, 1, 0);
    auto* Tile = World->SpawnActor<AWowTerrainTile>();
    Tile->BuildFromAdtData(*Data, 32, 32, nullptr, nullptr);
    const double Started = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Started]()
    {
        FHitResult Hit;
        FCollisionQueryParams Query;
        Query.bTraceComplex = true;
        const bool Found = World->LineTraceSingleByChannel(Hit, FVector(-1000, 1000, 2000), FVector(-1000, 1000, 0), ECC_WorldStatic, Query);
        if (!Found && FPlatformTime::Seconds() - Started < 5.0) return false;
        TestTrue(TEXT("Runtime terrain supplies actual ground collision"), Found);
        if (Found) TestTrue(TEXT("Floor height matches ADT, in centimeters"), FMath::IsNearlyEqual(Hit.ImpactPoint.Z, 1000.0, 1.0));
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
        return true;
    }));
    return true;
}
#endif
