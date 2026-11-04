#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WowM2EmitterComponent.generated.h"

class FMpqManager;
class FWowAssetCache;
struct FM2Data;
struct FM2ParticleFloatTrack;
class UProceduralMeshComponent;
class UTexture2D;
class UMaterialInstanceDynamic;

/** Bounded authored M2 plane/sphere head sprites. */
UCLASS(ClassGroup=(WowAssets))
class WOWASSETS_API UWowM2EmitterComponent : public USceneComponent
{
    friend class FGraphicsParticleDensity;
    GENERATED_BODY()
public:
    UWowM2EmitterComponent();
    bool Initialize(TSharedPtr<const FM2Data> InModel,FMpqManager* Mpq,FWowAssetCache* Cache,float EmitSeconds,bool DepthGeometry=false);
    void StopEmitting();
    void ExtendEmission(double Deadline) {if(Emitting) EmitUntil=FMath::Max(EmitUntil,Deadline);}
    static float OneShotSeconds(const FM2Data& Data);
    bool IsEmitting() const {return Emitting || ModelVisible;}
    int32 GetGeometryCount() const {return GeometryPasses.Num();}
    int32 GetParticleCount() const {return Particles.Num();}
    int32 GetEmitterCount() const {return ActiveEmitters.Num();}
    double GetRenderMilliseconds() const {return RenderMilliseconds;}
    FTransform GetModelBoneTransform(int32 Bone) const {return BoneTransforms.IsValidIndex(Bone)?BoneTransforms[Bone]:FTransform::Identity;}
    void SetCaptureView(const FVector& Position,const FRotator& Rotation) {CaptureView=FTransform(Rotation,Position);}
    // Opt-in source material combiners, lighting and larger bounded weather
    // budget for the full-screen login model. Spell/character callers retain
    // their existing material path and particle budgets.
    void ConfigureGlueScene() {bGlueScene=true;}
    void SetGlueLighting(FLinearColor Ambient,FLinearColor Diffuse,FVector Direction) {GlueAmbient=Ambient;GlueDiffuse=Diffuse;GlueDirection=Direction;}
    virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function) override;
    virtual void OnComponentDestroyed(bool Hierarchy) override;
private:
    struct FParticle
    {
        FVector Position=FVector::ZeroVector,Velocity=FVector::ZeroVector;
        FVector2f ScaleSeed=FVector2f::ZeroVector;
        float Age=0,Life=1,Spin=0,SpinRate=0;
        int32 Emitter=0,Cell=0;
        bool Local=false;
    };
    TSharedPtr<const FM2Data> Model;
    TArray<FParticle> Particles;
    TArray<int32> ActiveEmitters;
    TArray<double> Accumulators;
    FRandomStream Random;
    double StartedAt=0,LastAt=0,EmitUntil=0;
    double RenderMilliseconds=0;
    bool Emitting=true;
    bool LoopAnimation=false;
    bool bGlueScene=false;
    FLinearColor GlueAmbient=FLinearColor::White,GlueDiffuse=FLinearColor::Black;
    FVector GlueDirection=FVector::DownVector;
    int32 PerEmitterCapacity=0;
    TArray<FTransform> BoneTransforms;
    TOptional<FTransform> CaptureView;
    TArray<int32> GeometryPasses,ActiveRibbons;
    TArray<TArray<int32>> GeometryVertices,GeometryTriangles;
    struct FRibbonNode {FVector Position,Up; float Age=0;};
    TArray<TArray<FRibbonNode>> RibbonNodes;
    TArray<double> RibbonLastSpawn;
    float AnimationTimeMs=0,ModelAgeMs=0,StopAgeMs=-1;
    int32 AnimationIndex=0;
    bool ModelVisible=true;
    float SampleTrack(const FM2ParticleFloatTrack& Track,float Default=0) const;
    void UpdateRibbons(float Delta,float TimeMs);
    void RenderRibbons();
    void AnimateBones(float TimeMs);
    void RenderGeometry();
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> SectionMeshes;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> Textures;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    void Emit(int32 Index,float TimeMs);
    void AdvanceParticles(float Elapsed,float TimeMs);
    void Render();
};
