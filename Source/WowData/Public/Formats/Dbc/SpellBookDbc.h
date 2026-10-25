#pragma once
#include "CoreMinimal.h"
#include "Formats/DbcParser.h"

struct FWowSkillLine
{
    uint32 Id=0,Category=0,Icon=0;
    FString Name,Description;
};
struct FWowSkillAbility
{
    uint32 Skill=0,Spell=0,ClassMask=0,RaceMask=0;
};
class WOWDATA_API FSpellBookDbc
{
public:
    bool LoadSkills(const FDbcParser& Parser);
    bool LoadAbilities(const FDbcParser& Parser);
    bool LoadRaceClassInfo(const FDbcParser& Parser);
    uint32 DisplaySkill(uint32 Spell,uint8 Race,uint8 Class) const;
    const FWowSkillLine* Skill(uint32 Id) const { return Skills.Find(Id); }
    const TArray<FWowSkillAbility>* Abilities(uint32 Spell) const { return BySpell.Find(Spell); }
private:
    TMap<uint32,FWowSkillLine> Skills;
    TMap<uint32,TArray<FWowSkillAbility>> BySpell;
    struct FSkillInfo { uint32 RaceMask=0,ClassMask=0,Flags=0; };
    TMap<uint32,TArray<FSkillInfo>> RaceClassInfo;
};
