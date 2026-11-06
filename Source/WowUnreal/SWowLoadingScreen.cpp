#include "SWowLoadingScreen.h"
#include "Widgets/Images/SImage.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Mpq/MpqManager.h"
#include "Formats/BlpParser.h"
#include "WowTextureFactory.h"
#include "Rendering/DrawElements.h"

void SWowLoadingScreen::Construct(const FArguments& InArgs)
{
    auto Load = [&](const TCHAR* Name, TStrongObjectPtr<UTexture2D>& Texture, FSlateBrush& Brush)
    {
        TArray<uint8> Bytes;
        const FString Path = FString(TEXT("Interface\\Glues\\LoadingBar\\")) + Name + TEXT(".blp");
        if (!InArgs._Mpq || !InArgs._Mpq->ReadFile(Path, Bytes)) return;
        const auto Blp = FBlpParser::Parse(Bytes);
        if (!Blp.bIsValid) return;
        Texture.Reset(FWowTextureFactory::CreateTexture(Blp,
            MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), Name).ToString()));
        Brush.SetResourceObject(Texture.Get());
        Brush.DrawAs = ESlateBrushDrawType::Image;
        if (Texture.IsValid()) Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
    };
    Load(TEXT("Loading-BarFill"), FillTexture, FillBrush);
    Load(TEXT("Loading-BarBorder"), BorderTexture, BorderBrush);
    BackgroundBrush = FSlateColorBrush(FLinearColor::Black);
    ChildSlot[SAssignNew(BackgroundImage, SImage).Image(&BackgroundBrush)];
}

void SWowLoadingScreen::SetBackgroundImage(UTexture2D* Texture)
{
    if (!BackgroundImage.IsValid())
    {
        return;
    }

    if (Texture)
    {
        BackgroundTexture.Reset(Texture);
        BackgroundBrush = FSlateBrush();
        BackgroundBrush.SetResourceObject(BackgroundTexture.Get());
        BackgroundBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
        BackgroundBrush.DrawAs = ESlateBrushDrawType::Image;
        BackgroundImage->SetImage(&BackgroundBrush);
    }
    else
    {
        BackgroundTexture.Reset();
        BackgroundBrush = FSlateColorBrush(FLinearColor::Black);
        BackgroundImage->SetImage(&BackgroundBrush);
    }
}

void SWowLoadingScreen::SetProgressText(const FString& Text)
{
    // Loading diagnostics remain in the log; the original screen has no
    // percentage/status label over its artwork.
}

void SWowLoadingScreen::SetProgress(float Fraction, const FString& StatusText)
{
    Progress = FMath::IsFinite(Fraction) ? FMath::Clamp(Fraction, 0.f, 1.f) : 0.f;
    Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SWowLoadingScreen::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool Enabled) const
{
    Layer = SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements, Layer, Style, Enabled);
    if (!HasProgressArtwork()) return Layer;
    const FVector2D Size = Geometry.GetLocalSize();
    // 3.3.5a LoadingScreen::s_textureInfo uses bottom-origin normalized
    // coordinates: center (0.5,0.075), fill .525 x .025, border .6 x .05.
    if (Progress > 0)
        FSlateDrawElement::MakeBox(Elements, ++Layer,
            Geometry.ToPaintGeometry(FVector2D(Size.X * .525f * Progress, Size.Y * .025f),
                FSlateLayoutTransform(FVector2D(Size.X * .2375f, Size.Y * .9125f))), &FillBrush);
    FSlateDrawElement::MakeBox(Elements, ++Layer,
        Geometry.ToPaintGeometry(FVector2D(Size.X * .6f, Size.Y * .05f),
            FSlateLayoutTransform(FVector2D(Size.X * .2f, Size.Y * .9f))), &BorderBrush);
    return Layer;
}
