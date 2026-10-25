#pragma once
#include "CoreMinimal.h"
#include "Components/TextBlock.h"
#include "WowTextBlock.generated.h"

class FWowFrameManager;
class SBox;
class SRichTextBlock;
class UTexture2D;
struct FWowRichLabelState;
struct FWowInlineIcon
{
    FString Path;
    float Height=0,Width=0,OffsetX=0,OffsetY=0;
    FVector4f Crop=FVector4f(0,0,1,1);
};
struct FWowLabelMarkup
{
    FString Plain,Rich;
    TArray<FWowInlineIcon> Icons;
    bool Formatted=false;
};

/** Keeps the native TextBlock API while laying out WoW color/image runs. */
UCLASS()
class WOWUI_API UWowTextBlock : public UTextBlock
{
    GENERATED_BODY()
public:
    void InitializeLabel(FWowFrameManager* Frames,float Scale);
    virtual void SetText(FText InText) override;
    void RefreshRichStyle(float Scale=0);
    static FWowLabelMarkup ParseMarkup(const FString& Text);
    const FWowLabelMarkup& Markup() const { return Parsed; }
    virtual void SynchronizeProperties() override;
    virtual void ReleaseSlateResources(bool ReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void OnWrapTextAtChanged(float Value) override;
    virtual void OnAutoWrapTextChanged(bool Value) override;
    virtual void OnJustificationChanged(ETextJustify::Type Value) override;
private:
    FWowFrameManager* Frames=nullptr;
    float UnitScale=1;
    ETextJustify::Type LabelJustification=ETextJustify::Left;
    FWowLabelMarkup Parsed;
    FString OriginalText;
    bool HasOriginal=false;
    UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> IconTextures;
    TSharedPtr<FWowRichLabelState> RichState;
    TSharedPtr<SBox> Host;
    TSharedPtr<SWidget> PlainWidget;
    TSharedPtr<SRichTextBlock> RichWidget;
    void RebuildContent();
};
