#pragma once
#include "CoreMinimal.h"
struct FMeshDescription;
class FStaticMeshRenderData;
class UStaticMesh;
class UMaterialInterface;

/** CPU buffer preparation is worker-safe; UObject/RHI initialization stays on the game thread. */
struct WOWASSETS_API FWowRuntimeStaticMesh
{
    static TUniquePtr<FStaticMeshRenderData> Prepare(const FMeshDescription& Description,bool VertexColors=false,bool CastShadow=false);
    static TUniquePtr<FStaticMeshRenderData> PrepareSurface(const TArray<FVector>& Positions,const TArray<FVector>& Normals,const TArray<FVector2D>& UVs,const TArray<int32>& Indices,const TArray<FVector4f>* LiquidColors=nullptr);
    static void Initialize(UStaticMesh* Mesh,TUniquePtr<FStaticMeshRenderData> Data,const TArray<UMaterialInterface*>& Materials);
};
