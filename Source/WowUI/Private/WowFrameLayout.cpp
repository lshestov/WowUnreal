#include "WowFrameManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"

bool FWowFrameManager::IsFrameDrawn(int64 Handle) const
{
    const auto* Own = Frames.Find(Handle);
    if (!Own || Own->Def.bVirtual || !Own->Widget.IsValid()) return false;
    for (int32 Depth = 0; Handle >= 0 && Depth < 64; ++Depth)
    {
        const auto* Entry = Frames.Find(Handle);
        if (!Entry) break;
        if (Entry->Def.bHidden || (Entry->Widget.IsValid() && Entry->Widget->GetVisibility() == ESlateVisibility::Collapsed)) return false;
        Handle = Entry->ParentHandle;
    }
    return true;
}

float FWowFrameManager::GetFrameScale(int64 Handle) const
{
    const auto* Entry = Frames.Find(Handle);
    return Entry ? Entry->Def.Scale : 1.f;
}

float FWowFrameManager::GetEffectiveFrameScale(int64 Handle) const
{
    float Scale = 1;
    for (int32 Depth = 0; Handle >= 0 && Depth < 64; ++Depth)
    {
        const auto* Entry = Frames.Find(Handle);
        if (!Entry) break;
        Scale *= Entry->Def.Scale;
        Handle = Entry->ParentHandle;
    }
    return Scale;
}

void FWowFrameManager::SetFrameScale(int64 Handle, float Scale)
{
    auto* Entry = Frames.Find(Handle);
    if (!Entry || !FMath::IsFinite(Scale) || Scale <= 0 || FMath::IsNearlyEqual(Scale, Entry->Def.Scale)) return;
    Entry->Def.Scale = Scale;
    RefreshLayout();
}

FWowFrameManager::FFrameRect FWowFrameManager::CalculateFrameRect(const FWowFrameDef& Def)
{
    const FFrameRect Parent = GetFrameRect(Def.Parent);
    if (Def.bSetAllPoints) return Parent;
    const float Scale = Def.Scale * GetEffectiveFrameScale(FindFrame(Def.Parent));
    FFrameRect Rect(Parent.X, Parent.Y,
        Def.Width > 0 ? Def.Width * Scale : Parent.W,
        Def.Height > 0 ? Def.Height * Scale : Parent.H);
    if (Def.Anchors.IsEmpty())
    {
        return FFrameRect(); // Unanchored frames have no screen rectangle in WoW.
    }
    const auto Target = [&](const FWowAnchor& A)
    {
        const auto Relative = GetFrameRect(A.RelativeTo.IsEmpty() ? Def.Parent : A.RelativeTo);
        return FVector2D(Relative.X, Relative.Y) + GetAnchorPointOffset(A.RelativePoint, Relative.W, Relative.H)
            + FVector2D(A.OffsetX, -A.OffsetY) * Scale;
    };
    const auto& First = Def.Anchors[0];
    const FVector2D P = Target(First), Fraction = GetAnchorPointOffset(First.Point, 1, 1);
    // Each anchor constrains an axis. LEFT/RIGHT stretches width while preserving
    // explicit height; TOPLEFT/BOTTOMRIGHT constrains both axes.
    for (int32 I = 1; I < Def.Anchors.Num(); ++I)
    {
        const FVector2D Q = Target(Def.Anchors[I]);
        const FVector2D Other = GetAnchorPointOffset(Def.Anchors[I].Point, 1, 1);
        if (!FMath::IsNearlyEqual(Fraction.X, Other.X)) Rect.W = (Q.X - P.X) / (Other.X - Fraction.X);
        if (!FMath::IsNearlyEqual(Fraction.Y, Other.Y)) Rect.H = (Q.Y - P.Y) / (Other.Y - Fraction.Y);
    }
    Rect.W = FMath::Max(0.f, Rect.W); Rect.H = FMath::Max(0.f, Rect.H);
    Rect.X = P.X - Fraction.X * Rect.W;
    Rect.Y = P.Y - Fraction.Y * Rect.H;
    return Rect;
}

FWowFrameManager::FFrameRect FWowFrameManager::GetFrameRect(const FString& Name)
{
    const FString Key = Name.IsEmpty() ? TEXT("UIParent") : Name;
    if (const auto* Cached = FrameRects.Find(Key)) return *Cached;
    if (Key == TEXT("UIParent")) return FFrameRect(0, 0, 1024, 768);
    if (ResolvingRects.Contains(Key)) return FFrameRect();
    ResolvingRects.Add(Key);
    FFrameRect Result;
    if (const auto* Entry = Frames.Find(FindFrame(Key)))
        Result = CalculateFrameRect(Entry->Def);
    else if (const auto* Owner = RegionOwners.Find(Key))
    {
        const auto Parent = GetFrameRect(*Owner);
        UWidget* Widget = GetNamedTexture(Key);
        if (!Widget) Widget = GetNamedFontString(Key);
        const auto* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
        if (Slot)
        {
            const auto Anchors = Slot->GetAnchors();
            if (Anchors.Minimum != Anchors.Maximum) Result = Parent;
            else
            {
                const float Scale = GetEffectiveFrameScale(FindFrame(*Owner)) / UIScale;
                const auto S = Slot->GetAutoSize() ? Widget->GetDesiredSize() : Slot->GetSize();
                const auto P = Slot->GetPosition() - Slot->GetAlignment() * S;
                Result = FFrameRect(Parent.X + P.X * Scale, Parent.Y + P.Y * Scale, S.X * Scale, S.Y * Scale);
            }
        }
    }
    ResolvingRects.Remove(Key);
    // Missing forward references must not poison later lookups with a cached
    // rectangle at the center of the screen.
    if (FindFrame(Key) >= 0 || RegionOwners.Contains(Key)) FrameRects.Add(Key, Result);
    return Result;
}

FVector4 FWowFrameManager::GetLayoutRect(int64 Handle)
{
    const auto* Entry = Frames.Find(Handle);
    const auto Rect = GetFrameRect(Entry ? Entry->Def.Name : TEXT("UIParent"));
    return FVector4(Rect.X, Rect.Y, Rect.W, Rect.H);
}

void FWowFrameManager::ApplyAnchors(UWidget* Widget, const FWowFrameDef& Def)
{
    auto* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
    if (!Slot) return;
    const auto Parent = GetFrameRect(Def.Parent);
    const auto Rect = CalculateFrameRect(Def);
    const float ParentScale = GetEffectiveFrameScale(FindFrame(Def.Parent));
    Slot->SetAutoSize(false);
    Slot->SetAnchors(FAnchors(0, 0));
    Slot->SetAlignment(FVector2D::ZeroVector);
    Slot->SetPosition(FVector2D(Rect.X - Parent.X, Rect.Y - Parent.Y) * (UIScale / ParentScale));
    Slot->SetSize(FVector2D(Rect.W, Rect.H) * (UIScale / (ParentScale * Def.Scale)));
    Widget->SetRenderTransformPivot(FVector2D::ZeroVector);
    Widget->SetRenderScale(FVector2D(Def.Scale));
    Widget->SetVisibility(Def.bHidden || (!Def.bSetAllPoints && Def.Anchors.IsEmpty())
        ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
    FrameRects.Add(Def.Name, Rect);
}

void FWowFrameManager::RefreshLayout()
{
    const auto Root = GetFrameRect(TEXT("UIParent"));
    FrameRects.Empty();
    FrameRects.Add(TEXT("UIParent"), Root);
    TArray<int64> Handles;
    Frames.GetKeys(Handles); Handles.Sort();
    for (const auto Handle : Handles)
        if (const auto* Entry = Frames.Find(Handle); Entry && Entry->Widget.IsValid())
            ApplyAnchors(Entry->Widget.Get(), Entry->Def);
    for (const auto& Region : RegionLayouts)
    {
        const auto Owner = GetFrameRect(Region.Owner);
        const float Scale = GetEffectiveFrameScale(FindFrame(Region.Owner));
        if (Region.Widget.IsValid())
            ApplyElementAnchors(Region.Widget.Get(), nullptr, Region.Anchors, Region.Width, Region.Height,
                Region.bAllPoints, Owner.W / Scale, Owner.H / Scale);
    }
    // WoW orders frames globally. UMG nests panels, so anonymous grouping
    // canvases must carry their highest descendant level above sibling bars.
    TMap<int64, int32> GroupLevels;
    for (int32 I=Handles.Num()-1; I>=0; --I)
    {
        const int64 Handle=Handles[I]; const auto& Entry=Frames[Handle];
        const int32 Level=FMath::Max(Entry.Def.FrameLevel, GroupLevels.FindRef(Handle));
        if (Entry.Def.bAnonymous && Entry.Widget.IsValid())
            if (auto* Slot=Cast<UCanvasPanelSlot>(Entry.Widget->Slot))
                Slot->SetZOrder(static_cast<int32>(Entry.Def.Strata)*1000+Level);
        const auto* Parent=Frames.Find(Entry.ParentHandle);
        if (Parent && Parent->Def.bAnonymous)
            GroupLevels.FindOrAdd(Entry.ParentHandle)=FMath::Max(GroupLevels.FindRef(Entry.ParentHandle),Level);
    }
    bLayoutDirty = false;
}

void FWowFrameManager::ApplyElementAnchors(UWidget* Widget, UCanvasPanel* Parent,
    const TArray<FWowAnchor>& Anchors, float Width, float Height, bool bAllPoints,
    float ParentWidth, float ParentHeight)
{
    auto* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
    if (!Slot) return;
    if (!Parent) Parent = Cast<UCanvasPanel>(Widget->GetParent());
    const FString Owner = CanvasNames.FindRef(Parent);
    const auto OwnerRect = GetFrameRect(Owner);
    const float OwnerScale = FindFrame(Owner) >= 0 ? GetEffectiveFrameScale(FindFrame(Owner))
        : (ParentWidth > 0 && OwnerRect.W > 0 ? OwnerRect.W / ParentWidth : 1.f);
    Slot->SetAutoSize(false);
    Slot->SetAlignment(FVector2D::ZeroVector);
    if (bAllPoints)
    {
        Slot->SetAnchors(FAnchors(0,0,1,1)); Slot->SetOffsets(FMargin(0));
        return;
    }
    Slot->SetAnchors(FAnchors(0,0));
    bool bAutoSize = Cast<UTextBlock>(Widget) && (Width <= 0 || Height <= 0);
    if (const auto* Image = Cast<UImage>(Widget))
    {
        if (Width <= 0) Width = Image->GetBrush().ImageSize.X;
        if (Height <= 0) Height = Image->GetBrush().ImageSize.Y;
    }
    const auto Target = [&](const FWowAnchor& A)
    {
        const auto Relative = A.RelativeTo.IsEmpty() ? OwnerRect : GetFrameRect(A.RelativeTo);
        return (FVector2D(Relative.X - OwnerRect.X, Relative.Y - OwnerRect.Y)
            + GetAnchorPointOffset(A.RelativePoint, Relative.W, Relative.H)) / OwnerScale
            + FVector2D(A.OffsetX, -A.OffsetY);
    };
    FVector2D Point(ParentWidth/2,ParentHeight/2), Fraction(.5,.5);
    if (!Anchors.IsEmpty())
    {
        Point=Target(Anchors[0]); Fraction=GetAnchorPointOffset(Anchors[0].Point,1,1);
        for (int32 I=1; I<Anchors.Num(); ++I)
        {
            const auto Other=GetAnchorPointOffset(Anchors[I].Point,1,1), Q=Target(Anchors[I]);
            if (!FMath::IsNearlyEqual(Other.X,Fraction.X)) { Width=(Q.X-Point.X)/(Other.X-Fraction.X); bAutoSize=false; }
            if (!FMath::IsNearlyEqual(Other.Y,Fraction.Y)) { Height=(Q.Y-Point.Y)/(Other.Y-Fraction.Y); bAutoSize=false; }
        }
    }
    if (bAutoSize)
    {
        Slot->SetAutoSize(true); Slot->SetAlignment(Fraction); Slot->SetPosition(Point*UIScale);
    }
    else
    {
        const FVector2D Size(FMath::Max(0.f,Width), FMath::Max(0.f,Height));
        Slot->SetPosition((Point-Fraction*Size)*UIScale); Slot->SetSize(Size*UIScale);
    }
}

void FWowFrameManager::UpdateViewportLayout()
{
    FVector2D Viewport = FVector2D::ZeroVector;
    if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Viewport);
    if (Viewport.X >= 100 && Viewport.Y >= 100 && !Viewport.Equals(LayoutViewport))
    {
        LayoutViewport = Viewport;
        RawViewportScale = Viewport.Y / 768.f;
        const float Dpi = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(Viewport.X, Viewport.Y));
        UIScale = RawViewportScale / FMath::Max(Dpi, 0.01f);
        FrameRects.Add(TEXT("UIParent"), FFrameRect(0, 0, Viewport.X / RawViewportScale, 768));
        bLayoutDirty = true;
    }
    if (bLayoutDirty) RefreshLayout();
}
