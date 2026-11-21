#pragma once
#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "WowPortraitCameraData.generated.h"

/** Authored M2 portrait view and material sections which use their original shading. */
UCLASS()
class WOWASSETS_API UWowPortraitCameraData : public UAssetUserData
{
    GENERATED_BODY()
public:
    UPROPERTY() FVector Position=FVector::ZeroVector;
    UPROPERTY() FVector Target=FVector::ZeroVector;
    UPROPERTY() float SquareFovDegrees=0;
    UPROPERTY() TArray<int32> PreserveMaterialSlots;
};
