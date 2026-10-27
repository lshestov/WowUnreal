#include "Formats/Dbc/MapUiDbc.h"
#include "Formats/DbcParser.h"
#include "Mpq/MpqManager.h"

bool FMapUiDbc::Load(FMpqManager& Mpq)
{
    FDbcParser P; bool Complete=true;
    const auto Read=[&](const TCHAR* File,uint32 Fields) {
        TArray<uint8> Bytes;
        const bool Valid=Mpq.ReadFile(FString(TEXT("DBFilesClient\\"))+File,Bytes) && P.Parse(Bytes) && P.GetFieldCount()>=Fields;
        if(!Valid) {Complete=false; UE_LOG(LogTemp,Warning,TEXT("Missing map UI DBC: %s"),File);} return Valid;
    };
    AreaRows.Reset(); OverlayRows.Reset(); ContinentRows.Reset(); FloorRows.Reset(); TransformRows.Reset(); LandmarkRows.Reset(); FloorChunkRows.Reset(); WmoAreaRows.Reset();
    if(Read(TEXT("WorldMapArea.dbc"),11)) for(uint32 I=0;I<P.GetRecordCount();++I)
    {
        FArea A; A.Id=P.GetUInt(I,0); A.MapId=P.GetUInt(I,1); A.AreaId=P.GetUInt(I,2); A.Name=P.GetString(I,3);
        A.Y1=P.GetFloat(I,4); A.Y2=P.GetFloat(I,5); A.X1=P.GetFloat(I,6); A.X2=P.GetFloat(I,7);
        A.DisplayMapId=int32(P.GetUInt(I,8)); A.DefaultFloor=int32(P.GetUInt(I,9)); A.Parent=P.GetUInt(I,10);
        if(!A.Name.IsEmpty()) AreaRows.Add(MoveTemp(A));
    }
    if(Read(TEXT("WorldMapOverlay.dbc"),17)) for(uint32 I=0;I<P.GetRecordCount();++I)
    {
        FOverlay O; O.Id=P.GetUInt(I,0); O.MapAreaId=P.GetUInt(I,1); for(int32 J=0;J<4;++J) O.Areas[J]=P.GetUInt(I,2+J);
        O.MapPointX=P.GetUInt(I,6); O.MapPointY=P.GetUInt(I,7); O.Name=P.GetString(I,8);
        O.Width=P.GetUInt(I,9); O.Height=P.GetUInt(I,10); O.X=P.GetUInt(I,11); O.Y=P.GetUInt(I,12);
        O.Top=P.GetUInt(I,13); O.Left=P.GetUInt(I,14); O.Bottom=P.GetUInt(I,15); O.Right=P.GetUInt(I,16);
        if(!O.Name.IsEmpty() && O.Width>0 && O.Width<=1024 && O.Height>0 && O.Height<=768) OverlayRows.Add(MoveTemp(O));
    }
    if(Read(TEXT("WorldMapContinent.dbc"),14)) for(uint32 I=0;I<P.GetRecordCount();++I)
    {
        FContinent C; C.Id=P.GetUInt(I,0); C.MapId=P.GetUInt(I,1); C.Left=P.GetUInt(I,2); C.Right=P.GetUInt(I,3); C.Top=P.GetUInt(I,4); C.Bottom=P.GetUInt(I,5);
        C.Offset={P.GetFloat(I,6),P.GetFloat(I,7)}; C.Scale=P.GetFloat(I,8); C.WorldMap=P.GetUInt(I,13); ContinentRows.Add(C);
    }
    if(Read(TEXT("DungeonMap.dbc"),8)) for(uint32 I=0;I<P.GetRecordCount();++I)
    {
        FFloor F; F.Id=P.GetUInt(I,0); F.MapId=P.GetUInt(I,1); F.Index=P.GetUInt(I,2); F.MinX=P.GetFloat(I,3); F.MaxX=P.GetFloat(I,4); F.MinY=P.GetFloat(I,5); F.MaxY=P.GetFloat(I,6); F.Parent=P.GetUInt(I,7); FloorRows.Add(F);
    }
    if(Read(TEXT("WorldMapTransforms.dbc"),10)) for(uint32 I=0;I<P.GetRecordCount();++I)
    {
        FTransform T; T.MapId=P.GetUInt(I,1); T.Min={P.GetFloat(I,2),P.GetFloat(I,3)}; T.Max={P.GetFloat(I,4),P.GetFloat(I,5)};
        T.NewMap=P.GetUInt(I,6); T.Offset={P.GetFloat(I,7),P.GetFloat(I,8)}; T.Floor=P.GetUInt(I,9); TransformRows.Add(T);
    }
    if(Read(TEXT("AreaPOI.dbc"),54)) for(uint32 I=0;I<P.GetRecordCount();++I)
    {
        FLandmark M; M.Id=P.GetUInt(I,0); M.Importance=P.GetUInt(I,1); for(int32 J=0;J<9;++J) M.Icons[J]=P.GetUInt(I,2+J);
        M.Faction=P.GetUInt(I,11); M.Position={P.GetFloat(I,12),P.GetFloat(I,13),P.GetFloat(I,14)};
        M.MapId=P.GetUInt(I,15); M.Flags=P.GetUInt(I,16); M.AreaId=P.GetUInt(I,17); M.Name=P.GetString(I,18); M.Description=P.GetString(I,35); M.WorldState=P.GetUInt(I,52); M.Link=P.GetUInt(I,53);
        LandmarkRows.Add(MoveTemp(M));
    }
    if(Read(TEXT("DungeonMapChunk.dbc"),5)) for(uint32 I=0;I<P.GetRecordCount();++I)
    {FFloorChunk C; C.MapId=P.GetUInt(I,1); C.GroupId=P.GetUInt(I,2); C.Floor=P.GetUInt(I,3); C.MinZ=P.GetFloat(I,4); FloorChunkRows.Add(C);}
    if(Read(TEXT("WMOAreaTable.dbc"),28)) for(uint32 I=0;I<P.GetRecordCount();++I)
    {FWmoArea W; W.Root=P.GetUInt(I,1); W.NameSet=P.GetUInt(I,2); W.Group=P.GetUInt(I,3); W.Flags=P.GetUInt(I,9); W.AreaId=P.GetUInt(I,10); W.Name=P.GetString(I,11); WmoAreaRows.Add(MoveTemp(W));}
    ZoneMasks.Reset();
    Vehicles.Reset();
    ResourceLocks.Reset();
    if(Read(TEXT("Lock.dbc"),33)) for(uint32 I=0;I<P.GetRecordCount();++I)
    {uint32 Mask=0; for(uint32 J=0;J<8;++J) if(P.GetUInt(I,1+J)==2) {const uint32 Index=P.GetUInt(I,9+J); if(Index>0 && Index<=32) Mask|=1u<<(Index-1);} if(Mask) ResourceLocks.Add(P.GetUInt(I,0),Mask);}
    if(Read(TEXT("Vehicle.dbc"),40)) for(uint32 I=0;I<P.GetRecordCount();++I) Vehicles.Add(P.GetUInt(I,0),{P.GetUInt(I,1),P.GetUInt(I,34)});
    for(const auto& A:AreaRows) if(!A.AreaId)
    {
        TArray<uint8> Bytes;
        if(Mpq.ReadFile(FString::Printf(TEXT("Interface\\WorldMap\\%s.zmp"),*A.Name),Bytes) && Bytes.Num()==128*128*4)
        {auto& Mask=ZoneMasks.Add(A.MapId); Mask.SetNumUninitialized(128*128); FMemory::Memcpy(Mask.GetData(),Bytes.GetData(),Bytes.Num());}
    }
    return Complete;
}
const FMapUiDbc::FArea* FMapUiDbc::Area(uint32 Id) const {return AreaRows.FindByPredicate([&](const FArea& A){return A.Id==Id;});}
const FMapUiDbc::FFloor* FMapUiDbc::Floor(uint32 Id) const {return FloorRows.FindByPredicate([&](const FFloor& F){return F.Id==Id;});}
const FMapUiDbc::FContinent* FMapUiDbc::Continent(uint32 MapId) const {return ContinentRows.FindByPredicate([&](const FContinent& C){return C.MapId==MapId;});}
uint32 FMapUiDbc::Transform(uint32 MapId,FVector& P,uint32& FloorId) const
{
    FloorId=0;
    for(const auto& T:TransformRows) if(T.MapId==MapId && P.X>=T.Min.X && P.X<=T.Max.X && P.Y>=T.Min.Y && P.Y<=T.Max.Y)
    {P.X+=T.Offset.X; P.Y+=T.Offset.Y; FloorId=T.Floor; return T.NewMap;}
    return MapId;
}
uint32 FMapUiDbc::FloorAt(uint32 MapId,uint32 GroupId,float Z) const
{
    uint32 Id=0; float Highest=-MAX_flt;
    for(const auto& C:FloorChunkRows) if(C.MapId==MapId && C.GroupId==GroupId && Z>=C.MinZ && C.MinZ>Highest) {Id=C.Floor; Highest=C.MinZ;}
    return Id;
}
uint32 FMapUiDbc::HitArea(uint32 MapId,const FVector2D& Point) const
{
    const auto* Mask=ZoneMasks.Find(MapId); if(!Mask || Point.X<0 || Point.Y<0 || Point.X>=1 || Point.Y>=1) return 0;
    return (*Mask)[FMath::FloorToInt32(Point.Y*128)*128+FMath::FloorToInt32(Point.X*128)];
}
