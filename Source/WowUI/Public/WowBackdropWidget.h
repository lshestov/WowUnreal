#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "WowFrameTypes.h"
#include "WowBackdropWidget.generated.h"

struct FWowBackdropQuad
{
    FVector4f Rect;
    FVector2f UV[4]; // top-left, top-right, bottom-left, bottom-right
    bool Border=false;
};
class UTexture2D;
class SWowBackdrop;

/** Native tiled background and eight-piece Wrath border, batched by texture. */
UCLASS()
class WOWUI_API UWowBackdropWidget : public UWidget
{
    GENERATED_BODY()
public:
    void Configure(const FWowBackdrop& Value,UTexture2D* Background,UTexture2D* Edge,float Scale);
    void SetUnitScale(float Scale);
    void SetColors(const FLinearColor& Background,const FLinearColor& Border);
    static TArray<FWowBackdropQuad> BuildQuads(const FWowBackdrop& Value,FVector2f Size,float Scale);
    virtual void ReleaseSlateResources(bool ReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    UPROPERTY(Transient) TObjectPtr<UTexture2D> BackgroundTexture;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> EdgeTexture;
    FWowBackdrop Definition;
    FLinearColor BackgroundColor=FLinearColor::White,BorderColor=FLinearColor::White;
    float UnitScale=1;
    TSharedPtr<SWowBackdrop> Painter;
    void SynchronizePainter();
};
