#include "WowM2LightSubsystem.h"
#include "WowM2Lighting.h"
#include "WowDoodadManager.h"
#include "Coord/WowCoordinate.h"
#include "WowWorldCollision.h"
#include "Formats/M2Parser.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Mpq/MpqManager.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogM2Lights,Log,All);
namespace {constexpr int32 LightBudget=32; constexpr float DrawDistance=15000;}
static FIntPoint Cell(const FVector& P) {return FIntPoint(FMath::FloorToInt(P.X/DrawDistance),FMath::FloorToInt(P.Y/DrawDistance));}
static TAutoConsoleVariable<int32> CVarM2Lights(TEXT("wow.M2Lights.Enabled"),1,TEXT("Render authored M2 point lights"));

uint32 UWowM2LightSubsystem::RegisterModel(USceneComponent* Anchor,TSharedPtr<const FM2Data> Model,const FTransform& Local)
{
    if(!IsValid(Anchor) || !Model || Model->Lights.IsEmpty()) return 0;
    const uint32 Id=NextId++; FSource S; S.Anchor=Anchor; S.Model=MoveTemp(Model); S.Local=Local; S.Started=GetWorld()->GetTimeSeconds();
    Sources.Add(Id,MoveTemp(S)); SelectionTimer=0; return Id;
}
void UWowM2LightSubsystem::QueueWmoModel(USceneComponent* Anchor,const FString& Path,const FTransform& Local,FMpqManager* Mpq,FWowAssetCache* Cache)
{
    const FString Key=Path.ToLower().Replace(TEXT("/"),TEXT("\\"));
    if(!Anchor || !Mpq || !Cache || Key.IsEmpty() || UnlitPaths.Contains(Key)) return;
    FPending P; P.Anchor=Anchor; P.Path=Key; P.Local=Local; P.Mpq=Mpq; P.Cache=Cache;
    P.Position=Anchor->GetComponentTransform().TransformPosition(Local.GetLocation());
    Pending.FindOrAdd(Cell(P.Position)).Add(MoveTemp(P)); ++PendingCount;
}
void UWowM2LightSubsystem::RemoveModel(uint32 Id)
{
    Sources.Remove(Id);
    for(auto It=Active.CreateIterator();It;++It) if(It.Value().Source==Id)
        {if(It.Value().Light.IsValid()) It.Value().Light->DestroyComponent(); It.RemoveCurrent();}
}
void UWowM2LightSubsystem::ProcessPending(const FVector& Focus)
{
    int32 Best=INDEX_NONE; FIntPoint BestCell; double Distance=FMath::Square(double(DrawDistance));
    const FIntPoint Center=Cell(Focus);
    for(int32 X=-1;X<=1;++X) for(int32 Y=-1;Y<=1;++Y)
    {
        const FIntPoint Key=Center+FIntPoint(X,Y); auto* Bucket=Pending.Find(Key); if(!Bucket) continue;
        PendingCount-=Bucket->RemoveAllSwap([&](const auto& P){return !P.Anchor.IsValid() || UnlitPaths.Contains(P.Path);});
        for(int32 I=0;I<Bucket->Num();++I)
        {
            const double D=FVector::DistSquared(Focus,(*Bucket)[I].Position);
            if(D<Distance) {Distance=D; Best=I; BestCell=Key;}
        }
    }
    if(Best<0) return;
    // One nearby model read/build per frame, never an entire building's doodads.
    const double Before=FPlatformTime::Seconds(); auto& Bucket=Pending.FindChecked(BestCell); const auto P=Bucket[Best]; Bucket.RemoveAtSwap(Best); --PendingCount;
    TArray<uint8> Raw;
    if(!P.Mpq->ReadFile(P.Path,Raw) || !FM2Parser::HasLightTable(Raw)) UnlitPaths.Add(P.Path);
    else if(auto Model=FWowDoodadManager::GetOrParseM2(P.Path,P.Mpq); Model && !Model->Lights.IsEmpty())
    {
        USceneComponent* Anchor=P.Anchor.Get(); FTransform Local=P.Local;
        if(auto* Mesh=FWowDoodadManager::GetOrCreateStaticMesh(P.Path,P.Mpq,P.Cache))
        {
            auto* Visual=NewObject<UStaticMeshComponent>(Anchor->GetOwner()); Visual->SetupAttachment(Anchor); Visual->SetRelativeTransform(Local);
            Visual->SetMobility(EComponentMobility::Movable); Visual->SetStaticMesh(Mesh); Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Visual->SetCastShadow(true); Visual->RegisterComponent(); Anchor=Visual; Local=FTransform::Identity;
            TArray<FVector> Vertices; TArray<int32> Indices;
            FWowWorldCollision::AppendM2(*Model,FTransform::Identity,Vertices,Indices); FWowWorldCollision::Create(Visual,TEXT("LitDoodadCollision"),Vertices,Indices);
        }
        RegisterModel(Anchor,Model,Local);
        UE_LOG(LogM2Lights,Display,TEXT("AuthoredM2Light source=%s lights=%d at=%s"),*P.Path,Model->Lights.Num(),*(Local*Anchor->GetComponentTransform()).GetLocation().ToString());
    }
    MaxLoadMilliseconds=FMath::Max(MaxLoadMilliseconds,(FPlatformTime::Seconds()-Before)*1000);
}
void UWowM2LightSubsystem::Tick(float DeltaTime)
{
    const double Before=FPlatformTime::Seconds();
    PruneTimer-=DeltaTime;
    if(PruneTimer<=0)
    {
        PruneTimer=1;
        for(auto It=Pending.CreateIterator();It;++It)
        {
            PendingCount-=It.Value().RemoveAllSwap([&](const auto& P){return !P.Anchor.IsValid() || UnlitPaths.Contains(P.Path);});
            if(It.Value().IsEmpty()) It.RemoveCurrent();
        }
    }
    TArray<uint32> Gone; for(const auto& Pair:Sources) if(!Pair.Value.Anchor.IsValid() || !Pair.Value.Anchor->IsRegistered()) Gone.Add(Pair.Key);
    for(uint32 Id:Gone) RemoveModel(Id);
    auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0); if(!PC) return;
    FVector Focus; FRotator Rotation; PC->GetPlayerViewPoint(Focus,Rotation);
    ProcessPending(Focus);
    const float Now=GetWorld()->GetTimeSeconds(); SelectionTimer-=DeltaTime;
    if(SelectionTimer<=0)
    {
        SelectionTimer=.1f;
        struct FCandidate {uint64 Key; uint32 Source; int32 Index; double Distance;}; TArray<FCandidate> Candidates;
        for(const auto& Pair:Sources)
        {
            if(!CVarM2Lights.GetValueOnGameThread()) break;
            const auto& S=Pair.Value; const auto Transform=S.Local*S.Anchor->GetComponentTransform();
            for(int32 I=0;I<S.Model->Lights.Num();++I)
            {
                if(S.Model->Lights[I].Type!=1) continue;
                const FVector Position=Transform.TransformPosition(FWowCoordinate::ModelToUE(S.Model->Lights[I].Position));
                const double D=FVector::DistSquared(Focus,Position);
                if(D<FMath::Square(double(DrawDistance))) Candidates.Add({(uint64(Pair.Key)<<32)|uint32(I),Pair.Key,I,D});
            }
        }
        Candidates.Sort([](const auto& A,const auto& B){return A.Distance==B.Distance?A.Key<B.Key:A.Distance<B.Distance;});
        if(Candidates.Num()>LightBudget) Candidates.SetNum(LightBudget);
        TSet<uint64> Selected;
        for(const auto& C:Candidates)
        {
            Selected.Add(C.Key); if(Active.Contains(C.Key)) continue;
            const auto& S=Sources.FindChecked(C.Source);
            auto* Light=NewObject<UPointLightComponent>(S.Anchor->GetOwner()); S.Anchor->GetOwner()->AddInstanceComponent(Light);
            Light->SetMobility(EComponentMobility::Movable); Light->SetupAttachment(S.Anchor.Get()); Light->SetUsingAbsoluteScale(true);
            // UE encodes exponent zero as inverse-square in LightData.ush even
            // when bUseInverseSquaredFalloff is false. Keep a nearly flat,
            // nonzero native curve; the material supplies authored attenuation.
            Light->bUseInverseSquaredFalloff=false; Light->LightFalloffExponent=.0001f;
            Light->SetIntensityUnits(ELightUnits::Unitless);
            Light->SetCastShadows(false); Light->SetLightingChannels(true,true,false); Light->MaxDrawDistance=DrawDistance; Light->MaxDistanceFadeRange=2000;
            Light->ComponentTags.Add(TEXT("WowAuthoredM2Light"));
            auto* Base=LoadObject<UMaterial>(nullptr,TEXT("/Game/Materials/M_WowM2LightFunction"));
            if(!ensureMsgf(Base,TEXT("Missing authored M2 falloff material"))) {Light->DestroyComponent(); continue;}
            Light->SetLightFunctionMaterial(UMaterialInstanceDynamic::Create(Base,Light));
            Light->SetLightFunctionFadeDistance(20000); Light->SetLightFunctionDisabledBrightness(0);
            Light->RegisterComponent(); FActive A; A.Light=Light; A.Source=C.Source; A.Index=C.Index; Active.Add(C.Key,A);
        }
        for(auto It=Active.CreateIterator();It;++It) if(!Selected.Contains(It.Key()))
            {if(It.Value().Light.IsValid()) It.Value().Light->DestroyComponent(); It.RemoveCurrent();}
    }
    for(auto& Pair:Active)
    {
        auto& A=Pair.Value; const auto* S=Sources.Find(A.Source); auto* Light=A.Light.Get(); if(!S || !Light) continue;
        const auto Sample=FWowM2Lighting::Sample(*S->Model,A.Index,(Now-S->Started)*1000,Now*1000);
        const FTransform Transform=S->Local*S->Anchor->GetComponentTransform(); const float Scale=Transform.GetScale3D().GetAbsMax();
        const FVector Position=Transform.TransformPosition(Sample.Position);
        const float Bright=FMath::Max3(Sample.Diffuse.R,Sample.Diffuse.G,Sample.Diffuse.B);
        Light->SetWorldLocation(Position); Light->SetAttenuationRadius(Sample.Radius*FWowCoordinate::SCALE*Scale);
        Light->SetLightColor(Bright>0?Sample.Diffuse/Bright:FLinearColor::Black); Light->SetIntensity(PI*Bright);
        Light->SetVisibility(Sample.Visible && S->Anchor->IsVisible());
        if(auto* Material=Cast<UMaterialInstanceDynamic>(Light->LightFunctionMaterial))
        {
            Material->SetVectorParameterValue(TEXT("SourcePosition"),FLinearColor(Position.X,Position.Y,Position.Z));
            Material->SetScalarParameterValue(TEXT("WorldScale"),FWowCoordinate::SCALE*Scale);
            Material->SetScalarParameterValue(TEXT("Start"),Sample.Start); Material->SetScalarParameterValue(TEXT("End"),Sample.End);
            Material->SetScalarParameterValue(TEXT("Radius"),Sample.Radius);
            Material->SetScalarParameterValue(TEXT("AuthoredRange"),Sample.End>Sample.Start && Sample.End>0?1:0);
        }
    }
    TickMilliseconds=(FPlatformTime::Seconds()-Before)*1000;
}
void UWowM2LightSubsystem::Deinitialize()
{
    for(auto& Pair:Active) if(Pair.Value.Light.IsValid()) Pair.Value.Light->DestroyComponent();
    Active.Reset(); Sources.Reset(); Pending.Reset(); PendingCount=0; UnlitPaths.Reset(); Super::Deinitialize();
}
