#pragma once
#include "CoreMinimal.h"
#include "Formats/WmoTypes.h"

/** WotLK vertex-light preparation. RGB is linear light; A is outdoor weight. */
struct WOWWORLD_API FWowWmoLighting
{
    static TArray<FLinearColor> Prepare(const FWmoRootData& Root,const FWmoGroupData& Group);
    static float PortalWeight(const FWmoRootData& Root,const FWmoGroupData& Group,const FVector& Position);
    static FLinearColor UnitAmbient(const FLinearColor& Floor);
    static FLinearColor UnitDiffuse(const FLinearColor& Floor);
};
