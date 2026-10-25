#pragma once

#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FTalentTabDbcEntry
{
	uint32 TalentTabID;
	FString Name;
	uint32 NameFlags;
	uint32 SpellIcon;
	uint32 ClassMask;
	uint32 RaceMask;
	uint32 PetTalentMask;
	uint32 TabPage;
	FString InternalName;
};

class WOWDATA_API FTalentTabDbc
{
public:
	bool Load(const FDbcParser& Parser);
	bool LoadPetFamilies(const FDbcParser& Parser);
	struct FPetFamily { int32 TalentType=-1,Category=-1; };
	const FPetFamily* PetFamily(uint32 Id) const { return PetFamilies.Find(Id); }
	const FTalentTabDbcEntry* GetById(uint32 ID) const;
	const TArray<FTalentTabDbcEntry>& GetAll() const { return Entries; }
	int32 Num() const { return Entries.Num(); }

private:
	TArray<FTalentTabDbcEntry> Entries;
	TMap<uint32, int32> IdIndex;
	TMap<uint32,FPetFamily> PetFamilies;
};
