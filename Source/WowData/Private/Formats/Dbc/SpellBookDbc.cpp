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
