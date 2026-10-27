#pragma once
#include "CoreMinimal.h"

class UWorld;
class USceneComponent;
class UProceduralMeshComponent;
struct FWmoGroupData;
struct FM2Data;

class WOWWORLD_API FWowWorldCollision
{
public:
    static UProceduralMeshComponent* Create(USceneComponent* Parent, FName Name,
        const TArray<FVector>& Vertices, const TArray<int32>& Indices);
    static void DestroyChildren(USceneComponent* Parent);
    static UProceduralMeshComponent* AddWmo(USceneComponent* Parent, FName Name, const FWmoGroupData& Group);
    static void AppendM2(const FM2Data& Model, const FTransform& Transform,
        TArray<FVector>& Vertices, TArray<int32>& Indices);
    static bool FindFloor(UWorld* World, const FVector& Feet, const AActor* Ignore,
        FHitResult& OutHit, float MaxRise = 75, float MaxDrop = 1000,
        float CapsuleRadius = 0, float CapsuleHalfHeight = 0);
};
