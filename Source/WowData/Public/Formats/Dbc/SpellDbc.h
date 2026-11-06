#pragma once
#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FSpellDbcEntry
{
    uint32 ID;
    uint32 Category;
    uint32 Dispel;
    uint32 Mechanic;
    uint32 Attributes;
    uint32 AttributesEx;
    uint32 AttributesEx2;
    uint32 AttributesEx3;
    uint32 AttributesEx4;
    uint32 AttributesEx5;
    uint32 AttributesEx6;
    uint32 AttributesEx7;
    bool HasVisibleAuraIcon() const
    {
        return !(Attributes & 0x80) && !(AttributesEx & 0x10000000); // DO_NOT_DISPLAY / NO_AURA_ICON
    }
    bool IsNextMeleeSwing() const { return (Attributes & 0x404) != 0; }
    uint64 Stances;
    uint64 StancesNot;
    uint32 Targets;
    uint32 TargetCreatureType;
    uint32 RequiresSpellFocus;
    uint32 FacingCasterFlags;
    uint32 CasterAuraState;
    uint32 TargetAuraState;
    uint32 CastingTimeIndex;
    uint32 RecoveryTime;
    uint32 CategoryRecoveryTime;
    uint32 MaxLevel;
    uint32 BaseLevel;
    uint32 SpellLevel;
    uint32 DurationIndex;
    uint32 PowerType;
    uint32 ManaCost;
    uint32 RangeIndex;
    float Speed;
    uint32 StackAmount;
    uint32 ProcChance, ProcCharges, MaxAffectedTargets;
    uint32 Effect[3];
    int32 EffectBasePoints[3];
    int32 EffectDieSides[3];
    float EffectPointsPerLevel[3];
    uint32 EffectAmplitude[3];
    uint32 EffectRadiusIndex[3];
    uint32 EffectChainTarget[3];
    float EffectPointsPerComboPoint[3], EffectValueMultiplier[3], EffectDamageMultiplier[3];
    uint32 EffectImplicitTargetA[3];
    uint32 EffectImplicitTargetB[3];
    uint32 EffectApplyAuraName[3];
    int32 EffectMiscValue[3];
    uint32 EffectMiscValueB[3];
    uint32 EffectTriggerSpell[3];
    uint32 SpellVisual[2];
    uint32 RequiredAuraVisionMask = 0;
    uint32 SpellIconID;
    uint32 ActiveIconID;
    FString SpellName;
    FString Rank;
    int32 ProgressionRank() const
    {
        int32 Value=0; for(const TCHAR Ch:Rank) { if(FChar::IsDigit(Ch)) Value=Value*10+(Ch-'0'); else if(Value) break; }
        return Value;
    }
    FString Description;
    uint32 DescriptionVariablesID=0;
    FString ToolTip;
    uint32 ManaCostPercentage;
    uint32 SpellFamilyName;
    uint32 DmgClass;
    uint32 SchoolMask;
};

struct FSpellTextStats
{
    bool bKnown = true;
    double AttackPower = 0, RangedAttackPower = 0, HolyPower = 0, ShadowPower = 0, Spirit = 0;
    double WeaponSpeed = 0, WeaponMin = 0, WeaponMax = 0;
    double RangedSpeed = 0, RangedMin = 0, RangedMax = 0;
    int32 WeaponHands = 1;
    const TSet<uint32>* KnownSpells = nullptr;
    TSet<uint32> ActiveSpells;
};

class WOWDATA_API FSpellDbc
{
public:
    bool Load(const FDbcParser& Parser);
    const FSpellDbcEntry* GetById(uint32 ID) const;
    struct FRange { float Min[2] = {}, Max[2] = {}; uint32 Flags = 0; };
    bool LoadRanges(const FDbcParser& Parser);
    bool LoadRadii(const FDbcParser& Parser);
    bool LoadTimes(const FDbcParser& Parser,bool CastTimes);
    bool LoadDescriptionVariables(const FDbcParser& Parser);
    float Duration(uint32 Id) const { const auto* Value=Durations.Find(Id); return Value?*Value:0; }
    float CastTime(uint32 Id) const { const auto* Value=CastTimes.Find(Id); return Value?*Value:0; }
    FString Description(uint32 Id,int32 Level=1,bool Aura=false,const FSpellTextStats& Stats={}) const;
    const FRange* GetRange(uint32 ID) const { return Ranges.Find(ID); }
    const TArray<FSpellDbcEntry>& GetAll() const { return Entries; }
    int32 Num() const { return Entries.Num(); }

private:
    TArray<FSpellDbcEntry> Entries;
    TMap<uint32, int32> IdIndex;
    TMap<uint32, FRange> Ranges;
    TMap<uint32,float> Radii;
    TMap<uint32,float> Durations,CastTimes;
    TMap<uint32,TMap<FString,FString>> DescriptionVariables;
};
