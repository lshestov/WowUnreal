#pragma once

#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FSpellVisualKitDbcEntry
{
	uint32 ID;
	int32 StartAnimID;
	int32 AnimID;
	uint32 HeadEffectID;
	uint32 ChestEffectID;
	uint32 BaseEffectID;
	uint32 LeftHandEffectID;
	uint32 RightHandEffectID;
	uint32 BreathEffectID;
	uint32 LeftWeaponEffectID;
	uint32 RightWeaponEffectID;
	uint32 SpecialEffectIDs[3];
	uint32 WorldEffectID;
	uint32 SoundID;
	uint32 ShakeID;
	int32 CharProc[4]={-1,-1,-1,-1};
	float CharParams[4][4]={};
	uint32 Flags;
};

class WOWDATA_API FSpellVisualKitDbc
{
public:
	bool Load(const FDbcParser& Parser);
    bool LoadAreaModels(const FDbcParser& Parser);
    FString AreaModel(int32 Index) const {return AreaModels.IsValidIndex(Index)?AreaModels[Index]:AreaModels.Num()?AreaModels[0]:FString();}
	const FSpellVisualKitDbcEntry* GetById(uint32 ID) const;
	const TArray<FSpellVisualKitDbcEntry>& GetAll() const { return Entries; }
	int32 Num() const { return Entries.Num(); }

private:
	TArray<FSpellVisualKitDbcEntry> Entries;
    TArray<FString> AreaModels;
	TMap<uint32, int32> IdIndex;
};
