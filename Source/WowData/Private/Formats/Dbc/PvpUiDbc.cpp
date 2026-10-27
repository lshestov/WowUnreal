#include "Formats/Dbc/PvpUiDbc.h"
#include "Formats/DbcParser.h"
#include "Mpq/MpqManager.h"
bool FPvpUiDbc::Load(FMpqManager& Mpq)
{
    FDbcParser P; TArray<uint8> Bytes; BattlefieldRows.Reset(); WorldRows.Reset(); Descriptions.Reset();
    if(Mpq.ReadFile(TEXT("DBFilesClient\\Map.dbc"),Bytes) && P.Parse(Bytes) && P.GetFieldCount()>=57)
        for(uint32 I=0;I<P.GetRecordCount();++I) Descriptions.Add(P.GetUInt(I,0),{P.GetString(I,23),P.GetString(I,40)});
    if(!Mpq.ReadFile(TEXT("DBFilesClient\\BattlemasterList.dbc"),Bytes) || !P.Parse(Bytes) || P.GetFieldCount()!=32) return false;
    for(uint32 I=0;I<P.GetRecordCount();++I)
    {
        FBattlefield B; B.Id=P.GetUInt(I,0); for(int32 J=1;J<=8;++J) if(P.GetUInt(I,J)!=MAX_uint32) B.Maps.Add(P.GetUInt(I,J));
        B.Type=P.GetUInt(I,9); B.Groups=P.GetUInt(I,10)!=0; B.Name=P.GetString(I,11); B.Group=P.GetUInt(I,28); B.Holiday=P.GetUInt(I,29); B.Min=P.GetUInt(I,30); B.Max=P.GetUInt(I,31); BattlefieldRows.Add(MoveTemp(B));
    }
    if(!Mpq.ReadFile(TEXT("DBFilesClient\\WorldStateUI.dbc"),Bytes) || !P.Parse(Bytes) || P.GetFieldCount()!=63) return false;
    for(uint32 I=0;I<P.GetRecordCount();++I)
    {
        FWorldState S; S.Id=P.GetUInt(I,0); S.Map=P.GetUInt(I,1); S.Area=P.GetUInt(I,2); S.Phase=P.GetUInt(I,3); S.Icon=P.GetString(I,4); S.Text=P.GetString(I,5); S.Tooltip=P.GetString(I,22);
        S.Variable=P.GetUInt(I,39); S.Type=P.GetUInt(I,40); S.DynamicIcon=P.GetString(I,41); S.DynamicTooltip=P.GetString(I,42); S.ExtendedUi=P.GetString(I,59); for(int32 J=0;J<3;++J) S.Extended[J]=P.GetUInt(I,60+J); WorldRows.Add(MoveTemp(S));
    }
    return true;
}
