#include "WowWmoArea.h"
#include "Coord/WowCoordinate.h"
#include "Formats/Dbc/DbcStore.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
const FMapUiDbc::FWmoArea* Area(uint32 Root,uint32 NameSet,uint32 Group)
{
    for(const auto& A:FDbcStore::Get().MapUi().WmoAreas()) if(A.Root==Root && A.NameSet==NameSet && A.Group==Group) return &A;
    return nullptr;
}
uint32 Outcode(const FVector& P,const FBox& B)
{
    return (P.X<B.Min.X?1:0)|(P.X>B.Max.X?2:0)|(P.Y<B.Min.Y?4:0)|(P.Y>B.Max.Y?8:0)|(P.Z<B.Min.Z?16:0)|(P.Z>B.Max.Z?32:0);
}
}
FVector UWowWmoAreaComponent::ToLocal(const FVector& Position) const
{
    const auto P=GetOwner()->GetActorTransform().InverseTransformPosition(Position)/FWowCoordinate::SCALE; return FVector(P.Y,P.X,P.Z);
}
FVector UWowWmoAreaComponent::ToWorld(const FVector& Position) const
{
    return GetOwner()->GetActorTransform().TransformPosition(FVector(Position.Y,Position.X,Position.Z)*FWowCoordinate::SCALE);
}
FWowWmoLocation UWowWmoAreaComponent::Locate(UWorld* World,const FVector& Position,uint32 MapId)
{
    FWowWmoLocation Result; if(!World) return Result;
    TArray<FHitResult> Hits; const FCollisionObjectQueryParams Objects(ECC_WorldStatic); FCollisionQueryParams Query(SCENE_QUERY_STAT(WowWmoArea),true);
    World->LineTraceMultiByObjectType(Hits,Position+FVector(0,0,10),Position-FVector(0,0,500),Objects,Query);
    double Best=MAX_dbl,Terrain=MAX_dbl;
    for(const auto& Hit:Hits)
    {
        if(Hit.ImpactNormal.Z<.5 || !Hit.Component.IsValid()) continue;
        const auto* Component=Hit.Component.Get(); const auto* Actor=Component->GetOwner(); if(!Actor) continue;
        const double Distance=Position.Z-Hit.ImpactPoint.Z;
        if(Component->GetName().StartsWith(TEXT("TerrainCollision"))) Terrain=FMath::Min(Terrain,Distance);
        auto* Meta=Actor->FindComponentByClass<UWowWmoAreaComponent>(); if(!Meta || Distance>=Best) continue;
        for(int32 I=0;I<Meta->Groups.Num();++I) if(Meta->Groups[I].Collision.Get()==Component)
        {
            if(!Meta->Groups[I].Bounds.ExpandBy(.25).IsInsideOrOn(Meta->ToLocal(Position))) break;
            Result=Meta->Location(I,Position,MapId); Best=Distance; break;
        }
    }
    return Terrain+2<Best?FWowWmoLocation():Result;
}
FWowWmoLocation UWowWmoAreaComponent::Location(int32 Index,const FVector& Position,uint32 MapId) const
{
    FWowWmoLocation L; if(!Groups.IsValidIndex(Index)) return L;
    L.Component=const_cast<UWowWmoAreaComponent*>(this); L.Group=Index; L.LocalPosition=ToLocal(Position);
    const auto& G=Groups[Index]; const auto* Root=Area(RootId,NameSet,MAX_uint32); const auto* Room=Area(RootId,NameSet,G.Id);
    L.AreaId=Room && Room->AreaId?Room->AreaId:Root?Root->AreaId:0;
    if(const auto* A=FDbcStore::Get().AreaTable().GetById(L.AreaId); !A || A->MapID!=MapId) L.AreaId=0;
    if(Root) L.ZoneName=Root->Name; if(Room) L.SubzoneName=Room->Name;
    L.Indoors=!(G.Flags&8); const uint32 Flags=Room?Room->Flags:Root?Root->Flags:0;
    if(Flags&2) L.Indoors=true; if(Flags&4) L.Indoors=false;
    L.WholeMap=Root && (Root->Flags&0x18) && (G.Flags&0x48);
    L.Minimap=!(Flags&0x60) && (L.Indoors || L.WholeMap || (Root && (Root->Flags&1)));
    L.FloorId=FDbcStore::Get().MapUi().FloorAt(MapId,G.Id,FWowCoordinate::UEToWow(Position).Z);
    return L;
}
TArray<FWowWmoMapQuad> UWowWmoAreaComponent::GroupQuads(const FBox& B,int32 Group)
{
    TArray<FWowWmoMapQuad> Out; if(!B.IsValid || B.GetSize().X<=0 || B.GetSize().Y<=0) return Out;
    const int32 NX=FMath::Clamp(FMath::CeilToInt32(B.GetSize().X/128),1,256),NY=FMath::Clamp(FMath::CeilToInt32(B.GetSize().Y/128),1,256);
    const auto Step=[](double Size){return FMath::Clamp(FMath::RoundUpToPowerOfTwo(uint32(FMath::Max(1,FMath::CeilToInt32(Size*2)))),32u,256u)*.5;};
    const double SX=Step(B.GetSize().X),SY=Step(B.GetSize().Y),Z=B.GetCenter().Z;
    for(int32 Y=0;Y<NY && Out.Num()<256;++Y) for(int32 X=0;X<NX && Out.Num()<256;++X)
    {
        FWowWmoMapQuad Q; Q.Group=Group; Q.X=X; Q.Y=Y;
        Q.Bounds=FBox(FVector(B.Min.X+X*SX-(X==0?1:0),B.Min.Y+Y*SY-(Y==0?1:0),Z),FVector(B.Min.X+(X+1)*SX+(X==NX-1?1:0),B.Min.Y+(Y+1)*SY+(Y==NY-1?1:0),Z)); Out.Add(Q);
    }
    return Out;
}
TArray<FWowWmoMapQuad> UWowWmoAreaComponent::MapQuads(const FWowWmoLocation& L,float Radius,int32 MaxDepth) const
{
    TArray<FWowWmoMapQuad> Out; if(!Groups.IsValidIndex(L.Group) || !L.Minimap) return Out;
    const auto& Current=Groups[L.Group]; const double R=Radius*2.5/FMath::Max(.001,GetOwner()->GetActorScale3D().GetMin());
    const FBox Query(L.LocalPosition-FVector(R,R,Radius*1.5),FVector(L.LocalPosition.X+R,L.LocalPosition.Y+R,Current.Bounds.Max.Z));
    const auto Add=[&](const FBox& B,int32 Index){for(auto& Q:GroupQuads(B,Index)) if(Q.Bounds.Max.X>=Query.Min.X && Q.Bounds.Min.X<=Query.Max.X && Q.Bounds.Max.Y>=Query.Min.Y && Q.Bounds.Min.Y<=Query.Max.Y && Out.Num()<256) Out.Add(MoveTemp(Q));};
    if(L.WholeMap) {Add(Bounds,Groups.Num()); return Out;}
    struct FVisit {int32 Index,Depth;}; TArray<FVisit> Pending={{L.Group,0}}; TSet<int32> Seen; const uint32 Mask=L.Indoors?(Current.Flags&0x40):8;
    while(Pending.Num() && Out.Num()<256)
    {
        const auto Visit=Pending.Pop(EAllowShrinking::No); if(!Groups.IsValidIndex(Visit.Index) || Seen.Contains(Visit.Index)) continue; Seen.Add(Visit.Index);
        const auto& G=Groups[Visit.Index]; if((G.Flags&(L.Indoors?0x48:8))!=Mask || G.Bounds.Max.X<Query.Min.X || G.Bounds.Min.X>Query.Max.X || G.Bounds.Max.Y<Query.Min.Y || G.Bounds.Min.Y>Query.Max.Y) continue;
        Add(G.Bounds,Visit.Index); if(G.Id==23015 || G.Id==23016 || Visit.Depth>=FMath::Clamp(MaxDepth,0,99)) continue;
        for(int32 I=G.PortalStart;I<int32(G.PortalStart)+G.PortalCount;++I)
        {
            if(!PortalRefs.IsValidIndex(I)) break; const auto& Ref=PortalRefs[I];
            if(!Groups.IsValidIndex(Ref.Group) || (Groups[Ref.Group].Flags&0x80) || !Portals.IsValidIndex(Ref.Portal)) continue;
            const auto& Portal=Portals[Ref.Portal]; if(!Portal.Count || int32(Portal.Start)+Portal.Count>PortalVertices.Num()) continue;
            uint32 Outside=63; for(int32 V=Portal.Start;V<int32(Portal.Start)+Portal.Count;++V) Outside&=Outcode(PortalVertices[V],Query);
            if(!Outside && Groups[Ref.Group].Id!=23015 && Groups[Ref.Group].Id!=23016) Pending.Add({Ref.Group,Visit.Depth+1});
        }
    }
    Out.Sort([&](const auto& A,const auto& B){const double AZ=A.Group==L.Group?MAX_dbl:A.Bounds.GetCenter().Z,BZ=B.Group==L.Group?MAX_dbl:B.Bounds.GetCenter().Z; return AZ==BZ?A.Group<B.Group:AZ<BZ;});
    return Out;
}
