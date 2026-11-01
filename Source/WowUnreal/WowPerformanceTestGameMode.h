#pragma once
#include "CoreMinimal.h"
#include "WowTestGameMode.h"
#include "Dom/JsonObject.h"
#include "WowPerformanceTestGameMode.generated.h"

class AWowSkyManager;

/** Replays real MPQ streaming routes. No accounts, network or substitute geometry. */
UCLASS()
class WOWUNREAL_API AWowPerformanceTestGameMode : public AWowTestGameMode
{
    GENERATED_BODY()
public:
    AWowPerformanceTestGameMode();
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void SetupTestScene(UWorld* World, AWowWorldManager* WorldManager) override;
private:
    UPROPERTY() TObjectPtr<AWowSkyManager> Sky;
    TArray<TSharedPtr<FJsonValue>> Steps, Results;
    TMap<FString,TArray<double>> Ordinary, Capture;
    FString Output, Frames;
    int32 Index=-1, DiagnosticFrames=0;
    double Started=0, Previous=0, PhaseStarted=0, IdleSince=0, ShotAt=0;
    bool Finished=false, ShotRequested=false;
    FVector From=FVector::ZeroVector, To=FVector::ZeroVector, Target=FVector::ZeroVector;
    void StartStep();
    TSharedPtr<FJsonObject> Snapshot() const;
    void Finish(bool Passed,const FString& Reason=FString());
};
