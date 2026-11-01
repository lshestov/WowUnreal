#pragma once
#include "CoreMinimal.h"
#include "Formats/DbcParser.h"
class FMapDbc;

struct FLightDbcEntry
{
    uint32 ID;              // 0
    uint32 MapID;           // 1
    float  X;               // 2
    float  Y;               // 3
    float  Z;               // 4
    float  FalloffStart;    // 5
    float  FalloffEnd;      // 6
    uint32 ParamIDs[8];     // 7-14
    bool IsGlobal() const { return X==0 && Y==0 && Z==0; }
};

class WOWDATA_API FLightDbc
{
public:
    bool Load(const FDbcParser& Parser,const FMapDbc* Maps=nullptr);
    const FLightDbcEntry* GetById(uint32 ID) const;
    TArray<const FLightDbcEntry*> GetByMap(uint32 MapID) const;
    const TArray<FLightDbcEntry>& GetAll() const { return Entries; }
    int32 Num() const { return Entries.Num(); }

private:
    TArray<FLightDbcEntry> Entries;
    TMap<uint32, int32> IdIndex;
};
