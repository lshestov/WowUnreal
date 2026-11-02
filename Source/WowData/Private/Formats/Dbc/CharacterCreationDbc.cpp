#include "Formats/Dbc/CharacterCreationDbc.h"
#include "Formats/Dbc/CharSectionsDbc.h"
#include "Formats/Dbc/CharacterFacialHairStylesDbc.h"

bool FCharacterCreationDbc::LoadBase(const FDbcParser& Parser)
{
    Combinations.Reset();
    if (!Parser.IsValid() || Parser.GetRecordSize()!=2 || Parser.GetFieldCount()!=2) return false;
    for (uint32 I=0; I<Parser.GetRecordCount(); ++I)
    {
        uint8 Race=Parser.GetByte(I,0), Class=Parser.GetByte(I,1);
        if (Race && Class) Combinations.FindOrAdd(Race).AddUnique(Class);
    }
    for (auto& Pair:Combinations) Pair.Value.Sort();
    return Combinations.Num()>0;
}

bool FCharacterCreationDbc::LoadClasses(const FDbcParser& Parser)
{
    Names.Reset(); Tokens.Reset();
    if (!Parser.IsValid() || Parser.GetFieldCount()!=60) return false;
    for (uint32 I=0; I<Parser.GetRecordCount(); ++I)
    {Names.Add(Parser.GetUInt(I,0),Parser.GetString(I,4));Tokens.Add(Parser.GetUInt(I,0),Parser.GetString(I,55));}
    return Names.Num()>0;
}

TArray<uint8> FCharacterCreationDbc::Races() const
{
    TArray<uint8> Result; Combinations.GetKeys(Result); Result.Sort(); return Result;
}

TArray<uint8> FCharacterCreationDbc::Classes(uint8 Race) const
{
    const auto* Result=Combinations.Find(Race); return Result?*Result:TArray<uint8>();
}

bool FCharacterCreationDbc::Allows(uint8 Race,uint8 Class) const
{
    const auto* Result=Combinations.Find(Race); return Result && Result->Contains(Class);
}

bool FCharacterCreationDbc::CreationFlags(uint32 Flags,uint8 Class)
{
    // Original client: normal selection 0; death knight selection 1.
    return Class==6 ? (Flags&1) && (Flags&0x14) && !(Flags&8) : (Flags&1) && !(Flags&0xc);
}

TArray<uint8> FCharacterCreationDbc::Options(EWowAppearanceOption Option,const FWowCharacterAppearance& A,
    const FCharSectionsDbc& Sections,const FCharacterFacialHairStylesDbc& Facial)
{
    TArray<uint8> Result;
    bool HasFacialTextures=false;
    for (const auto& E:Sections.GetAll())
    {
        if (E.RaceID!=A.Race || E.SexID!=A.Gender) continue;
        if (E.Type==2) HasFacialTextures=true;
        if (!CreationFlags(E.Flags,A.Class)) continue;
        uint32 Value=256;
        switch (Option)
        {
        case EWowAppearanceOption::Skin: if (E.Type==0 && E.Variation==0) Value=E.Color; break;
        case EWowAppearanceOption::Face: if (E.Type==1 && E.Color==A.Skin) Value=E.Variation; break;
        case EWowAppearanceOption::HairStyle: if (E.Type==3 && E.Color==A.HairColor) Value=E.Variation; break;
        case EWowAppearanceOption::HairColor: if (E.Type==3 && E.Variation==A.HairStyle) Value=E.Color; break;
        case EWowAppearanceOption::FacialHair: if (E.Type==2 && E.Color==A.HairColor) Value=E.Variation; break;
        }
        if (Value<=255) Result.AddUnique(uint8(Value));
    }
    // Horns, tusks, piercings etc. may have geosets without a texture variation.
    if (Option==EWowAppearanceOption::FacialHair && !HasFacialTextures)
        for (const auto& E:Facial.GetAll())
            if (E.RaceID==A.Race && E.SexID==A.Gender && E.VariationID<=255) Result.AddUnique(uint8(E.VariationID));
    Result.Sort();
    return Result;
}

bool FCharacterCreationDbc::Normalize(FWowCharacterAppearance& A,const FCharSectionsDbc& Sections,
    const FCharacterFacialHairStylesDbc& Facial)
{
    auto Keep=[&](EWowAppearanceOption Option,uint8& Value)
    {
        const auto Values=Options(Option,A,Sections,Facial);
        if (Values.IsEmpty()) return false;
        if (!Values.Contains(Value)) Value=Values[0];
        return true;
    };
    if (!Keep(EWowAppearanceOption::Skin,A.Skin) || !Keep(EWowAppearanceOption::Face,A.Face)) return false;
    // The previous colour might not exist in the new race/class. First find a
    // real style/colour pair, preserving either coordinate whenever possible.
    if (Options(EWowAppearanceOption::HairStyle,A,Sections,Facial).IsEmpty())
    {
        if (!Keep(EWowAppearanceOption::HairColor,A.HairColor))
        {
            for (const auto& E:Sections.GetAll())
                if (E.RaceID==A.Race && E.SexID==A.Gender && E.Type==3 && E.Variation<=255 && E.Color<=255 && CreationFlags(E.Flags,A.Class))
                { A.HairStyle=E.Variation; A.HairColor=E.Color; break; }
        }
    }
    return Keep(EWowAppearanceOption::HairStyle,A.HairStyle) && Keep(EWowAppearanceOption::HairColor,A.HairColor)
        && Keep(EWowAppearanceOption::FacialHair,A.FacialHair);
}
