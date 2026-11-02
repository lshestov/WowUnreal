#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WowSessionState.h"
#include "Formats/Dbc/CharacterCreationDbc.h"
#include "WowCharacterCreationArt.h"
#include "WowCharacterPreview.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UPointLightComponent;
class UDirectionalLightComponent;
class UWowAnimationController;
class AWowWorldManager;
class UWowM2EmitterComponent;
struct FM2Data;

/** Isolated UI capture rig. Latest selection wins before the next tick. */
UCLASS()
class WOWUNREAL_API AWowCharacterPreview : public AActor
{
    GENERATED_BODY()
public:
    AWowCharacterPreview();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void Setup(AWowWorldManager* InWorldManager,int32 Width=640,int32 Height=800);
    void ShowCharacter(const FWowCharacterInfo& Character);
    void ShowCharacter(uint8 Race,uint8 Gender);
    void ShowCreation(const FWowCharacterAppearance& Appearance);
    void ClearCharacter(bool ClearScene=true);
    void EnableCreationScene();
    void SetOriginalLighting(const TArray<FWowCreationLight>& Lights);
    void SetViewAspect(float Aspect);
    void CaptureNow();
    void Rotate(float Degrees);
    void Zoom(float Steps);
    void ResetCamera();
    bool IsReady() const { return IsValid(CurrentCharacterActor) && !bPending; }
    AActor* GetCharacterActor() const { return CurrentCharacterActor; }
    const FWowCharacterInfo& GetShownCharacter() const { return Requested; }
    uint32 GetRevision() const { return Revision; }
    UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }
    AWowWorldManager* GetWorldManager() const { return WorldManager; }
private:
    friend class AWowCharacterCreationTestGameMode;
    void Request(const FWowCharacterInfo& Info,bool Starter);
    void Rebuild();
    void PositionCamera();
    bool SetupCreationScene();
    void UpdateSceneAttachment();
    void UpdateSceneLights();
    UPROPERTY() TObjectPtr<USceneCaptureComponent2D> CaptureComponent;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> RenderTarget;
    UPROPERTY() TObjectPtr<UPointLightComponent> KeyLight;
    UPROPERTY() TObjectPtr<UPointLightComponent> FillLight;
    UPROPERTY() TObjectPtr<UPointLightComponent> RimLight;
    UPROPERTY() TArray<TObjectPtr<UDirectionalLightComponent>> OriginalLights;
    UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> ScenePointLights;
    TArray<int32> SceneLightIndices;
    double SceneStartedAt=0;
    FLinearColor SceneAmbient=FLinearColor::Black;
    UPROPERTY() TObjectPtr<AActor> CurrentCharacterActor;
    UPROPERTY() TObjectPtr<UWowAnimationController> CurrentAnimationController;
    UPROPERTY() TObjectPtr<AWowWorldManager> WorldManager;
    UPROPERTY() TObjectPtr<AActor> SceneActor;
    UPROPERTY() TObjectPtr<UWowM2EmitterComponent> SceneRenderer;
    TSharedPtr<const FM2Data> SceneModel;
    FString ScenePath;
    FVector SceneCamera=FVector::ZeroVector,SceneTarget=FVector::ZeroVector;
    bool bCreationScene=false;
    FWowCharacterInfo Requested;
    bool bPending=false,bStarter=false;
    float Yaw=180,ZoomFactor=1,Distance=400;
    FVector Focus=FVector::ZeroVector;
    double CloseFocusZ=0;
    uint32 Revision=0;
};
