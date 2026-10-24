#include "WowWorldCollision.h"
#include "Formats/WmoTypes.h"
#include "Formats/M2Types.h"
#include "Coord/WowCoordinate.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/UObjectGlobals.h"

UProceduralMeshComponent* FWowWorldCollision::Create(USceneComponent* Parent, FName Name,
    const TArray<FVector>& Vertices, const TArray<int32>& Indices)
{
    if (!Parent || Vertices.IsEmpty() || Indices.Num() < 3) return nullptr;
    auto* Collision = NewObject<UProceduralMeshComponent>(Parent->GetOwner(),
        MakeUniqueObjectName(Parent->GetOwner(), UProceduralMeshComponent::StaticClass(), Name));
    Collision->SetupAttachment(Parent);
    if (Parent->Mobility == EComponentMobility::Static) Collision->SetMobility(EComponentMobility::Static);
    Collision->bUseAsyncCooking = true;
    Collision->bUseComplexAsSimpleCollision = true;
    Collision->GetBodySetup()->bDoubleSidedGeometry = true;
    Collision->SetCollisionObjectType(ECC_WorldStatic);
    Collision->SetCollisionResponseToAllChannels(ECR_Block);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Collision->SetGenerateOverlapEvents(false);
    Collision->SetVisibility(false);
    Collision->SetHiddenInGame(true);
    Collision->ComponentTags.Add(TEXT("WowGroundSurface"));
    Collision->CreateMeshSection(0, Vertices, Indices, TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), true);
    Collision->RegisterComponent();
    return Collision;
}

void FWowWorldCollision::DestroyChildren(USceneComponent* Parent)
{
    if (!Parent) return;
    const auto Children = Parent->GetAttachChildren();
    for (const auto& Child : Children)
        if (Child && Child->ComponentHasTag(TEXT("WowGroundSurface"))) Child->DestroyComponent();
}

void FWowWorldCollision::AddWmo(USceneComponent* Parent, FName Name, const FWmoGroupData& Group)
{
    TArray<FVector> Vertices;
    for (const FVector& V : Group.Vertices) Vertices.Add(FVector(V.Y, V.X, V.Z) * FWowCoordinate::SCALE);
    TArray<int32> Indices;
    for (int32 I = 0; I + 2 < Group.Indices.Num(); I += 3)
    {
        if (!Group.HasCollision(I / 3)) continue;
        if (!Vertices.IsValidIndex(Group.Indices[I]) || !Vertices.IsValidIndex(Group.Indices[I + 1]) || !Vertices.IsValidIndex(Group.Indices[I + 2])) continue;
        Indices.Append({Group.Indices[I], Group.Indices[I + 2], Group.Indices[I + 1]});
    }
    Create(Parent, Name, Vertices, Indices);
}

void FWowWorldCollision::AppendM2(const FM2Data& Model, const FTransform& Transform,
    TArray<FVector>& Vertices, TArray<int32>& Indices)
{
    const int32 Base = Vertices.Num();
    for (const FVector& V : Model.CollisionVertices)
        Vertices.Add(Transform.TransformPosition(FVector(V.Y, V.X, V.Z) * FWowCoordinate::SCALE));
    for (int32 I = 0; I + 2 < Model.CollisionIndices.Num(); I += 3)
    {
        if (!Model.CollisionVertices.IsValidIndex(Model.CollisionIndices[I]) || !Model.CollisionVertices.IsValidIndex(Model.CollisionIndices[I + 1]) || !Model.CollisionVertices.IsValidIndex(Model.CollisionIndices[I + 2])) continue;
        Indices.Append({Base + Model.CollisionIndices[I], Base + Model.CollisionIndices[I + 2], Base + Model.CollisionIndices[I + 1]});
    }
}

bool FWowWorldCollision::FindFloor(UWorld* World, const FVector& Feet, const AActor* Ignore,
    FHitResult& OutHit, float MaxRise, float MaxDrop, float CapsuleRadius, float CapsuleHalfHeight)
{
    if (!World) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(WowFloor), true);
    if (Ignore) Query.AddIgnoredActor(Ignore);
    FCollisionObjectQueryParams Objects(ECC_WorldStatic);
    FVector Start = Feet + FVector(0, 0, MaxRise);
    const FVector End = Feet - FVector(0, 0, MaxDrop);
    // NPC support needs a single local probe. Layer/clearance recovery is
    // reserved for the player's initial capsule placement.
    if (CapsuleHalfHeight <= 0)
        return World->LineTraceSingleByObjectType(OutHit, Start, End, Objects, Query) && OutHit.ImpactNormal.Z >= 0.5;
    double BestDistance = TNumericLimits<double>::Max();
    double FallbackDistance = TNumericLimits<double>::Max();
    FHitResult Fallback;
    bool Found = false;
    // Descend through nearby layers; do not pick a roof high above an indoor spawn.
    for (int32 Layer = 0; Layer < 12 && Start.Z > End.Z; ++Layer)
    {
        FHitResult Hit;
        if (!World->LineTraceSingleByObjectType(Hit, Start, End, Objects, Query)) break;
        if (Hit.ImpactNormal.Z >= 0.5)
        {
            const double Distance = FMath::Abs(Hit.ImpactPoint.Z - Feet.Z);
            if (Distance < FallbackDistance) { Fallback = Hit; FallbackDistance = Distance; }
            bool Clear = true;
            if (CapsuleHalfHeight > 0 && CapsuleRadius > 0)
                Clear = !World->OverlapAnyTestByObjectType(Hit.ImpactPoint + FVector(0, 0, CapsuleHalfHeight + 2), FQuat::Identity,
                    Objects, FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight), Query);
            if (Clear && Distance < BestDistance) { OutHit = Hit; BestDistance = Distance; Found = true; }
        }
        Start.Z = Hit.ImpactPoint.Z - 0.5;
    }
    if (!Found && FallbackDistance < TNumericLimits<double>::Max()) { OutHit = Fallback; Found = true; }
    return Found;
}
