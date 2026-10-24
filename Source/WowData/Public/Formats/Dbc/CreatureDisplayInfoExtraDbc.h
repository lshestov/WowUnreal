#pragma once
#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FCreatureDisplayInfoExtraDbcEntry
{
    uint32 ID = 0;
    uint32 Race = 0, Gender = 0, Skin = 0, Face = 0, HairStyle = 0, HairColor = 0, FacialHair = 0;
    uint32 ItemDisplay[11] = {};
    uint32 Flags = 0;
    FString BakedTexture;
};

class WOWDATA_API FCreatureDisplayInfoExtraDbc
{
public:
    bool Load(const FDbcParser& Parser);
    const FCreatureDisplayInfoExtraDbcEntry* GetById(uint32 ID) const { return Entries.Find(ID); }
    int32 Num() const { return Entries.Num(); }
private:
    TMap<uint32, FCreatureDisplayInfoExtraDbcEntry> Entries;
};
