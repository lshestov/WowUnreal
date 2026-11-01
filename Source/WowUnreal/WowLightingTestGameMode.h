#pragma once
#include "CoreMinimal.h"
#include "WowTestGameMode.h"
#include "Dom/JsonObject.h"
#include "WowLightingTestGameMode.generated.h"

class AWowSkyManager;

/** Offline verification using the production streaming, materials and sky. */
UCLASS()
class WOWUNREAL_API AWowLightingTestGameMode : public AWowTestGameMode
{
    GENERATED_BODY()
public:
    AWowLightingTestGameMode();
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void SetupTestScene(UWorld* World, AWowWorldManager* WorldManager) override;
private:
    UPROPERTY() TObjectPtr<AWowSkyManager> Sky;
    UPROPERTY() TObjectPtr<AActor> Subject;
    UPROPERTY() TObjectPtr<AActor> Fixture;
    TArray<TSharedPtr<FJsonValue>> Cases, Results;
    TMap<FString,TArray<double>> Profile;
    FString Output;
    int32 Index=INDEX_NONE, Shot=0;
    double Started=0, PhaseStarted=0, NextShot=0;
    bool Ready=false, Finished=false;
    FVector From=FVector::ZeroVector, To=FVector::ZeroVector;
    FVector Target=FVector::ZeroVector;
    FVector PreviousSun=FVector::ZeroVector;
    void StartCase();
    void Finish(bool Passed, const FString& Reason=FString());
};
