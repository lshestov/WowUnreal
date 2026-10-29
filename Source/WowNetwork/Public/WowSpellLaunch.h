#pragma once
#include "CoreMinimal.h"

struct FWowSpellMissTarget
{
    uint64 Guid=0;
    uint8 Reason=0,ReflectReason=0;
};

/** Validated SMSG_SPELL_GO identity; independent of the currently selected unit. */
struct FWowSpellLaunch
{
    uint64 CastObjectGuid=0,CasterGuid=0,ExplicitTargetGuid=0;
    uint32 SpellId=0,CastFlags=0,ServerTimeMs=0;
    uint8 Counter=0;
    TArray<uint64> HitTargets;
    TArray<FWowSpellMissTarget> MissedTargets;
    uint64 PrimaryTarget() const
    {
        if(ExplicitTargetGuid) return ExplicitTargetGuid;
        if(HitTargets.Num()) return HitTargets[0];
        return MissedTargets.Num()?MissedTargets[0].Guid:0;
    }
};
