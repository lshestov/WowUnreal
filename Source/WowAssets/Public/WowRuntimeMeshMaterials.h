#pragma once
#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;
struct FMeshDescription;

/** Bind polygon groups before BuildFromMeshDescriptions, including cooked builds. */
class WOWASSETS_API FWowRuntimeMeshMaterials
{
public:
    static void Assign(UStaticMesh* Mesh, FMeshDescription& Description,
        const TArray<UMaterialInterface*>& Materials);
    static void AssignSingle(UStaticMesh* Mesh, FMeshDescription& Description, UMaterialInterface* Material);
};
