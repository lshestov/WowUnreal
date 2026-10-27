#include "Formats/Dbc/AchievementUiDbc.h"
#include "Formats/DbcParser.h"
#include "Mpq/MpqManager.h"
bool FAchievementUiDbc::Load(FMpqManager& Mpq)
{
    TMap<uint32,FAchievement> Achievements; TMap<uint32,FCategory> Categories; TMap<uint32,FCriteria> Criteria;
    FDbcParser P; TArray<uint8> Bytes;
    if(!Mpq.ReadFile(TEXT("DBFilesClient\\Achievement.dbc"),Bytes) || !P.Parse(Bytes) || P.GetFieldCount()!=62) return false;
    for(uint32 I=0;I<P.GetRecordCount();++I)
    {
        FAchievement A; A.Id=P.GetUInt(I,0); A.Faction=int32(P.GetUInt(I,1)); A.Map=int32(P.GetUInt(I,2)); A.Previous=P.GetUInt(I,3);
        A.Name=P.GetString(I,4); A.Description=P.GetString(I,21); A.Category=P.GetUInt(I,38); A.Points=P.GetUInt(I,39); A.Order=P.GetUInt(I,40); A.Flags=P.GetUInt(I,41); A.Icon=P.GetUInt(I,42); A.Reward=P.GetString(I,43); A.Count=P.GetUInt(I,60); A.Reference=P.GetUInt(I,61);
        if(!A.Id || Achievements.Contains(A.Id)) return false; Achievements.Add(A.Id,MoveTemp(A));
    }
    if(!Mpq.ReadFile(TEXT("DBFilesClient\\Achievement_Category.dbc"),Bytes) || !P.Parse(Bytes) || P.GetFieldCount()!=20) return false;
    for(uint32 I=0;I<P.GetRecordCount();++I)
    {FCategory C; C.Id=P.GetUInt(I,0); C.Parent=int32(P.GetUInt(I,1)); C.Name=P.GetString(I,2); C.Order=P.GetUInt(I,19); if(!C.Id || Categories.Contains(C.Id)) return false; Categories.Add(C.Id,MoveTemp(C));}
    if(!Mpq.ReadFile(TEXT("DBFilesClient\\Achievement_Criteria.dbc"),Bytes) || !P.Parse(Bytes) || P.GetFieldCount()!=31) return false;
    for(uint32 I=0;I<P.GetRecordCount();++I)
    {
        FCriteria C; C.Id=P.GetUInt(I,0); C.Achievement=P.GetUInt(I,1); C.Type=P.GetUInt(I,2); C.Asset=P.GetUInt(I,3); C.Quantity=P.GetUInt(I,4); C.Name=P.GetString(I,9); C.Flags=P.GetUInt(I,26); C.TimedType=P.GetUInt(I,27); C.TimedAsset=P.GetUInt(I,28); C.Duration=P.GetUInt(I,29); C.Order=P.GetUInt(I,30);
        if(!C.Id || Criteria.Contains(C.Id)) return false; Criteria.Add(C.Id,MoveTemp(C));
    }
    AchievementRows=MoveTemp(Achievements); CategoryRows=MoveTemp(Categories); CriteriaRows=MoveTemp(Criteria); return true;
}
