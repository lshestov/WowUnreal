#include "WowMapUi.h"
#include "LuaApi/LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "Formats/Dbc/DbcStore.h"

namespace
{
bool Complete(FWowLuaContext* C,const FWowQuestLogEntry& Q)
{
    if(Q.State) return Q.State==1;
    const auto* T=C->ConnectionManager->PacketHandler.QuestTemplates.Find(Q.QuestId); if(!T || (T->Flags&6)) return false;
    for(uint32 Id:T->CreatureId) if(Id) return false; for(uint32 Id:T->ItemId) if(Id) return false;
    const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; return T->Money>=0 || (P && P->GetField(PlayerField::COINAGE)>=uint32(-int64(T->Money)));
}
bool Active(FWowLuaContext* C,const FWowQuestLogEntry& Q,int32 Objective)
{
    if(Q.State==2) return false;
    if(Complete(C,Q)) return Objective==-1;
    if(Objective<0) return false;
    const auto* T=C->ConnectionManager->PacketHandler.QuestTemplates.Find(Q.QuestId); if(!T) return true;
    const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    if(Objective<4 && T->CreatureId[Objective] && P)
        for(int32 I=0;I<25;++I) if(P->GetField(PlayerField::BASE+0x000A+I*5)==Q.QuestId)
            return ((P->GetField64(PlayerField::BASE+0x000A+I*5+2)>>(Objective*16))&0xffff)<T->CreatureCount[Objective];
    if(Objective>=4 && Objective<10 && T->ItemId[Objective-4])
    {
        uint32 Count=0; if(C->EntityManager) for(const auto* Item:C->EntityManager->GetInventoryItems()) if(Item->Entry==T->ItemId[Objective-4]) Count+=FMath::Max(Item->GetStackCount(),1);
        return Count<T->ItemCount[Objective-4];
    }
    return true;
}
bool OnMap(FWowLuaContext* C,const FWowPacketHandler::FQuestPoi& P)
{
    const auto S=WowMapUi::Selection(C); if(!S.Area || !S.Area->AreaId || P.Points.IsEmpty() || ((P.Flags&4) && P.AreaId!=S.Area->Id)) return false;
    if(S.Floor && (S.Area->MapId!=P.MapId || S.Floor->Index!=P.Floor)) return false;
    return true;
}
FVector2D ProjectPoi(FWowLuaContext* C,const FWowPacketHandler::FQuestPoi& P,const FIntPoint& Point)
{
    const auto S=WowMapUi::Selection(C); return WowMapUi::Project(C,P.MapId,FVector(Point.X,Point.Y,0),S.Floor?S.Floor->Id:0,true);
}
TArray<FVector2D> Hull(TArray<FVector2D> P)
{
    P.Sort([](const auto& A,const auto& B){return A.X==B.X?A.Y<B.Y:A.X<B.X;});
    for(int32 I=P.Num()-1;I>0;--I) if(P[I].Equals(P[I-1],1.e-7)) P.RemoveAt(I);
    if(P.Num()<3) return P;
    TArray<FVector2D> H;
    const auto Cross=[](const FVector2D& A,const FVector2D& B,const FVector2D& D){return (B.X-A.X)*(D.Y-A.Y)-(B.Y-A.Y)*(D.X-A.X);};
    for(const auto& V:P) {while(H.Num()>1 && Cross(H[H.Num()-2],H.Last(),V)<=0) H.Pop(EAllowShrinking::No); H.Add(V);}
    const int32 Lower=H.Num(); for(int32 I=P.Num()-2;I>=0;--I) {while(H.Num()>Lower && Cross(H[H.Num()-2],H.Last(),P[I])<=0) H.Pop(EAllowShrinking::No); H.Add(P[I]);}
    H.Pop(EAllowShrinking::No); return H;
}
}
TArray<WowMapUi::FQuestIcon> WowMapUi::QuestIcons(FWowLuaContext* C)
{
    TArray<FQuestIcon> Out; if(!C || !C->ConnectionManager) return Out; const auto& H=C->ConnectionManager->PacketHandler;
    const auto* Player=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    for(int32 I=0;I<H.QuestLog.Num();++I)
    {
        const auto& Q=H.QuestLog[I]; const auto* Pois=H.QuestPois.Find(Q.QuestId); if(!Pois) continue;
        FQuestIcon Best; double Distance=DBL_MAX;
        for(const auto& P:*Pois) if(OnMap(C,P) && Active(C,Q,P.Objective))
        {
            FVector Center=FVector::ZeroVector; for(const auto& V:P.Points) Center+=FVector(V.X,V.Y,0); Center/=P.Points.Num();
            const auto Point=ProjectPoi(C,P,{FMath::RoundToInt32(Center.X),FMath::RoundToInt32(Center.Y)});
            if(Point.X<0 || Point.Y<0 || Point.X>1 || Point.Y>1) continue;
            const double D=Player?FVector::DistSquared2D(Center,Player->Movement.Position):double(P.Id);
            if(D<Distance) {Distance=D; Best.Quest=Q.QuestId; Best.LogIndex=I+1; Best.Objective=P.Objective; Best.Complete=Complete(C,Q); Best.Point=Point;}
        }
        if(Best.Quest) Out.Add(Best);
    }
    Out.StableSort([](const auto& A,const auto& B){return A.Complete && !B.Complete;}); return Out;
}
uint32 WowMapUi::QuestDestination(FWowLuaContext* C,uint32 Quest,uint32& Floor)
{
    Floor=0; if(!C || !C->ConnectionManager) return 0; const auto& H=C->ConnectionManager->PacketHandler;
    const auto* Q=H.QuestLog.FindByPredicate([&](const auto& Row){return Row.QuestId==Quest;}); const auto* Pois=H.QuestPois.Find(Quest); if(!Q || !Pois) return 0;
    const auto* Player=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const auto* ActualFloor=FDbcStore::Get().MapUi().Floor(CurrentFloor(C));
    const auto* ActualArea=FDbcStore::Get().AreaTable().GetById(WowLuaApi::ResolveAreaId(C)); uint32 Zone=0;
    for(int32 Depth=0;ActualArea && Depth<8;++Depth) {for(const auto& A:FDbcStore::Get().MapUi().Areas()) if(A.MapId==H.CurrentMapId && A.AreaId==ActualArea->ID) {Zone=A.Id; break;} if(Zone) break; ActualArea=FDbcStore::Get().AreaTable().GetById(ActualArea->ParentAreaID);}
    uint32 Area=0; int32 Rank=MAX_int32; double Distance=DBL_MAX;
    for(const auto& P:*Pois) if(!P.Points.IsEmpty() && Active(C,*Q,P.Objective) && FDbcStore::Get().MapUi().Area(P.AreaId))
    {
        const int32 R=(P.MapId==H.CurrentMapId?0:4)+(P.Floor==(ActualFloor?ActualFloor->Index:0)?0:2)+(P.AreaId==Zone?0:1);
        FVector Center=FVector::ZeroVector; for(const auto& V:P.Points) Center+=FVector(V.X,V.Y,0); Center/=P.Points.Num(); const double D=Player?FVector::DistSquared2D(Center,Player->Movement.Position):P.Id;
        if(R<Rank || (R==Rank && D<Distance)) {Rank=R; Distance=D; Area=P.AreaId; Floor=P.Floor;}
    }
    return Area;
}
TArray<WowMapUi::FQuestShape> WowMapUi::QuestShapes(FWowLuaContext* C,const FWowMapBlob& Style)
{
    TArray<FQuestShape> Out; if(!C || !C->ConnectionManager) return Out; const auto& H=C->ConnectionManager->PacketHandler;
    for(const auto& Q:H.QuestLog) if(Style.Quests.Contains(Q.QuestId)) if(const auto* Pois=H.QuestPois.Find(Q.QuestId))
        for(const auto& P:*Pois) if(P.Points.Num()>1 && OnMap(C,P) && Active(C,Q,P.Objective))
        {
            FQuestShape Shape; Shape.Quest=Q.QuestId; Shape.Objectives.Add(P.Objective);
            for(const auto& V:P.Points) {const auto Point=ProjectPoi(C,P,V); if(Point.X>=-.1 && Point.Y>=-.1 && Point.X<=1.1 && Point.Y<=1.1) Shape.Points.Add(Point);}
            Shape.Points=Hull(MoveTemp(Shape.Points)); if(Shape.Points.Num()>2) Out.Add(MoveTemp(Shape));
        }
    if(Style.Merge) for(int32 I=0;I<Out.Num();++I) for(int32 J=Out.Num()-1;J>I;--J) if(Out[I].Quest==Out[J].Quest)
    {
        bool Merge=false; FBox2D A(Out[I].Points),B(Out[J].Points); const double Threshold=FMath::Min(A.GetSize().Length(),B.GetSize().Length())*Style.MergeThreshold;
        for(const auto& P:Out[I].Points) if(Contains(Out[J].Points,P) || B.ComputeSquaredDistanceToPoint(P)<Threshold*Threshold) {Merge=true; break;}
        if(Merge) {Out[I].Points.Append(Out[J].Points); Out[I].Points=Hull(MoveTemp(Out[I].Points)); for(int32 O:Out[J].Objectives) Out[I].Objectives.AddUnique(O); Out.RemoveAt(J);}
    }
    if(Style.Smooth) for(auto& S:Out)
    {
        const auto P=S.Points; S.Points.Reset(); const int32 Steps=FMath::Clamp(Style.SplinePoints*2/P.Num(),2,8);
        for(int32 I=0;I<P.Num();++I) for(int32 J=0;J<Steps;++J)
        {
            const double T=double(J)/Steps; const auto& A=P[(I+P.Num()-1)%P.Num()]; const auto& B=P[I]; const auto& D=P[(I+1)%P.Num()]; const auto& E=P[(I+2)%P.Num()];
            S.Points.Add(.5*((2*B)+(-A+D)*T+(2*A-5*B+4*D-E)*T*T+(-A+3*B-3*D+E)*T*T*T));
        }
    }
    return Out;
}
bool WowMapUi::Contains(const TArray<FVector2D>& Points,const FVector2D& P)
{
    bool In=false; for(int32 I=0,J=Points.Num()-1;I<Points.Num();J=I++) if((Points[I].Y>P.Y)!=(Points[J].Y>P.Y) && P.X<(Points[J].X-Points[I].X)*(P.Y-Points[I].Y)/(Points[J].Y-Points[I].Y)+Points[I].X) In=!In; return In;
}
