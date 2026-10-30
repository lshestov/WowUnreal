#pragma once

#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FSpellVisualDbcEntry
{
	uint32 ID;
	uint32 PrecastKit;
	uint32 CastKit;
	uint32 ImpactKit;
	uint32 StateKit;
	uint32 StateDoneKit;
	uint32 ChannelKit;
	uint32 HasMissile;
	int32 MissileModel;
	uint32 MissilePathType;
	uint32 MissileDestinationAttachment;
	uint32 MissileSound;
	float MissileCastOffsetX;
	float MissileCastOffsetY;
	float MissileCastOffsetZ;
	float MissileImpactOffsetX;
	float MissileImpactOffsetY;
	float MissileImpactOffsetZ;
	uint32 AnimEventSoundID;
	uint32 Flags;
	uint32 CasterImpactKit;
	uint32 TargetImpactKit;
	uint32 MissileAttachment;
	int32 MissileFollowGroundHeight;
	int32 MissileFollowGroundDropSpeed;
	int32 MissileFollowGroundApproach;
	uint32 MissileFollowGroundFlags;
	uint32 MissileMotion;
	uint32 MissileTargetingKit;
	uint32 InstantAreaKit;
	uint32 ImpactAreaKit;
	uint32 PersistentAreaKit;
    static uint32 EffectAttachment(uint32 Index,bool Raw=false)
    {
        static constexpr uint32 Ids[]={20,34,19,21,22,17,23,24,25,15,16,37,38,22,47,48};
        return Raw?Index:Index<UE_ARRAY_COUNT(Ids)?Ids[Index]:MAX_uint32;
    }
};

class WOWDATA_API FSpellVisualDbc
{
public:
	bool Load(const FDbcParser& Parser);
	const FSpellVisualDbcEntry* GetById(uint32 ID) const;
	const TArray<FSpellVisualDbcEntry>& GetAll() const { return Entries; }
	int32 Num() const { return Entries.Num(); }

private:
	TArray<FSpellVisualDbcEntry> Entries;
	TMap<uint32, int32> IdIndex;
};
