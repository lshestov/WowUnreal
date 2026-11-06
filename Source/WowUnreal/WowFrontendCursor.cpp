#include "WowFrontendCursor.h"
#include "Mpq/MpqManager.h"
#include "Formats/BlpParser.h"
#include "WowTextureFactory.h"
#include "Engine/Texture2D.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"

class SWowFrontendCursor : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWowFrontendCursor) {} SLATE_ARGUMENT(FMpqManager*, Mpq) SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        SetVisibility(EVisibility::HitTestInvisible);
        ForceVolatile(true);
        TArray<uint8> Bytes;
        if (!Args._Mpq || !Args._Mpq->ReadFile(TEXT("Interface\\Cursor\\Point.blp"), Bytes)) return;
        const auto Blp = FBlpParser::Parse(Bytes);
        if (!Blp.bIsValid) return;
        Texture.Reset(FWowTextureFactory::CreateTexture(Blp,
            MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), TEXT("GluePointCursor")).ToString()));
        Brush.SetResourceObject(Texture.Get());
        Brush.DrawAs = ESlateBrushDrawType::Image;
        Brush.ImageSize = FVector2D(32, 32);
    }
    bool IsReady() const { return Texture.IsValid(); }
    bool IsDrawn() const { return Drawn; }
    FString DescribeDrawState() const
    {
        return FString::Printf(TEXT("paints=%u size=(%.1f,%.1f) pointer=(%.1f,%.1f) origin=(%.1f,%.1f) windowOffset=(%.1f,%.1f)"),
            PaintCount,LastSize.X,LastSize.Y,LastPointer.X,LastPointer.Y,LastOrigin.X,LastOrigin.Y,LastWindowOffset.X,LastWindowOffset.Y);
    }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
    {
        ++PaintCount;
        Drawn = false;
        if (!IsReady() || !FSlateApplication::IsInitialized()) return Layer;
        // Paint geometry is in window space, whereas Slate's pointer is in
        // desktop space. The paint args supply the window-to-desktop offset;
        // converting before AbsoluteToLocal also preserves the window's DPI
        // scale and works for windows on monitors with negative coordinates.
        const FVector2D WindowOffset = Args.GetWindowToDesktopTransform();
        const FVector2D Pointer = Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos() - WindowOffset);
        const FVector2D Size = Geometry.GetLocalSize();
        LastSize=Size;LastPointer=Pointer;LastOrigin=Geometry.GetAbsolutePosition();LastWindowOffset=WindowOffset;
        if (Pointer.X < 0 || Pointer.Y < 0 || Pointer.X >= Size.X || Pointer.Y >= Size.Y) return Layer;
        const float Scale = FMath::Max(.001f, Geometry.GetAccumulatedLayoutTransform().GetScale());
        FSlateDrawElement::MakeBox(Elements, ++Layer,
            Geometry.ToPaintGeometry(FVector2D(32.f / Scale), FSlateLayoutTransform(Pointer)), &Brush);
        Drawn = true;
        return Layer;
    }
private:
    TStrongObjectPtr<UTexture2D> Texture;
    FSlateBrush Brush;
    mutable bool Drawn = false;
    mutable uint32 PaintCount = 0;
    mutable FVector2D LastSize=FVector2D::ZeroVector,LastPointer=FVector2D::ZeroVector,LastOrigin=FVector2D::ZeroVector;
    mutable FVector2D LastWindowOffset=FVector2D::ZeroVector;
};

FWowFrontendCursor::FWowFrontendCursor(FMpqManager* Mpq)
{ Widget = SNew(SWowFrontendCursor).Mpq(Mpq); }
FWowFrontendCursor::~FWowFrontendCursor() { Hide(); }
bool FWowFrontendCursor::IsReady() const { return Widget && Widget->IsReady(); }
bool FWowFrontendCursor::IsActive() const { return Viewport.IsValid(); }
bool FWowFrontendCursor::IsDrawn() const { return IsActive() && Widget->IsDrawn(); }
FString FWowFrontendCursor::DescribeDrawState() const
{
    return FString::Printf(TEXT("active=%d ready=%d %s"),IsActive(),IsReady(),Widget?*Widget->DescribeDrawState():TEXT("widget absent"));
}
void FWowFrontendCursor::Show(UGameViewportClient* InViewport)
{
    if (Viewport.Get() == InViewport || !InViewport || !IsReady()) return;
    Hide();
    Viewport = InViewport;
    PreviousSoftwareCursors = InViewport->GetIsUsingSoftwareCursorWidgets();
    // Suppress native arrows, hands and edit-beams through the viewport's
    // cursor mapper. Drawing the glove in the viewport keeps it in captures
    // and above every menu/modal without intercepting pointer input.
    for (int32 I = EMouseCursor::None + 1; I < EMouseCursor::TotalCursorCount; ++I)
    {
        const auto Type = static_cast<EMouseCursor::Type>(I);
        PreviousCursors.Add(I, InViewport->GetSoftwareCursorWidget(Type));
        InViewport->SetSoftwareCursorWidget(Type, SNullWidget::NullWidget);
    }
    InViewport->SetUseSoftwareCursorWidgets(true);
    InViewport->AddViewportWidgetContent(Widget.ToSharedRef(), 10000);
}
void FWowFrontendCursor::Hide()
{
    if (auto* V = Viewport.Get())
    {
        V->RemoveViewportWidgetContent(Widget.ToSharedRef());
        for (const auto& Pair : PreviousCursors)
            V->SetSoftwareCursorWidget(static_cast<EMouseCursor::Type>(Pair.Key), Pair.Value);
        V->SetUseSoftwareCursorWidgets(PreviousSoftwareCursors);
    }
    PreviousCursors.Reset();
    Viewport.Reset();
}
