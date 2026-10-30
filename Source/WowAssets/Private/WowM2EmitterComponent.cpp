#include "WowM2EmitterComponent.h"
#include "Formats/M2Types.h"
#include "Formats/BlpParser.h"
#include "Mpq/MpqManager.h"
#include "WowAssetCache.h"
#include "WowTextureFactory.h"
#include "Coord/WowCoordinate.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"

namespace
{
FVector M2Vector(const FVector& V) {return FVector(V.Y,V.X,V.Z)*FWowCoordinate::SCALE;}
UTexture2D* ParticleTexture(const FString& Path,FMpqManager* Mpq,FWowAssetCache* Cache)
{
    const FString Key=TEXT("particle:")+Path.ToLower().Replace(TEXT("/"),TEXT("\\"));
    if(auto* Existing=Cache->FindTexture(Key)) return Existing;
    TArray<uint8> Raw; if(!Mpq->ReadFile(Path,Raw)) return nullptr;
    const auto Blp=FBlpParser::Parse(Raw); if(!Blp.bIsValid) return nullptr;
    auto* Texture=FWowTextureFactory::CreateTexture(Blp,MakeUniqueObjectName(GetTransientPackage(),UTexture2D::StaticClass(),TEXT("M2ParticleTexture")).ToString());
    if(Texture) Cache->CacheTexture(Key,Texture); return Texture;
}
}

UWowM2EmitterComponent::UWowM2EmitterComponent()
{
    PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
}
bool UWowM2EmitterComponent::Initialize(TSharedPtr<const FM2Data> InModel,FMpqManager* Mpq,FWowAssetCache* Cache,float EmitSeconds)
{
    if(!InModel || !Mpq || !Cache || !GetOwner() || !GetWorld()) return false;
    Model=MoveTemp(InModel); Random.Initialize(GetUniqueID());
    for(int32 I=0;I<FMath::Min(Model->ParticleEmitters.Num(),64);++I)
    {
        const auto& E=Model->ParticleEmitters[I];
        if((E.EmitterType!=1 && E.EmitterType!=2) || !(E.Flags&0x20000) || E.BlendMode<2 || E.BlendMode>4 || !Model->TexturePaths.IsValidIndex(E.Texture)) continue;
        const auto& Path=Model->TexturePaths[E.Texture]; if(Path.IsEmpty()) continue;
        auto* Texture=ParticleTexture(Path,Mpq,Cache); if(!Texture) continue;
        const TCHAR* MaterialPath=E.BlendMode==2?TEXT("/Game/Materials/M_WowParticleAlpha"):TEXT("/Game/Materials/M_WowParticleAdd");
        auto* Parent=LoadObject<UMaterial>(nullptr,MaterialPath); if(!Parent) continue;
        auto* Material=UMaterialInstanceDynamic::Create(Parent,this); Material->SetTextureParameterValue(TEXT("ParticleTexture"),Texture); Material->SetScalarParameterValue(TEXT("IgnoreAlpha"),E.BlendMode==3?1:0);
        ActiveEmitters.Add(I); Textures.Add(Texture); Materials.Add(Material);
    }
    if(ActiveEmitters.IsEmpty()) return false;
    PerEmitterCapacity=FMath::Min(128,512/ActiveEmitters.Num()); Accumulators.Init(0,ActiveEmitters.Num()); Particles.Reserve(512);
    Mesh=NewObject<UProceduralMeshComponent>(GetOwner()); GetOwner()->AddInstanceComponent(Mesh); Mesh->SetupAttachment(this);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCastShadow(false); Mesh->SetCanEverAffectNavigation(false); Mesh->RegisterComponent();
    Mesh->SetVisibleInRayTracing(false); Mesh->bAffectDistanceFieldLighting=false; Mesh->bAffectDynamicIndirectLighting=false;
    if(GetAttachParent()) AddTickPrerequisiteComponent(GetAttachParent());
    StartedAt=LastAt=FPlatformTime::Seconds(); EmitUntil=StartedAt+FMath::Clamp(EmitSeconds>0?EmitSeconds:60.f,.01f,60.f);
    Emitting=true; Render(); return true;
}
void UWowM2EmitterComponent::StopEmitting() {Emitting=false;}
void UWowM2EmitterComponent::Emit(int32 Index,float TimeMs)
{
    const auto& E=Model->ParticleEmitters[ActiveEmitters[Index]]; const auto& Global=Model->GlobalSequenceDurations;
    const float Width=E.WidthTrack.Sample(0,TimeMs,Global),Length=E.LengthTrack.Sample(0,TimeMs,Global);
    FVector Position(Random.FRandRange(-.5f,.5f)*Width,Random.FRandRange(-.5f,.5f)*Length,0);
    const float Speed=E.SpeedTrack.Sample(0,TimeMs,Global)*(1+Random.FRandRange(-1.f,1.f)*E.VariationTrack.Sample(0,TimeMs,Global));
    const float Lat=Random.FRandRange(-1.f,1.f)*E.LatitudeTrack.Sample(0,TimeMs,Global),Lon=Random.FRandRange(-1.f,1.f)*E.LongitudeTrack.Sample(0,TimeMs,Global);
    const float Z=E.ZSourceTrack.Sample(0,TimeMs,Global);
    FVector Direction(FMath::Cos(Lon)*FMath::Sin(Lat),FMath::Sin(Lon)*FMath::Sin(Lat),FMath::Cos(Lat));
    if(E.EmitterType==2)
    {
        Direction=FVector(FMath::Cos(Lon)*FMath::Cos(Lat),FMath::Sin(Lon)*FMath::Cos(Lat),FMath::Sin(Lat));
        Position=Direction*FMath::Lerp(Width,Length,Random.FRand());
        if(E.Flags&0x100) Direction=FVector::UpVector;
    }
    if(Z!=0) Direction=FVector(Position.X,Position.Y,Position.Z-Z).GetSafeNormal();
    const FVector Velocity=Direction*Speed;
    FParticle P; P.Emitter=Index; P.Local=(E.Flags&0x10)!=0;
    P.Position=M2Vector(E.Position+Position); P.Velocity=M2Vector(Velocity);
    if(!P.Local) {P.Position=GetComponentTransform().TransformPosition(P.Position); P.Velocity=GetComponentTransform().TransformVector(P.Velocity);}
    P.Life=FMath::Clamp(E.LifeTrack.Sample(0,TimeMs,Global,1)+Random.FRandRange(-1.f,1.f)*E.LifeVariation,.001f,10.f);
    P.ScaleSeed=FVector2f(Random.FRandRange(-1.f,1.f),Random.FRandRange(-1.f,1.f));
    P.Spin=E.InitialSpin+Random.FRandRange(-1.f,1.f)*E.InitialSpinVariation; P.SpinRate=E.Spin+Random.FRandRange(-1.f,1.f)*E.SpinVariation;
    const int32 Cells=FMath::Clamp(int32(E.Rows),1,256)*FMath::Clamp(int32(E.Cols),1,256); if(E.Flags&0x10000) P.Cell=Random.RandRange(0,Cells-1);
    Particles.Add(P);
}
void UWowM2EmitterComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,Type,Function); if(!Model || !Mesh) return;
    const double Now=FPlatformTime::Seconds(); const float Elapsed=FMath::Max(0.f,float(Now-LastAt)),Step=FMath::Min(Elapsed,.1f),TimeMs=float((Now-StartedAt)*1000); LastAt=Now;
    TArray<int32> Counts; Counts.Init(0,ActiveEmitters.Num());
    for(int32 I=Particles.Num()-1;I>=0;--I)
    {
        auto& P=Particles[I]; P.Age+=Elapsed;
        if(P.Age>=P.Life) {Particles.RemoveAtSwap(I,1,EAllowShrinking::No); continue;}
        const auto& E=Model->ParticleEmitters[ActiveEmitters[P.Emitter]];
        const float Gravity=E.GravityTrack.Sample(0,TimeMs,Model->GlobalSequenceDurations);
        P.Velocity.Z-=Gravity*FWowCoordinate::SCALE*Step; P.Velocity*=FMath::Exp(-FMath::Max(0.f,E.Drag)*Step); P.Position+=P.Velocity*Step; ++Counts[P.Emitter];
    }
    if(Emitting && Now<EmitUntil) for(int32 I=0;I<ActiveEmitters.Num();++I)
    {
        const auto& E=Model->ParticleEmitters[ActiveEmitters[I]];
        if(E.VisibilityTrack.Sample(0,TimeMs,Model->GlobalSequenceDurations,1)<=0) continue;
        Accumulators[I]+=FMath::Clamp(E.RateTrack.Sample(0,TimeMs,Model->GlobalSequenceDurations),0.f,1000.f)*Step;
        const int32 Count=FMath::Min3(FMath::FloorToInt32(Accumulators[I]),32,PerEmitterCapacity-Counts[I]); Accumulators[I]=FMath::Fmod(Accumulators[I],1.);
        for(int32 N=0;N<Count;++N) Emit(I,TimeMs);
    }
    else Emitting=false;
    Render(); if(!Emitting && Particles.IsEmpty()) DestroyComponent();
}
void UWowM2EmitterComponent::Render()
{
    if(!Mesh || !Model) return;
    const double Began=FPlatformTime::Seconds();
    FVector Camera=FVector::ZeroVector; FRotator View=FRotator::ZeroRotator; if(auto* PC=GetWorld()->GetFirstPlayerController()) PC->GetPlayerViewPoint(Camera,View);
    const FVector Right=FRotationMatrix(View).GetUnitAxis(EAxis::Y),Up=FRotationMatrix(View).GetUnitAxis(EAxis::Z);
    const FTransform Transform=GetComponentTransform(); const float SizeScale=Transform.GetScale3D().GetAbsMax()*FWowCoordinate::SCALE;
    for(int32 S=0;S<ActiveEmitters.Num();++S)
    {
        const auto& E=Model->ParticleEmitters[ActiveEmitters[S]]; const int32 Rows=FMath::Clamp(int32(E.Rows),1,256),Cols=FMath::Clamp(int32(E.Cols),1,256);
        TArray<FVector> Vertices,Normals; TArray<FVector2D> UV; TArray<FLinearColor> Colors; TArray<FProcMeshTangent> Tangents;
        Vertices.SetNumZeroed(PerEmitterCapacity*4); Normals.Init(FVector::UpVector,Vertices.Num()); UV.SetNumZeroed(Vertices.Num()); Colors.Init(FLinearColor::Transparent,Vertices.Num());
        int32 Slot=0;
        for(const auto& P:Particles) if(P.Emitter==S && Slot<PerEmitterCapacity)
        {
            const float Age=FMath::Clamp(P.Age/P.Life,0.f,1.f); const FVector3f RGB=E.ColorCurve.Sample(Age,FVector3f(1)); const float Alpha=E.AlphaCurve.Sample(Age,1.f);
            const auto Scale=E.ScaleCurve.Sample(Age,FVector2f(1)); const FVector2f Size(Scale.X*(1+P.ScaleSeed.X*E.ScaleVariance.X),Scale.Y*(1+P.ScaleSeed.Y*E.ScaleVariance.Y));
            const int32 Cell=(P.Cell+FMath::Max(0,FMath::FloorToInt(E.HeadCellCurve.Sample(Age,0.f))))%(Rows*Cols);
            const FVector Center=P.Local?Transform.TransformPosition(P.Position):P.Position;
            const float Angle=P.Spin+P.Age*P.SpinRate,Cos=FMath::Cos(Angle),Sin=FMath::Sin(Angle);
            const FVector X=Right*Cos+Up*Sin,Y=Up*Cos-Right*Sin;
            const float Corners[4][2]={{-1,1},{-1,-1},{1,1},{1,-1}};
            for(int32 V=0;V<4;++V)
            {
                const int32 Index=Slot*4+V; Vertices[Index]=Transform.InverseTransformPosition(Center+X*(Corners[V][0]*Size.X*SizeScale)+Y*(Corners[V][1]*Size.Y*SizeScale));
                UV[Index]=FVector2D((Cell%Cols+(V>=2?1:0))/float(Cols),(Cell/Cols+(V%2?1:0))/float(Rows)); Colors[Index]=FLinearColor(RGB.X,RGB.Y,RGB.Z,Alpha);
            }
            ++Slot;
        }
        if(Mesh->GetProcMeshSection(S)) Mesh->UpdateMeshSection_LinearColor(S,Vertices,Normals,UV,Colors,Tangents,false);
        else
        {
            TArray<int32> Indices; for(int32 I=0;I<PerEmitterCapacity;++I) {const int32 V=I*4; Indices.Append({V,V+1,V+2,V+2,V+1,V+3});}
            Mesh->CreateMeshSection_LinearColor(S,Vertices,Indices,Normals,UV,Colors,Tangents,false,false); Mesh->SetMaterial(S,Materials[S]);
        }
    }
    RenderMilliseconds=(FPlatformTime::Seconds()-Began)*1000;
}
void UWowM2EmitterComponent::OnComponentDestroyed(bool Hierarchy)
{
    if(IsValid(Mesh)) Mesh->DestroyComponent(); Mesh=nullptr; Super::OnComponentDestroyed(Hierarchy);
}
