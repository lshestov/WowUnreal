#pragma once
#include "CoreMinimal.h"
#include "Formats/AdtTypes.h"

struct WOWDATA_API FWdtData
{
    bool TileExists[64][64] = {};
    uint32 Flags = 0;
    bool bUseBigAlpha = false;
    FString GlobalWmoPath;
    FAdtWmoPlacement GlobalWmoPlacement;
    bool bHasGlobalWmoPlacement=false;
    bool bIsValid = false;
};
