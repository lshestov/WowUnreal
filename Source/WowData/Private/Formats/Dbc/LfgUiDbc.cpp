#include "Formats/Dbc/LfgUiDbc.h"
#include "Formats/DbcParser.h"
#include "Mpq/MpqManager.h"
bool FLfgUiDbc::Load(FMpqManager& Mpq)
{
    DungeonRows.Reset(); GroupRows.Reset(); EncounterRows.Reset(); FDbcParser P; TArray<uint8> Bytes;
    if(!Mpq.ReadFile(TEXT("DBFilesClient\\LFGDungeons.dbc"),Bytes) || !P.Parse(Bytes) || P.GetFieldCount()!=49) return false;
    for(uint32 I=0;I<P.GetRecordCount();++I)
    {
        FDungeon D; D.Id=P.GetUInt(I,0); D.Name=P.GetString(I,1); D.Min=P.GetUInt(I,18); D.Max=P.GetUInt(I,19); D.Recommended=P.GetUInt(I,20); D.MinRecommended=P.GetUInt(I,21); D.MaxRecommended=P.GetUInt(I,22);
        D.Map=P.GetUInt(I,23); D.Difficulty=P.GetUInt(I,24); D.Flags=P.GetUInt(I,25); D.Type=P.GetUInt(I,26); D.Faction=int32(P.GetUInt(I,27)); D.Texture=P.GetString(I,28); D.Expansion=P.GetUInt(I,29); D.Order=P.GetUInt(I,30); D.Group=P.GetUInt(I,31); D.Description=P.GetString(I,32); DungeonRows.Add(D.Id,MoveTemp(D));
    }
    if(!Mpq.ReadFile(TEXT("DBFilesClient\\LFGDungeonGroup.dbc"),Bytes) || !P.Parse(Bytes) || P.GetFieldCount()!=21) return false;
    for(uint32 I=0;I<P.GetRecordCount();++I) {FGroup G; G.Id=P.GetUInt(I,0); G.Name=P.GetString(I,1); G.Order=P.GetUInt(I,18); G.Type=P.GetUInt(I,20); GroupRows.Add(G.Id,MoveTemp(G));}
    if(!Mpq.ReadFile(TEXT("DBFilesClient\\DungeonEncounter.dbc"),Bytes) || !P.Parse(Bytes) || P.GetFieldCount()!=23) return false;
    for(uint32 I=0;I<P.GetRecordCount();++I) {FEncounter E; E.Id=P.GetUInt(I,0); E.Map=P.GetUInt(I,1); E.Difficulty=P.GetUInt(I,2); E.Order=P.GetUInt(I,3); E.Index=P.GetUInt(I,4); E.Name=P.GetString(I,5); E.Icon=P.GetUInt(I,22); EncounterRows.Add(MoveTemp(E));}
    return true;
}
