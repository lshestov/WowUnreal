#pragma once
#include "CoreMinimal.h"
#include "Formats/Dbc/MapDbc.h"
#include "Formats/Dbc/AreaTableDbc.h"
#include "Formats/Dbc/LightDbc.h"
#include "Formats/Dbc/LightParamsDbc.h"
#include "Formats/Dbc/LightIntParamsDbc.h"
#include "Formats/Dbc/LiquidTypeDbc.h"
#include "Formats/Dbc/AnimationDataDbc.h"
#include "Formats/Dbc/ChrRacesDbc.h"
#include "Formats/Dbc/CharSectionsDbc.h"
#include "Formats/Dbc/CreatureDisplayInfoDbc.h"
#include "Formats/Dbc/CreatureDisplayInfoExtraDbc.h"
#include "Formats/Dbc/CreatureModelDataDbc.h"
#include "Formats/Dbc/ItemDisplayInfoDbc.h"
#include "Formats/Dbc/SpellDbc.h"
#include "Formats/Dbc/SpellBookDbc.h"
#include "Formats/Dbc/SpellIconDbc.h"
#include "Formats/Dbc/SpellVisualDbc.h"
#include "Formats/Dbc/SpellVisualKitDbc.h"
#include "Formats/Dbc/SpellVisualEffectNameDbc.h"
#include "Formats/Dbc/SoundEntriesDbc.h"
#include "Formats/Dbc/LoadingScreensDbc.h"
#include "Formats/Dbc/GroundEffectTextureDbc.h"
#include "Formats/Dbc/EmotesTextDbc.h"
#include "Formats/Dbc/TalentDbc.h"
#include "Formats/Dbc/LightFloatParamsDbc.h"
#include "Formats/Dbc/TalentTabDbc.h"
#include "Formats/Dbc/ZoneMusicDbc.h"
#include "Formats/Dbc/SoundAmbienceDbc.h"
#include "Formats/Dbc/CharComponentTextureLayoutsDbc.h"
#include "Formats/Dbc/CharComponentTextureSectionsDbc.h"
#include "Formats/Dbc/CharHairGeosetsDbc.h"
#include "Formats/Dbc/CharacterFacialHairStylesDbc.h"
#include "Formats/Dbc/CharStartOutfitDbc.h"
#include "Formats/Dbc/TaxiNodesDbc.h"
#include "Formats/Dbc/ItemUiDbc.h"

class FMpqManager;

class WOWDATA_API FDbcStore
{
public:
    static FDbcStore& Get();

    bool LoadAll(FMpqManager& Mpq);
    void LoadDeferred(FMpqManager& Mpq);
    const FItemUiDbc& ItemUi() const { return ItemUiDbc; }
    float StatRatio(FName Table,uint32 Id) const { const auto* Values=StatRatios.Find(Table); const auto* Value=Values?Values->Find(Id):nullptr; return Value?*Value:0.f; }
    struct FTitle { uint32 Mask=0; FString Male,Female; };
    const FTitle* Title(uint32 Mask) const { return Titles.Find(Mask); }
    uint32 MaxTitleIndex() const { return TitleMax; }
    uint32 QuestXP(uint32 Level,uint32 Difficulty) const { const auto* Row=QuestXPValues.Find(Level); return Row && Row->IsValidIndex(Difficulty)?(*Row)[Difficulty]:0; }
    int32 QuestReputation(int32 ValueId) const { const auto* Value=QuestReputationValues.Find(ValueId); return Value?*Value:0; }
    const FTitle* TitleById(uint32 Id) const { const auto* Mask=TitleMasks.Find(Id); return Mask?Title(*Mask):nullptr; }
    struct FWorldMapArea
    {
        uint32 Id=0,MapId=0,AreaId=0;
        FString Name;
        float Y1=0,Y2=0,X1=0,X2=0;
        FVector2D Project(const FVector& Position) const { return FVector2D((Y1-Position.Y)/(Y1-Y2),(X1-Position.X)/(X1-X2)); }
    };
    const TArray<FWorldMapArea>& WorldMapAreas() const { return MapAreas; }
    struct FWorldMapOverlay { uint32 MapAreaId=0,Areas[4]={}; FString Name; int32 Width=0,Height=0,X=0,Y=0; };
    const TArray<FWorldMapOverlay>& WorldMapOverlays() const { return MapOverlays; }
    struct WOWDATA_API FFaction
    {
        uint32 Id=0,Parent=0,Races[4]={},Classes[4]={}; int32 Index=-1,Base[4]={}; FString Name,Description;
        int32 BaseFor(uint8 Race,uint8 Class) const;
    };
    struct FCurrency { uint32 Item=0,Category=0,Bit=0; };
    const TArray<FFaction>& Factions() const { return UiFactions; }
    const TArray<FCurrency>& Currencies() const { return UiCurrencies; }

    struct FGlyph { uint32 Spell=0,Type=0,Icon=0; };
    struct FGlyphSlot { uint32 Type=0,Tooltip=0; };
    const FGlyph* Glyph(uint32 Id) const { return GlyphProperties.Find(Id); }
    const FGlyphSlot* GlyphSlot(uint32 Id) const { return GlyphSlots.Find(Id); }
    FMapDbc& Maps() { return MapDbc; }
    FAreaTableDbc& AreaTable() { return AreaTableDbc; }
    FLightDbc& Lights() { return LightDbc; }
    FLightParamsDbc& LightParams() { return LightParamsDbc; }
    FLightIntParamsDbc& LightIntParams() { return LightIntParamsDbc; }
    FLightFloatParamsDbc& LightFloatParams() { return LightFloatParamsDbc; }
    FLiquidTypeDbc& LiquidTypes() { return LiquidTypeDbc; }
    FAnimationDataDbc& AnimationData() { return AnimationDataDbc; }
    FChrRacesDbc& ChrRaces() { return ChrRacesDbc; }
    FCharSectionsDbc& CharSections() { return CharSectionsDbc; }
    FCreatureDisplayInfoDbc& CreatureDisplayInfo() { return CreatureDisplayInfoDbc; }
    FCreatureModelDataDbc& CreatureModelData() { return CreatureModelDataDbc; }
    FItemDisplayInfoDbc& ItemDisplayInfo() { return ItemDisplayInfoDbc; }
    FSpellDbc& Spells() { return SpellDbc; }
    FSpellBookDbc& SpellBook() { return SpellBookDbc; }
    FSpellIconDbc& SpellIcons() { return SpellIconDbc; }
    FSpellVisualDbc& SpellVisuals() { return SpellVisualDbc; }
    FSpellVisualKitDbc& SpellVisualKits() { return SpellVisualKitDbc; }
    FSpellVisualEffectNameDbc& SpellVisualEffectNames() { return SpellVisualEffectNameDbc; }
    FSoundEntriesDbc& SoundEntries() { return SoundEntriesDbc; }
    FLoadingScreensDbc& LoadingScreens() { return LoadingScreensDbc; }
    FGroundEffectTextureDbc& GroundEffectTextures() { return GroundEffectTextureDbc; }
    FEmotesTextDbc& EmotesText() { return EmotesTextDbc; }
    FTalentDbc& Talents() { return TalentDbc; }
    FTalentTabDbc& TalentTabs() { return TalentTabDbc; }
    FZoneMusicDbc& ZoneMusic() { return ZoneMusicDbc; }
    FSoundAmbienceDbc& SoundAmbience() { return SoundAmbienceDbc; }
    FCharComponentTextureLayoutsDbc& CharComponentTextureLayouts() { return CharComponentTextureLayoutsDbc; }
    FCharComponentTextureSectionsDbc& CharComponentTextureSections() { return CharComponentTextureSectionsDbc; }
    FCharHairGeosetsDbc& CharHairGeosets() { return CharHairGeosetsDbc; }
    FCharacterFacialHairStylesDbc& CharacterFacialHairStyles() { return CharacterFacialHairStylesDbc; }
    FCharStartOutfitDbc& CharStartOutfit() { return CharStartOutfitDbc; }
    FTaxiNodesDbc& TaxiNodes() { return TaxiNodesDbc; }

    const FMapDbc& Maps() const { return MapDbc; }
    const FAreaTableDbc& AreaTable() const { return AreaTableDbc; }
    const FLightDbc& Lights() const { return LightDbc; }
    const FLightParamsDbc& LightParams() const { return LightParamsDbc; }
    const FLightIntParamsDbc& LightIntParams() const { return LightIntParamsDbc; }
    const FLightFloatParamsDbc& LightFloatParams() const { return LightFloatParamsDbc; }
    const FLiquidTypeDbc& LiquidTypes() const { return LiquidTypeDbc; }
    const FAnimationDataDbc& AnimationData() const { return AnimationDataDbc; }
    const FChrRacesDbc& ChrRaces() const { return ChrRacesDbc; }
    const FCharSectionsDbc& CharSections() const { return CharSectionsDbc; }
    const FCreatureDisplayInfoDbc& CreatureDisplayInfo() const { return CreatureDisplayInfoDbc; }
    const FCreatureDisplayInfoExtraDbc& CreatureDisplayInfoExtra() const { return CreatureDisplayInfoExtraDbc; }
    const FCreatureModelDataDbc& CreatureModelData() const { return CreatureModelDataDbc; }
    const FItemDisplayInfoDbc& ItemDisplayInfo() const { return ItemDisplayInfoDbc; }
    const FSpellDbc& Spells() const { return SpellDbc; }
    const FSpellBookDbc& SpellBook() const { return SpellBookDbc; }
    const FSpellIconDbc& SpellIcons() const { return SpellIconDbc; }
    const FSpellVisualDbc& SpellVisuals() const { return SpellVisualDbc; }
    const FSpellVisualKitDbc& SpellVisualKits() const { return SpellVisualKitDbc; }
    const FSpellVisualEffectNameDbc& SpellVisualEffectNames() const { return SpellVisualEffectNameDbc; }
    const FSoundEntriesDbc& SoundEntries() const { return SoundEntriesDbc; }
    const FLoadingScreensDbc& LoadingScreens() const { return LoadingScreensDbc; }
    const FGroundEffectTextureDbc& GroundEffectTextures() const { return GroundEffectTextureDbc; }
    const FEmotesTextDbc& EmotesText() const { return EmotesTextDbc; }
    const FTalentDbc& Talents() const { return TalentDbc; }
    const FTalentTabDbc& TalentTabs() const { return TalentTabDbc; }
    const FZoneMusicDbc& ZoneMusic() const { return ZoneMusicDbc; }
    const FSoundAmbienceDbc& SoundAmbience() const { return SoundAmbienceDbc; }
    const FCharComponentTextureLayoutsDbc& CharComponentTextureLayouts() const { return CharComponentTextureLayoutsDbc; }
    const FCharComponentTextureSectionsDbc& CharComponentTextureSections() const { return CharComponentTextureSectionsDbc; }
    const FCharHairGeosetsDbc& CharHairGeosets() const { return CharHairGeosetsDbc; }
    const FCharacterFacialHairStylesDbc& CharacterFacialHairStyles() const { return CharacterFacialHairStylesDbc; }
    const FCharStartOutfitDbc& CharStartOutfit() const { return CharStartOutfitDbc; }
    const FTaxiNodesDbc& TaxiNodes() const { return TaxiNodesDbc; }

    bool IsLoaded() const { return bLoaded; }
    bool IsDeferredLoaded() const { return bDeferredLoaded; }

private:
    FMapDbc MapDbc;
    FAreaTableDbc AreaTableDbc;
    FLightDbc LightDbc;
    FLightParamsDbc LightParamsDbc;
    FLightIntParamsDbc LightIntParamsDbc;
    FLightFloatParamsDbc LightFloatParamsDbc;
    FLiquidTypeDbc LiquidTypeDbc;
    FAnimationDataDbc AnimationDataDbc;
    FChrRacesDbc ChrRacesDbc;
    FCharSectionsDbc CharSectionsDbc;
    FCreatureDisplayInfoDbc CreatureDisplayInfoDbc;
    FCreatureDisplayInfoExtraDbc CreatureDisplayInfoExtraDbc;
    FCreatureModelDataDbc CreatureModelDataDbc;
    FItemDisplayInfoDbc ItemDisplayInfoDbc;
    FSpellDbc SpellDbc;
    FSpellBookDbc SpellBookDbc;
    TMap<FName,TMap<uint32,float>> StatRatios;
    TMap<uint32,FTitle> Titles;
    TMap<uint32,uint32> TitleMasks;
    TMap<uint32,TArray<uint32>> QuestXPValues;
    TMap<int32,int32> QuestReputationValues;
    TArray<FWorldMapArea> MapAreas;
    TArray<FWorldMapOverlay> MapOverlays;
    TArray<FFaction> UiFactions;
    TArray<FCurrency> UiCurrencies;
    uint32 TitleMax=0;
    FSpellIconDbc SpellIconDbc;
    FSpellVisualDbc SpellVisualDbc;
    FSpellVisualKitDbc SpellVisualKitDbc;
    FSpellVisualEffectNameDbc SpellVisualEffectNameDbc;
    FSoundEntriesDbc SoundEntriesDbc;
    FLoadingScreensDbc LoadingScreensDbc;
    FGroundEffectTextureDbc GroundEffectTextureDbc;
    FEmotesTextDbc EmotesTextDbc;
    FTalentDbc TalentDbc;
    FTalentTabDbc TalentTabDbc;
    FZoneMusicDbc ZoneMusicDbc;
    FSoundAmbienceDbc SoundAmbienceDbc;
    FCharComponentTextureLayoutsDbc CharComponentTextureLayoutsDbc;
    FCharComponentTextureSectionsDbc CharComponentTextureSectionsDbc;
    FCharHairGeosetsDbc CharHairGeosetsDbc;
    FCharacterFacialHairStylesDbc CharacterFacialHairStylesDbc;
    FCharStartOutfitDbc CharStartOutfitDbc;
    FTaxiNodesDbc TaxiNodesDbc;
    FItemUiDbc ItemUiDbc;
    TMap<uint32,FGlyph> GlyphProperties;
    TMap<uint32,FGlyphSlot> GlyphSlots;
    bool bLoaded = false;
    bool bDeferredLoaded = false;
};
