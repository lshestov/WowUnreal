#pragma once
#include "CoreMinimal.h"
#include "WowTestGameMode.h"
#include "WowSessionState.h"
#include "Dom/JsonObject.h"
#include "WowCharacterSelectionTestGameMode.generated.h"
class AWowCharacterPreview;
class SWowCharacterSelectWidget;

/** Offline Slate input/render checks. All account actions terminate in spies. */
UCLASS()
class WOWUNREAL_API AWowCharacterSelectionTestGameMode : public AWowTestGameMode
{
    GENERATED_BODY()
public:
    AWowCharacterSelectionTestGameMode();
    virtual void Tick(float) override;
    virtual void EndPlay(const EEndPlayReason::Type) override;
protected:
    virtual void SetupTestScene(UWorld*,AWowWorldManager*) override;
private:
    bool VerifyActions();
    void StartCase();
    void OpenNextCase();
    void CloseScreen();
    void Finish(bool Passed,const FString& Failure=FString());
    UPROPERTY() TObjectPtr<AWowCharacterPreview> Preview;
    TSharedPtr<SWowCharacterSelectWidget> Widget;
    TArray<FWowCharacterInfo> Roster;
    TArray<TSharedPtr<FJsonValue>> Cases,Results;
    FString Output;
    int32 Index=-1,Phase=0,EnterCount=0,DeleteCount=0,CreateCount=0,Cycles=0,ActionStep=0;
    int64 EnterGuid=0,DeletedGuid=0,ActionInitialGuid=0;
    double Started=0,Changed=0;
    bool Finished=false,ActionsVerified=false;
};
