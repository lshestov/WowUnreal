#pragma once
#include "CoreMinimal.h"
#include "WowTestGameMode.h"
#include "Dom/JsonObject.h"
#include "WowLoginTestGameMode.generated.h"
class AWowLoginController;

/** Opt-in offline replay of the real menu controller, with account-action spies. */
UCLASS()
class WOWUNREAL_API AWowLoginTestGameMode : public AWowTestGameMode
{
    GENERATED_BODY()
public:
    AWowLoginTestGameMode();
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
protected:
    virtual void SetupTestScene(UWorld*,AWowWorldManager*) override;
private:
    void Finish(bool Passed,const FString& Failure=FString());
    UPROPERTY() TObjectPtr<AWowLoginController> Controller;
    FString Output;
    FString PendingCapture;
    TArray<TSharedPtr<FJsonValue>> Steps,Results;
    TArray<TWeakObjectPtr<AActor>> Retired;
    double Started=0,Changed=0;
    int32 Index=0,Phase=0,Submits=0,Cycles=0;
    bool Finished=false,Recording=false;
    TOptional<FVector2D> OriginalCursor;
};
