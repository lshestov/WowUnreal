#include "WowMapUi.h"
#include "WowMinimapUi.h"
#include "LuaApi/LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"
#include "Coord/WowCoordinate.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

namespace
{
const FMapUiDbc& Data() {return FDbcStore::Get().MapUi();}
const FMapUiDbc::FArea* Current(FWowLuaContext* C)
{
    if(!C || !C->ConnectionManager) return nullptr;
    const uint32 Map=C->ConnectionManager->PacketHandler.CurrentMapId;
    const auto* A=FDbcStore::Get().AreaTable().GetById(WowLuaApi::ResolveAreaId(C));
    for(int32 Depth=0;A && Depth<8;++Depth)
    {
        if(const auto* Row=Data().Areas().FindByPredicate([&](const auto& M){return M.AreaId==A->ID && M.MapId==Map;})) return Row;
        A=FDbcStore::Get().AreaTable().GetById(A->ParentAreaID);
    }
    const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    const FMapUiDbc::FArea* Found=nullptr; double Size=DBL_MAX;
    if(P) for(const auto& M:Data().Areas()) if(M.MapId==Map && M.HasBounds())
    {
        const auto Point=M.Project(P->Movement.Position); const double Extent=FMath::Abs((M.Y1-M.Y2)*(M.X1-M.X2));
        if(Point.X>=0 && Point.X<=1 && Point.Y>=0 && Point.Y<=1 && Extent<Size) {Found=&M; Size=Extent;}
    }
    return Found;
}
bool Inside(const FVector2D& P) {return P.X>=0 && P.Y>=0 && P.X<=1 && P.Y<=1;}
}
FWowMapUiView& WowMapUi::View(FWowLuaContext* C) {if(!C->Map) C->Map=MakeShared<FWowMapUiView>(); return *C->Map;}
TArray<const FMapUiDbc::FArea*> WowMapUi::Continents()
{
    TArray<const FMapUiDbc::FArea*> Rows; for(const auto& A:Data().Areas()) if(!A.AreaId) Rows.Add(&A); return Rows;
}
TArray<const FMapUiDbc::FArea*> WowMapUi::Zones(int32 Continent)
{
    const auto All=Continents(); TArray<const FMapUiDbc::FArea*> Rows; if(!All.IsValidIndex(Continent-1)) return Rows;
    for(const auto& A:Data().Areas()) if(A.AreaId && A.DisplayMap()==All[Continent-1]->MapId) Rows.Add(&A);
    Rows.Sort([](const auto& A,const auto& B) {
        const auto* AN=FDbcStore::Get().AreaTable().GetById(A.AreaId); const auto* BN=FDbcStore::Get().AreaTable().GetById(B.AreaId);
        return (AN?AN->Name:A.Name).Compare(BN?BN->Name:B.Name,ESearchCase::IgnoreCase)<0;
    }); return Rows;
}
WowMapUi::FSelection WowMapUi::Selection(FWowLuaContext* C)
{
    FSelection S; if(!C) return S;
    if(const auto* Id=C->SelectedRows.Find(TEXT("mapArea"))) {if(*Id<0) {S.Cosmic=*Id==-2; S.Continent=S.Cosmic?-1:0; return S;} S.Area=Data().Area(*Id);}
    else S.Area=Current(C);
    if(!S.Area) return S;
    const auto All=Continents(); S.Continent=-1;
    for(int32 I=0;I<All.Num();++I) if(All[I]->MapId==S.Area->DisplayMap()) {S.Continent=I+1; break;}
    const auto List=Zones(S.Continent); for(int32 I=0;I<List.Num();++I) if(List[I]->Id==S.Area->Id) {S.Zone=I+1; break;}
    if(const auto* Floor=C->SelectedRows.Find(TEXT("mapFloor"))) S.Floor=Data().Floor(*Floor);
    else
    {
        const auto* Actual=!C->SelectedRows.Contains(TEXT("mapArea"))?Data().Floor(CurrentFloor(C)):nullptr;
        if(Actual && S.Area->AreaId && Actual->MapId==S.Area->MapId) S.Floor=Actual;
        else if(S.Area->DefaultFloor>0) S.Floor=Data().Floor(S.Area->DefaultFloor);
        else if(S.Area->AreaId && S.Area->DefaultFloor!=-1 && (!S.Area->HasBounds() || !Data().Continent(S.Area->MapId)))
            S.Floor=Data().Floors().FindByPredicate([&](const auto& F){return F.MapId==S.Area->MapId && F.Index==1;});
    }
    return S;
}
void WowMapUi::Select(FWowLuaContext* C,int32 AreaId,int32 FloorId)
{
    if(!C || (AreaId>=0 && !Data().Area(AreaId))) return;
    C->SelectedRows.FindOrAdd(TEXT("mapArea"))=AreaId; C->SelectedRows.Remove(TEXT("mapFloor"));
    if(FloorId>=0) C->SelectedRows.Add(TEXT("mapFloor"),FloorId);
    if(C->EventSystem) C->EventSystem->FireEvent(TEXT("WORLD_MAP_UPDATE"));
}
bool WowMapUi::SelectZoom(FWowLuaContext* C,int32 Continent,int32 Zone)
{
    if(!C || Continent < -1) return false;
    if(Continent<=0) {Select(C,Continent==0?-1:-2); return true;}
    const auto All=Continents(); if(!All.IsValidIndex(Continent-1)) return false;
    const auto List=Zones(Continent);
    if(Zone>0 && !List.IsValidIndex(Zone-1)) return false;
    Select(C,Zone>0?List[Zone-1]->Id:All[Continent-1]->Id); return true;
}
uint32 WowMapUi::CurrentFloor(FWowLuaContext* C)
{
    if(C && C->ResolveDungeonFloor) if(const uint32 Floor=C->ResolveDungeonFloor()) return Floor;
    if(C && WowMinimapUi::View(C).Location.FloorId) return WowMinimapUi::View(C).Location.FloorId;
    const auto* Area=Current(C); return Area && Area->DefaultFloor>0?uint32(Area->DefaultFloor):0;
}
FVector2D WowMapUi::Project(FWowLuaContext* C,uint32 MapId,const FVector& Position,uint32 ActualFloor,bool Unclamped)
{
    const auto S=Selection(C); FVector P=Position; uint32 TransformedFloor=0; const uint32 NewMap=Data().Transform(MapId,P,TransformedFloor);
    FVector2D Point(-1,-1);
    if(S.Cosmic) return Point;
    if(!S.Area)
    {
        if(const auto* Continent=Data().Continent(NewMap); Continent && Continent->WorldMap==1)
            Point={.5+(Continent->Offset.X-P.Y*Continent->Scale/FWowCoordinate::TILE_SIZE)/(1002./16),.5+(Continent->Offset.Y-P.X*Continent->Scale/FWowCoordinate::TILE_SIZE)/(668./16)};
    }
    else if((S.Area->AreaId && (S.Area->MapId==MapId || S.Area->MapId==NewMap)) || (!S.Area->AreaId && S.Area->MapId==NewMap))
    {
        const FVector& Pos=S.Area->AreaId?Position:P;
        if(S.Floor)
        {
            if(ActualFloor==S.Floor->Id) Point=S.Floor->Project(Pos);
            else if(TransformedFloor==S.Floor->Id) Point=S.Floor->Project(P);
        }
        else Point=S.Area->Project(Pos);
    }
    return Unclamped || Inside(Point)?Point:FVector2D(-1,-1);
}
FVector2D WowMapUi::PlayerPosition(FWowLuaContext* C,uint64 Guid)
{
    if(!C || !C->ConnectionManager || !C->EntityManager) return {-1,-1};
    const auto* E=C->EntityManager->Find(Guid); if(!E) return {-1,-1};
    FVector Position=E->Movement.Position;
    if(C->ResolveActor) if(auto* Actor=C->ResolveActor(Guid))
    {Position=Actor->GetActorLocation(); if(const auto* Character=Cast<ACharacter>(Actor)) Position.Z-=Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(); Position=FWowCoordinate::UEToWow(Position);}
    return Project(C,C->ConnectionManager->PacketHandler.CurrentMapId,Position,Guid==C->EntityManager->LocalPlayerGuid?CurrentFloor(C):0);
}
bool WowMapUi::Explored(FWowLuaContext* C,uint32 AreaId)
{
    const auto* A=FDbcStore::Get().AreaTable().GetById(AreaId); if(!A) return false; if(A->AreaLevel<0) return true;
    const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    return P && A->ExploreFlag<4096 && (P->GetField(PlayerField::BASE+0x037D+A->ExploreFlag/32)&(1u<<(A->ExploreFlag%32)));
}
TArray<const FMapUiDbc::FOverlay*> WowMapUi::Overlays(FWowLuaContext* C)
{
    TArray<const FMapUiDbc::FOverlay*> Rows; const auto S=Selection(C); if(!S.Area || S.Floor) return Rows;
    for(const auto& O:Data().Overlays()) if(O.MapAreaId==S.Area->Id) for(uint32 Id:O.Areas) if(Id && Explored(C,Id)) {Rows.Add(&O); break;}
    return Rows;
}
TArray<WowMapUi::FLandmark> WowMapUi::Landmarks(FWowLuaContext* C)
{
    TArray<FLandmark> Rows; if(!C || !C->ConnectionManager) return Rows;
    const auto S=Selection(C); if(S.Cosmic) return Rows;
    const auto& H=C->ConnectionManager->PacketHandler;
    for(const auto& P:Data().Landmarks())
    {
        const bool Zone=S.Area && S.Area->AreaId;
        if((Zone && !(P.Flags&4)) || (!Zone && S.Area && !(P.Flags&8)) || (!S.Area && !(P.Flags&0x10))) continue;
        if((P.Flags&0x400) && (!S.Area || S.Area->AreaId!=P.AreaId || S.Area->MapId!=P.MapId)) continue;
        if((P.AreaId && !Explored(C,P.AreaId)) || (P.WorldState && !H.WorldStates.FindRef(P.WorldState))) continue;
        const auto Point=Project(C,P.MapId,P.Position,S.Floor?S.Floor->Id:0); if(!Inside(Point)) continue;
        FLandmark M; M.Name=P.Name; M.Description=P.Description; M.Point=Point; M.Link=P.Link; M.Graveyard=(P.Flags&0x200)!=0;
        if(!Zone || (P.Flags&0x80)) {const uint32 State=P.WorldState?H.WorldStates.FindRef(P.WorldState)-1:0; M.Icon=P.Icons[State<9?State:0];} Rows.Add(MoveTemp(M));
    }
    if(H.GossipPoi.Present)
    {
        const auto Point=Project(C,H.GossipPoi.MapId,H.GossipPoi.Position,S.Floor?S.Floor->Id:0);
        if(Inside(Point)) {FLandmark M; M.Name=H.GossipPoi.Name; M.Icon=H.GossipPoi.Icon; M.Point=Point; Rows.Add(MoveTemp(M));}
    }
    return Rows;
}
FVector4 WowMapUi::WorldContinentBounds(const FMapUiDbc::FContinent& C)
{
    const double X=1002./32-C.Scale*32+C.Offset.X,Y=668./32-C.Scale*32+C.Offset.Y;
    return FVector4((X+C.Left*C.Scale)*16/1002,(Y+C.Top*C.Scale)*16/668,(C.Right-C.Left+1)*C.Scale*16/1002,(C.Bottom-C.Top+1)*C.Scale*16/668);
}
const FMapUiDbc::FArea* WowMapUi::HoverArea(FWowLuaContext* C,const FVector2D& Point)
{
    if(!Inside(Point)) return nullptr; const auto S=Selection(C);
    if(!S.Area)
    {
        if(S.Cosmic) return nullptr;
        for(const auto* A:Continents()) if(const auto* Continent=Data().Continent(A->MapId); Continent && Continent->WorldMap==1)
        {const auto R=WorldContinentBounds(*Continent); if(Point.X>=R.X && Point.Y>=R.Y && Point.X<=R.X+R.Z && Point.Y<=R.Y+R.W) return A;}
        return nullptr;
    }
    if(S.Area->AreaId) return S.Area;
    FVector World(S.Area->X1-Point.Y*(S.Area->X1-S.Area->X2),S.Area->Y1-Point.X*(S.Area->Y1-S.Area->Y2),0); uint32 Floor=0;
    const uint32 Map=Data().Transform(S.Area->MapId,World,Floor);
    const FVector2D Grid(.5-World.Y/(FWowCoordinate::TILE_SIZE*64),.5-World.X/(FWowCoordinate::TILE_SIZE*64));
    const auto& Areas=FDbcStore::Get().AreaTable(); const auto* A=Areas.GetById(Data().HitArea(Map,Grid));
    for(int32 Depth=0;A && A->ParentAreaID && Depth<8;++Depth) A=Areas.GetById(A->ParentAreaID);
    return A?Data().Areas().FindByPredicate([&](const auto& M){return M.AreaId==A->ID && M.DisplayMap()==S.Area->MapId;}):nullptr;
}
