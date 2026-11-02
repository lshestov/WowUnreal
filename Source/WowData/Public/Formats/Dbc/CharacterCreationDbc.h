#pragma once
#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

class FCharSectionsDbc;
class FCharacterFacialHairStylesDbc;

/** Wire values, not indices in a contiguous guessed range. */
struct WOWDATA_API FWowCharacterAppearance
{
    uint8 Race = 1, Class = 1, Gender = 0;
    uint8 Skin = 0, Face = 0, HairStyle = 0, HairColor = 0, FacialHair = 0;
    bool operator==(const FWowCharacterAppearance& B) const
    {
        return Race==B.Race && Class==B.Class && Gender==B.Gender && Skin==B.Skin && Face==B.Face &&
            HairStyle==B.HairStyle && HairColor==B.HairColor && FacialHair==B.FacialHair;
    }
};

enum class EWowAppearanceOption : uint8 { Skin, Face, HairStyle, HairColor, FacialHair };

/** Original CharBaseInfo, ChrClasses and ComponentFlagsMatch creation semantics. */
class WOWDATA_API FCharacterCreationDbc
{
public:
    bool LoadBase(const FDbcParser& Parser);
    bool LoadClasses(const FDbcParser& Parser);
    TArray<uint8> Races() const;
    TArray<uint8> Classes(uint8 Race) const;
    FString ClassName(uint8 Class) const { const auto* Name=Names.Find(Class); return Name?*Name:FString(); }
    FString ClassToken(uint8 Class) const { const auto* Token=Tokens.Find(Class); return Token?*Token:FString(); }
    bool Allows(uint8 Race, uint8 Class) const;
    static bool CreationFlags(uint32 Flags, uint8 Class);
    static TArray<uint8> Options(EWowAppearanceOption Option, const FWowCharacterAppearance& Appearance,
        const FCharSectionsDbc& Sections, const FCharacterFacialHairStylesDbc& Facial);
    static bool Normalize(FWowCharacterAppearance& Appearance,
        const FCharSectionsDbc& Sections, const FCharacterFacialHairStylesDbc& Facial);
private:
    TMap<uint8,TArray<uint8>> Combinations;
    TMap<uint8,FString> Names;
    TMap<uint8,FString> Tokens;
};
