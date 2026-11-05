#include "Formats/Dbc/SpellDbc.h"
#include "Internationalization/Regex.h"
#include "Math/BasicMathExpressionEvaluator.h"

bool FSpellDbc::LoadTimes(const FDbcParser& Parser,bool Cast)
{
    if(!Parser.IsValid() || Parser.GetFieldCount()<4) return false;
    auto& Values=Cast?CastTimes:Durations; Values.Reset();
    for(uint32 I=0; I<Parser.GetRecordCount(); ++I) Values.Add(Parser.GetUInt(I,0),int32(Parser.GetUInt(I,1))/1000.f);
    return true;
}

bool FSpellDbc::LoadDescriptionVariables(const FDbcParser& Parser)
{
    if(!Parser.IsValid() || Parser.GetFieldCount()<2) return false;
    DescriptionVariables.Reset();
    for(uint32 I=0;I<Parser.GetRecordCount();++I)
    {
        TArray<FString> Lines; Parser.GetString(I,1).ParseIntoArrayLines(Lines);
        auto& Values=DescriptionVariables.FindOrAdd(Parser.GetUInt(I,0));
        for(const auto& Line:Lines)
        {FRegexMatcher Definition(FRegexPattern(TEXT("^\\$([A-Za-z_][A-Za-z_0-9]*)=(.*)$")),Line.TrimStartAndEnd()); if(Definition.FindNext()) Values.Add(Definition.GetCaptureGroup(1),Definition.GetCaptureGroup(2));}
    }
    return true;
}
bool FSpellDbc::LoadRadii(const FDbcParser& Parser)
{
    if(!Parser.IsValid() || Parser.GetFieldCount()<4) return false;
    Radii.Reset();
    for(uint32 I=0; I<Parser.GetRecordCount(); ++I) Radii.Add(Parser.GetUInt(I,0),Parser.GetFloat(I,1));
    return true;
}

namespace
{
// Spell.dbc uses integer damage by default and an optional }.1 / }.2 precision.
TOptional<double> EvaluateSpellExpression(FString Expr, FBasicMathExpressionEvaluator& Evaluator)
{
    // Functions may contain other functions and parenthesized arithmetic. Resolve inside out.
    int32 Start;
    while(Expr.FindLastChar('$',Start))
    {
        const int32 Open=Expr.Find(TEXT("("),ESearchCase::CaseSensitive,ESearchDir::FromStart,Start+1);
        if(Open==INDEX_NONE) return {};
        const FString Func=Expr.Mid(Start+1,Open-Start-1).ToLower();
        TArray<double,TInlineAllocator<3>> Args;
        int32 Argument=Open+1, Depth=0, End=Argument;
        for(; End<Expr.Len(); ++End)
        {
            const TCHAR Ch=Expr[End];
            if(Depth==0 && (Ch==',' || Ch==')'))
            {
                auto Value=Evaluator.Evaluate(*Expr.Mid(Argument,End-Argument));
                if(!Value.IsValid() || !FMath::IsFinite(Value.GetValue())) return {};
                Args.Add(Value.GetValue());
                if(Ch==')') break;
                Argument=End+1;
            }
            else if(Ch=='(') ++Depth;
            else if(Ch==')') --Depth;
        }
        if(End==Expr.Len()) return {};
        double Result;
        if(Func==TEXT("cond") && Args.Num()==3) Result=Args[0]!=0?Args[1]:Args[2];
        else if(Func==TEXT("eq") && Args.Num()==2) Result=Args[0]==Args[1];
        else if(Func==TEXT("gt") && Args.Num()==2) Result=Args[0]>Args[1];
        else if(Func==TEXT("gte") && Args.Num()==2) Result=Args[0]>=Args[1];
        else if(Func==TEXT("min") && Args.Num()==2) Result=FMath::Min(Args[0],Args[1]);
        else if(Func==TEXT("max") && Args.Num()==2) Result=FMath::Max(Args[0],Args[1]);
        else if(Func==TEXT("abs") && Args.Num()==1) Result=FMath::Abs(Args[0]);
        else if(Func==TEXT("floor") && Args.Num()==1) Result=FMath::FloorToDouble(Args[0]);
        else return {};
        Expr=Expr.Left(Start)+FString::SanitizeFloat(Result)+Expr.Mid(End+1);
    }
    auto Number=Evaluator.Evaluate(*Expr);
    if(!Number.IsValid() || !FMath::IsFinite(Number.GetValue())) return {};
    return Number.GetValue();
}
}

FString FSpellDbc::Description(uint32 Id,int32 Level,bool Aura,const FSpellTextStats& Stats) const
{
    const auto* Spell=GetById(Id); if(!Spell) return {};
    FString Text=Aura && !Spell->ToolTip.IsEmpty()?Spell->ToolTip:Spell->Description;
    if(const auto* Variables=DescriptionVariables.Find(Spell->DescriptionVariablesID))
    {
        for(int32 Depth=0;Depth<8;++Depth)
        {
            FRegexMatcher Reference(FRegexPattern(TEXT("\\$<([A-Za-z_][A-Za-z_0-9]*)>")),Text); FString Expanded; int32 End=0; bool Changed=false;
            while(Reference.FindNext()) {Expanded+=Text.Mid(End,Reference.GetMatchBeginning()-End); if(const auto* Value=Variables->Find(Reference.GetCaptureGroup(1))) {Expanded+=*Value; Changed=true;} else Expanded+=Reference.GetCaptureGroup(0); End=Reference.GetMatchEnding(); if(Expanded.Len()>65536) return Spell->Description;}
            Expanded+=Text.Mid(End); if(!Changed || Expanded==Text || Expanded.Len()>65536) break; Text=MoveTemp(Expanded);
        }
    }
    // Original 12340 descriptions use talent/glyph and player-level clauses.
    FRegexMatcher Condition(FRegexPattern(TEXT("\\$\\?(?:\\(?s([0-9]+)\\)?|\\$gte\\(\\$pl,([0-9]+)\\))\\[([^\\[\\]]*)\\]\\[([^\\[\\]]*)\\]")),Text);
    FString ConditionalText;
    int32 ConditionalEnd=0;
    while(Condition.FindNext())
    {
        ConditionalText+=Text.Mid(ConditionalEnd,Condition.GetMatchBeginning()-ConditionalEnd);
        const uint32 RequiredSpell=FCString::Atoi(*Condition.GetCaptureGroup(1));
        const bool Known=Condition.GetCaptureGroup(2).IsEmpty()
            ? Stats.bKnown && ((Stats.KnownSpells && Stats.KnownSpells->Contains(RequiredSpell)) || Stats.ActiveSpells.Contains(RequiredSpell))
            : Level>=FCString::Atoi(*Condition.GetCaptureGroup(2));
        ConditionalText+=Condition.GetCaptureGroup(Known?3:4);
        ConditionalEnd=Condition.GetMatchEnding();
    }
    Text=ConditionalText+Text.Mid(ConditionalEnd);
    // Match entire identifiers first: $M is an effect maximum, $MWS a weapon speed.
    FRegexMatcher Stat(FRegexPattern(TEXT("\\$([A-Za-z]{2,})(?![A-Za-z_(])")),Text);
    FString WithStats;
    int32 Last=0;
    while(Stat.FindNext())
    {
        WithStats+=Text.Mid(Last,Stat.GetMatchBeginning()-Last);
        const FString Token=Stat.GetCaptureGroup(1);
        TOptional<double> Value;
        if(Token==TEXT("AP")) Value=Stats.AttackPower;
        else if(Token==TEXT("RAP")) Value=Stats.RangedAttackPower;
        else if(Token==TEXT("SPH")) Value=Stats.HolyPower;
        else if(Token==TEXT("SPS")) Value=Stats.ShadowPower;
        else if(Token==TEXT("SPI")) Value=Stats.Spirit;
        else if(Token==TEXT("PL")) Value=Level;
        else if(Token==TEXT("MWS")) Value=Stats.WeaponSpeed;
        else if(Token==TEXT("MW")) Value=FChar::IsUpper(Token[0])?Stats.WeaponMax:Stats.WeaponMin;
        else if(Token==TEXT("RWS")) Value=Stats.RangedSpeed;
        else if(Token==TEXT("RW")) Value=FChar::IsUpper(Token[0])?Stats.RangedMax:Stats.RangedMin;
        else if(Token==TEXT("HND")) Value=Stats.WeaponHands;
        if(!Value.IsSet()) WithStats+=Stat.GetCaptureGroup(0);
        else if(Stats.bKnown || Token==TEXT("PL")) WithStats+=FString::SanitizeFloat(Value.GetValue(),0);
        else WithStats+=TEXT("?");
        Last=Stat.GetMatchEnding();
    }
    Text=WithStats+Text.Mid(Last);
    FRegexMatcher Match(FRegexPattern(TEXT("\\$(?:([*/])([0-9]+(?:\\.[0-9]+)?);)?(?:(\\d+))?([aAbBdDeEfFhHiImMnNoOqQrRsStTuUxX])([1-3])?(?![a-zA-Z])")),Text);
    FString Result; int32 Previous=0;
    while(Match.FindNext())
    {
        Result+=Text.Mid(Previous,Match.GetMatchBeginning()-Previous);
        const auto* Referenced=Match.GetCaptureGroup(3).IsEmpty()?Spell:GetById(FCString::Atoi(*Match.GetCaptureGroup(3)));
        const int32 Effect=FMath::Clamp(FCString::Atoi(*Match.GetCaptureGroup(5))-1,0,2);
        const TCHAR Token=FChar::ToLower(Match.GetCaptureGroup(4)[0]); FString Value;
        if(Referenced)
        {
            const int32 AtLevel=FMath::Clamp(Level,int32(Referenced->BaseLevel),int32(Referenced->MaxLevel?Referenced->MaxLevel:FMath::Max(Level,int32(Referenced->BaseLevel))));
            const int32 Base=Referenced->EffectBasePoints[Effect]+int32(Referenced->EffectPointsPerLevel[Effect]*(AtLevel-int32(FMath::Max(Referenced->SpellLevel,Referenced->BaseLevel))));
            const int32 Sides=Referenced->EffectDieSides[Effect];
            const int32 First=FMath::Abs(Base+(Sides!=0)),LastRoll=FMath::Abs(Base+Sides);
            const int32 Points=FMath::Min(First,LastRoll),Maximum=FMath::Max(First,LastRoll);
            const int32 Start=Match.GetMatchBeginning();
            const bool InsideFormula=Text.Find(TEXT("${"),ESearchCase::CaseSensitive,ESearchDir::FromEnd,Start)>Text.Find(TEXT("}"),ESearchCase::CaseSensitive,ESearchDir::FromEnd,Start);
            if(Token=='d')
            {
                const float Seconds=Duration(Referenced->DurationIndex);
                if(InsideFormula) Value=FString::Printf(TEXT("%g"),Seconds);
                else Value=Seconds<0?TEXT("until cancelled"):FString::Printf(TEXT("%g %s"),Seconds>=60?Seconds/60:Seconds,Seconds>=60?TEXT("min"):TEXT("sec"));
            }
            else if(Token=='s' || Token=='m')
            {
                if(Token=='s' && Maximum!=Points && !InsideFormula && Match.GetCaptureGroup(1).IsEmpty())
                    Value=FString::Printf(TEXT("%d to %d"),Points,Maximum);
                else Value=FString::FromInt(Match.GetCaptureGroup(4)[0]=='M'?Maximum:Points);
            }
            else if(Token=='a') Value=FString::Printf(TEXT("%g"),Radii.FindRef(Referenced->EffectRadiusIndex[Effect]));
            else if(Token=='t') Value=FString::Printf(TEXT("%g"),Referenced->EffectAmplitude[Effect]/1000.f);
            else if(Token=='u') Value=FString::FromInt(Referenced->StackAmount);
            else if(Token=='h') Value=FString::FromInt(Referenced->ProcChance);
            else if(Token=='n') Value=FString::FromInt(Referenced->ProcCharges);
            else if(Token=='i') Value=FString::FromInt(Referenced->MaxAffectedTargets);
            else if(Token=='x') Value=FString::FromInt(Referenced->EffectChainTarget[Effect]);
            else if(Token=='q') Value=FString::FromInt(Referenced->EffectMiscValue[Effect]);
            else if(Token=='b') Value=FString::Printf(TEXT("%g"),Referenced->EffectPointsPerComboPoint[Effect]);
            else if(Token=='f') Value=FString::Printf(TEXT("%g"),Referenced->EffectValueMultiplier[Effect]);
            else if(Token=='e') Value=FString::Printf(TEXT("%g"),Referenced->EffectDamageMultiplier[Effect]);
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
    // DBC descriptions contain numeric expressions such as ${$s1/10}.
    // Evaluate only the arithmetic grammar, after resolving spell tokens.
    FRegexMatcher Formula(FRegexPattern(TEXT("\\$\\{([^{}]+)\\}(?:\\.([0-9]))?")),Result);
    FString Expanded; Previous=0; FBasicMathExpressionEvaluator Evaluator;
    while(Formula.FindNext())
    {
        Expanded+=Result.Mid(Previous,Formula.GetMatchBeginning()-Previous);
        const auto Number=EvaluateSpellExpression(Formula.GetCaptureGroup(1),Evaluator);
        const int32 Precision=FCString::Atoi(*Formula.GetCaptureGroup(2));
        if(!Number.IsSet()) Expanded+=Formula.GetCaptureGroup(1).Contains(TEXT("?"))?TEXT("?"):Formula.GetCaptureGroup(0);
        else if(Precision) Expanded+=FString::Printf(TEXT("%.*f"),Precision,Number.GetValue());
        else Expanded+=FString::Printf(TEXT("%.0f"),FMath::TruncToDouble(Number.GetValue()));
        Previous=Formula.GetMatchEnding();
    }
    Result=Expanded+Result.Mid(Previous);
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
        E.ProcChance=Parser.GetUInt(i,35);
        E.ProcCharges=Parser.GetUInt(i,36);
        E.MaxAffectedTargets=Parser.GetUInt(i,212);
        E.BaseLevel = Parser.GetUInt(i, 38);
        E.SpellLevel = Parser.GetUInt(i, 39);
        E.DurationIndex = Parser.GetUInt(i, 40);
        E.PowerType = Parser.GetUInt(i, 41);
        E.ManaCost = Parser.GetUInt(i, 42);
        E.RangeIndex = Parser.GetUInt(i, 46);
        E.Speed = Parser.GetFloat(i, 47);
        E.StackAmount = Parser.GetUInt(i, 49);

        const uint32 RequiredVision = Parser.GetUInt(i, 221);
        E.RequiredAuraVisionMask = RequiredVision >= 1 && RequiredVision <= 32 ? 1u << (RequiredVision - 1) : 0;

        // Effect arrays
        for (int32 j = 0; j < 3; ++j)
        {
            E.Effect[j] = Parser.GetUInt(i, 71 + j);
            E.EffectBasePoints[j] = static_cast<int32>(Parser.GetUInt(i, 80 + j));
            E.EffectDieSides[j] = int32(Parser.GetUInt(i,74+j));
            E.EffectPointsPerLevel[j] = Parser.GetFloat(i,77+j);
            E.EffectAmplitude[j] = Parser.GetUInt(i,98+j);
            E.EffectRadiusIndex[j] = Parser.GetUInt(i,92+j);
            E.EffectValueMultiplier[j]=Parser.GetFloat(i,101+j);
            E.EffectChainTarget[j]=Parser.GetUInt(i,104+j);
            E.EffectPointsPerComboPoint[j]=Parser.GetFloat(i,119+j);
            E.EffectDamageMultiplier[j]=Parser.GetFloat(i,216+j);
            E.EffectImplicitTargetA[j] = Parser.GetUInt(i, 86 + j);
            E.EffectImplicitTargetB[j] = Parser.GetUInt(i, 89 + j);
            E.EffectApplyAuraName[j] = Parser.GetUInt(i, 95 + j);
            E.EffectMiscValue[j] = static_cast<int32>(Parser.GetUInt(i, 110 + j));
            // Build 12340 also gates detection visuals implicitly through their aura effects.
            if (E.EffectApplyAuraName[j] == 17) E.RequiredAuraVisionMask |= 0x20; // MOD_STEALTH_DETECT
            if (E.EffectApplyAuraName[j] == 19 && (E.EffectMiscValue[j] == 0 || E.EffectMiscValue[j] == 10))
                E.RequiredAuraVisionMask |= 0x40; // MOD_INVISIBILITY_DETECT
            E.EffectMiscValueB[j] = Parser.GetUInt(i,113+j);
            E.EffectTriggerSpell[j] = Parser.GetUInt(i, 116 + j);
        }

        E.SpellVisual[0] = Parser.GetUInt(i, 131);
        E.SpellVisual[1] = Parser.GetUInt(i, 132);
        E.SpellIconID = Parser.GetUInt(i, 133);
        E.ActiveIconID = Parser.GetUInt(i, 134);
        E.SpellName = Parser.GetString(i, 136);
        E.Rank = Parser.GetString(i, 153);
        E.Description = Parser.GetString(i, 170);
        if(Parser.GetFieldCount()>232) E.DescriptionVariablesID=Parser.GetUInt(i,232);
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
