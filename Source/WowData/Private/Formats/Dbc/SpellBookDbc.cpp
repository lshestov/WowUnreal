#include "Formats/Dbc/SpellBookDbc.h"

bool FSpellBookDbc::LoadSkills(const FDbcParser& P)
{
    if(!P.IsValid() || P.GetFieldCount()<38) return false;
    Skills.Reset();
    for(uint32 I=0; I<P.GetRecordCount(); ++I)
    {
        FWowSkillLine S; S.Id=P.GetUInt(I,0); S.Category=P.GetUInt(I,1); S.Name=P.GetString(I,3); S.Icon=P.GetUInt(I,37);
        S.Description=P.GetString(I,20);
        Skills.Add(S.Id,MoveTemp(S));
    }
    return true;
}
bool FSpellBookDbc::LoadAbilities(const FDbcParser& P)
{
    if(!P.IsValid() || P.GetFieldCount()<5) return false;
    BySpell.Reset();
    for(uint32 I=0; I<P.GetRecordCount(); ++I)
    {
        FWowSkillAbility A; A.Skill=P.GetUInt(I,1); A.Spell=P.GetUInt(I,2); A.RaceMask=P.GetUInt(I,3); A.ClassMask=P.GetUInt(I,4);
        BySpell.FindOrAdd(A.Spell).Add(A);
    }
    return true;
}
bool FSpellBookDbc::LoadRaceClassInfo(const FDbcParser& P)
{
    if(!P.IsValid() || P.GetFieldCount()<8) return false;
    RaceClassInfo.Reset();
    for(uint32 I=0;I<P.GetRecordCount();++I) RaceClassInfo.FindOrAdd(P.GetUInt(I,1)).Add({P.GetUInt(I,2),P.GetUInt(I,3),P.GetUInt(I,4)});
    return true;
}
uint32 FSpellBookDbc::DisplaySkill(uint32 Spell,uint8 Race,uint8 Class) const
{
    const uint32 RaceBit=Race>=1 && Race<=32?1u<<(Race-1):0,ClassBit=Class>=1 && Class<=32?1u<<(Class-1):0;
    const auto Matches=[&](uint32 R,uint32 C) { return (!R || (R&RaceBit)!=0) && (!C || (C&ClassBit)!=0); };
    if(const auto* AbilitiesForSpell=Abilities(Spell)) for(const auto& Ability:*AbilitiesForSpell)
    {
        if(!Matches(Ability.RaceMask,Ability.ClassMask)) continue;
        if(const auto* Infos=RaceClassInfo.Find(Ability.Skill)) for(const auto& Info:*Infos)
            if(Matches(Info.RaceMask,Info.ClassMask)) return Info.Flags&0x80?0:Ability.Skill;
    }
    return 0;
}
