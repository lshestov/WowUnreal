#pragma once
#include "CoreMinimal.h"
#include "WowTestGameMode.h"
#include "Dom/JsonObject.h"
#include "WowCharacterCreationTestGameMode.generated.h"
class AWowCharacterPreview;
class SWowCharacterCreateWidget;
class UProceduralMeshComponent;
UCLASS()
class WOWUNREAL_API AWowCharacterCreationTestGameMode : public AWowTestGameMode
{
    GENERATED_BODY()
public:
    AWowCharacterCreationTestGameMode();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type) override;
protected:
    virtual void SetupTestScene(UWorld*,AWowWorldManager*) override;
private:
    void StartCase();
    void Finish(bool Passed,const FString& Failure=FString());
    void CloseScreen();
    void OpenScreen();
    UPROPERTY() TObjectPtr<AWowCharacterPreview> Preview;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> CalibrationCard;
    TSharedPtr<SWowCharacterCreateWidget> Widget;
    TArray<TSharedPtr<FJsonValue>> Cases,Results;
    FString Output;
    int32 Index=-1,Phase=0,Stress=0;
    double Started=0,Changed=0;
    bool Finished=false;
    bool ObservedAnimation=false;
    TArray<FTransform> Bones;
    TArray<uint64> Retained;
};
