#pragma once

#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct WOWDATA_API FCreatureFamilyDbcEntry
{
    uint32 ID = 0;
    float MinScale = 1, MaxScale = 1;
    int32 MinScaleLevel = 1, MaxScaleLevel = 1;

    /** Build 12340 CGUnit_C::GetModelScale: wild creatures have a family floor;
     * pets always use the family's level-dependent size. This is not a multiplier. */
    float ModelScale(float DisplayScale, int32 Level, bool bPet) const;
};

class WOWDATA_API FCreatureFamilyDbc
{
public:
    bool Load(const FDbcParser& Parser);
    const FCreatureFamilyDbcEntry* GetById(uint32 ID) const { return Entries.Find(ID); }

private:
    TMap<uint32, FCreatureFamilyDbcEntry> Entries;
};
