#pragma once
#include "CoreMinimal.h"
struct FAdtData;
class FStaticMeshRenderData;

/** Worker-owned geometry; no actors, UObject construction or renderer access. */
struct WOWWORLD_API FWowTerrainGeometry
{
    TUniquePtr<FStaticMeshRenderData> RenderData;
    TArray<FVector> CollisionVertices;
    TArray<int32> CollisionIndices, Representatives;
    TArray<FColor> AlphaPixels;
    int32 Chunks=0, TexturedChunks=0, HeightChunks=0;
    FWowTerrainGeometry(const FAdtData& Data,int32 TX,int32 TY,bool Lod1=false);
    ~FWowTerrainGeometry();
};
