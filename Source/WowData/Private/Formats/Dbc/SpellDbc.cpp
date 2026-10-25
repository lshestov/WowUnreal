#include "Formats/Dbc/SpellDbc.h"
#include "Internationalization/Regex.h"

bool FSpellDbc::LoadTimes(const FDbcParser& Parser,bool Cast)
{
    if(!Parser.IsValid() || Parser.GetFieldCount()<4) return false;
    auto& Values=Cast?CastTimes:Durations; Values.Reset();
    for(uint32 I=0; I<Parser.GetRecordCount(); ++I) Values.Add(Parser.GetUInt(I,0),int32(Parser.GetUInt(I,1))/1000.f);
    return true;
}

FString FSpellDbc::Description(uint32 Id,int32 Level,bool Aura) const
{
    const auto* Spell=GetById(Id); if(!Spell) return {};
    const FString Text=Aura && !Spell->ToolTip.IsEmpty()?Spell->ToolTip:Spell->Description;
    FRegexMatcher Match(FRegexPattern(TEXT("\\$(?:([*/])([0-9]+(?:\\.[0-9]+)?);)?(?:(\\d+))?([sSmMdDtTuUxXoOrRhH])([1-3])?")),Text);
    FString Result; int32 Previous=0;
    while(Match.FindNext())
    {
        Result+=Text.Mid(Previous,Match.GetMatchBeginning()-Previous);
        const auto* Referenced=Match.GetCaptureGroup(3).IsEmpty()?Spell:GetById(FCString::Atoi(*Match.GetCaptureGroup(3)));
        const int32 Effect=FMath::Clamp(FCString::Atoi(*Match.GetCaptureGroup(5))-1,0,2);
        const TCHAR Token=FChar::ToLower(Match.GetCaptureGroup(4)[0]); FString Value;
        if(Referenced)
        {
            const float Points=FMath::Abs(Referenced->EffectBasePoints[Effect]+(Referenced->EffectDieSides[Effect]?1:0)+Referenced->EffectPointsPerLevel[Effect]*FMath::Max(0,FMath::Min(Level,int32(Referenced->MaxLevel?Referenced->MaxLevel:Level))-int32(Referenced->SpellLevel)));
            if(Token=='d') { const float Seconds=Duration(Referenced->DurationIndex); Value=Seconds<0?TEXT("until cancelled"):FString::Printf(TEXT("%g %s"),Seconds>=60?Seconds/60:Seconds,Seconds>=60?TEXT("min"):TEXT("sec")); }
            else if(Token=='s' || Token=='m') Value=FString::Printf(TEXT("%g"),Points);
            else if(Token=='t') Value=FString::Printf(TEXT("%g"),Referenced->EffectAmplitude[Effect]/1000.f);
            else if(Token=='u') Value=FString::FromInt(Referenced->StackAmount);
            else if(Token=='h') Value=FString::FromInt(Referenced->RecoveryTime/1000);
            else if(Token=='o') { const float Interval=Referenced->EffectAmplitude[Effect]/1000.f; Value=FString::Printf(TEXT("%g"),Interval>0?Points*Duration(Referenced->DurationIndex)/Interval:Points); }
            else if(Token=='r') { const auto* Range=GetRange(Referenced->RangeIndex); Value=FString::Printf(TEXT("%g"),Range?Range->Max[0]:0); }
        }
        if(!Value.IsEmpty() && !Match.GetCaptureGroup(1).IsEmpty() && Value.IsNumeric())
        {
            const double Factor=FCString::Atod(*Match.GetCaptureGroup(2));
            if(Match.GetCaptureGroup(1)==TEXT("*")) Value=FString::Printf(TEXT("%g"),FCString::Atod(*Value)*Factor);
            else if(Factor!=0) Value=FString::Printf(TEXT("%g"),FCString::Atod(*Value)/Factor);
            else Value.Empty();
        }
        Result+=Value.IsEmpty()?Match.GetCaptureGroup(0):Value; Previous=Match.GetMatchEnding();
    }
    Result+=Text.Mid(Previous);
    FRegexMatcher Plural(FRegexPattern(TEXT("\\$[lLgG]([^:;]*):([^;]*);")),Result); FString Clean; Previous=0;
    while(Plural.FindNext()) { Clean+=Result.Mid(Previous,Plural.GetMatchBeginning()-Previous)+Plural.GetCaptureGroup(2); Previous=Plural.GetMatchEnding(); }
    return Clean+Result.Mid(Previous);
}

bool FSpellDbc::LoadRanges(const FDbcParser& Parser)
{
    if (!Parser.IsValid() || Parser.GetFieldCount()<6) return false;
    Ranges.Empty();
    for (uint32 I=0; I<Parser.GetRecordCount(); ++I)
    {
        FRange Range;
        Range.Min[0]=Parser.GetFloat(I,1); Range.Min[1]=Parser.GetFloat(I,2);
        Range.Max[0]=Parser.GetFloat(I,3); Range.Max[1]=Parser.GetFloat(I,4); Range.Flags=Parser.GetUInt(I,5);
        Ranges.Add(Parser.GetUInt(I,0),Range);
    }
    return true;
}

DEFINE_LOG_CATEGORY_STATIC(LogSpellDbc, Log, All);

bool FSpellDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid())
    {
        return false;
    }

    Entries.Reserve(Parser.GetRecordCount());

    for (uint32 i = 0; i < Parser.GetRecordCount(); ++i)
    {
        FSpellDbcEntry E;

        E.ID = Parser.GetUInt(i, 0);
        E.Category = Parser.GetUInt(i, 1);
        E.Dispel = Parser.GetUInt(i, 2);
        E.Mechanic = Parser.GetUInt(i, 3);
        E.Attributes = Parser.GetUInt(i, 4);
        E.AttributesEx = Parser.GetUInt(i, 5);
        E.AttributesEx2 = Parser.GetUInt(i, 6);
        E.AttributesEx3 = Parser.GetUInt(i, 7);
        E.AttributesEx4 = Parser.GetUInt(i, 8);
        E.AttributesEx5 = Parser.GetUInt(i, 9);
        E.AttributesEx6 = Parser.GetUInt(i, 10);
        E.AttributesEx7 = Parser.GetUInt(i, 11);

        // Combine uint64 fields from two uint32 columns
        E.Stances = static_cast<uint64>(Parser.GetUInt(i, 12)) | (static_cast<uint64>(Parser.GetUInt(i, 13)) << 32);
        E.StancesNot = static_cast<uint64>(Parser.GetUInt(i, 14)) | (static_cast<uint64>(Parser.GetUInt(i, 15)) << 32);

        E.Targets = Parser.GetUInt(i, 16);
        E.TargetCreatureType = Parser.GetUInt(i, 17);
        E.RequiresSpellFocus = Parser.GetUInt(i, 18);
        E.FacingCasterFlags = Parser.GetUInt(i, 19);
        E.CasterAuraState = Parser.GetUInt(i, 20);
        E.TargetAuraState = Parser.GetUInt(i, 21);
        E.CastingTimeIndex = Parser.GetUInt(i, 28);
        E.RecoveryTime = Parser.GetUInt(i, 29);
        E.CategoryRecoveryTime = Parser.GetUInt(i, 30);
        E.MaxLevel = Parser.GetUInt(i, 37);
        E.BaseLevel = Parser.GetUInt(i, 38);
        E.SpellLevel = Parser.GetUInt(i, 39);
        E.DurationIndex = Parser.GetUInt(i, 40);
        E.PowerType = Parser.GetUInt(i, 41);
        E.ManaCost = Parser.GetUInt(i, 42);
        E.RangeIndex = Parser.GetUInt(i, 46);
        E.Speed = Parser.GetFloat(i, 47);
        E.StackAmount = Parser.GetUInt(i, 49);

        // Effect arrays
        for (int32 j = 0; j < 3; ++j)
        {
            E.Effect[j] = Parser.GetUInt(i, 71 + j);
            E.EffectBasePoints[j] = static_cast<int32>(Parser.GetUInt(i, 80 + j));
            E.EffectDieSides[j] = int32(Parser.GetUInt(i,74+j));
            E.EffectPointsPerLevel[j] = Parser.GetFloat(i,77+j);
            E.EffectAmplitude[j] = Parser.GetUInt(i,98+j);
            E.EffectImplicitTargetA[j] = Parser.GetUInt(i, 86 + j);
            E.EffectImplicitTargetB[j] = Parser.GetUInt(i, 89 + j);
            E.EffectApplyAuraName[j] = Parser.GetUInt(i, 95 + j);
            E.EffectMiscValue[j] = static_cast<int32>(Parser.GetUInt(i, 110 + j));
            E.EffectTriggerSpell[j] = Parser.GetUInt(i, 116 + j);
        }

        E.SpellVisual[0] = Parser.GetUInt(i, 131);
        E.SpellVisual[1] = Parser.GetUInt(i, 132);
        E.SpellIconID = Parser.GetUInt(i, 133);
        E.ActiveIconID = Parser.GetUInt(i, 134);
        E.SpellName = Parser.GetString(i, 136);
        E.Rank = Parser.GetString(i, 153);
        E.Description = Parser.GetString(i, 170);
        E.ToolTip = Parser.GetString(i, 187);
        E.ManaCostPercentage = Parser.GetUInt(i, 204);
        E.SpellFamilyName = Parser.GetUInt(i, 208);
        E.DmgClass = Parser.GetUInt(i, 213);
        E.SchoolMask = Parser.GetUInt(i, 225);

        IdIndex.Add(E.ID, Entries.Num());
        Entries.Add(MoveTemp(E));
    }

    UE_LOG(LogSpellDbc, Log, TEXT("Loaded Spell.dbc: %d records"), Entries.Num());
    return true;
}

const FSpellDbcEntry* FSpellDbc::GetById(uint32 ID) const
{
    const int32* Idx = IdIndex.Find(ID);
    return Idx ? &Entries[*Idx] : nullptr;
}
