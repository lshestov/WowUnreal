#pragma once
#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "WowPortraitCameraData.generated.h"

/** Authored M2 portrait camera in skeletal-mesh coordinates, retained at runtime. */
UCLASS()
class WOWASSETS_API UWowPortraitCameraData : public UAssetUserData
{
    GENERATED_BODY()
public:
    UPROPERTY() FVector Position=FVector::ZeroVector;
    UPROPERTY() FVector Target=FVector::ZeroVector;
    UPROPERTY() float Fov=45;
};
