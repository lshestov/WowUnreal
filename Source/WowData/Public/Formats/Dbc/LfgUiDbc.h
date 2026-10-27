#pragma once
#include "CoreMinimal.h"
class FMpqManager;
class WOWDATA_API FLfgUiDbc
{
public:
    struct FDungeon
    {
        uint32 Id=0,Min=0,Max=0,Recommended=0,MinRecommended=0,MaxRecommended=0,Map=0,Difficulty=0,Flags=0,Type=0,Expansion=0,Order=0,Group=0;
        int32 Faction=-1; FString Name,Texture,Description;
        uint32 Entry() const {return Id|(Type<<24);}
    };
    struct FGroup {uint32 Id=0,Order=0,Type=0; FString Name;};
    struct FEncounter {uint32 Id=0,Map=0,Difficulty=0,Order=0,Index=0,Icon=0; FString Name;};
    bool Load(FMpqManager& Mpq);
    const FDungeon* Dungeon(uint32 Id) const {return DungeonRows.Find(Id&0xFFFFFF);}
    const FGroup* Group(uint32 Id) const {return GroupRows.Find(Id);}
    const TMap<uint32,FDungeon>& Dungeons() const {return DungeonRows;}
    const TMap<uint32,FGroup>& Groups() const {return GroupRows;}
    const TArray<FEncounter>& Encounters() const {return EncounterRows;}
private:
    TMap<uint32,FDungeon> DungeonRows;
    TMap<uint32,FGroup> GroupRows;
    TArray<FEncounter> EncounterRows;
};
