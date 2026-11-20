#pragma once
#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "WowGameViewportClient.generated.h"

/** Route native viewport input and cursor requests to the game UI. */
UCLASS()
class WOWUNREAL_API UWowGameViewportClient : public UGameViewportClient
{
    GENERATED_BODY()
public:
    virtual bool InputChar(FViewport* InViewport,int32 ControllerId,TCHAR Character) override;
    virtual TOptional<TSharedRef<SWidget>> MapCursor(FViewport* InViewport, const FCursorReply& CursorReply) override;

#if PLATFORM_WINDOWS
    virtual void Init(FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice = true) override;
    virtual void BeginDestroy() override;
private:
    void RefreshNativeCursor(float DeltaTime);
    FDelegateHandle SlatePostTick;
#endif
};
