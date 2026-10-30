#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WowM2EmitterComponent.generated.h"

class FMpqManager;
class FWowAssetCache;
struct FM2Data;
class UProceduralMeshComponent;
class UTexture2D;
class UMaterialInstanceDynamic;

/** Bounded authored M2 plane/sphere head sprites. */
UCLASS(ClassGroup=(WowAssets))
class WOWASSETS_API UWowM2EmitterComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    UWowM2EmitterComponent();
    bool Initialize(TSharedPtr<const FM2Data> InModel,FMpqManager* Mpq,FWowAssetCache* Cache,float EmitSeconds);
    void StopEmitting();
    int32 GetParticleCount() const {return Particles.Num();}
    int32 GetEmitterCount() const {return ActiveEmitters.Num();}
    double GetRenderMilliseconds() const {return RenderMilliseconds;}
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
    int32 PerEmitterCapacity=0;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> Textures;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    void Emit(int32 Index,float TimeMs);
    void Render();
};
