#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WowLoginScene.generated.h"
class AWowWorldManager;
class AWowAudioManager;
class UWowM2EmitterComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
struct FM2Data;

/** AccountLogin's original ModelFFX, isolated from the streamed world. */
UCLASS()
class WOWUNREAL_API AWowLoginScene : public AActor
{
    GENERATED_BODY()
public:
    AWowLoginScene();
    bool Setup(AWowWorldManager* Manager,AWowAudioManager* Audio);
    void SetViewAspect(float Aspect);
    UTextureRenderTarget2D* GetRenderTarget() const {return Target;}
    UMaterialInstanceDynamic* GetDisplayMaterial() const {return Composite;}
    const TSharedPtr<const FM2Data>& GetModel() const {return Model;}
    UWowM2EmitterComponent* GetRenderer() const {return Renderer;}
    float GetAgeMs() const;
    FVector GetCameraPosition() const;
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void UpdateCamera();
    void ApplyGlow();
    UPROPERTY() TObjectPtr<USceneCaptureComponent2D> Capture;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Target;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> QuarterA;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> QuarterB;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Box4;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GaussH;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GaussV;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Composite;
    UPROPERTY() TObjectPtr<UWowM2EmitterComponent> Renderer;
    TWeakObjectPtr<AWowAudioManager> AudioManager;
    TSharedPtr<const FM2Data> Model;
    double Started=0;
    float LastEventMs=-1;
    float SourceGlow=0;
};
