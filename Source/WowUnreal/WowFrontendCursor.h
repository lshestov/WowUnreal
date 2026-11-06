#pragma once
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class FMpqManager;
class UGameViewportClient;
class SWidget;
class SWowFrontendCursor;

/** Viewport-owned cursor for the Glue screens, also visible in UE screenshots. */
class FWowFrontendCursor
{
public:
    explicit FWowFrontendCursor(FMpqManager* Mpq);
    ~FWowFrontendCursor();
    void Show(UGameViewportClient* Viewport);
    void Hide();
    bool IsReady() const;
    bool IsActive() const;
    bool IsDrawn() const;
    FString DescribeDrawState() const;
private:
    TSharedPtr<SWowFrontendCursor> Widget;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TMap<int32,TSharedPtr<SWidget>> PreviousCursors;
    bool PreviousSoftwareCursors = false;
};
