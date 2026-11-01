#include "WowWmoLighting.h"

FLinearColor FWowWmoLighting::UnitAmbient(const FLinearColor& Floor)
{
    const float Bright=FMath::Max3(Floor.R,Floor.G,Floor.B);
    if(Bright<=SMALL_NUMBER) return FLinearColor::Black;
    FLinearColor Result=Floor*FMath::Min(1.f,(96.f/255.f)/Bright);
    Result.A=1; return Result;
}
FLinearColor FWowWmoLighting::UnitDiffuse(const FLinearColor& Floor)
{
    const float Bright=FMath::Max3(Floor.R,Floor.G,Floor.B);
    if(Bright<=SMALL_NUMBER) return FLinearColor::Black;
    FLinearColor Result=Floor*FMath::Max(1.f,(168.f/255.f)/Bright);
    Result.A=1; return Result;
}

float FWowWmoLighting::PortalWeight(const FWmoRootData& Root,const FWmoGroupData& Group,const FVector& Position)
{
    float Weight=0;
    for(int32 I=Group.PortalStart; I<int32(Group.PortalStart)+Group.PortalCount; ++I)
    {
        if(!Root.PortalRefs.IsValidIndex(I)) break;
        const auto& Ref=Root.PortalRefs[I]; if(!Root.Portals.IsValidIndex(Ref.Portal) || !Root.GroupFlags.IsValidIndex(Ref.Group)) continue;
        const auto& Portal=Root.Portals[Ref.Portal];
        if(Portal.Count<3 || int32(Portal.Start)+Portal.Count>Root.PortalVertices.Num() || Portal.Normal.IsNearlyZero() || Portal.Normal.ContainsNaN() || !FMath::IsFinite(Portal.Distance)) continue;
        const FVector N=Portal.Normal.GetSafeNormal();
        const double Signed=FVector::DotProduct(N,Position)+Portal.Distance/Portal.Normal.Size();
        const FVector Projected=Position-N*Signed;
        bool Inside=true; double Winding=0,EdgeDistance=MAX_dbl;
        for(int32 J=0; J<Portal.Count; ++J)
        {
            const FVector A=Root.PortalVertices[Portal.Start+J],B=Root.PortalVertices[Portal.Start+(J+1)%Portal.Count];
            const double Side=FVector::DotProduct(FVector::CrossProduct(B-A,Projected-A),N);
            if(FMath::Abs(Side)>1.e-6) {if(Winding && Winding*Side<0) Inside=false; Winding=Side;}
            EdgeDistance=FMath::Min(EdgeDistance,FMath::PointDistToSegment(Position,A,B));
        }
        const double Distance=Inside?Signed*(Ref.Side==1?1:-1):EdgeDistance;
        if(Root.GroupFlags[Ref.Group]&0x48) Weight+=FMath::Max(0.,1.-FMath::Max(0.,Distance)*.15);
        else if(Distance>-1 && Distance<1) return 0;
    }
    return FMath::Clamp(Weight,0.f,1.f);
}

TArray<FLinearColor> FWowWmoLighting::Prepare(const FWmoRootData& Root,const FWmoGroupData& Group)
{
    TArray<FLinearColor> Out;
    if(Group.VertexColors.IsEmpty() && !(Group.Flags&0x48))
    {
        Out.Init(FLinearColor(Root.AmbientColor.R/255.f,Root.AmbientColor.G/255.f,Root.AmbientColor.B/255.f,0),Group.Vertices.Num());
        return Out;
    }
    if(Group.VertexColors.Num()!=Group.Vertices.Num()) return Out;
    const int32 Trans=FMath::Min(int32(Group.TransitionBatchCount),Group.Batches.Num());
    const int32 FirstExterior=Trans?int32(Group.Batches[Trans-1].VertexEnd)+1:0;
    Out.Reserve(Group.Vertices.Num());
    for(int32 I=0;I<Group.Vertices.Num();++I)
    {
        FColor C=Group.VertexColors[I];
        // CMapObjGroup::FixColorVertexAlpha (skipped by MOHD flag 8).
        if(!(Root.Flags&8))
        {
            const auto Fix=[&](uint8 V){return uint8(I<FirstExterior?V>>1:FMath::Min(255u,(uint32(V)+((uint32(C.A)*V)>>6))>>1));};
            C.R=Fix(C.R); C.G=Fix(C.G); C.B=Fix(C.B); if(I>=FirstExterior) C.A=255;
        }
        const bool Transition=I<FirstExterior;
        if(Transition && !(Root.Flags&1))
        {
            const float Alpha=PortalWeight(Root,Group,Group.Vertices[I]);
            C.R=uint8(FMath::Lerp(float(C.R),127.f,Alpha)); C.G=uint8(FMath::Lerp(float(C.G),127.f,Alpha)); C.B=uint8(FMath::Lerp(float(C.B),127.f,Alpha)); C.A=uint8(Alpha*255);
        }
        const auto Channel=[&](uint8 V,uint8 Ambient){return FMath::Min(255,2*int32(V)+((Root.Flags&2)?int32(Ambient):0))/255.f;};
        Out.Emplace(Channel(C.R,Root.AmbientColor.R),Channel(C.G,Root.AmbientColor.G),Channel(C.B,Root.AmbientColor.B),
            Transition?C.A/255.f:((Group.Flags&0x48)?1.f:0.f));
    }
    return Out;
}
