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
        if((E.EmitterType!=1 && E.EmitterType!=2) || !(E.Flags&0x60000) || E.BlendMode<2 || E.BlendMode>4 || !Model->TexturePaths.IsValidIndex(E.Texture)) continue;
        const auto& Path=Model->TexturePaths[E.Texture]; if(Path.IsEmpty()) continue;
        auto* Texture=ParticleTexture(Path,Mpq,Cache); if(!Texture) continue;
        const TCHAR* MaterialPath=E.BlendMode==2?TEXT("/Game/Materials/M_WowParticleAlpha"):TEXT("/Game/Materials/M_WowParticleAdd");
        auto* Parent=LoadObject<UMaterial>(nullptr,MaterialPath); if(!Parent) continue;
        auto* Material=UMaterialInstanceDynamic::Create(Parent,this); Material->SetTextureParameterValue(TEXT("ParticleTexture"),Texture); Material->SetScalarParameterValue(TEXT("IgnoreAlpha"),E.BlendMode==3?1:0);
        ActiveEmitters.Add(I); Textures.Add(Texture); Materials.Add(Material);
    }
    for(int32 I=0;I<Model->RenderPasses.Num();++I)
    {
        const auto& Pass=Model->RenderPasses[I]; if(!Model->TexturePaths.IsValidIndex(Pass.TextureIndex) || !Model->Submeshes.IsValidIndex(Pass.SubmeshIndex)) continue;
        auto* Texture=ParticleTexture(Model->TexturePaths[Pass.TextureIndex],Mpq,Cache); if(!Texture) continue;
        auto* Parent=LoadObject<UMaterial>(nullptr,Pass.BlendMode==3 || Pass.BlendMode==4?TEXT("/Game/Materials/M_WowParticleAdd"):TEXT("/Game/Materials/M_WowParticleAlpha")); if(!Parent) continue;
        auto* Material=UMaterialInstanceDynamic::Create(Parent,this); Material->SetTextureParameterValue(TEXT("ParticleTexture"),Texture); Material->SetScalarParameterValue(TEXT("IgnoreAlpha"),Pass.BlendMode==0 || Pass.BlendMode==3?1:0);
        GeometryPasses.Add(I); Textures.Add(Texture); Materials.Add(Material);
    }
    for(int32 I=0;I<Model->Ribbons.Num();++I)
    {
        const auto& Ribbon=Model->Ribbons[I]; if(!Model->TexturePaths.IsValidIndex(Ribbon.Texture)) continue;
        auto* Texture=ParticleTexture(Model->TexturePaths[Ribbon.Texture],Mpq,Cache); if(!Texture) continue;
        auto* Parent=LoadObject<UMaterial>(nullptr,Ribbon.BlendMode==3 || Ribbon.BlendMode==4?TEXT("/Game/Materials/M_WowParticleAdd"):TEXT("/Game/Materials/M_WowParticleAlpha")); if(!Parent) continue;
        auto* Material=UMaterialInstanceDynamic::Create(Parent,this); Material->SetTextureParameterValue(TEXT("ParticleTexture"),Texture); Material->SetScalarParameterValue(TEXT("IgnoreAlpha"),Ribbon.BlendMode==3?1:0);
        ActiveRibbons.Add(I); Textures.Add(Texture); Materials.Add(Material);
    }
    RibbonNodes.SetNum(ActiveRibbons.Num()); RibbonLastSpawn.Init(-1,ActiveRibbons.Num());
    if(ActiveEmitters.IsEmpty() && GeometryPasses.IsEmpty() && ActiveRibbons.IsEmpty()) return false;
    PerEmitterCapacity=FMath::Min(128,512/FMath::Max(1,ActiveEmitters.Num())); Accumulators.Init(0,ActiveEmitters.Num()); Particles.Reserve(512);
    Mesh=NewObject<UProceduralMeshComponent>(GetOwner()); GetOwner()->AddInstanceComponent(Mesh); Mesh->SetupAttachment(this);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCastShadow(false); Mesh->SetCanEverAffectNavigation(false); Mesh->RegisterComponent();
    Mesh->SetVisibleInRayTracing(false); Mesh->bAffectDistanceFieldLighting=false; Mesh->bAffectDynamicIndirectLighting=false;
    if(GetAttachParent()) AddTickPrerequisiteComponent(GetAttachParent());
    StartedAt=LastAt=FPlatformTime::Seconds(); EmitUntil=StartedAt+FMath::Clamp(EmitSeconds>0?EmitSeconds:60.f,.01f,60.f);
    LoopAnimation=EmitSeconds>OneShotSeconds(*Model)+.1f; Emitting=true; AnimateBones(0); Render(); return true;
}
float UWowM2EmitterComponent::OneShotSeconds(const FM2Data& Data)
{
    // Sequence zero is the authored effect clip. Emitter-only models can have
    // no bones/animation sequence, but still carry finite rate/visibility keys.
    float Duration=Data.Animations.Num()?Data.Animations[0].Length/1000.f:0;
    if(Duration<=0) for(const auto& E:Data.ParticleEmitters)
        for(const auto* Track:{&E.RateTrack,&E.VisibilityTrack}) if(Track->GlobalSequence<0 && Track->Sequences.Num() && Track->Sequences[0].Times.Num())
            Duration=FMath::Max(Duration,Track->Sequences[0].Times.Last()/1000.f);
    return FMath::Clamp(Duration>0?Duration:.5f,.05f,30.f);
}
void UWowM2EmitterComponent::StopEmitting()
{
    if(!Emitting) return; Emitting=false; StopAgeMs=float((FPlatformTime::Seconds()-StartedAt)*1000);
}
float UWowM2EmitterComponent::SampleTrack(const FM2ParticleFloatTrack& Track,float Default) const
{
    return Track.Sample(AnimationIndex,Track.GlobalSequence>=0?ModelAgeMs:AnimationTimeMs,Model->GlobalSequenceDurations,Default);
}
namespace
{
template<typename T,typename L> T SampleBone(const TArray<uint32>& Times,const TArray<T>& Values,float Time,const T& Default,L Lerp)
{
    const int32 Count=FMath::Min(Times.Num(),Values.Num()); if(!Count) return Default;
    if(Count==1 || Time<=Times[0]) return Values[0];
    for(int32 I=1;I<Count;++I) if(Time<Times[I]) return Lerp(Values[I-1],Values[I],(Time-Times[I-1])/FMath::Max(1.f,float(Times[I]-Times[I-1])));
    return Values[Count-1];
}
}
void UWowM2EmitterComponent::AnimateBones(float TimeMs)
{
    ModelAgeMs=TimeMs; AnimationIndex=0; ModelVisible=Emitting;
    const auto& Sequences=Model->Animations;
    if(!Emitting && LoopAnimation)
    {
        const int32 Decay=Sequences.IndexOfByPredicate([](const auto& A){return A.AnimationId==159;});
        if(Decay>=0) {AnimationIndex=Decay; TimeMs=FMath::Max(0.f,TimeMs-StopAgeMs); ModelVisible=TimeMs<Sequences[Decay].Length;}
    }
    else if(LoopAnimation && Sequences.Num() && TimeMs>=Sequences[0].Length)
    {
        const int32 Hold=Sequences.IndexOfByPredicate([](const auto& A){return A.AnimationId==158;});
        if(Hold>=0) {TimeMs-=Sequences[0].Length; AnimationIndex=Hold;}
        if(Sequences[AnimationIndex].Length) TimeMs=FMath::Fmod(TimeMs,float(Sequences[AnimationIndex].Length));
    }
    AnimationTimeMs=TimeMs; BoneTransforms.SetNum(Model->Bones.Num());
    const auto* Animation=Sequences.IsValidIndex(AnimationIndex)?Model->AnimationTracks.FindByPredicate([&](const auto& A){return A.AnimationId==Sequences[AnimationIndex].AnimationId && A.SubAnimationId==Sequences[AnimationIndex].SubAnimationId;}):nullptr;
    for(int32 I=0;I<Model->Bones.Num();++I)
    {
        const auto& Bone=Model->Bones[I]; FTransform Local=FTransform::Identity;
        if(Animation && Animation->BoneTracks.IsValidIndex(I))
        {
            const auto& Track=Animation->BoneTracks[I]; const auto Lerp=[](const FVector& A,const FVector& B,float T){return FMath::Lerp(A,B,T);};
            const FVector Translation=SampleBone(Track.TransTimestamps,Track.TransValues,TimeMs,FVector::ZeroVector,Lerp);
            const FVector Scale=SampleBone(Track.ScaleTimestamps,Track.ScaleValues,TimeMs,FVector::OneVector,Lerp);
            const FQuat R=SampleBone(Track.RotTimestamps,Track.RotValues,TimeMs,FQuat::Identity,[](const FQuat& A,const FQuat& B,float T){return FQuat::Slerp(A,B,T).GetNormalized();});
            const FQuat Rotation(-R.Y,-R.X,-R.Z,R.W); const FVector Pivot=M2Vector(Bone.PivotPoint);
            Local=FTransform(Rotation,Pivot+M2Vector(Translation)-Rotation.RotateVector(Pivot*FVector(Scale.Y,Scale.X,Scale.Z)),FVector(Scale.Y,Scale.X,Scale.Z));
        }
        if(Bone.Flags&0x48)
        {
            FVector Camera; FRotator View; if(auto* PC=GetWorld()->GetFirstPlayerController())
            {
                PC->GetPlayerViewPoint(Camera,View); const FVector Pivot=M2Vector(Bone.PivotPoint);
                FVector Direction=GetComponentTransform().InverseTransformPosition(Camera)-Pivot;
                if(Bone.Flags&0x40) Direction.Z=0;
                if(!Direction.IsNearlyZero())
                {
                    const FQuat Rotation=FRotationMatrix::MakeFromYZ(Direction.GetSafeNormal(),FVector::UpVector).ToQuat();
                    const FVector Center=Local.TransformPosition(Pivot);
                    Local.SetRotation(Rotation); Local.SetTranslation(Center-Rotation.RotateVector(Pivot*Local.GetScale3D()));
                }
            }
        }
        BoneTransforms[I]=Bone.ParentBone>=0 && Bone.ParentBone<I?Local*BoneTransforms[Bone.ParentBone]:Local;
    }
}
void UWowM2EmitterComponent::RenderGeometry()
{
    for(int32 S=0;S<GeometryPasses.Num();++S)
    {
        const int32 Section=ActiveEmitters.Num()+S; if(!ModelVisible) {Mesh->SetMeshSectionVisible(Section,false); continue;}
        const auto& Pass=Model->RenderPasses[GeometryPasses[S]]; const auto& Submesh=Model->Submeshes[Pass.SubmeshIndex];
        const auto& Durations=Model->GlobalSequenceDurations; FLinearColor Color=FLinearColor::White;
        if(Model->Colors.IsValidIndex(Pass.ColorIndex))
        {
            const auto& C=Model->Colors[Pass.ColorIndex]; const auto RGB=C.Color.Sample(C.Color.GlobalSequence>=0?ModelAgeMs:AnimationTimeMs,Durations,FVector3f(1),AnimationIndex);
            Color=FLinearColor(RGB.X,RGB.Y,RGB.Z,SampleTrack(C.Alpha,1));
        }
        if(Model->TextureWeights.IsValidIndex(Pass.AlphaIndex)) Color.A*=SampleTrack(Model->TextureWeights[Pass.AlphaIndex],1);
        const int32 Count=Submesh.NumVertices,Start=Submesh.StartVertex; if(Start+Count>Model->Vertices.Num()) continue;
        TArray<FVector> Positions,Normals; TArray<FVector2D> UV; TArray<FLinearColor> Colors; TArray<FProcMeshTangent> Tangents;
        for(int32 I=Start;I<Start+Count;++I)
        {
            const auto& Vertex=Model->Vertices[I]; const FVector Position=M2Vector(Vertex.Position);
            FVector Skinned=FVector::ZeroVector; float Weight=0;
            for(int32 J=0;J<4;++J) if(Vertex.BoneWeights[J] && BoneTransforms.IsValidIndex(Vertex.BoneIndices[J]))
            {const float W=Vertex.BoneWeights[J]/255.f; Skinned+=BoneTransforms[Vertex.BoneIndices[J]].TransformPosition(Position)*W; Weight+=W;}
            Positions.Add(Weight>0?Skinned/Weight:Position); Normals.Add(FVector(Vertex.Normal.Y,Vertex.Normal.X,Vertex.Normal.Z)); FVector2D TexCoord=Vertex.TexCoord;
            if(Model->TextureTransforms.IsValidIndex(Pass.TransformIndex))
            {
                const auto& T=Model->TextureTransforms[Pass.TransformIndex];
                const auto Translate=T.Translation.Sample(T.Translation.GlobalSequence>=0?ModelAgeMs:AnimationTimeMs,Durations,FVector3f::ZeroVector,AnimationIndex),Scale=T.Scale.Sample(T.Scale.GlobalSequence>=0?ModelAgeMs:AnimationTimeMs,Durations,FVector3f(1),AnimationIndex);
                const auto Q=T.Rotation.Sample(T.Rotation.GlobalSequence>=0?ModelAgeMs:AnimationTimeMs,Durations,FQuat4f::Identity,AnimationIndex).GetNormalized();
                const auto P=Q.RotateVector(FVector3f((TexCoord.X-.5f)*Scale.X,(TexCoord.Y-.5f)*Scale.Y,0))+Translate;
                TexCoord=FVector2D(P.X+.5f,P.Y+.5f);
            }
            UV.Add(TexCoord); Colors.Add(Color);
        }
        if(Mesh->GetProcMeshSection(Section)) Mesh->UpdateMeshSection_LinearColor(Section,Positions,Normals,UV,Colors,Tangents,false);
        else
        {
            TArray<int32> Indices;
            for(int32 I=Submesh.StartTriangle;I<int32(Submesh.StartTriangle)+Submesh.NumTriangles && I+2<Model->Indices.Num();I+=3)
            {
                const int32 A=Model->Indices[I]-Start,B=Model->Indices[I+1]-Start,C=Model->Indices[I+2]-Start;
                if(A>=0 && B>=0 && C>=0 && A<Count && B<Count && C<Count) Indices.Append({A,C,B});
            }
            Mesh->CreateMeshSection_LinearColor(Section,Positions,Indices,Normals,UV,Colors,Tangents,false,false); Mesh->SetMaterial(Section,Materials[Section]);
        }
    }
}
void UWowM2EmitterComponent::UpdateRibbons(float Delta,float TimeMs)
{
    for(int32 I=0;I<ActiveRibbons.Num();++I)
    {
        const auto& R=Model->Ribbons[ActiveRibbons[I]]; auto& Nodes=RibbonNodes[I];
        for(auto& Node:Nodes) {Node.Age+=Delta; Node.Position.Z-=R.Gravity*FWowCoordinate::SCALE*Node.Age*Delta;}
        Nodes.RemoveAll([&](const auto& Node){return Node.Age>=R.EdgeLifetime;});
        if(!Emitting || SampleTrack(R.Visibility,1)<=0 || TimeMs-RibbonLastSpawn[I]<1000.f/R.EdgesPerSecond) continue;
        RibbonLastSpawn[I]=TimeMs;
        const auto Bone=BoneTransforms.IsValidIndex(R.Bone)?BoneTransforms[R.Bone]:FTransform::Identity;
        const auto Transform=GetComponentTransform();
        FRibbonNode Node; Node.Position=Transform.TransformPosition(Bone.TransformPosition(M2Vector(R.Position))); Node.Up=Transform.TransformVector(Bone.TransformVector(FVector::UpVector));
        if(Nodes.Num()>=64) Nodes.RemoveAt(0,1,EAllowShrinking::No); Nodes.Add(Node);
    }
}
void UWowM2EmitterComponent::RenderRibbons()
{
    for(int32 I=0;I<ActiveRibbons.Num();++I)
    {
        const int32 Section=ActiveEmitters.Num()+GeometryPasses.Num()+I; const auto& R=Model->Ribbons[ActiveRibbons[I]];
        const auto& Nodes=RibbonNodes[I]; const auto& Durations=Model->GlobalSequenceDurations;
        const auto RGB=R.Color.Sample(R.Color.GlobalSequence>=0?ModelAgeMs:AnimationTimeMs,Durations,FVector3f(1),AnimationIndex); const FLinearColor Color(RGB.X,RGB.Y,RGB.Z,SampleTrack(R.Alpha,1));
        const float Above=SampleTrack(R.Above,.1f)*FWowCoordinate::SCALE,Below=SampleTrack(R.Below,.1f)*FWowCoordinate::SCALE;
        const int32 Rows=FMath::Max(1,int32(R.Rows)),Cols=FMath::Max(1,int32(R.Cols)),Cell=FMath::Max(0,FMath::FloorToInt(SampleTrack(R.Slot)))%(Rows*Cols);
        TArray<FVector> Positions,Normals; TArray<FVector2D> UV; TArray<FLinearColor> Colors; TArray<FProcMeshTangent> Tangents;
        Positions.SetNumZeroed(128); Normals.Init(FVector::UpVector,128); UV.SetNumZeroed(128); Colors.Init(FLinearColor::Transparent,128);
        for(int32 N=0;N<Nodes.Num();++N)
        {
            const auto& Node=Nodes[N];
            Positions[N*2]=GetComponentTransform().InverseTransformPosition(Node.Position+Node.Up*Above); Positions[N*2+1]=GetComponentTransform().InverseTransformPosition(Node.Position-Node.Up*Below);
            const float U=(Cell%Cols+FMath::Clamp(Node.Age/R.EdgeLifetime,0.f,1.f))/Cols;
            UV[N*2]=FVector2D(U,float(Cell/Cols)/Rows); UV[N*2+1]=FVector2D(U,float(Cell/Cols+1)/Rows); Colors[N*2]=Colors[N*2+1]=Color;
        }
        if(Mesh->GetProcMeshSection(Section)) Mesh->UpdateMeshSection_LinearColor(Section,Positions,Normals,UV,Colors,Tangents,false);
        else
        {
            TArray<int32> Indices; for(int32 N=0;N<63;++N) {const int32 V=N*2; Indices.Append({V,V+1,V+2,V+2,V+1,V+3});}
            Mesh->CreateMeshSection_LinearColor(Section,Positions,Indices,Normals,UV,Colors,Tangents,false,false); Mesh->SetMaterial(Section,Materials[Section]);
        }
    }
}
void UWowM2EmitterComponent::Emit(int32 Index,float TimeMs)
{
    const auto& E=Model->ParticleEmitters[ActiveEmitters[Index]]; const auto& Global=Model->GlobalSequenceDurations;
    const float Width=SampleTrack(E.WidthTrack),Length=SampleTrack(E.LengthTrack);
    FVector Position(Random.FRandRange(-.5f,.5f)*Width,Random.FRandRange(-.5f,.5f)*Length,0);
    const float Speed=SampleTrack(E.SpeedTrack)*(1+Random.FRandRange(-1.f,1.f)*SampleTrack(E.VariationTrack));
    const float Lat=Random.FRandRange(-1.f,1.f)*SampleTrack(E.LatitudeTrack),Lon=Random.FRandRange(-1.f,1.f)*SampleTrack(E.LongitudeTrack);
    const float Z=SampleTrack(E.ZSourceTrack);
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
    if(!P.Local && BoneTransforms.IsValidIndex(E.Bone)) {P.Position=BoneTransforms[E.Bone].TransformPosition(P.Position); P.Velocity=BoneTransforms[E.Bone].TransformVector(P.Velocity);}
    if(!P.Local) {P.Position=GetComponentTransform().TransformPosition(P.Position); P.Velocity=GetComponentTransform().TransformVector(P.Velocity);}
    P.Life=FMath::Clamp(SampleTrack(E.LifeTrack,1)+Random.FRandRange(-1.f,1.f)*E.LifeVariation,.001f,10.f);
    P.ScaleSeed=FVector2f(Random.FRandRange(-1.f,1.f),Random.FRandRange(-1.f,1.f));
    P.Spin=E.InitialSpin+Random.FRandRange(-1.f,1.f)*E.InitialSpinVariation; P.SpinRate=E.Spin+Random.FRandRange(-1.f,1.f)*E.SpinVariation;
    const int32 Cells=FMath::Clamp(int32(E.Rows),1,256)*FMath::Clamp(int32(E.Cols),1,256); if(E.Flags&0x10000) P.Cell=Random.RandRange(0,Cells-1);
    Particles.Add(P);
}
void UWowM2EmitterComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,Type,Function); if(!Model || !Mesh) return;
    const double Now=FPlatformTime::Seconds(); const float Elapsed=FMath::Max(0.f,float(Now-LastAt)),Step=FMath::Min(Elapsed,.1f),TimeMs=float((Now-StartedAt)*1000); LastAt=Now;
    if(Emitting && Now>=EmitUntil) StopEmitting();
    AnimateBones(TimeMs);
    TArray<int32> Counts; Counts.Init(0,ActiveEmitters.Num());
    for(int32 I=Particles.Num()-1;I>=0;--I)
    {
        auto& P=Particles[I]; P.Age+=Elapsed;
        if(P.Age>=P.Life) {Particles.RemoveAtSwap(I,1,EAllowShrinking::No); continue;}
        const auto& E=Model->ParticleEmitters[ActiveEmitters[P.Emitter]];
        const float Gravity=SampleTrack(E.GravityTrack);
        P.Velocity.Z-=Gravity*FWowCoordinate::SCALE*Step; P.Velocity*=FMath::Exp(-FMath::Max(0.f,E.Drag)*Step); P.Position+=P.Velocity*Step; ++Counts[P.Emitter];
    }
    if(Emitting && Now<EmitUntil) for(int32 I=0;I<ActiveEmitters.Num();++I)
    {
        const auto& E=Model->ParticleEmitters[ActiveEmitters[I]];
        if(SampleTrack(E.VisibilityTrack,1)<=0) continue;
        Accumulators[I]+=FMath::Clamp(SampleTrack(E.RateTrack),0.f,1000.f)*Step;
        const int32 Count=FMath::Min3(FMath::FloorToInt32(Accumulators[I]),32,PerEmitterCapacity-Counts[I]); Accumulators[I]=FMath::Fmod(Accumulators[I],1.);
        for(int32 N=0;N<Count;++N) Emit(I,TimeMs);
    }
    UpdateRibbons(Elapsed,AnimationTimeMs);
    Render(); if(!Emitting && !ModelVisible && Particles.IsEmpty() && !RibbonNodes.ContainsByPredicate([](const auto& Nodes){return !Nodes.IsEmpty();})) DestroyComponent();
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
        Vertices.SetNumZeroed(PerEmitterCapacity*8); Normals.Init(FVector::UpVector,Vertices.Num()); UV.SetNumZeroed(Vertices.Num()); Colors.Init(FLinearColor::Transparent,Vertices.Num());
        int32 Slot=0;
        for(const auto& P:Particles) if(P.Emitter==S && Slot<PerEmitterCapacity)
        {
            const float Age=FMath::Clamp(P.Age/P.Life,0.f,1.f); const FVector3f RGB=E.ColorCurve.Sample(Age,FVector3f(1)); const float Alpha=E.AlphaCurve.Sample(Age,1.f);
            const auto Scale=E.ScaleCurve.Sample(Age,FVector2f(1)); const FVector2f Size(Scale.X*(1+P.ScaleSeed.X*E.ScaleVariance.X),Scale.Y*(1+P.ScaleSeed.Y*E.ScaleVariance.Y));
            const FTransform Bone=BoneTransforms.IsValidIndex(E.Bone)?BoneTransforms[E.Bone]:FTransform::Identity;
            const FVector Center=P.Local?Transform.TransformPosition(Bone.TransformPosition(P.Position)):P.Position;
            const float Angle=P.Spin+P.Age*P.SpinRate,Cos=FMath::Cos(Angle),Sin=FMath::Sin(Angle);
            const FVector X=Right*Cos+Up*Sin,Y=Up*Cos-Right*Sin;
            const float Corners[4][2]={{-1,1},{-1,-1},{1,1},{1,-1}};
            for(int32 Kind=0;Kind<2;++Kind)
            {
                if(!(E.Flags&(Kind==0?0x20000:0x40000))) continue;
                const int32 Cell=(P.Cell+FMath::Max(0,FMath::FloorToInt((Kind==0?E.HeadCellCurve:E.TailCellCurve).Sample(Age,0.f))))%(Rows*Cols);
                const FVector Velocity=P.Local?Transform.TransformVector(Bone.TransformVector(P.Velocity)):P.Velocity;
                const FVector Tail=-Velocity*FMath::Min(P.Age,FMath::Max(0.f,E.TailLengthUnits));
                FVector Side=FVector::CrossProduct(Tail,(Camera-Center).GetSafeNormal()).GetSafeNormal(); if(Side.IsNearlyZero()) Side=Right;
                for(int32 V=0;V<4;++V)
                {
                    const int32 Index=Slot*8+Kind*4+V;
                    const FVector World=Kind==0?Center+X*(Corners[V][0]*Size.X*SizeScale)+Y*(Corners[V][1]*Size.Y*SizeScale):Center+(V%2?Tail:FVector::ZeroVector)+Side*(Corners[V][0]*Size.X*SizeScale);
                    Vertices[Index]=Transform.InverseTransformPosition(World);
                    UV[Index]=FVector2D((Cell%Cols+(V>=2?1:0))/float(Cols),(Cell/Cols+(V%2?1:0))/float(Rows)); Colors[Index]=FLinearColor(RGB.X,RGB.Y,RGB.Z,Alpha);
                }
            }
            ++Slot;
        }
        if(Mesh->GetProcMeshSection(S)) Mesh->UpdateMeshSection_LinearColor(S,Vertices,Normals,UV,Colors,Tangents,false);
        else
        {
            TArray<int32> Indices; for(int32 I=0;I<PerEmitterCapacity*2;++I) {const int32 V=I*4; Indices.Append({V,V+1,V+2,V+2,V+1,V+3});}
            Mesh->CreateMeshSection_LinearColor(S,Vertices,Indices,Normals,UV,Colors,Tangents,false,false); Mesh->SetMaterial(S,Materials[S]);
        }
    }
    RenderGeometry(); RenderRibbons();
    RenderMilliseconds=(FPlatformTime::Seconds()-Began)*1000;
}
void UWowM2EmitterComponent::OnComponentDestroyed(bool Hierarchy)
{
    if(IsValid(Mesh)) Mesh->DestroyComponent(); Mesh=nullptr; Super::OnComponentDestroyed(Hierarchy);
}
