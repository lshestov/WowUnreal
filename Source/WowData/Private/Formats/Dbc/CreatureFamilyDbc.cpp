#include "Formats/Dbc/CreatureFamilyDbc.h"

float FCreatureFamilyDbcEntry::ModelScale(float DisplayScale, int32 Level, bool bPet) const
{
    const float Base = FMath::IsFinite(DisplayScale) && DisplayScale > 0 ? DisplayScale : 1.f;
    const int32 Range = MaxScaleLevel - MinScaleLevel;
    const float Fraction = Range > 0 ? FMath::Clamp(float(Level - MinScaleLevel) / Range, 0.f, 1.f) : 0.f;
    const float FamilyScale = FMath::Lerp(MinScale, MaxScale, Fraction);
    if (!FMath::IsFinite(FamilyScale) || FamilyScale <= 0) return Base;
    return bPet ? FamilyScale : FMath::Max(Base, FamilyScale);
}

bool FCreatureFamilyDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid() || Parser.GetFieldCount() < 5) return false;
    Entries.Reset();
    for (uint32 Row = 0; Row < Parser.GetRecordCount(); ++Row)
    {
        FCreatureFamilyDbcEntry Entry;
        Entry.ID = Parser.GetUInt(Row, 0);
        Entry.MinScale = Parser.GetFloat(Row, 1);
        Entry.MinScaleLevel = int32(Parser.GetUInt(Row, 2));
        Entry.MaxScale = Parser.GetFloat(Row, 3);
        Entry.MaxScaleLevel = int32(Parser.GetUInt(Row, 4));
        Entries.Add(Entry.ID, Entry);
    }
    return true;
}
