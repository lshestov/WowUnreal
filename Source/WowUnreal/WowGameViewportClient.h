#pragma once
#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "WowGameViewportClient.generated.h"

/** Route native text input to keyboard-enabled FrameXML frames. */
UCLASS()
class WOWUNREAL_API UWowGameViewportClient : public UGameViewportClient
{
    GENERATED_BODY()
public:
    virtual bool InputChar(FViewport* InViewport,int32 ControllerId,TCHAR Character) override;
};
