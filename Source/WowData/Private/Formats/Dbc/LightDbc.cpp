#include "Formats/Dbc/LightDbc.h"
#include "Formats/Dbc/MapDbc.h"
#include "Coord/WowCoordinate.h"

DEFINE_LOG_CATEGORY_STATIC(LogLightDbc, Log, All);

bool FLightDbc::Load(const FDbcParser& Parser,const FMapDbc* Maps)
{
    if (!Parser.IsValid()) return false;

    Entries.Reserve(Parser.GetRecordCount());
    for (uint32 i = 0; i < Parser.GetRecordCount(); ++i)
    {
        FLightDbcEntry E;
        E.ID           = Parser.GetUInt(i, 0);
        E.MapID        = Parser.GetUInt(i, 1);
        E.X            = Parser.GetFloat(i, 2);
        E.Y            = Parser.GetFloat(i, 3);
        E.Z            = Parser.GetFloat(i, 4);
        E.FalloffStart = Parser.GetFloat(i, 5);
        E.FalloffEnd   = Parser.GetFloat(i, 6);
        // DBClient/Lights.cpp::TransformMapCoord: DBC editor coordinates are
        // 36 times ADT units (X east, Y height, Z south), not movement coords.
        if(!E.IsGlobal())
        {
            const auto* Map=Maps?Maps->GetById(E.MapID):nullptr;
            const float Origin=Map && (Map->MapType==1 || Map->MapType==2)?0.f:FWowCoordinate::MAP_ORIGIN;
            const float X=Origin-E.Z/36.f,Y=Origin-E.X/36.f,Z=E.Y/36.f;
            E.X=X; E.Y=Y; E.Z=Z; E.FalloffStart/=36.f; E.FalloffEnd/=36.f;
        }
        for (int32 j = 0; j < 8; ++j)
        {
            E.ParamIDs[j] = Parser.GetUInt(i, 7 + j);
        }
        IdIndex.Add(E.ID, Entries.Num());
        Entries.Add(MoveTemp(E));
    }

    UE_LOG(LogLightDbc, Log, TEXT("Loaded Light.dbc: %d records"), Entries.Num());
    return true;
}

const FLightDbcEntry* FLightDbc::GetById(uint32 ID) const
{
    const int32* Idx = IdIndex.Find(ID);
    return Idx ? &Entries[*Idx] : nullptr;
}

TArray<const FLightDbcEntry*> FLightDbc::GetByMap(uint32 MapID) const
{
    TArray<const FLightDbcEntry*> Result;
    for (const FLightDbcEntry& E : Entries)
    {
        if (E.MapID == MapID)
        {
            Result.Add(&E);
        }
    }
    return Result;
}
