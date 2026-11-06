#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class UTexture2D;
class FMpqManager;

/**
 * Original loading artwork and progress bar, loaded from the user's MPQ.
 */
class SWowLoadingScreen : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowLoadingScreen) {}
        SLATE_ARGUMENT(FMpqManager*, Mpq)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /** Set the background loading screen image */
    void SetBackgroundImage(UTexture2D* Texture);

    /** Update the progress text */
    void SetProgressText(const FString& Text);

    /** Update progress with fraction and status (convenience) */
    void SetProgress(float Fraction, const FString& StatusText);
    float GetProgress() const { return Progress; }
    bool HasProgressArtwork() const { return FillTexture.IsValid() && BorderTexture.IsValid(); }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool Enabled) const override;

private:
    TSharedPtr<class SImage> BackgroundImage;
    TStrongObjectPtr<UTexture2D> BackgroundTexture;
    TStrongObjectPtr<UTexture2D> FillTexture, BorderTexture;
    FSlateBrush BackgroundBrush;
    FSlateBrush FillBrush, BorderBrush;
    float Progress = 0;
};
