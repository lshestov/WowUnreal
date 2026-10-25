#include "WowTextBlock.h"
#include "WowBackdropWidget.h"
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

bool FWowFrameManager::IsFrameHierarchyShown(int64 Handle) const
{
    if(Handle<0 || !Frames.Contains(Handle)) return false;
    for(int32 Depth=0; Handle>=0 && Depth<64; ++Depth)
    {
        const auto* Entry=Frames.Find(Handle);
        if(!Entry) return Depth>0;
        if(Entry->Def.bHidden) return false;
        Handle=Entry->ParentHandle;
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
    int32 FirstIndex=0; double Priority=-1;
    for(int32 I=0;I<Def.Anchors.Num();++I) { const auto P=GetAnchorPointOffset(Def.Anchors[I].Point,1,1); const double Score=FMath::Abs(P.X-.5f)+FMath::Abs(P.Y-.5f); if(Score>Priority) { FirstIndex=I; Priority=Score; } }
    const auto& First = Def.Anchors[FirstIndex];
    const FVector2D P = Target(First), Fraction = GetAnchorPointOffset(First.Point, 1, 1);
    // Each anchor constrains an axis. LEFT/RIGHT stretches width while preserving
    // explicit height; TOPLEFT/BOTTOMRIGHT constrains both axes.
    for (int32 I = 0; I < Def.Anchors.Num(); ++I)
    {
        const FVector2D Q = Target(Def.Anchors[I]);
        const FVector2D Other = GetAnchorPointOffset(Def.Anchors[I].Point, 1, 1);
        if (FMath::Abs(Fraction.X-Other.X)>.99f) Rect.W = (Q.X - P.X) / (Other.X - Fraction.X);
        if (FMath::Abs(Fraction.Y-Other.Y)>.99f) Rect.H = (Q.Y - P.Y) / (Other.Y - Fraction.Y);
    }
    Rect.W = FMath::Max(0.f, Rect.W); Rect.H = FMath::Max(0.f, Rect.H);
    Rect.X = P.X - Fraction.X * Rect.W;
    Rect.Y = P.Y - Fraction.Y * Rect.H;
    if(Def.bClampedToScreen)
    {
        const auto Screen=GetFrameRect(TEXT("UIParent"));
        const auto Insets=Def.ClampInsets*Scale;
        Rect.X=FMath::Clamp(Rect.X,float(-Insets.X),FMath::Max(float(-Insets.X),Screen.W-Rect.W+float(Insets.Y)));
        Rect.Y=FMath::Clamp(Rect.Y,float(-Insets.Z),FMath::Max(float(-Insets.Z),Screen.H-Rect.H+float(Insets.W)));
    }
    const int64 ParentHandle=FindFrame(Def.Parent);
    if(ScrollChildHandles.FindRef(ParentHandle)==FindFrame(Def.Name) && ScrollChildHandles.Contains(ParentHandle))
    {
        Rect.X-=ScrollHorizontalOffsets.FindRef(ParentHandle)*Scale;
        Rect.Y-=ScrollVerticalOffsets.FindRef(ParentHandle)*Scale;
    }
    return Rect;
}

FWowFrameManager::FFrameRect FWowFrameManager::GetFrameRect(const FString& Name)
{
    const FString Key = Name.IsEmpty() ? TEXT("UIParent") : Name;
    if (const auto* Cached = FrameRects.Find(Key)) return *Cached;
    if (Key == TEXT("UIParent"))
    {
        if(LayoutViewport.X>=100) return FFrameRect(0,0,LayoutViewport.X/RawViewportScale,768);
        if(const auto* Root=Frames.Find(FindFrame(Key)); Root && Root->Def.Width>0 && Root->Def.Height>0) return FFrameRect(0,0,Root->Def.Width,Root->Def.Height);
        return FFrameRect(0,0,1024,768);
    }
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
        // Frames can anchor to paragraphs whose wrapped height changed in this
        // callback. Resolve those regions from their definitions before reading
        // their slots, rather than using geometry from the previous frame.
        bool Anchored=true;
        if(Widget) for(const auto& Layout:RegionLayouts) if(Layout.Widget.Get()==Widget)
        {
            if(Layout.bUnanchored) { Anchored=false; break; }
            const float Scale=GetEffectiveFrameScale(FindFrame(*Owner));
            ApplyElementAnchors(Widget,nullptr,Layout.Anchors,Layout.Width,Layout.Height,Layout.bAllPoints,Parent.W/Scale,Parent.H/Scale);
            break;
        }
        const auto* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
        if (Slot && Anchored)
        {
            if(Slot->GetAutoSize()) Widget->ForceLayoutPrepass();
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

FVector4 FWowFrameManager::GetRegionRect(const FString& Name)
{
    const auto R=GetFrameRect(Name); return FVector4(R.X,R.Y,R.W,R.H);
}

FVector2D FWowFrameManager::GetRegionSize(const FString& Name)
{
    UWidget* Widget=GetNamedTexture(Name); if(!Widget) Widget=GetNamedFontString(Name);
    for(const auto& Layout:RegionLayouts) if(Widget && Layout.Widget.Get()==Widget && Layout.bUnanchored)
    {
        Widget->TakeWidget(); Widget->ForceLayoutPrepass(); auto Size=Widget->GetDesiredSize()/FMath::Max(UIScale,.001f);
        if(const auto* Image=Cast<UImage>(Widget)) Size=Image->GetBrush().DrawAs==ESlateBrushDrawType::NoDrawType?FVector2D::ZeroVector:FVector2D(Image->GetBrush().ImageSize);
        return FVector2D(Layout.Width>0?Layout.Width:Size.X,Layout.Height>0?Layout.Height:Size.Y);
    }
    // GetFrameRect measures the requested region and its anchor dependencies.
    // Rebuilding thousands of unrelated frames for every FontString query
    // makes FrameXML bootstrap and panel OnShow handlers stall for seconds.
    const auto R=GetFrameRect(Name);
    const float Scale=GetEffectiveFrameScale(FindFrame(RegionOwners.FindRef(Name)));
    return FVector2D(R.W,R.H)/FMath::Max(Scale,.001f);
}

void FWowFrameManager::SetRegionSize(const FString& Name,float Width,float Height)
{
    UWidget* Widget=GetNamedTexture(Name); if(!Widget) Widget=GetNamedFontString(Name);
    for(auto& Layout:RegionLayouts) if(Layout.Widget.Get()==Widget && Widget)
    {
        if(Width>=0) Layout.Width=Width; if(Height>=0) Layout.Height=Height;
        FrameRects.Empty(); bLayoutDirty=true; return;
    }
}

void FWowFrameManager::SetRegionPoint(const FString& Name,const FWowAnchor& Anchor,bool Clear)
{
    UWidget* Widget=GetNamedTexture(Name); if(!Widget) Widget=GetNamedFontString(Name);
    for(auto& Layout:RegionLayouts) if(Layout.Widget.Get()==Widget && Widget)
    {
        Layout.bAllPoints=false;
        Layout.bUnanchored=Clear;
        if(Clear) UnanchoredRegions.Add(Name); else UnanchoredRegions.Remove(Name);
        if(Widget->GetVisibility()!=ESlateVisibility::Collapsed) Widget->SetVisibility(Clear?ESlateVisibility::Hidden:ESlateVisibility::SelfHitTestInvisible);
        if(Clear) Layout.Anchors.Reset();
        else
        {
            if(auto* Existing=Layout.Anchors.FindByPredicate([&](const auto& A){return A.Point==Anchor.Point;})) *Existing=Anchor;
            else Layout.Anchors.Add(Anchor);
        }
        FrameRects.Empty(); bLayoutDirty=true; return;
    }
}

TArray<FWowAnchor> FWowFrameManager::GetRegionPoints(const FString& Name) const
{
    UWidget* Widget=GetNamedTexture(Name); if(!Widget) Widget=GetNamedFontString(Name);
    for(const auto& Layout:RegionLayouts) if(Widget && Layout.Widget.Get()==Widget)
    {
        if(Layout.bAllPoints) return {{EWowAnchorPoint::TOPLEFT,Layout.Owner,EWowAnchorPoint::TOPLEFT,0,0},{EWowAnchorPoint::BOTTOMRIGHT,Layout.Owner,EWowAnchorPoint::BOTTOMRIGHT,0,0}};
        return Layout.Anchors;
    }
    return {};
}
bool FWowFrameManager::IsRegionAnchored(const FString& Name) const
{
    return !UnanchoredRegions.Contains(Name);
}

bool FWowFrameManager::StartFrameMovement(int64 Handle,const FString& Point)
{
    const auto* Def=GetFrameDef(Handle);
    if(!Def || (Point.IsEmpty()?!Def->bMovable:!Def->bResizable)) return false;
    if(MovingFrame>=0) return true;
    MovingFrame=Handle; SizingPoint=Point; MovePointerOrigin=LastPointerPosition;
    MoveRect=GetLayoutRect(Handle);
    return true;
}

void FWowFrameManager::StopFrameMovement(int64 Handle)
{
    if(MovingFrame!=Handle) return;
    UpdateFrameMovement(); MovingFrame=-1; SizingPoint.Reset();
}

void FWowFrameManager::UpdateFrameMovement()
{
    if(MovingFrame<0) return;
    auto* Def=GetMutableFrameDef(MovingFrame);
    if(!Def || Def->bHidden) { MovingFrame=-1; return; }
    const auto Delta=(LastPointerPosition-MovePointerOrigin)/FMath::Max(RawViewportScale,.001f);
    const float Scale=GetEffectiveFrameScale(MovingFrame);
    double X=MoveRect.X,Y=MoveRect.Y,W=MoveRect.Z,H=MoveRect.W;
    if(SizingPoint.IsEmpty()) { X+=Delta.X; Y+=Delta.Y; }
    else
    {
        const bool Left=SizingPoint.Contains(TEXT("LEFT")),Right=SizingPoint.Contains(TEXT("RIGHT"));
        const bool Top=SizingPoint.Contains(TEXT("TOP")),Bottom=SizingPoint.Contains(TEXT("BOTTOM"));
        W=FMath::Clamp(W+(Right?Delta.X:Left?-Delta.X:0),Def->MinResize.X*Scale,Def->MaxResize.X*Scale);
        H=FMath::Clamp(H+(Bottom?Delta.Y:Top?-Delta.Y:0),Def->MinResize.Y*Scale,Def->MaxResize.Y*Scale);
        if(Left) X=MoveRect.X+MoveRect.Z-W;
        if(Top) Y=MoveRect.Y+MoveRect.W-H;
        SetFrameSize(MovingFrame,W/Scale,H/Scale);
    }
    FWowAnchor Anchor; Anchor.Point=Anchor.RelativePoint=EWowAnchorPoint::TOPLEFT;
    Anchor.RelativeTo=TEXT("UIParent"); Anchor.OffsetX=X/Scale; Anchor.OffsetY=-Y/Scale;
    SetFrameAnchors(MovingFrame,{Anchor});
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
    // CreateWidgetForFrame lays out the widget before registering its handle.
    const int64 Handle=FindFrame(Def.Name);
    if(GetWidgetForHandle(Handle)==Widget) UpdateFrameVisibility(Handle);
    else Widget->SetVisibility(Def.bHidden || (!Def.bSetAllPoints && Def.Anchors.IsEmpty())
        ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
    FrameRects.Add(Def.Name, Rect);
}

void FWowFrameManager::RefreshLayout()
{
    const auto Root = GetFrameRect(TEXT("UIParent"));
    TArray<int64> Handles;
    Frames.GetKeys(Handles); Handles.Sort();
    // Wrapped text is measured during layout. A second bounded pass places
    // frames anchored below it using the resulting paragraph height.
    for(int32 Pass=0;Pass<2;++Pass)
    {
    FrameRects.Empty(); FrameRects.Add(TEXT("UIParent"),Root);
    for (const auto Handle : Handles)
        if (const auto* Entry = Frames.Find(Handle); Entry && Entry->Widget.IsValid())
            ApplyAnchors(Entry->Widget.Get(), Entry->Def);
    for (const auto& Region : RegionLayouts)
    {
        if(const auto* OwnerDef=GetFrameDef(FindFrame(Region.Owner)); OwnerDef && OwnerDef->Type==EWowFrameType::GameTooltip) continue;
        const auto Owner = GetFrameRect(Region.Owner);
        const float Scale = GetEffectiveFrameScale(FindFrame(Region.Owner));
        if (Region.Widget.IsValid())
        {
            if(Region.bUnanchored) { if(Region.Widget->GetVisibility()!=ESlateVisibility::Collapsed) Region.Widget->SetVisibility(ESlateVisibility::Hidden); continue; }
            ApplyElementAnchors(Region.Widget.Get(), nullptr, Region.Anchors, Region.Width, Region.Height,
                Region.bAllPoints, Owner.W / Scale, Owner.H / Scale);
        }
    }
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
    for(const auto& Pair:BackdropWidgets) if(Pair.Value.IsValid()) Pair.Value->SetUnitScale(UIScale);
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
        const bool Empty=Image->GetBrush().DrawAs==ESlateBrushDrawType::NoDrawType;
        if (Width <= 0) Width = Empty?0:Image->GetBrush().ImageSize.X;
        if (Height <= 0) Height = Empty?0:Image->GetBrush().ImageSize.Y;
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
        int32 FirstIndex=0; double Priority=-1;
        for(int32 I=0;I<Anchors.Num();++I) { const auto P=GetAnchorPointOffset(Anchors[I].Point,1,1); const double Score=FMath::Abs(P.X-.5f)+FMath::Abs(P.Y-.5f); if(Score>Priority) { FirstIndex=I; Priority=Score; } }
        Point=Target(Anchors[FirstIndex]); Fraction=GetAnchorPointOffset(Anchors[FirstIndex].Point,1,1);
        for (int32 I=0; I<Anchors.Num(); ++I)
        {
            const auto Other=GetAnchorPointOffset(Anchors[I].Point,1,1), Q=Target(Anchors[I]);
            if (FMath::Abs(Other.X-Fraction.X)>.99f) { Width=(Q.X-Point.X)/(Other.X-Fraction.X); bAutoSize=false; }
            if (FMath::Abs(Other.Y-Fraction.Y)>.99f) { Height=(Q.Y-Point.Y)/(Other.Y-Fraction.Y); bAutoSize=false; }
        }
    }
    if(auto* Text=Cast<UTextBlock>(Widget))
    {
        // A horizontal text constraint still needs its natural wrapped height.
        // Treating an unspecified height as zero erased quest descriptions.
        const auto* Label=Cast<UWowTextBlock>(Text);
        Text->SetWrapTextAt(Width>0 && (!Label || Label->CanWordWrap())?Width*UIScale:0);
        if(auto* Rich=Cast<UWowTextBlock>(Text)) Rich->RefreshRichStyle(UIScale);
        Text->TakeWidget()->Invalidate(EInvalidateWidgetReason::Prepass);
        Text->ForceLayoutPrepass(); const auto Desired=Text->GetDesiredSize()/UIScale;
        if(Width<=0)
        {
            // Fractional DPI/parent scales can clip the final glyph at its
            // advance boundary. Reserve a physical pixel for glyph overhang.
            const float Pixel=1/FMath::Max(.001f,RawViewportScale*OwnerScale);
            Width=FMath::CeilToFloat(Desired.X/Pixel)*Pixel+Pixel;
        }
        // WoW permits glyphs to extend beyond a FontString's nominal height.
        // Give Slate its full line box so short XML labels keep their baseline.
        Height=FMath::Max(Height,float(Desired.Y));
        bAutoSize=false;
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
        if(auto* Root=Frames.Find(FindFrame(TEXT("UIParent")))) { Root->Def.Width=Viewport.X/RawViewportScale; Root->Def.Height=768; }
        bLayoutDirty = true;
    }
    if (bLayoutDirty) RefreshLayout();
    for(const auto& Text:RichTextWidgets) if(Text.IsValid() && Text->Markup().Formatted) Text->RefreshRichStyle(UIScale);
}
