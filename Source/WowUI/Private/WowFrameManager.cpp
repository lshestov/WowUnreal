#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowFontManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Slate/SlateBrushAsset.h"
#include "Mpq/MpqManager.h"
#include "WowAssetCache.h"
#include "Formats/BlpParser.h"
#include "WowTextureFactory.h"
#include "Styling/CoreStyle.h"
#include "Slate/WidgetTransform.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/UserInterfaceSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowUiText.h"
DEFINE_LOG_CATEGORY_STATIC(LogWowFrame, Log, All);

namespace
{
FString BuildParentFrameAliasName(const FString& ParentName, const FString& ChildName)
{
	if (!ParentName.EndsWith(TEXT("Frame")) || ChildName.StartsWith(ParentName) || ParentName.Len() <= 5)
	{
		return FString();
	}

	const FString ParentBase = ParentName.LeftChop(5);
	if (ParentBase.IsEmpty() || !ChildName.StartsWith(ParentBase))
	{
		return FString();
	}

	const FString Suffix = ChildName.Mid(ParentBase.Len());
	if (Suffix.IsEmpty())
	{
		return FString();
	}

	return ParentName + Suffix;
}

FString NormalizeClickRegistrationToken(FString Token)
{
	Token.TrimStartAndEndInline();
	Token.ReplaceInline(TEXT(" "), TEXT(""));
	Token = Token.ToUpper();
	return Token;
}

FString BuildClickRegistrationKey(const FString& Button, bool bMouseDown)
{
	return NormalizeClickRegistrationToken(Button + (bMouseDown ? TEXT("Down") : TEXT("Up")));
}

bool HasRegisteredClickToken(const TSet<FString>& RegisteredClicks, const FString& Button, bool bMouseDown)
{
	const FString SpecificKey = BuildClickRegistrationKey(Button, bMouseDown);
	const FString AnyKey = bMouseDown ? TEXT("ANYDOWN") : TEXT("ANYUP");
	return RegisteredClicks.Contains(SpecificKey) || RegisteredClicks.Contains(AnyKey);
}

bool UsesDefaultButtonClickRegistration(const FWowFrameDef& Def, const FString& Button, bool bMouseDown)
{
	return !bMouseDown
		&& Button.Equals(TEXT("LeftButton"), ESearchCase::IgnoreCase)
		&& (Def.Type == EWowFrameType::Button || Def.Type == EWowFrameType::CheckButton);
}
}

FWowFrameManager::FWowFrameManager()
{
}

void FWowFrameManager::Initialize(UCanvasPanel* InRootCanvas)
{
	RootCanvas = InRootCanvas;

	if (InRootCanvas)
	{
		// WoW's base UI height (the reference for scaling)
		const float WowBaseHeight = 768.0f;

		// Get actual viewport size
		FVector2D ViewportSize = FVector2D(1024.0f, 768.0f); // Default fallback

		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->GetViewportSize(ViewportSize);
		}

		// Guard against viewport not ready (returns 0,0 early in startup)
		if (ViewportSize.X < 100.f || ViewportSize.Y < 100.f)
		{
			ViewportSize = FVector2D(1920.0f, 1080.0f); // Sensible default
			UE_LOG(LogWowFrame, Warning, TEXT("Viewport size not available yet, defaulting to %.0fx%.0f"), ViewportSize.X, ViewportSize.Y);
		}

		// UMG applies its own DPI scaling curve (Project Settings → Engine → User Interface).
		// At 720p the default curve yields ~0.67, so a widget at pixel 670 renders at
		// 670×0.67 ≈ 450 — nowhere near the bottom. We must divide our scale by the
		// UMG viewport scale so positions come out correct after DPI scaling.
		// Get the effective DPI scale that UMG applies to viewport widgets.
		// UGameViewportClient::GetDPIScale() may return 1.0 even when UMG
		// is applying a scale internally via the DPI curve.
		float ViewportDPIScale = 1.0f;
		if (GEngine && GEngine->GameViewport)
		{
			// Try multiple APIs to find the actual scale
			float Method1 = GEngine->GameViewport->GetDPIScale();

			// The viewport overlay uses SDPIScaler whose scale comes from
			// UUserInterfaceSettings::GetDPIScaleBasedOnSize
			FIntPoint ViewSize((int32)ViewportSize.X, (int32)ViewportSize.Y);
			float Method2 = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(ViewSize);

			ViewportDPIScale = Method2; // This is what actually gets applied
			if (ViewportDPIScale <= 0.f) ViewportDPIScale = 1.0f;

			UE_LOG(LogWowFrame, Verbose, TEXT("DPI detection: GetDPIScale=%.3f, UISettings.GetDPIScaleBasedOnSize=%.3f, using=%.3f"),
				Method1, Method2, ViewportDPIScale);
		}

		// WoW scales by screen HEIGHT only — UIParent stretches to fill the full
		// viewport width.  This means on a 1920×1080 screen the UI scale is
		// 1080/768 = 1.40625 and UIParent is 1365×768 in WoW-coordinate units.
		// Divide by DPI scale so that WoW-coord → pixel conversion accounts for
		// the additional DPI transform UMG applies.
		RawViewportScale = ViewportSize.Y / WowBaseHeight;
		UIScale = RawViewportScale / ViewportDPIScale;

		// UIParent dimensions in WoW coordinate units (covers the full viewport)
		// UIScale already accounts for DPI, so ViewportSize / UIScale gives correct WoW coords.
		float UIParentW = ViewportSize.X / (ViewportSize.Y / WowBaseHeight); // Use raw viewport ratio
		float UIParentH = WowBaseHeight;

		UE_LOG(LogWowFrame, Verbose, TEXT("UI Scale: Viewport (%.0fx%.0f) -> UIParent %.1fx%.1f, scale %.3f"),
			ViewportSize.X, ViewportSize.Y, UIParentW, UIParentH, UIScale);

		// Initialize UIParent as a virtual frame that represents the screen
		// UIParent doesn't get a widget - it's just a coordinate reference point
		FWowFrameDef UIParentDef;
		UIParentDef.Name = TEXT("UIParent");
		UIParentDef.Type = EWowFrameType::Frame;
		UIParentDef.Width = UIParentW;   // Full viewport width in WoW units
		UIParentDef.Height = UIParentH;  // Always 768 WoW units tall
		UIParentDef.bVirtual = true; // Don't create a widget for this
		UIParentDef.Strata = EWowFrameStrata::MEDIUM;
		UIParentDef.FrameLevel = 0;

		// Register UIParent as a virtual frame and store its rect
		RegisterTemplate(TEXT("UIParent"), UIParentDef);

		// Store the rect so anchor lookups use the correct full-screen dimensions
		FrameRects.Add(TEXT("UIParent"), FFrameRect(0.0f, 0.0f, UIParentW, UIParentH));

		// Create a handle for UIParent but without a widget
		int64 UIParentHandle = NextHandle++;
		FFrameEntry UIParentEntry;
		UIParentEntry.Def = UIParentDef;
		UIParentEntry.Widget = nullptr; // No widget for virtual frames
		UIParentEntry.ParentHandle = -1;

		Frames.Add(UIParentHandle, MoveTemp(UIParentEntry));
		NameToHandle.Add(TEXT("UIParent"), UIParentHandle);

		// Check DPI / application scale — on Retina displays, Slate operates
		// in physical pixels while GetViewportSize returns logical pixels.
		float DPIScale = 1.0f;
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWindow())
		{
			DPIScale = GEngine->GameViewport->GetWindow()->GetNativeWindow()->GetDPIScaleFactor();
		}

		UE_LOG(LogWowFrame, Verbose, TEXT("Initialized UIParent virtual frame (%.1fx%.1f WoW coordinates, viewport %.0fx%.0f, UIScale %.3f, DPIScale=%.3f, viewportDPI=%.3f)"),
			UIParentW, UIParentH, ViewportSize.X, ViewportSize.Y, UIScale, DPIScale, ViewportDPIScale);
	}

	// Register built-in virtual templates that WoW defines in Lua but not in XML.
	// These templates are referenced by inherits="..." but their Frame definitions
	// aren't in the MPQ's XML files (they're defined implicitly by FrameXML Lua code).
	{
		// FadingFrame template — used by ZoneTextFrame, SubZoneTextFrame etc.
		// Frames inheriting this should start hidden; OnUpdate runs FadingFrame_OnUpdate.
		FWowFrameDef FadingFrameDef;
		FadingFrameDef.Name = TEXT("FadingFrame");
		FadingFrameDef.Type = EWowFrameType::Frame;
		FadingFrameDef.bVirtual = true;
		FadingFrameDef.bHidden = true;
		FadingFrameDef.Width = 128.0f;
		FadingFrameDef.Height = 128.0f;
		FWowScriptHandler FadingOnLoad;
		FadingOnLoad.Event = TEXT("OnLoad");
		FadingOnLoad.Code = TEXT("FadingFrame_OnLoad(self);");
		FadingFrameDef.Scripts.Add(FadingOnLoad);
		FWowScriptHandler FadingOnUpdate;
		FadingOnUpdate.Event = TEXT("OnUpdate");
		FadingOnUpdate.Code = TEXT("FadingFrame_OnUpdate(self, elapsed);");
		FadingFrameDef.Scripts.Add(FadingOnUpdate);
		RegisterTemplate(TEXT("FadingFrame"), FadingFrameDef);
	}

	UE_LOG(LogWowFrame, Verbose, TEXT("Frame manager initialized - WoW anchor positioning system ready"));
}

void FWowFrameManager::RegisterTemplate(const FString& Name, const FWowFrameDef& Def)
{
	Templates.Add(Name, Def);
	UE_LOG(LogWowFrame, Verbose, TEXT("Registered template: %s"), *Name);
}

int64 FWowFrameManager::FindFrame(const FString& Name) const
{
	const int64* Handle = NameToHandle.Find(Name);
	return Handle ? *Handle : -1;
}

FString FWowFrameManager::GetFrameName(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? Entry->Def.Name : FString();
}

const FWowFrameDef* FWowFrameManager::GetFrameDef(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? &Entry->Def : nullptr;
}

void FWowFrameManager::SetFrameVisible(int64 Handle, bool bVisible)
{
	if(!bVisible && FSlateApplication::IsInitialized())
	{
		for(const auto& Edit:EditBoxWidgets) if(Edit.Value.IsValid() && Edit.Value->HasKeyboardFocus())
		{
			for(int64 Parent=Edit.Key; Parent>=0; Parent=GetParentHandle(Parent)) if(Parent==Handle) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); break; }
		}
	}
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;
	if(Entry->Def.bHidden==!bVisible) return;
	TArray<int64> Descendants={Handle};
	for(int32 I=0; I<Descendants.Num(); ++I)
		for(const auto& Pair:Frames) if(Pair.Value.ParentHandle==Descendants[I]) Descendants.Add(Pair.Key);
	TMap<int64,bool> WasDrawn;
	for(const int64 Child:Descendants) WasDrawn.Add(Child,IsFrameHierarchyShown(Child));
	Entry->Def.bHidden = !bVisible;

	if (Entry->Widget.IsValid())
	{
		if (bVisible && (Entry->Def.bSetAllPoints || !Entry->Def.Anchors.IsEmpty()))
		{
			Entry->Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			Entry->Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Propagate visibility recursively to ALL descendant frames
	TArray<int64> ToPropagate;
	ToPropagate.Add(Handle);
	while (ToPropagate.Num() > 0)
	{
		int64 Current = ToPropagate.Pop();
		for (auto& Pair : Frames)
		{
			if (Pair.Value.ParentHandle == Current && Pair.Value.Widget.IsValid())
			{
				if (!bVisible)
				{
					Pair.Value.Widget->SetVisibility(ESlateVisibility::Collapsed);
				}
				else if (!Pair.Value.Def.bHidden)
				{
					Pair.Value.Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				}
				ToPropagate.Add(Pair.Key);
			}
		}
	}
	if(EventSystem) for(const int64 Child:Descendants)
	{
		const bool Drawn=IsFrameHierarchyShown(Child);
		if(Drawn!=WasDrawn[Child]) EventSystem->RunFrameScript(Child,Drawn?TEXT("OnShow"):TEXT("OnHide"));
	}
}

bool FWowFrameManager::IsFrameVisible(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? !Entry->Def.bHidden : false;
}

FWowFrameDef* FWowFrameManager::GetMutableFrameDef(int64 Handle)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? &Entry->Def : nullptr;
}

bool FWowFrameManager::ReparentFrame(int64 Handle,int64 ParentHandle)
{
    auto* Entry=Frames.Find(Handle); if(!Entry || Handle==ParentHandle) return false;
    for(int64 Ancestor=ParentHandle;Ancestor>=0;Ancestor=GetParentHandle(Ancestor)) if(Ancestor==Handle) return false;
    if(Entry->ParentHandle==ParentHandle) return true;
    auto* Canvas=ParentHandle<0?RootCanvas.Get():Cast<UCanvasPanel>(GetWidgetForHandle(ParentHandle));
    if(!Canvas && ParentHandle==FindFrame(TEXT("UIParent"))) Canvas=RootCanvas.Get();
    if(!Canvas || !Entry->Widget.IsValid()) return false;
    Entry->Widget->RemoveFromParent(); auto* Slot=Canvas->AddChildToCanvas(Entry->Widget.Get());
    if(Slot) Slot->SetZOrder(int32(Entry->Def.Strata)*1000+Entry->Def.FrameLevel);
    Entry->ParentHandle=ParentHandle; Entry->Def.Parent=ParentHandle<0?FString():GetFrameName(ParentHandle);
    if(const auto* Parent=Frames.Find(ParentHandle))
    {
        if(Entry->Def.FrameLevel<=Parent->Def.FrameLevel) SetFrameLevel(Handle,Parent->Def.FrameLevel+1);
        if(Entry->Def.Strata<Parent->Def.Strata) SetFrameStrata(Handle,Parent->Def.Strata);
    }
    FrameRects.Reset(); bLayoutDirty=true; RefreshLayout(); return true;
}

bool FWowFrameManager::ReparentRegion(const FString& Name,int64 ParentHandle)
{
    auto* Canvas=Cast<UCanvasPanel>(GetWidgetForHandle(ParentHandle));
    if(!Canvas && ParentHandle==FindFrame(TEXT("UIParent"))) Canvas=RootCanvas.Get();
    UWidget* Widget=GetNamedFontString(Name); if(!Widget) Widget=GetNamedTexture(Name);
    if(!Canvas || !Widget) return false;
    if(Widget->GetParent()!=Canvas)
    {
        const auto* Previous=Cast<UCanvasPanelSlot>(Widget->Slot); const int32 Z=Previous?Previous->GetZOrder():10;
        Widget->RemoveFromParent(); if(auto* Slot=Canvas->AddChildToCanvas(Widget)) Slot->SetZOrder(Z);
    }
    RegionOwners.Add(Name,GetFrameName(ParentHandle));
    for(auto& Layout:RegionLayouts) if(Layout.Widget.Get()==Widget) Layout.Owner=GetFrameName(ParentHandle);
    FrameRects.Reset(); bLayoutDirty=true; return true;
}

FString FWowFrameManager::CreateFontStringRegion(int64 Parent,const FString& Name,const FString& Inherits)
{
    auto* Canvas=Cast<UCanvasPanel>(GetWidgetForHandle(Parent)); if(!Canvas && Parent==FindFrame(TEXT("UIParent"))) Canvas=RootCanvas.Get();
    if(!Canvas) return {};
    FString Key=Name; if(Key.IsEmpty()) { int32 Id=RegionLayouts.Num(); do { Key=FString::Printf(TEXT("_DynamicFontString%d"),Id++); } while(NamedFontStringWidgets.Contains(Key)); }
    FWowFrameDef Def; Def.Name=GetFrameName(Parent); const auto Rect=GetLayoutRect(Parent); Def.Width=Rect.Z; Def.Height=Rect.W;
    FWowLayer Layer; Layer.Level=EWowDrawLayer::OVERLAY; FWowFontStringElement Text; Text.Name=Key; Text.Inherits=Inherits; Text.JustifyH=TEXT("LEFT"); Layer.FontStrings.Add(Text); Def.Layers.Add(Layer);
    CreateLayerContent(Canvas,Def,Parent); return Key;
}

void FWowFrameManager::SetFrameSize(int64 Handle, float W, float H)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;

	if (Entry->Def.Name.Contains(TEXT("MainMenu")))
	{
		UE_LOG(LogWowFrame, Verbose, TEXT("SetFrameSize %s: %.0fx%.0f -> %.0fx%.0f (anchors=%d)"),
			*Entry->Def.Name, Entry->Def.Width, Entry->Def.Height, W, H, Entry->Def.Anchors.Num());
	}

	if (FMath::IsNearlyEqual(Entry->Def.Width, W) && FMath::IsNearlyEqual(Entry->Def.Height, H)) return;
	bLayoutDirty = true;
	Entry->Def.Width = W;
	Entry->Def.Height = H;

	// Clear cached frame rect for this frame and ALL descendants.
	// When a parent moves/resizes, all child absolute positions become stale.
	InvalidateFrameRectsRecursive(Handle);

	if (Entry->Widget.IsValid())
	{
		// Re-apply anchors to recalculate position with new size
		ApplyAnchors(Entry->Widget.Get(), Entry->Def);
	}
}

void FWowFrameManager::SetFrameAnchors(int64 Handle, const TArray<FWowAnchor>& NewAnchors)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;
	if (!Entry->Def.bSetAllPoints && Entry->Def.Anchors.Num() == NewAnchors.Num())
	{
		bool bSame = true;
		for (int32 I = 0; I < NewAnchors.Num(); ++I)
		{
			const auto& A = Entry->Def.Anchors[I]; const auto& B = NewAnchors[I];
			bSame &= A.Point == B.Point && A.RelativePoint == B.RelativePoint && A.RelativeTo == B.RelativeTo
				&& FMath::IsNearlyEqual(A.OffsetX, B.OffsetX) && FMath::IsNearlyEqual(A.OffsetY, B.OffsetY);
		}
		if (bSame) return;
	}

	Entry->Def.Anchors = NewAnchors;
	bLayoutDirty = true;
	Entry->Def.bSetAllPoints = false;

	// Clear cached frame rect for this frame and ALL descendants.
	InvalidateFrameRectsRecursive(Handle);

	if (Entry->Widget.IsValid())
	{
		ApplyAnchors(Entry->Widget.Get(), Entry->Def);
	}
}

void FWowFrameManager::ClearFrameAnchors(int64 Handle)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;

	Entry->Def.Anchors.Empty();
	Entry->Def.bSetAllPoints = false;
}

void FWowFrameManager::SetFrameAlpha(int64 Handle, float Alpha)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;

	if (Entry->Widget.IsValid())
	{
		Entry->Widget->SetRenderOpacity(Alpha);
	}
}

void FWowFrameManager::SetFrameStrata(int64 Handle, EWowFrameStrata Strata)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;

	Entry->Def.Strata = Strata;

	if (Entry->Widget.IsValid())
	{
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Entry->Widget->Slot))
		{
			Slot->SetZOrder(static_cast<int32>(Strata) * 1000 + Entry->Def.FrameLevel);
		}
	}
}

void FWowFrameManager::SetFrameLevel(int64 Handle, int32 Level)
{
	FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;
	const int32 Delta=Level-Entry->Def.FrameLevel; if(!Delta) return;
	TArray<int64> Descendants={Handle};
	for(int32 I=0;I<Descendants.Num();++I) for(const auto& Pair:Frames) if(Pair.Value.ParentHandle==Descendants[I]) Descendants.Add(Pair.Key);
	for(const int64 Child:Descendants)
	{
		auto& E=Frames[Child]; E.Def.FrameLevel=FMath::Max(0,E.Def.FrameLevel+Delta);
		if(E.Widget.IsValid()) if(auto* Slot=Cast<UCanvasPanelSlot>(E.Widget->Slot)) Slot->SetZOrder(int32(E.Def.Strata)*1000+E.Def.FrameLevel);
	}
}

int64 FWowFrameManager::GetParentHandle(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? Entry->ParentHandle : -1;
}

TArray<int64> FWowFrameManager::GetChildHandles(int64 Handle) const
{
	TArray<int64> Children;
	for (const auto& Pair : Frames)
	{
		if (Pair.Value.ParentHandle == Handle)
		{
			Children.Add(Pair.Key);
		}
	}
	return Children;
}

UWidget* FWowFrameManager::GetWidgetForHandle(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry && Entry->Widget.IsValid() ? Entry->Widget.Get() : nullptr;
}

UButton* FWowFrameManager::GetButtonWidget(int64 Handle) const
{
	const TWeakObjectPtr<UButton>* Found = ButtonWidgets.Find(Handle);
	return (Found && Found->IsValid()) ? Found->Get() : nullptr;
}

UEditableTextBox* FWowFrameManager::GetEditBoxWidget(int64 Handle) const
{
	const TWeakObjectPtr<UEditableTextBox>* Found = EditBoxWidgets.Find(Handle);
	return (Found && Found->IsValid()) ? Found->Get() : nullptr;
}

void FWowFrameManager::SetSliderValue(int64 Handle, float Value)
{
	if (auto* Slider = GetSliderWidget(Handle))
	{
		Slider->SetValue(Value);
		SliderLastValue.Add(Handle, Slider->GetValue());
	}
}

USlider* FWowFrameManager::GetSliderWidget(int64 Handle) const
{
	const TWeakObjectPtr<USlider>* Found = SliderWidgets.Find(Handle);
	return (Found && Found->IsValid()) ? Found->Get() : nullptr;
}

UTextBlock* FWowFrameManager::GetPrimaryTextWidget(int64 Handle) const
{
	const TWeakObjectPtr<UTextBlock>* Found = PrimaryTextWidgets.Find(Handle);
	return (Found && Found->IsValid()) ? Found->Get() : nullptr;
}

UTextBlock* FWowFrameManager::GetNamedFontString(const FString& Name) const
{
	const TWeakObjectPtr<UTextBlock>* Found = NamedFontStringWidgets.Find(Name);
	return (Found && Found->IsValid()) ? Found->Get() : nullptr;
}

void FWowFrameManager::SetFrameMouseEnabled(int64 Handle, bool bEnabled)
{
	if (FFrameEntry* Entry = Frames.Find(Handle))
	{
		Entry->bMouseEnabled = bEnabled;
	}
}

void FWowFrameManager::SetFrameKeyboardEnabled(int64 Handle, bool bEnabled)
{
	if (FFrameEntry* Entry = Frames.Find(Handle))
	{
		Entry->bKeyboardEnabled = bEnabled;
	}
}

bool FWowFrameManager::DispatchKeyboard(const FString& Key,bool bDown)
{
    if(!EventSystem) return false;
    int64 Best=-1; int32 BestZ=MIN_int32;
    for(const auto& Pair:Frames)
    {
        const auto& E=Pair.Value; if(!E.bKeyboardEnabled || E.Def.Type==EWowFrameType::EditBox || !IsFrameDrawn(Pair.Key)) continue;
        if(!EventSystem->HasFrameScript(Pair.Key,bDown?TEXT("OnKeyDown"):TEXT("OnKeyUp"))) continue;
        const int32 Z=int32(E.Def.Strata)*1000+E.Def.FrameLevel;
        if(Z>BestZ || (Z==BestZ && Pair.Key>Best)) { Best=Pair.Key; BestZ=Z; }
    }
    return Best>=0 && EventSystem->RunFrameScript(Best,bDown?TEXT("OnKeyDown"):TEXT("OnKeyUp"),{Key});
}

void FWowFrameManager::SetFrameMouseWheelEnabled(int64 Handle, bool bEnabled)
{
	if (FFrameEntry* Entry = Frames.Find(Handle))
	{
		Entry->bMouseWheelEnabled = bEnabled;
	}
}

void FWowFrameManager::SetFrameRegisteredClicks(int64 Handle, const TArray<FString>& ClickTypes)
{
	if (FFrameEntry* Entry = Frames.Find(Handle))
	{
		Entry->bHasExplicitClickRegistration = true;
		Entry->RegisteredClicks.Reset();

		for (const FString& ClickType : ClickTypes)
		{
			const FString Normalized = NormalizeClickRegistrationToken(ClickType);
			if (!Normalized.IsEmpty())
			{
				Entry->RegisteredClicks.Add(Normalized);
			}
		}
	}
}

bool FWowFrameManager::IsFrameMouseEnabled(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? Entry->bMouseEnabled : false;
}

bool FWowFrameManager::IsFrameKeyboardEnabled(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? Entry->bKeyboardEnabled : false;
}

bool FWowFrameManager::IsFrameMouseWheelEnabled(int64 Handle) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	return Entry ? Entry->bMouseWheelEnabled : false;
}

bool FWowFrameManager::IsFrameClickRegistered(int64 Handle, const FString& Button, bool bMouseDown) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry)
	{
		return false;
	}

	if (Entry->bHasExplicitClickRegistration)
	{
		return HasRegisteredClickToken(Entry->RegisteredClicks, Button, bMouseDown);
	}

	return UsesDefaultButtonClickRegistration(Entry->Def, Button, bMouseDown);
}

// ── Template Inheritance ──────────────────────────────────────────────────────

void FWowFrameManager::MergeTemplate(FWowFrameDef& Target, const FWowFrameDef& Template)
{
	// Type — template sets the base type if target doesn't override
	// (typically the inheriting frame specifies its own type, so only fallback)

	// Size — use template size if target doesn't specify
	if (Target.Scale == 1.f) Target.Scale = Template.Scale;
	if (Target.Width == 0.f && Template.Width > 0.f) Target.Width = Template.Width;
	if (Target.Height == 0.f && Template.Height > 0.f) Target.Height = Template.Height;

	// Strata/level — use template if target is at default
	if (Target.Strata == EWowFrameStrata::MEDIUM && Template.Strata != EWowFrameStrata::MEDIUM)
		Target.Strata = Template.Strata;
	if (Target.FrameLevel == 0 && Template.FrameLevel != 0)
		Target.FrameLevel = Template.FrameLevel;

	// Hidden — template hidden overrides unless target explicitly sets visible
	if (Template.bHidden && !Target.bHidden) Target.bHidden = Template.bHidden;
	if (Target.ParentKey.IsEmpty()) Target.ParentKey = Template.ParentKey;

	// EnableMouse / EnableKeyboard — inherit from template
	if (Template.bEnableMouse && !Target.bEnableMouse) Target.bEnableMouse = true;
	if (Template.bEnableKeyboard && !Target.bEnableKeyboard) Target.bEnableKeyboard = true;

	// SetAllPoints
	if (Template.bSetAllPoints && !Target.bSetAllPoints) Target.bSetAllPoints = Template.bSetAllPoints;

	// Anchors — template anchors are used if target has none
	if (Target.Anchors.IsEmpty() && !Template.Anchors.IsEmpty())
	{
		Target.Anchors = Template.Anchors;
	}

	// Layers — merge: template layers first, then target layers (target overrides by draw layer level)
	for (const FWowLayer& TplLayer : Template.Layers)
	{
		bool bFound = false;
		for (FWowLayer& TargetLayer : Target.Layers)
		{
			if (TargetLayer.Level == TplLayer.Level)
			{
				// Target already has this layer — prepend template textures/fontstrings
				TArray<FWowTextureElement> Merged = TplLayer.Textures;
				Merged.Append(TargetLayer.Textures);
				TargetLayer.Textures = MoveTemp(Merged);

				TArray<FWowFontStringElement> MergedFS = TplLayer.FontStrings;
				MergedFS.Append(TargetLayer.FontStrings);
				TargetLayer.FontStrings = MoveTemp(MergedFS);
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			Target.Layers.Add(TplLayer);
		}
	}

	// Scripts — concatenate: template scripts run first
	TArray<FWowScriptHandler> MergedScripts = Template.Scripts;
	MergedScripts.Append(Target.Scripts);
	Target.Scripts = MoveTemp(MergedScripts);

	for (const FString& NamedObject : Template.NamedObjectGlobals)
	{
		Target.NamedObjectGlobals.AddUnique(NamedObject);
	}

	// Children — template children are prepended
	TArray<FWowFrameDef> MergedChildren = Template.Children;
	MergedChildren.Append(Target.Children);
	Target.Children = MoveTemp(MergedChildren);

	// Backdrop — use template if target doesn't have one
	if (!Target.Backdrop.IsSet() && Template.Backdrop.IsSet())
	{
		Target.Backdrop = Template.Backdrop;
	}

	// Type-specific properties — use template values if target is empty
	if (Target.ButtonText.IsEmpty()) Target.ButtonText = Template.ButtonText;
	for(const auto& Pair:Template.ButtonTextureRegions) if(!Target.ButtonTextureRegions.Contains(Pair.Key)) Target.ButtonTextureRegions.Add(Pair.Key,Pair.Value);
	if (Target.NormalTexture.IsEmpty()) { Target.NormalTexture = Template.NormalTexture; Target.NormalTextureName = Template.NormalTextureName; Target.NormalTextureParentKey = Template.NormalTextureParentKey; }
	if (Target.PushedTexture.IsEmpty()) { Target.PushedTexture = Template.PushedTexture; Target.PushedTextureName = Template.PushedTextureName; Target.PushedTextureParentKey = Template.PushedTextureParentKey; }
	if (Target.HighlightTexture.IsEmpty()) { Target.HighlightTexture = Template.HighlightTexture; Target.HighlightTextureName = Template.HighlightTextureName; Target.HighlightTextureParentKey = Template.HighlightTextureParentKey; }
	if (Target.DisabledTexture.IsEmpty()) { Target.DisabledTexture = Template.DisabledTexture; Target.DisabledTextureName = Template.DisabledTextureName; Target.DisabledTextureParentKey = Template.DisabledTextureParentKey; }
	if(Target.CheckedTexture.IsEmpty()) Target.CheckedTexture=Template.CheckedTexture;
	if (Target.Orientation.IsEmpty()) Target.Orientation = Template.Orientation;
}

FWowFrameDef FWowFrameManager::ResolveInherits(const FWowFrameDef& Def) const
{
	if (Def.Inherits.IsEmpty()) return Def;

	FWowFrameDef Resolved = Def;

	// Parse comma-separated template names
	TArray<FString> TemplateNames;
	Def.Inherits.ParseIntoArray(TemplateNames, TEXT(","), true);

	// Apply templates left-to-right (later templates override earlier ones)
	// Build a merged template first, then merge into the frame def
	for (FString& TplName : TemplateNames)
	{
		TplName.TrimStartAndEndInline();
		const FWowFrameDef* Tpl = Templates.Find(TplName);
		if (Tpl)
		{
			// Recursively resolve the template's own inheritance
			FWowFrameDef ResolvedTpl = ResolveInherits(*Tpl);
			MergeTemplate(Resolved, ResolvedTpl);
			UE_LOG(LogWowFrame, Verbose, TEXT("Applied template '%s' to frame '%s'"), *TplName, *Def.Name);
		}
		else
		{
			UE_LOG(LogWowFrame, Warning, TEXT("Template '%s' not found for frame '%s'"), *TplName, *Def.Name);
		}
	}

	// Clear the inherits field so we don't re-resolve
	Resolved.Inherits.Empty();

	return Resolved;
}

// ── Anchor Positioning ────────────────────────────────────────────────────────

/** Get the screen position offset of an anchor point on a frame */
FVector2D FWowFrameManager::GetAnchorPointOffset(EWowAnchorPoint Point, float Width, float Height)
{
	switch (Point)
	{
	case EWowAnchorPoint::TOPLEFT:     return FVector2D(0.0f, 0.0f);
	case EWowAnchorPoint::TOP:         return FVector2D(Width * 0.5f, 0.0f);
	case EWowAnchorPoint::TOPRIGHT:    return FVector2D(Width, 0.0f);
	case EWowAnchorPoint::LEFT:        return FVector2D(0.0f, Height * 0.5f);
	case EWowAnchorPoint::CENTER:      return FVector2D(Width * 0.5f, Height * 0.5f);
	case EWowAnchorPoint::RIGHT:       return FVector2D(Width, Height * 0.5f);
	case EWowAnchorPoint::BOTTOMLEFT:  return FVector2D(0.0f, Height);
	case EWowAnchorPoint::BOTTOM:      return FVector2D(Width * 0.5f, Height);
	case EWowAnchorPoint::BOTTOMRIGHT: return FVector2D(Width, Height);
	default: return FVector2D(Width * 0.5f, Height * 0.5f);
	}
}

/** Recursively invalidate cached FrameRects for a frame and all its children */
void FWowFrameManager::InvalidateFrameRectsRecursive(int64 Handle)
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry) return;

	if (!Entry->Def.Name.IsEmpty())
	{
		FrameRects.Remove(Entry->Def.Name);
	}

	// Invalidate all children
	for (const auto& Pair : Frames)
	{
		if (Pair.Value.ParentHandle == Handle)
		{
			InvalidateFrameRectsRecursive(Pair.Key);
		}
	}
}

void FWowFrameManager::CreateLayerContent(UCanvasPanel* Container, const FWowFrameDef& Def, int64 OwnerHandle)
{
	if (!Container) return;

	// Resolve effective parent dimensions for element anchoring.
	// Frames with SetAllPoints=true or no explicit size have Width/Height=0 in the def,
	// but their actual size is computed during ApplyAnchors and cached in FrameRects.
	float EffectiveParentW = Def.Width;
	float EffectiveParentH = Def.Height;
	if (EffectiveParentW <= 0.f || EffectiveParentH <= 0.f)
	{
		if (FrameRects.Contains(Def.Name))
		{
			const FFrameRect& Rect = FrameRects[Def.Name];
			if (Rect.W > 0.f) EffectiveParentW = Rect.W;
			if (Rect.H > 0.f) EffectiveParentH = Rect.H;
		}
		if (EffectiveParentW <= 0.f || EffectiveParentH <= 0.f)
		{
			UE_LOG(LogWowFrame, Warning, TEXT("CreateLayerContent: %s has zero effective size (def=%.0fx%.0f rect=%.0fx%.0f) — element anchors will collapse"),
				*Def.Name, Def.Width, Def.Height, EffectiveParentW, EffectiveParentH);
		}
	}

	int32 TotalTextures = 0, TotalFontStrings = 0, NamedTextures = 0;
	for (const FWowLayer& L : Def.Layers) { TotalTextures += L.Textures.Num(); TotalFontStrings += L.FontStrings.Num(); for (const auto& T : L.Textures) { if (!T.Name.IsEmpty()) NamedTextures++; } }
	if (TotalTextures > 0 || TotalFontStrings > 0)
	{
		UE_LOG(LogWowFrame, Verbose, TEXT("CreateLayerContent: %s — %d layers, %d textures (%d named), %d fontstrings, effectiveSize=%.0fx%.0f"),
			*Def.Name, Def.Layers.Num(), TotalTextures, NamedTextures, TotalFontStrings, EffectiveParentW, EffectiveParentH);
	}

	UObject* Outer = Container->GetOuter();
	if (!Outer) Outer = GetTransientPackage();

	// Layer draw order: BACKGROUND(0), BORDER(1), ARTWORK(2), OVERLAY(3), HIGHLIGHT(4)
	int32 LayerZBase = 0;

	for (const FWowLayer& Layer : Def.Layers)
	{
		int32 ZOrder = static_cast<int32>(Layer.Level) * 100;

		// Create UImage widgets for textures
		for (const FWowTextureElement& Tex : Layer.Textures)
		{
			UImage* ImgWidget = NewObject<UImage>(Outer);
			// Start with transparent brush — prevents default UE white/grey square
			// from showing before the BLP texture is loaded (or for empty textures).
			FSlateBrush TransparentBrush;
			TransparentBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
			ImgWidget->SetBrush(TransparentBrush);
			ImgWidget->SetColorAndOpacity(Tex.VertexColor);
			if (Tex.bHidden)
			{
				ImgWidget->SetVisibility(ESlateVisibility::Collapsed);
			}

			// Register named texture regions for Lua access + create Lua global
			if (!Tex.Name.IsEmpty())
			{
				NamedTextureWidgets.Add(Tex.Name, ImgWidget);

				// Create a Lua global table for this texture region so scripts can call
				// MainMenuBarLeftEndCap:SetTexture("Interface\\...")
				if (EventSystem)
				{
					EventSystem->CreateTextureRegionGlobal(Tex.Name, OwnerHandle, TEXT("Texture"));
					UE_LOG(LogWowFrame, Verbose, TEXT("    Registered texture region global: %s"), *Tex.Name);
				}
				else
				{
					UE_LOG(LogWowFrame, Warning, TEXT("    NO EventSystem for texture region: %s"), *Tex.Name);
				}
			}

			// If no file specified, hide until Lua sets a texture via SetTexture()
			if (Tex.File.IsEmpty())
			{
				ImgWidget->SetRenderOpacity(0.0f);
			}

			// Load the BLP texture if specified
			if (!Tex.File.IsEmpty())
			{
				UTexture2D* LoadedTexture = LoadUITexture(Tex.File);
				if (LoadedTexture)
				{
					// Create slate brush with texture and UV coordinates
					FSlateBrush SlateBrush;
					SlateBrush.SetResourceObject(LoadedTexture);
					if(Tex.bAdditive) if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowUiAdditive.M_WowUiAdditive")))
					{
						auto* Material=UMaterialInstanceDynamic::Create(Base,ImgWidget); Material->SetTextureParameterValue(TEXT("Image"),LoadedTexture); SlateBrush.SetResourceObject(Material);
					}
					SlateBrush.DrawAs = ESlateBrushDrawType::Image;
					float FullW = (float)LoadedTexture->GetSizeX();
					float FullH = (float)LoadedTexture->GetSizeY();
					SlateBrush.Tiling = ESlateBrushTileType::NoTile;

					// Apply texture coordinates if specified (default is 0,0,1,1 for full texture)
					bool bHasSubRegion = (Tex.Left != 0.0f || Tex.Right != 1.0f || Tex.Top != 0.0f || Tex.Bottom != 1.0f);
					if (bHasSubRegion)
					{
						// WoW uses Left,Right,Top,Bottom UV coordinates.
						// FBox2D expects Min < Max, but WoW uses Left > Right for horizontal flip
						// and Top > Bottom for vertical flip.  Normalize the UVs and apply
						// a RenderTransform to handle flipping.
						float UVLeft  = FMath::Min(Tex.Left, Tex.Right);
						float UVRight = FMath::Max(Tex.Left, Tex.Right);
						float UVTop   = FMath::Min(Tex.Top, Tex.Bottom);
						float UVBot   = FMath::Max(Tex.Top, Tex.Bottom);

						FBox2D UVRegion(FVector2D(UVLeft, UVTop), FVector2D(UVRight, UVBot));
						SlateBrush.SetUVRegion(UVRegion);

						// ImageSize = visible sub-region size (not full atlas)
						// so fallback sizing uses correct dimensions
						SlateBrush.ImageSize = FVector2D(
							FullW * (UVRight - UVLeft),
							FullH * (UVBot - UVTop));

						// Apply render transform for flipped texcoords
						bool bFlipH = Tex.Left > Tex.Right;
						bool bFlipV = Tex.Top > Tex.Bottom;
						if (bFlipH || bFlipV)
						{
							FVector2D Scale(bFlipH ? -1.0f : 1.0f, bFlipV ? -1.0f : 1.0f);
							ImgWidget->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
							ImgWidget->SetRenderTransform(FWidgetTransform(FVector2D::ZeroVector, Scale, FVector2D::ZeroVector, 0.0f));
						}
					}
					else
					{
						SlateBrush.ImageSize = FVector2D(FullW, FullH);
					}

					ImgWidget->SetBrush(SlateBrush);
					if (Tex.Left > Tex.Right || Tex.Top > Tex.Bottom)
					{
						UE_LOG(LogWowFrame, Verbose, TEXT("  FLIPPED texture: %s in frame %s (UV: L=%.3f R=%.3f T=%.3f B=%.3f) flipH=%d flipV=%d"),
							*Tex.File, *Def.Name, Tex.Left, Tex.Right, Tex.Top, Tex.Bottom,
							Tex.Left > Tex.Right ? 1 : 0, Tex.Top > Tex.Bottom ? 1 : 0);
					}
					UE_LOG(LogWowFrame, Verbose, TEXT("  Applied texture: %s (UV: %.3f,%.3f to %.3f,%.3f)"),
						*Tex.File, Tex.Left, Tex.Top, Tex.Right, Tex.Bottom);
				}
				else
				{
					UE_LOG(LogWowFrame, Warning, TEXT("  Failed to load texture: %s for frame %s"), *Tex.File, *Def.Name);
				}
				// Log texture with position info
				UCanvasPanelSlot* DbgSlot = Cast<UCanvasPanelSlot>(ImgWidget->Slot);
				FVector2D DbgPos = DbgSlot ? DbgSlot->GetPosition() : FVector2D::ZeroVector;
				FVector2D DbgSize = DbgSlot ? DbgSlot->GetSize() : FVector2D::ZeroVector;
				UE_LOG(LogWowFrame, Verbose, TEXT("  TEX: %s -> %s (%s) xmlSize=%.0fx%.0f slot=(%.0f,%.0f) slotSz=(%.0f,%.0f) anchors=%d setAll=%d"),
					*Tex.File, LoadedTexture ? TEXT("OK") : TEXT("FAIL"), *Def.Name, Tex.Width, Tex.Height,
					DbgPos.X, DbgPos.Y, DbgSize.X, DbgSize.Y, Tex.Anchors.Num(), Tex.bSetAllPoints ? 1 : 0);
			}

			UCanvasPanelSlot* Slot = Container->AddChildToCanvas(ImgWidget);
			if (Slot)
			{
				Slot->SetZOrder(ZOrder++);
			}

			ApplyElementAnchors(ImgWidget, Container, Tex.Anchors, Tex.Width, Tex.Height,
				Tex.bSetAllPoints || (Tex.Anchors.IsEmpty() && Tex.Width <= 0 && Tex.Height <= 0), EffectiveParentW, EffectiveParentH);
			RegionLayouts.Add({ImgWidget, Def.Name, Tex.Anchors, Tex.Width, Tex.Height,
				Tex.bSetAllPoints || (Tex.Anchors.IsEmpty() && Tex.Width <= 0 && Tex.Height <= 0)});
			if (!Tex.Name.IsEmpty()) RegionOwners.Add(Tex.Name, Def.Name);

			// Register named texture elements so other frames can anchor to them
			if (!Tex.Name.IsEmpty())
			{
				// Calculate and store absolute rect for this texture element.
				// WoW XML allows frames to anchor relativeTo a named texture/fontstring.
				UCanvasPanelSlot* TexSlot2 = Cast<UCanvasPanelSlot>(ImgWidget->Slot);
				if (TexSlot2)
				{
					// Get the parent frame's absolute position
					FFrameRect ParentAbsRect = FrameRects.Contains(Def.Name)
						? FrameRects[Def.Name]
						: FFrameRect(0.f, 0.f, EffectiveParentW, EffectiveParentH);

					// Element position within parent canvas (in WoW coords, pre-scale)
					FVector2D SlotPos = TexSlot2->GetPosition();
					FVector2D SlotSize = TexSlot2->GetSize();
					float ElemX = ParentAbsRect.X + SlotPos.X / UIScale;
					float ElemY = ParentAbsRect.Y + SlotPos.Y / UIScale;
					float ElemW = SlotSize.X / UIScale;
					float ElemH = SlotSize.Y / UIScale;

					// Named-region bounds are resolved from their current owner and slot.
					UE_LOG(LogWowFrame, Verbose, TEXT("  Registered texture rect: %s abs=(%.1f,%.1f) sz=(%.1f,%.1f)"),
						*Tex.Name, ElemX, ElemY, ElemW, ElemH);
				}
			}
		}

		// Create UTextBlock widgets for fontstrings
		for (const FWowFontStringElement& FS : Layer.FontStrings)
		{
			UTextBlock* TextWidget = NewObject<UTextBlock>(Outer);
			TextWidget->SetVisibility(FS.bHidden ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);

			const bool IsCaption=FS.Name==Def.Name+TEXT("Text") || FS.Name==Def.Name+TEXT("NormalText") || FS.ParentKey==TEXT("normalText");
			const FString Caption=FS.Text.IsEmpty() && IsCaption?Def.ButtonText:FS.Text;
			if (!Caption.IsEmpty())
			{
				TextWidget->SetText(FText::FromString(EventSystem ? EventSystem->ResolveText(Caption) : Caption));
			}

			// Apply color
			TextWidget->SetColorAndOpacity(FSlateColor(FS.Color));

			// Apply font from font manager or fallback to default
			FSlateFontInfo FontInfo;
			if (FontManager && FontManager->IsInitialized())
			{
				// Try to get the font based on inheritance or use default
				FString FontName = FS.Inherits.IsEmpty() ? TEXT("GameFontNormal") : FS.Inherits;
				// Scale font size with UI scale for proper sizing
				int32 ScaledFontSize = FMath::RoundToInt((FS.FontHeight>0?FS.FontHeight:FontManager->GetFontSize(FontName)) * UIScale);
				FontInfo = FontManager->GetFont(FontName, ScaledFontSize);
			}
			else
			{
				// Fallback to default UE font (also scale the size)
				FontInfo = TextWidget->GetFont();
				FontInfo.Size = FMath::Max(1,FMath::RoundToInt((FS.FontHeight>0?FS.FontHeight:12) * UIScale*.75f));
			}
			TextWidget->SetFont(FontInfo);

			// Apply justification
			if (FS.JustifyH == TEXT("LEFT"))
				TextWidget->SetJustification(ETextJustify::Left);
			else if (FS.JustifyH == TEXT("RIGHT"))
				TextWidget->SetJustification(ETextJustify::Right);
			else
				TextWidget->SetJustification(ETextJustify::Center);

			UCanvasPanelSlot* Slot = Container->AddChildToCanvas(TextWidget);
			if (Slot)
			{
				Slot->SetZOrder(ZOrder++);
			}

			ApplyElementAnchors(TextWidget, Container, FS.Anchors, FS.Width, FS.Height, false, EffectiveParentW, EffectiveParentH);
			RegionLayouts.Add({TextWidget, Def.Name, FS.Anchors, FS.Width, FS.Height, false});
			if (!FS.Name.IsEmpty()) RegionOwners.Add(FS.Name, Def.Name);

			if (!FS.Name.IsEmpty())
			{
				// Create a Lua global for this fontstring so scripts can access it
				if (EventSystem)
				{
					EventSystem->CreateTextureRegionGlobal(FS.Name, OwnerHandle, TEXT("FontString"));
				}
				NamedFontStringWidgets.Add(FS.Name, TextWidget);
				if(EventSystem && !FS.Inherits.IsEmpty()) EventSystem->ApplyRegionFont(FS.Name,FS.Inherits);
				if(FS.FontHeight>0) TextWidget->SetFont(FontInfo);
				if(FS.bHasColor) TextWidget->SetColorAndOpacity(FS.Color);
				// Warn if fontstring name still contains unresolved $parent
				if (FS.Name.Contains(TEXT("$parent")))
				{
					UE_LOG(LogWowFrame, Warning, TEXT("  UNRESOLVED $parent in fontstring: %s (frame: %s)"), *FS.Name, *Def.Name);
				}

				if (!Def.Name.IsEmpty() && (FS.Name == (Def.Name + TEXT("Text")) || FS.Name == (Def.Name + TEXT("NormalText")) || FS.ParentKey==TEXT("normalText")))
				{
					PrimaryTextWidgets.Add(OwnerHandle, TextWidget);
				}

				// Register fontstring rect so other frames can anchor to it
				UCanvasPanelSlot* FSSlot = Cast<UCanvasPanelSlot>(TextWidget->Slot);
				if (FSSlot)
				{
					FFrameRect ParentAbsRect = FrameRects.Contains(Def.Name)
						? FrameRects[Def.Name]
						: FFrameRect(0.f, 0.f, EffectiveParentW, EffectiveParentH);
					FVector2D SlotPos = FSSlot->GetPosition();
					FVector2D SlotSize = FSSlot->GetSize();
					float ElemX = ParentAbsRect.X + SlotPos.X / UIScale;
					float ElemY = ParentAbsRect.Y + SlotPos.Y / UIScale;
					float ElemW = SlotSize.X / UIScale;
					float ElemH = SlotSize.Y / UIScale;
					// Named-region bounds are resolved from their current owner and slot.
				}
			}
		}
	}
}

// ── Backdrop Creation ────────────────────────────────────────────────────────

void FWowFrameManager::CreateBackdrop(UCanvasPanel* Container, const FWowBackdrop& Backdrop)
{
	if (!Container) return;

	UObject* Outer = Container->GetOuter();
	if (!Outer) Outer = GetTransientPackage();

	// For now, implement a simple backdrop that just renders the background texture
	// A full 9-slice implementation would be more complex
	if (!Backdrop.BgFile.IsEmpty())
	{
		UTexture2D* BgTexture = LoadUITexture(Backdrop.BgFile);
		if (BgTexture)
		{
			UImage* BgImage = NewObject<UImage>(Outer);

			FSlateBrush SlateBrush;
			SlateBrush.SetResourceObject(BgTexture);
			SlateBrush.DrawAs = ESlateBrushDrawType::Image;
			BgImage->SetBrush(SlateBrush);

			// Add as first child (lowest Z order) so it appears behind other content
			UCanvasPanelSlot* Slot = Container->AddChildToCanvas(BgImage);
			if (Slot)
			{
				Slot->SetZOrder(-1000); // Behind all other elements
				// Fill the entire frame (insets are in WoW coords, scale them)
				Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
				Slot->SetOffsets(FMargin(
					Backdrop.InsetLeft * UIScale,
					Backdrop.InsetTop * UIScale,
					-Backdrop.InsetRight * UIScale,
					-Backdrop.InsetBottom * UIScale
				));
			}

			UE_LOG(LogWowFrame, Verbose, TEXT("Created backdrop with background: %s"), *Backdrop.BgFile);
		}
	}

	// TODO: Implement edge textures for proper 9-slice rendering
	// For now, we just render the background texture
}

// ── Widget Creation ──────────────────────────────────────────────────────────

UWidget* FWowFrameManager::CreateWidgetForFrame(const FWowFrameDef& Def, int64 Handle, int64 ParentHandle)
{
	UCanvasPanel* Canvas = RootCanvas.Get();
	if (!Canvas)
	{
		UE_LOG(LogWowFrame, Error, TEXT("No root canvas for frame creation"));
		return nullptr;
	}

	bool bForceHidden = false; // Set to true for orphaned frames whose parent doesn't exist

	// Determine parent canvas: use parent frame's canvas if it exists, otherwise root canvas
	UCanvasPanel* ParentCanvas = Canvas; // Default to root canvas

	if (ParentHandle != -1)
	{
		UWidget* ParentWidget = GetWidgetForHandle(ParentHandle);
		if (ParentWidget)
		{
			if (UCanvasPanel* ParentCanvasPanel = Cast<UCanvasPanel>(ParentWidget))
			{
				ParentCanvas = ParentCanvasPanel;
				UE_LOG(LogWowFrame, Verbose, TEXT("  %s: Using parent canvas from frame %lld"), *Def.Name, ParentHandle);
			}
			else
			{
				UE_LOG(LogWowFrame, Warning, TEXT("  %s: Parent frame %lld is not a canvas, using root canvas"), *Def.Name, ParentHandle);
			}
		}
		else
		{
			// Parent frame exists but has no widget (like UIParent or other virtual frames)
			// Use root canvas
			const FFrameEntry* ParentEntry = Frames.Find(ParentHandle);
			if (ParentEntry)
			{
				UE_LOG(LogWowFrame, Verbose, TEXT("  %s: Parent frame %lld (%s) is virtual, using root canvas"),
					*Def.Name, ParentHandle, *ParentEntry->Def.Name);
			}
		}
	}
	else if (!Def.Parent.IsEmpty())
	{
		UE_LOG(LogWowFrame, Warning, TEXT("  %s: Parent '%s' not found, using root canvas (hidden)"), *Def.Name, *Def.Parent);
		// Force-hide orphaned frames — they belong inside a container that doesn't exist yet
		bForceHidden = true;
	}

	UE_LOG(LogWowFrame, Verbose, TEXT("CreateFrame: %s type=%d canvas=%p parent=%p hidden=%d"),
		*Def.Name, (int32)Def.Type, Canvas, ParentCanvas, Def.bHidden);

	UObject* Outer = ParentCanvas->GetOuter();
	if (!Outer) Outer = GetTransientPackage();

	UWidget* Widget = nullptr;

	// WorldFrame is the 3D world viewport — skip widget creation (it's handled by UE5's viewport)
	if (Def.Type == EWowFrameType::WorldFrame)
	{
		UE_LOG(LogWowFrame, Verbose, TEXT("  %s: WorldFrame type — skipping widget (3D viewport)"), *Def.Name);
		return nullptr;
	}

	// ALL frame types use UCanvasPanel so they can contain children (WoW allows children on any frame type).
	// Button/EditBox/StatusBar/Slider behavior is handled through Lua scripts and event routing,
	// not UMG widget types. Using non-canvas widgets (UButton etc.) breaks the parent-child hierarchy
	// because UButton can't contain arbitrary positioned children.
	{
		UCanvasPanel* Container = NewObject<UCanvasPanel>(Outer);
		// WoW frames do NOT clip their layer content by default.
		// Layer textures (portraits, borders, decorations) regularly extend
		// beyond the frame bounds. Only clip if XML explicitly sets clipsChildren.
		// ScrollFrames are the exception - they must clip to provide scrolling behavior.
		if (Def.Type == EWowFrameType::ScrollFrame)
		{
			Container->SetClipping(EWidgetClipping::ClipToBounds);
		}
		else
		{
			Container->SetClipping(EWidgetClipping::Inherit);
		}
		Widget = Container;
	}

	if (!Widget)
	{
		return nullptr;
	}

	UCanvasPanel* ContainerWidget = Cast<UCanvasPanel>(Widget);
	if (ContainerWidget)
	{
		auto AddFillChild = [this, ContainerWidget](UWidget* Child, int32 ZOrder, ESlateVisibility Visibility)
		{
			if (!Child) return;
			Child->SetVisibility(Visibility);
			if (UCanvasPanelSlot* ChildSlot = ContainerWidget->AddChildToCanvas(Child))
			{
				ChildSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
				ChildSlot->SetOffsets(FMargin(0.f));
				ChildSlot->SetZOrder(ZOrder);
			}
		};

		if (Def.Type == EWowFrameType::Button || Def.Type == EWowFrameType::CheckButton)
		{
			UButton* Button = NewObject<UButton>(ContainerWidget);
			// Make button transparent — WoW buttons have no default background.
			// Visual appearance comes from NormalTexture/PushedTexture/HighlightTexture.
			FButtonStyle TransparentStyle;
			FSlateBrush EmptyBrush;
			EmptyBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
			TransparentStyle.SetNormal(EmptyBrush);
			TransparentStyle.SetHovered(EmptyBrush);
			TransparentStyle.SetPressed(EmptyBrush);
			TransparentStyle.SetDisabled(EmptyBrush);
			TransparentStyle.SetNormalPadding(FMargin(0));
			TransparentStyle.SetPressedPadding(FMargin(0));
			Button->SetStyle(TransparentStyle);
			Button->SetBackgroundColor(FLinearColor::Transparent);
			// HitTestInvisible — clicks are routed via our custom HitTestFrames(),
			// not UMG's Slate hit-testing.
			AddFillChild(Button, -100, ESlateVisibility::HitTestInvisible);
			ButtonWidgets.Add(Handle, Button);

			bool HasCaptionRegion=false;
			for(const auto& Layer:Def.Layers) for(const auto& Text:Layer.FontStrings) HasCaptionRegion|=Text.Name==Def.Name+TEXT("Text") || Text.Name==Def.Name+TEXT("NormalText") || Text.ParentKey==TEXT("text") || Text.ParentKey==TEXT("normalText");
			if (!HasCaptionRegion)
			{
				UTextBlock* ButtonText = NewObject<UTextBlock>(ContainerWidget);
				ButtonText->SetText(FText::FromString(EventSystem?EventSystem->ResolveText(Def.ButtonText):Def.ButtonText));
				ButtonText->SetJustification(ETextJustify::Center);
				ButtonText->SetColorAndOpacity(FLinearColor(1,.82f,0));
				FSlateFontInfo FontInfo = FontManager?FontManager->GetDefaultFont(FMath::RoundToInt(12.f*UIScale)):ButtonText->GetFont();
				if(!FontManager) FontInfo.Size=FMath::Max(1,FMath::RoundToInt(9.f*UIScale));
				ButtonText->SetFont(FontInfo);
				AddFillChild(ButtonText, 250, ESlateVisibility::HitTestInvisible);
				if(auto* TextSlot=Cast<UCanvasPanelSlot>(ButtonText->Slot)) { TextSlot->SetAnchors(FAnchors(.5f,.5f)); TextSlot->SetAlignment(FVector2D(.5f,.5f)); TextSlot->SetPosition(FVector2D::ZeroVector); TextSlot->SetAutoSize(true); }
				PrimaryTextWidgets.Add(Handle, ButtonText);

				if (!Def.Name.IsEmpty())
				{
					const FString ButtonTextName = Def.Name + TEXT("Text");
					NamedFontStringWidgets.Add(ButtonTextName, ButtonText);
					RegionOwners.Add(ButtonTextName,Def.Name);
					RegionLayouts.Add({ButtonText,Def.Name,{{EWowAnchorPoint::CENTER,TEXT(""),EWowAnchorPoint::CENTER,0,0}},0,0,false});
					if (EventSystem)
					{
						EventSystem->CreateTextureRegionGlobal(ButtonTextName, Handle, TEXT("FontString"));
					}
				}
			}
		}
		else if (Def.Type == EWowFrameType::EditBox)
		{
			UEditableTextBox* EditBox = NewObject<UEditableTextBox>(ContainerWidget);
			// Make edit box background transparent — WoW edit boxes use XML textures for appearance
			FEditableTextBoxStyle BoxStyle = EditBox->GetWidgetStyle();
			FSlateBrush EmptyBrush;
			EmptyBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
			BoxStyle.SetBackgroundImageNormal(EmptyBrush);
			BoxStyle.SetBackgroundImageHovered(EmptyBrush);
			BoxStyle.SetBackgroundImageFocused(EmptyBrush);
			BoxStyle.SetBackgroundImageReadOnly(EmptyBrush);
			BoxStyle.SetPadding(FMargin(2.f));
			EditBox->SetWidgetStyle(BoxStyle);
			AddFillChild(EditBox, 50, ESlateVisibility::Visible);
			EditBoxWidgets.Add(Handle, EditBox);
		}
		else if (Def.Type == EWowFrameType::Slider)
		{
			USlider* Slider = NewObject<USlider>(ContainerWidget);
			if (Def.Orientation.Equals(TEXT("VERTICAL"), ESearchCase::IgnoreCase))
			{
				Slider->SetOrientation(EOrientation::Orient_Vertical);
			}
			// Use transparent bar — WoW sliders use XML textures
			Slider->SetSliderBarColor(FLinearColor::Transparent);
			AddFillChild(Slider, 50, ESlateVisibility::Visible);
			SliderWidgets.Add(Handle, Slider);
		}
	}

	// Walk the full parent chain to check if ANY ancestor is hidden
	bool bParentHidden = false;
	{
		int64 CheckHandle = ParentHandle;
		int32 MaxDepth = 20; // prevent infinite loops
		while (CheckHandle >= 0 && MaxDepth-- > 0)
		{
			const FFrameEntry* AncestorEntry = Frames.Find(CheckHandle);
			if (!AncestorEntry) break;
			if (AncestorEntry->Def.bHidden || (AncestorEntry->Widget.IsValid() &&
				AncestorEntry->Widget->GetVisibility() == ESlateVisibility::Collapsed))
			{
				bParentHidden = true;
				break;
			}
			CheckHandle = AncestorEntry->ParentHandle;
		}
	}

	// Frames that WoW's Lua hides at startup but aren't marked hidden="true" in XML.
	// These are typically voice chat, quest info sub-frames, and other panels that
	// are shown conditionally by scripts we haven't fully implemented.
	static const TSet<FString> LuaHiddenFrames = {
		TEXT("LoopbackVUMeter"),
		TEXT("QuestInfoRequiredMoneyFrame"),
		TEXT("QuestInfoRequiredMoneyDisplay"),
		TEXT("WatchFrame"),  // Objectives tracker — shown by WatchFrame_Update Lua
	};
	bool bLuaHidden = LuaHiddenFrames.Contains(Def.Name);

	// Frames that WoW's Lua SHOWS at startup (via OnLoad scripts) but our FrameXML
	// scripts fail to execute. Force these visible so the UI is usable.
	// ActionButton1-12, their icons, and micro buttons are key examples.
	bool bForceVisible = false;
	if (Def.bHidden && !bForceHidden && !bParentHidden)
	{
		// ActionButton1-12, MultiBarBottomLeftButton1-12, etc.
		if (Def.Name.StartsWith(TEXT("ActionButton")) ||
			Def.Name.StartsWith(TEXT("MultiBarBottomLeftButton")) ||
			Def.Name.StartsWith(TEXT("MultiBarBottomRightButton")) ||
			Def.Name.StartsWith(TEXT("MultiBarRightButton")) ||
			Def.Name.StartsWith(TEXT("MultiBarLeftButton")))
		{
			bForceVisible = true;
		}
	}

	if ((Def.bHidden && !bForceVisible) || bForceHidden || bParentHidden || bLuaHidden)
	{
		Widget->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		// All frame containers use SelfHitTestInvisible — clicks pass through to the
		// 3D world and are routed to Lua via our custom HitTestFrames() in the
		// PlayerController.  EditBox and Slider children that need UMG interaction
		// set their own visibility to Visible.
		Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	// Add to parent canvas (may be root canvas or a parent frame's canvas)
	UCanvasPanelSlot* Slot = ParentCanvas->AddChildToCanvas(Widget);

	// Apply strata z-ordering (1000 per strata level + frame level offset)
	if (Slot)
	{
		int32 ZOrder = static_cast<int32>(Def.Strata) * 1000 + Def.FrameLevel;
		Slot->SetZOrder(ZOrder);
		UE_LOG(LogWowFrame, Verbose, TEXT("  Added to canvas: slot=%p ZOrder=%d"), Slot, ZOrder);
	}
	else
	{
		UE_LOG(LogWowFrame, Error, TEXT("  Failed to add widget to canvas!"));
	}

	// Apply anchor positioning
	ApplyAnchors(Widget, Def);

	// Ensure frame has minimum size if not auto-sizing
	if (Slot && Def.Width > 0.f && Def.Height > 0.f && (Def.bSetAllPoints || !Def.Anchors.IsEmpty()))
	{
		// Double-check that size was set (in case ApplyAnchors didn't handle it)
		FVector2D CurrentSize = Slot->GetSize();
		if (CurrentSize.X == 0.f && CurrentSize.Y == 0.f)
		{
			Slot->SetSize(FVector2D(Def.Width * UIScale, Def.Height * UIScale));
			UE_LOG(LogWowFrame, Warning, TEXT("  %s: Fallback size set to %.1fx%.1f (scaled)"), *Def.Name, Def.Width * UIScale, Def.Height * UIScale);
		}
	}

	// Create UProgressBar child for StatusBar frames
	if (Def.Type == EWowFrameType::StatusBar)
	{
		UProgressBar* Bar = NewObject<UProgressBar>(Widget);
		// Transparent style by default — Lua will set fill color via :SetStatusBarColor()
		// Remove the default UE grey background/border
		FSlateBrush EmptyBrush;
		EmptyBrush.DrawAs = ESlateBrushDrawType::NoDrawType;

		// Create solid fill brush so the bar is actually visible
		FSlateBrush FillBrush;
		FillBrush.DrawAs = ESlateBrushDrawType::Box;
		FillBrush.SetResourceObject(nullptr); // solid color brush

		FProgressBarStyle BarStyle;
		BarStyle.SetBackgroundImage(EmptyBrush);
		BarStyle.SetFillImage(FillBrush); // Set the fill brush to solid
		Bar->SetWidgetStyle(BarStyle);
		Bar->SetFillColorAndOpacity(FLinearColor::Green); // default, Lua will override
		Bar->SetPercent(0.0f);
		UCanvasPanel* CanvasPanel = Cast<UCanvasPanel>(Widget);
		UCanvasPanelSlot* BarSlot = CanvasPanel ? CanvasPanel->AddChildToCanvas(Bar) : nullptr;
		if (BarSlot)
		{
			BarSlot->SetAnchors(FAnchors(0, 0, 1, 1));
			BarSlot->SetOffsets(FMargin(0));
		}
		StatusBarWidgets.Add(Handle, Bar);
	}

	// Create backdrop (9-slice background) if specified
	if (UCanvasPanel* BackdropCanvas = Cast<UCanvasPanel>(Widget); Def.Backdrop.IsSet() && BackdropCanvas)
	{
		CreateBackdrop(BackdropCanvas, Def.Backdrop.GetValue());
	}

	// Create layer content (textures and fontstrings) inside the frame
	if (auto* FrameCanvas = Cast<UCanvasPanel>(Widget)) CanvasNames.Add(FrameCanvas, Def.Name);
	if (Def.Layers.Num() > 0)
	{
		UCanvasPanel* Container = Cast<UCanvasPanel>(Widget);
		// Only create layers for canvas-type frames; skip for buttons/statusbars/etc.
		// to avoid dumping textures onto the root canvas where they'd fill the screen
		if (Container)
		{
			CreateLayerContent(Container, Def, Handle);
		}
	}

	return Widget;
}

// ── $parent Resolution ───────────────────────────────────────────────────────

void FWowFrameManager::ResolveParentReferences(FWowFrameDef& Def, const FString& ParentName)
{
	// Resolve $parent in the frame name itself
	if (Def.Name.StartsWith(TEXT("$parent")) && !ParentName.IsEmpty())
	{
		FString ResolvedName = Def.Name;
		ResolvedName.ReplaceInline(TEXT("$parent"), *ParentName);
		Def.Name = ResolvedName;

		UE_LOG(LogWowFrame, Verbose, TEXT("Resolved frame name: $parent -> %s"), *Def.Name);
	}

	// Resolve $parent in anchor RelativeTo fields
	for (FWowAnchor& Anchor : Def.Anchors)
	{
		if (Anchor.RelativeTo.StartsWith(TEXT("$parent")) && !ParentName.IsEmpty())
		{
			FString ResolvedRelativeTo = Anchor.RelativeTo;
			ResolvedRelativeTo.ReplaceInline(TEXT("$parent"), *ParentName);
			Anchor.RelativeTo = ResolvedRelativeTo;

			UE_LOG(LogWowFrame, Verbose, TEXT("Resolved anchor RelativeTo: $parent -> %s"), *Anchor.RelativeTo);
		}
	}

	// Resolve $parent in layer content (textures and fontstrings)
	// $parent in layer content refers to the FRAME itself. For anonymous frames
	// (no name), fall back to using ParentName (the frame's parent).
	const FString& FrameName = Def.Name.IsEmpty() ? ParentName : Def.Name;
	for (FWowLayer& Layer : Def.Layers)
	{
		// Resolve in texture elements
		for (FWowTextureElement& Texture : Layer.Textures)
		{
			if(Texture.Name.IsEmpty() && !Texture.ParentKey.IsEmpty()) { FString Suffix=Texture.ParentKey; Suffix[0]=FChar::ToUpper(Suffix[0]); Texture.Name=FrameName+Suffix; }
			// Resolve texture name: $parentBorder → ActionButton1Border
			if (Texture.Name.Contains(TEXT("$parent")) && !FrameName.IsEmpty())
			{
				Texture.Name.ReplaceInline(TEXT("$parent"), *FrameName);
			}

			// Resolve texture anchor RelativeTo fields
			for (FWowAnchor& TexAnchor : Texture.Anchors)
			{
				if (TexAnchor.RelativeTo.Contains(TEXT("$parent")) && !FrameName.IsEmpty())
				{
					TexAnchor.RelativeTo.ReplaceInline(TEXT("$parent"), *FrameName);
				}
			}
		}

		// Resolve in fontstring elements
		for (FWowFontStringElement& FontString : Layer.FontStrings)
		{
			if(FontString.Name.IsEmpty() && !FontString.ParentKey.IsEmpty()) { FString Suffix=FontString.ParentKey; Suffix[0]=FChar::ToUpper(Suffix[0]); FontString.Name=FrameName+Suffix; }
			// Resolve fontstring name: $parentName → PlayerFrameName
			if (FontString.Name.Contains(TEXT("$parent")) && !FrameName.IsEmpty())
			{
				FontString.Name.ReplaceInline(TEXT("$parent"), *FrameName);
			}

			// Resolve fontstring anchor RelativeTo fields
			for (FWowAnchor& FSAnchor : FontString.Anchors)
			{
				if (FSAnchor.RelativeTo.Contains(TEXT("$parent")) && !FrameName.IsEmpty())
				{
					FSAnchor.RelativeTo.ReplaceInline(TEXT("$parent"), *FrameName);
				}
			}
		}
	}

	// Resolve $parent in button texture names (NormalTexture, PushedTexture, etc.)
	if (!FrameName.IsEmpty())
	{
		if (Def.NormalTextureName.Contains(TEXT("$parent")))
			Def.NormalTextureName.ReplaceInline(TEXT("$parent"), *FrameName);
		if (Def.PushedTextureName.Contains(TEXT("$parent")))
			Def.PushedTextureName.ReplaceInline(TEXT("$parent"), *FrameName);
		if (Def.HighlightTextureName.Contains(TEXT("$parent")))
			Def.HighlightTextureName.ReplaceInline(TEXT("$parent"), *FrameName);
		if (Def.DisabledTextureName.Contains(TEXT("$parent")))
			Def.DisabledTextureName.ReplaceInline(TEXT("$parent"), *FrameName);
	}

	for (FString& NamedObject : Def.NamedObjectGlobals)
	{
		if (NamedObject.Contains(TEXT("$parent")) && !FrameName.IsEmpty())
		{
			NamedObject.ReplaceInline(TEXT("$parent"), *FrameName);
		}
	}

	// Recursively resolve $parent in child frames.
	// For anonymous frames (empty name), pass the effective name (ParentName)
	// so grandchildren can still resolve $parent.
	const FString& EffectiveName = Def.Name.IsEmpty() ? ParentName : Def.Name;
	for (FWowFrameDef& Child : Def.Children)
	{
		ResolveParentReferences(Child, EffectiveName);
	}
}

// ── Frame Creation ───────────────────────────────────────────────────────────

int64 FWowFrameManager::CreateFrame(const FWowFrameDef& Def)
{
	// Resolve template inheritance first
	FWowFrameDef Resolved = ResolveInherits(Def);
	if(Resolved.Name==TEXT("UIParent"))
	{
		const auto Root=GetFrameRect(TEXT("UIParent"));
		Resolved.Width=Root.W; Resolved.Height=Root.H; Resolved.bSetAllPoints=true;
	}
	if (Resolved.Name.IsEmpty() && !Resolved.bVirtual)
	{
		Resolved.bAnonymous = true;
		Resolved.Name = FString::Printf(TEXT("__WowAnonymous_%lld"), NextHandle);
	}

	// Resolve $parent references in frame names and anchors.
	// SKIP for virtual frames — they are templates whose children should keep
	// $parent unresolved so it gets resolved during instantiation.
	if (!Resolved.bVirtual)
	{
		// Always resolve, even if Parent is empty — layer content ($parent in
		// texture/fontstring names) uses the frame's OWN name, not the parent.
		FString ParentForResolution = Resolved.Parent.IsEmpty() ? Resolved.Name : Resolved.Parent;
		ResolveParentReferences(Resolved, ParentForResolution);
	}

	int64 Handle = NextHandle++;
	FFrameEntry Entry;
	Entry.Def = Resolved;
	Entry.bMouseEnabled = Resolved.bEnableMouse ||
		Resolved.Type == EWowFrameType::Button ||
		Resolved.Type == EWowFrameType::CheckButton ||
		Resolved.Type == EWowFrameType::EditBox ||
		Resolved.Type == EWowFrameType::Slider;
	Entry.bKeyboardEnabled = Resolved.bEnableKeyboard ||
		Resolved.Type == EWowFrameType::EditBox;
	Entry.bMouseWheelEnabled = Resolved.Scripts.ContainsByPredicate([](const FWowScriptHandler& Script){return Script.Event==TEXT("OnMouseWheel");});

	// Resolve parent handle
	if (!Resolved.Parent.IsEmpty())
	{
		Entry.ParentHandle = FindFrame(Resolved.Parent);
		if (Entry.ParentHandle == -1)
		{
			UE_LOG(LogWowFrame, Warning, TEXT("Parent frame '%s' not found for frame '%s'"), *Resolved.Parent, *Resolved.Name);
		}
	}

	// Handle virtual frames (like templates and UIParent)
	if (const auto* Parent = Frames.Find(Entry.ParentHandle))
	{
		if (Resolved.FrameLevel == 0) Resolved.FrameLevel = Parent->Def.FrameLevel + 1;
		if (Resolved.Strata == EWowFrameStrata::MEDIUM) Resolved.Strata = Parent->Def.Strata;
		Entry.Def = Resolved;
	}
	if (Resolved.bVirtual)
	{
		// Virtual frames don't get widgets but are stored for reference/inheritance
		Entry.Widget = nullptr;
		Frames.Add(Handle, MoveTemp(Entry));

		if (!Resolved.Name.IsEmpty())
		{
			NameToHandle.Add(Resolved.Name, Handle);
			RegisterTemplate(Resolved.Name, Resolved);
		}

		UE_LOG(LogWowFrame, Verbose, TEXT("Created virtual frame [%lld] %s"), Handle, *Resolved.Name);
		return Handle;
	}

	// Create the widget for non-virtual frames
	Entry.Widget = CreateWidgetForFrame(Resolved, Handle, Entry.ParentHandle);

	Frames.Add(Handle, MoveTemp(Entry));

	if (!Resolved.Name.IsEmpty())
	{
		NameToHandle.Add(Resolved.Name, Handle);
	}

	// Register frame as Lua global FIRST (so children and scripts can find it)
	if (EventSystem && !Resolved.Name.IsEmpty())
	{
		EventSystem->CreateFrameObject(Handle, Resolved.Name, Resolved.FrameID);

		if (Entry.ParentHandle != -1)
		{
			const FString ParentName = GetFrameName(Entry.ParentHandle);
			const FString AliasName = BuildParentFrameAliasName(ParentName, Resolved.Name);
			if (!AliasName.IsEmpty())
			{
				EventSystem->AliasGlobalObject(Resolved.Name, AliasName);
			}
		}

		// Create Lua globals for button texture names (e.g., ActionButton1NormalTexture)
		// These are needed by GetNormalTexture() etc.
		if (!Resolved.NormalTextureName.IsEmpty())
			EventSystem->CreateTextureRegionGlobal(Resolved.NormalTextureName, Handle, TEXT("Texture"));
		if (!Resolved.PushedTextureName.IsEmpty())
			EventSystem->CreateTextureRegionGlobal(Resolved.PushedTextureName, Handle, TEXT("Texture"));
		if (!Resolved.HighlightTextureName.IsEmpty())
			EventSystem->CreateTextureRegionGlobal(Resolved.HighlightTextureName, Handle, TEXT("Texture"));
		if (!Resolved.DisabledTextureName.IsEmpty())
			EventSystem->CreateTextureRegionGlobal(Resolved.DisabledTextureName, Handle, TEXT("Texture"));
		for(const auto& Pair:Resolved.ButtonTextureRegions) EnsureButtonTexture(Handle,Pair.Key);

		for (const FString& NamedObject : Resolved.NamedObjectGlobals)
		{
			if (!NamedObject.IsEmpty())
			{
				EventSystem->CreateTextureRegionGlobal(NamedObject, Handle, TEXT("Animation"));
			}
		}
	}

	// Create child frames BEFORE firing OnLoad (WoW creates children first,
	// then fires parent OnLoad — scripts like ActionButton_OnLoad expect
	// _G["ActionButton1Cooldown"] to exist when OnLoad runs)
	for (const FWowFrameDef& ChildDef : Resolved.Children)
	{
		FWowFrameDef ChildWithParent = ChildDef;
		if (ChildWithParent.Parent.IsEmpty() && !Resolved.Name.IsEmpty())
		{
			ChildWithParent.Parent = Resolved.Name;
		}
		const int64 ChildHandle=CreateFrame(ChildWithParent);
		if(ChildWithParent.bScrollChild) SetScrollChild(Handle,ChildHandle);
	}

	// NOW compile and fire OnLoad — all children exist
	// Always call CompileFrameScripts so child-field mapping and self.name are set,
	// even for frames without scripts (they still need fields for parent scripts to access)
	if (EventSystem)
	{
		EventSystem->CompileFrameScripts(Handle, Resolved);
	}

	UE_LOG(LogWowFrame, Verbose, TEXT("Created frame [%lld] %s (type %d, %d layers, %d children, widget=%p)"),
		Handle, *Resolved.Name, (int32)Resolved.Type, Resolved.Layers.Num(), Resolved.Children.Num(), Entry.Widget.Get());
	return Handle;
}

// ── Texture Loading ──────────────────────────────────────────────────────

UTexture2D* FWowFrameManager::LoadUITexture(const FString& TexturePath)
{
	if (TexturePath.IsEmpty() || !MpqManager || !AssetCache)
	{
		return nullptr;
	}

	// Check cache first
	if (TWeakObjectPtr<UTexture2D>* CachedPtr = TextureCache.Find(TexturePath))
	{
		if (CachedPtr->IsValid())
		{
			return CachedPtr->Get();
		}
		else
		{
			// Remove stale entry
			TextureCache.Remove(TexturePath);
		}
	}

	// Check asset cache
	UTexture2D* ExistingTexture = AssetCache->FindTexture(TexturePath);
	if (ExistingTexture)
	{
		TextureCache.Add(TexturePath, ExistingTexture);
		return ExistingTexture;
	}

	// Convert interface path to BLP file path
	FString BlpPath = TexturePath;
	// Replace known extensions with .blp
	if (BlpPath.EndsWith(TEXT(".tga"), ESearchCase::IgnoreCase))
	{
		BlpPath = BlpPath.Left(BlpPath.Len() - 4) + TEXT(".blp");
	}
	else if (BlpPath.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
	{
		BlpPath = BlpPath.Left(BlpPath.Len() - 4) + TEXT(".blp");
	}
	else if (!BlpPath.EndsWith(TEXT(".blp"), ESearchCase::IgnoreCase))
	{
		BlpPath += TEXT(".blp");
	}

	// Read BLP file from MPQ
	TArray<uint8> BlpData;
	if (!MpqManager->ReadFile(BlpPath, BlpData))
	{
		UE_LOG(LogWowFrame, Warning, TEXT("Failed to read UI texture: %s"), *BlpPath);
		return nullptr;
	}

	// Parse BLP
	FBlpTexture BlpTexture = FBlpParser::Parse(BlpData);
	if (!BlpTexture.bIsValid)
	{
		UE_LOG(LogWowFrame, Warning, TEXT("Failed to parse UI texture: %s"), *BlpPath);
		return nullptr;
	}

	// Create UTexture2D
	UTexture2D* Texture = FWowTextureFactory::CreateTexture(BlpTexture, TexturePath);
	if (!Texture)
	{
		UE_LOG(LogWowFrame, Warning, TEXT("Failed to create UI texture: %s"), *BlpPath);
		return nullptr;
	}

	// Cache the texture
	AssetCache->CacheTexture(TexturePath, Texture);
	TextureCache.Add(TexturePath, Texture);

	UE_LOG(LogWowFrame, Verbose, TEXT("Loaded UI texture: %s (%dx%d)"),
		*TexturePath, Texture->GetSizeX(), Texture->GetSizeY());

	return Texture;
}

int64 FWowFrameManager::CreateDebugFrame(const FString& Name, float Width, float Height, float X, float Y)
{
	// Create a simple debug frame with a background and text
	FWowFrameDef DebugDef;
	DebugDef.Name = Name;
	DebugDef.Type = EWowFrameType::Frame;
	DebugDef.Width = Width;
	DebugDef.Height = Height;
	DebugDef.Strata = EWowFrameStrata::DIALOG;
	DebugDef.FrameLevel = 10;

	// Add anchor to position it
	FWowAnchor Anchor;
	Anchor.Point = EWowAnchorPoint::TOPLEFT;
	Anchor.RelativePoint = EWowAnchorPoint::TOPLEFT;
	Anchor.OffsetX = X;
	Anchor.OffsetY = Y;
	DebugDef.Anchors.Add(Anchor);

	// Add a backdrop for visual feedback
	FWowBackdrop Backdrop;
	// For now, create a debug frame without backdrop since we need texture files
	// A real implementation would load Interface/DialogFrame/UI-DialogBox-Background.blp

	// Add a text layer to show the frame name
	FWowLayer TextLayer;
	TextLayer.Level = EWowDrawLayer::OVERLAY;

	FWowFontStringElement DebugText;
	DebugText.Name = Name + TEXT("_Text");
	DebugText.Text = Name + FString::Printf(TEXT(" (%.0fx%.0f)"), Width, Height);
	DebugText.FontHeight = 12.f;
	DebugText.Color = FLinearColor::Yellow;
	DebugText.JustifyH = TEXT("CENTER");

	// Center the text in the frame
	FWowAnchor TextAnchor;
	TextAnchor.Point = EWowAnchorPoint::CENTER;
	TextAnchor.RelativePoint = EWowAnchorPoint::CENTER;
	DebugText.Anchors.Add(TextAnchor);

	TextLayer.FontStrings.Add(DebugText);
	DebugDef.Layers.Add(TextLayer);

	int64 Handle = CreateFrame(DebugDef);
	UE_LOG(LogWowFrame, Warning, TEXT("Created debug frame '%s' [%lld] at (%.0f,%.0f) size %.0fx%.0f"),
		*Name, Handle, X, Y, Width, Height);

	return Handle;
}

void FWowFrameManager::SyncChildVisibility()
{
	// After all OnLoad scripts fire, some parents got hidden but their children
	// were already created visible. Walk the tree and hide children of hidden ancestors.
	int32 HiddenCount = 0;
	for (auto& Pair : Frames)
	{
		if (!Pair.Value.Widget.IsValid()) continue;
		if (Pair.Value.Def.bHidden) continue; // Already hidden by itself

		// Walk parent chain
		int64 CheckHandle = Pair.Value.ParentHandle;
		int32 MaxDepth = 20;
		bool bAncestorHidden = false;
		while (CheckHandle >= 0 && MaxDepth-- > 0)
		{
			const FFrameEntry* Ancestor = Frames.Find(CheckHandle);
			if (!Ancestor) break;
			if (Ancestor->Def.bHidden || (Ancestor->Widget.IsValid() &&
				Ancestor->Widget->GetVisibility() == ESlateVisibility::Collapsed))
			{
				bAncestorHidden = true;
				break;
			}
			CheckHandle = Ancestor->ParentHandle;
		}

		if (bAncestorHidden && Pair.Value.Widget->GetVisibility() != ESlateVisibility::Collapsed)
		{
			Pair.Value.Widget->SetVisibility(ESlateVisibility::Collapsed);
			HiddenCount++;
		}
	}

	if (HiddenCount > 0)
	{
		UE_LOG(LogWowFrame, Verbose, TEXT("SyncChildVisibility: hid %d orphaned visible children of hidden parents"), HiddenCount);
	}

	// Force-show frames that WoW's Lua would show at startup but our FrameXML
	// scripts can't execute (missing API stubs cause OnLoad to fail/hide them).
	int32 ForceShownCount = 0;
	for (auto& Pair : Frames)
	{
		if (!Pair.Value.Widget.IsValid()) continue;
		const FString& Name = Pair.Value.Def.Name;

		bool bShouldForceShow = false;

		// ActionButton1-12 (main action bar buttons)
		if (Name.StartsWith(TEXT("ActionButton")) && Name.Len() <= 14)
			bShouldForceShow = true;
		// BonusActionButton1-12
		if (Name.StartsWith(TEXT("BonusActionButton")))
			bShouldForceShow = true;

		if (bShouldForceShow && Pair.Value.Widget->GetVisibility() == ESlateVisibility::Collapsed)
		{
			// Check parent is actually visible first
			bool bParentVis = true;
			int64 PH = Pair.Value.ParentHandle;
			if (PH >= 0)
			{
				const FFrameEntry* PE = Frames.Find(PH);
				if (PE && PE->Widget.IsValid() && PE->Widget->GetVisibility() == ESlateVisibility::Collapsed)
					bParentVis = false;
			}
			if (bParentVis)
			{
				bool bInteractive = (Pair.Value.Def.Type == EWowFrameType::Button ||
					Pair.Value.Def.Type == EWowFrameType::CheckButton);
				Pair.Value.Widget->SetVisibility(bInteractive ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible);
				ForceShownCount++;
			}
		}
	}
	if (ForceShownCount > 0)
	{
		UE_LOG(LogWowFrame, Verbose, TEXT("SyncChildVisibility: force-showed %d action bar frames"), ForceShownCount);
	}

	// Force-hide frames that WoW expects to start hidden but our XML/template
	// system didn't mark as hidden (e.g., FadingFrame templates not in MPQ XML).
	static const TCHAR* ForceHideFrames[] = {
		TEXT("ZoneTextFrame"),
		TEXT("SubZoneTextFrame"),
		TEXT("AutoFollowStatusText"),
		TEXT("PVPInfoTextString"),
		TEXT("ReadyCheckListenerFrame"),
		TEXT("ReadyCheckFrame"),
		TEXT("GMChatFrame"),
		TEXT("GMChatFrameEditBox"),
	};
	int32 ForceHiddenCount = 0;
	for (const TCHAR* FrameName : ForceHideFrames)
	{
		int64 H = FindFrame(FrameName);
		if (H >= 0)
		{
			SetFrameVisible(H, false);
			ForceHiddenCount++;
		}
	}
	if (ForceHiddenCount > 0)
	{
		UE_LOG(LogWowFrame, Verbose, TEXT("SyncChildVisibility: force-hid %d frames (FadingFrame pattern)"), ForceHiddenCount);
	}

	// Hide Flash textures on action buttons — these start visible but should be hidden
	// until the FrameXML combat flash animation activates them
	int32 FlashHiddenCount = 0;
	for (auto& TexPair : NamedTextureWidgets)
	{
		if (TexPair.Key.Contains(TEXT("Flash")) && TexPair.Value.IsValid())
		{
			TexPair.Value->SetVisibility(ESlateVisibility::Collapsed);
			FlashHiddenCount++;
		}
	}
	if (FlashHiddenCount > 0)
	{
		UE_LOG(LogWowFrame, Verbose, TEXT("SyncChildVisibility: hid %d Flash textures"), FlashHiddenCount);
	}

	// Dump all visible frames with their positions for debugging
	UE_LOG(LogWowFrame, Warning, TEXT("=== VISIBLE FRAMES (post-sync) ==="));
	for (const auto& Pair : Frames)
	{
		if (!Pair.Value.Widget.IsValid()) continue;
		if (Pair.Value.Widget->GetVisibility() == ESlateVisibility::Collapsed) continue;

		UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Pair.Value.Widget->Slot);
		if (!Slot) continue;
		FVector2D Pos = Slot->GetPosition();
		FVector2D Sz = Slot->GetSize();
		// Only log frames with significant area (skip zero-size and tiny frames)
		if (Sz.X * Sz.Y > 100.f)
		{
			UE_LOG(LogWowFrame, Warning, TEXT("  VISIBLE: %-40s pos=(%.0f,%.0f) sz=(%.0f,%.0f) parent='%s'"),
				*Pair.Value.Def.Name, Pos.X, Pos.Y, Sz.X, Sz.Y, *Pair.Value.Def.Parent);
		}
	}
}

void FWowFrameManager::SetCooldown(int64 Handle, double StartTime, float Duration)
{
	if (Duration <= 0.0f)
	{
		// Clear cooldown
		CooldownStates.Remove(Handle);
		if (TWeakObjectPtr<UImage>* OverlayPtr = CooldownOverlayWidgets.Find(Handle))
		{
			if (OverlayPtr->IsValid())
			{
				(*OverlayPtr)->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		return;
	}

	FCooldownState& State = CooldownStates.FindOrAdd(Handle);
	State.StartTime = StartTime;
	State.Duration = Duration;

	// Create overlay if needed
	if (!CooldownOverlayWidgets.Contains(Handle))
	{
		const FFrameEntry* Entry = Frames.Find(Handle);
		if (Entry && Entry->Widget.IsValid())
		{
			UCanvasPanel* Container = Cast<UCanvasPanel>(Entry->Widget.Get());
			if (Container)
			{
				UImage* Overlay = NewObject<UImage>(Container);
				FSlateBrush DarkBrush;
				if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowUiCooldown.M_WowUiCooldown")))
					DarkBrush.SetResourceObject(UMaterialInstanceDynamic::Create(Base,Overlay));
				DarkBrush.DrawAs=ESlateBrushDrawType::Image;
				DarkBrush.TintColor = FLinearColor::White;
				Overlay->SetBrush(DarkBrush);
				Overlay->SetColorAndOpacity(FLinearColor::White);
				UCanvasPanelSlot* Slot = Container->AddChildToCanvas(Overlay);
				if (Slot)
				{
					// Fill parent
					Slot->SetAnchors(FAnchors(0, 0, 1, 1));
					Slot->SetOffsets(FMargin(0));
				}
				CooldownOverlayWidgets.Add(Handle, Overlay);
			}
		}
	}

	// Show overlay
	if (TWeakObjectPtr<UImage>* OverlayPtr = CooldownOverlayWidgets.Find(Handle))
	{
		if (OverlayPtr->IsValid())
		{
			(*OverlayPtr)->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
}

void FWowFrameManager::TickCooldowns()
{
	const double Now = FPlatformTime::Seconds();
	TArray<int64> Expired;

	for (auto& [Handle, State] : CooldownStates)
	{
		double Elapsed = Now - State.StartTime;
		if (Elapsed >= State.Duration)
		{
			// Cooldown expired — hide overlay
			Expired.Add(Handle);
			if (TWeakObjectPtr<UImage>* OverlayPtr = CooldownOverlayWidgets.Find(Handle))
			{
				if (OverlayPtr->IsValid())
				{
					(*OverlayPtr)->SetVisibility(ESlateVisibility::Collapsed);
				}
			}
		}
		else
		{
			const float Progress=FMath::Clamp(float(Elapsed/State.Duration),0.f,1.f);
			if (TWeakObjectPtr<UImage>* OverlayPtr = CooldownOverlayWidgets.Find(Handle))
			{
				if (OverlayPtr->IsValid())
				{
					if(auto* Material=Cast<UMaterialInstanceDynamic>((*OverlayPtr)->GetBrush().GetResourceObject())) Material->SetScalarParameterValue(TEXT("Progress"),Progress);
				}
			}
		}
	}

	for (int64 H : Expired)
	{
		CooldownStates.Remove(H);
	}
}

void FWowFrameManager::AddMessage(int64 Handle,const FString& Text,FLinearColor Color,int32 Id)
{
	if(Text.IsEmpty() || !Frames.Contains(Handle)) return;
	auto& State=MessageFrames.FindOrAdd(Handle);
	State.Lines.Add({WowUiPlainText(Text),Color,Id});
	if(State.Lines.Num()>State.Limit) State.Lines.RemoveAt(0,State.Lines.Num()-State.Limit);
	if(State.Offset>0) ++State.Offset;
	DrawMessages(Handle);
}

void FWowFrameManager::SetChecked(int64 Handle,bool Checked)
{
	if(Checked) CheckedFrames.Add(Handle); else CheckedFrames.Remove(Handle);
	if(Checked && !CheckImages.Contains(Handle)) if(auto* Canvas=Cast<UCanvasPanel>(GetWidgetForHandle(Handle)))
	{
		auto* Image=NewObject<UImage>(Canvas); const auto* Def=GetFrameDef(Handle);
		const FString Path=Def && !Def->CheckedTexture.IsEmpty()?Def->CheckedTexture:TEXT("Interface\\Buttons\\UI-CheckBox-Check");
		if(auto* Texture=LoadUITexture(Path))
		{
			Image->SetBrushFromTexture(Texture);
			if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowUiImage.M_WowUiImage")))
			{
				auto* Material=UMaterialInstanceDynamic::Create(Base,Image); Material->SetTextureParameterValue(TEXT("Image"),Texture); Material->SetScalarParameterValue(TEXT("Circle"),0); Material->SetScalarParameterValue(TEXT("Additive"),1);
				FSlateBrush Brush=Image->GetBrush(); Brush.SetResourceObject(Material); Image->SetBrush(Brush);
			}
		}
		if(auto* Slot=Canvas->AddChildToCanvas(Image)) { Slot->SetAnchors(FAnchors(0,0,1,1)); Slot->SetOffsets(FMargin(0)); Slot->SetZOrder(180); }
		CheckImages.Add(Handle,Image);
	}
	if(auto* Image=CheckImages.Find(Handle); Image && Image->IsValid()) (*Image)->SetVisibility(Checked?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
}

void FWowFrameManager::ScrollMessages(int64 Handle,int32 Offset,bool Absolute)
{
	auto& State=MessageFrames.FindOrAdd(Handle); State.Offset=FMath::Clamp(Absolute?Offset:State.Offset+Offset,0,FMath::Max(State.Lines.Num()-1,0)); DrawMessages(Handle);
}

void FWowFrameManager::DrawMessages(int64 Handle)
{
	auto* Canvas=Cast<UCanvasPanel>(GetWidgetForHandle(Handle)); if(!Canvas) return;
	auto& State=MessageFrames.FindOrAdd(Handle); const auto R=GetLayoutRect(Handle);
	const float Height=FMath::Max(float(R.W),14.f),Width=FMath::Max(float(R.Z),100.f);
	const int32 Visible=FMath::Clamp(FMath::FloorToInt(Height/16),1,30);
	Canvas->SetClipping(EWidgetClipping::ClipToBounds);
	while(State.Widgets.Num()<Visible)
	{
		auto* Text=NewObject<UTextBlock>(Canvas); Text->SetVisibility(ESlateVisibility::HitTestInvisible);
		Text->SetJustification(ETextJustify::Left); Text->SetAutoWrapText(true);
		if(FontManager) Text->SetFont(FontManager->GetDefaultFont(FMath::Max(1,FMath::RoundToInt(12*UIScale))));
		Canvas->AddChildToCanvas(Text); State.Widgets.Add(Text);
	}
	float Bottom=Height*UIScale;
	for(int32 I=0; I<State.Widgets.Num(); ++I) if(auto* Text=State.Widgets[I].Get())
	{
		const int32 Index=State.Lines.Num()-1-State.Offset-I;
		if(I>=Visible || Index<0 || Bottom<=0) { Text->SetVisibility(ESlateVisibility::Collapsed); continue; }
		const auto& Line=State.Lines[Index]; Text->SetText(FText::FromString(Line.Text)); Text->SetColorAndOpacity(Line.Color); Text->SetWrapTextAt(Width*UIScale);
		Text->ForceLayoutPrepass(); const float RowHeight=FMath::Max(16*UIScale,Text->GetDesiredSize().Y);
		Bottom-=RowHeight;
		if(auto* Slot=Cast<UCanvasPanelSlot>(Text->Slot)) { Slot->SetAnchors(FAnchors(0,0)); Slot->SetAlignment(FVector2D::ZeroVector); Slot->SetPosition(FVector2D(0,Bottom)); Slot->SetSize(FVector2D(Width*UIScale,RowHeight)); Slot->SetZOrder(200); }
		Text->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void FWowFrameManager::DebugDumpLayout() const
{
	// Dump key WoW UI frames with expected vs actual sizes
	static const TCHAR* KeyFrames[] = {
		TEXT("UIParent"), TEXT("PlayerFrame"), TEXT("TargetFrame"),
		TEXT("MainMenuBar"), TEXT("MainMenuBarArtFrame"), TEXT("MinimapCluster"),
		TEXT("Minimap"), TEXT("ActionButton1"), TEXT("MultiBarBottomLeft"),
		TEXT("ChatFrame1"), TEXT("BuffFrame"), TEXT("CastingBarFrame"),
	};

	UE_LOG(LogWowFrame, Warning, TEXT("═══════════ LAYOUT DEBUG DUMP ═══════════"));
	UE_LOG(LogWowFrame, Warning, TEXT("UIScale=%.3f  RootCanvas=%p  TotalFrames=%d"),
		UIScale, RootCanvas.Get(), Frames.Num());

	for (const TCHAR* FrameName : KeyFrames)
	{
		const int64* HandlePtr = NameToHandle.Find(FrameName);
		if (!HandlePtr) {
			UE_LOG(LogWowFrame, Warning, TEXT("  %-25s  NOT FOUND"), FrameName);
			continue;
		}

		const FFrameEntry* Entry = Frames.Find(*HandlePtr);
		if (!Entry) continue;

		const FWowFrameDef& Def = Entry->Def;
		UWidget* W = Entry->Widget.IsValid() ? Entry->Widget.Get() : nullptr;

		// Get stored rect
		const FFrameRect* Rect = FrameRects.Find(FrameName);
		float RX = Rect ? Rect->X : -1, RY = Rect ? Rect->Y : -1;
		float RW = Rect ? Rect->W : -1, RH = Rect ? Rect->H : -1;

		// Get actual slot info
		FString SlotInfo = TEXT("no widget");
		if (W)
		{
			UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(W->Slot);
			if (Slot)
			{
				FVector2D Pos = Slot->GetPosition();
				FVector2D Size = Slot->GetSize();
				FAnchors Anch = Slot->GetAnchors();
				SlotInfo = FString::Printf(TEXT("slot=(%.0f,%.0f) sz=(%.0f,%.0f) anch=(%.1f,%.1f,%.1f,%.1f) vis=%d clip=%d"),
					Pos.X, Pos.Y, Size.X, Size.Y,
					Anch.Minimum.X, Anch.Minimum.Y, Anch.Maximum.X, Anch.Maximum.Y,
					(int32)W->GetVisibility(), (int32)W->GetClipping());
			}
			else
			{
				SlotInfo = TEXT("no slot");
			}
		}

		UE_LOG(LogWowFrame, Warning, TEXT("  %-25s  rect=(%.0f,%.0f,%.0f,%.0f) hidden=%d setAll=%d parent='%s' %s"),
			FrameName, RX, RY, RW, RH, Def.bHidden, Def.bSetAllPoints, *Def.Parent, *SlotInfo);
	}

	if (RootCanvas.IsValid())
	{
		const auto RootGeometry = RootCanvas->GetCachedGeometry();
		for (const auto& Pair : NamedTextureWidgets)
		{
			UWidget* Image = Pair.Value.Get();
			if (!Image) continue;
			bool bVisible = true;
			for (UWidget* W = Image; W; W = W->GetParent())
				if (W->GetVisibility() == ESlateVisibility::Collapsed || W->GetVisibility() == ESlateVisibility::Hidden) { bVisible = false; break; }
			if (!bVisible) continue;
			const auto Geometry = Image->GetCachedGeometry();
			const auto P = RootGeometry.AbsoluteToLocal(Geometry.LocalToAbsolute(FVector2D::ZeroVector)) / UIScale;
			const auto End = RootGeometry.AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize())) / UIScale;
			if ((P.X < 760 && End.X > 600 && P.Y < 420 && End.Y > 350) || (End.X - P.X > 600 && End.Y - P.Y < 100))
				UE_LOG(LogWowFrame, Verbose, TEXT("HudRegion: %s owner=%s actual=(%.1f,%.1f,%.1f,%.1f)"),
					*Pair.Key, *RegionOwners.FindRef(Pair.Key), P.X, P.Y, End.X-P.X, End.Y-P.Y);
		}
	}
	UE_LOG(LogWowFrame, Warning, TEXT("═══════════ END LAYOUT DUMP ═══════════"));
}

// ── Mouse Hit-Testing & Click Dispatch ───────────────────────────────────────

int64 FWowFrameManager::HitTestFrames(float ScreenX, float ScreenY) const
{
    const float X = ScreenX / RawViewportScale, Y = ScreenY / RawViewportScale;
    int64 Best = -1;
    int32 BestZ = MIN_int32;
    for (const auto& Pair : Frames)
    {
        const auto& Entry = Pair.Value;
        if ((!Entry.bMouseEnabled && !Entry.bMouseWheelEnabled) || !Entry.Widget.IsValid()) continue;
        bool Visible = true;
        int64 Ancestor = Pair.Key;
        for (int32 Depth = 0; Ancestor >= 0 && Depth < 64; ++Depth)
        {
            const auto* Parent = Frames.Find(Ancestor);
            if (!Parent) break;
            if (Parent->Def.bHidden || (Parent->Widget.IsValid() && Parent->Widget->GetVisibility() == ESlateVisibility::Collapsed)) { Visible = false; break; }
            if(Parent->Def.Type==EWowFrameType::ScrollFrame)
                if(const auto* Clip=FrameRects.Find(Parent->Def.Name); Clip && (X<Clip->X || X>Clip->X+Clip->W || Y<Clip->Y || Y>Clip->Y+Clip->H)) { Visible=false; break; }
            Ancestor = Parent->ParentHandle;
        }
        if (!Visible) continue;
        const auto* Rect = FrameRects.Find(Entry.Def.Name);
        if (!Rect || X < Rect->X || X > Rect->X + Rect->W || Y < Rect->Y || Y > Rect->Y + Rect->H) continue;
        const int32 Z = static_cast<int32>(Entry.Def.Strata) * 1000 + Entry.Def.FrameLevel;
        if (Z > BestZ || (Z == BestZ && Pair.Key > Best)) { Best = Pair.Key; BestZ = Z; }
    }
    return Best;
}
bool FWowFrameManager::IsMouseOverFrame(int64 Handle, float TopOffset, float BottomOffset, float LeftOffset, float RightOffset) const
{
	const FFrameEntry* Entry = Frames.Find(Handle);
	if (!Entry || !Entry->Widget.IsValid())
	{
		return false;
	}

	if (Entry->Widget->GetVisibility() == ESlateVisibility::Collapsed)
	{
		return false;
	}

	// Mouse handlers may move a child region before asking IsMouseOver().
	// Resolve this frame again after that cache invalidation.
	const FFrameRect CurrentRect=const_cast<FWowFrameManager*>(this)->GetFrameRect(Entry->Def.Name);
	const FFrameRect* Rect=&CurrentRect;
	if (Rect->W <= 0.f || Rect->H <= 0.f || RawViewportScale <= 0.f)
	{
		return false;
	}

	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}

	const FVector2D CursorPos = LastPointerPosition;
	const float WowX = CursorPos.X / RawViewportScale;
	const float WowY = CursorPos.Y / RawViewportScale;

	const float Left = Rect->X + LeftOffset;
	const float Right = Rect->X + Rect->W + RightOffset;
	const float Top = Rect->Y - TopOffset;
	const float Bottom = Rect->Y + Rect->H - BottomOffset;
	return WowX >= Left && WowX <= Right && WowY >= Top && WowY <= Bottom;
}

void FWowFrameManager::TickWidgetEvents()
{
    if(EventSystem && FPlatformTime::Seconds()>=NextScrollRangeCheck)
    {
        NextScrollRangeCheck=FPlatformTime::Seconds()+.1;
        const auto Children=ScrollChildHandles;
        for(const auto& Pair:Children) if(IsFrameDrawn(Pair.Key) && EventSystem->HasFrameScript(Pair.Key,TEXT("OnScrollRangeChanged")))
        {
            const float Range=GetVerticalScrollRange(Pair.Key); const auto* Previous=LastScrollRanges.Find(Pair.Key);
            if(!Previous || !FMath::IsNearlyEqual(*Previous,Range,.5f))
            { LastScrollRanges.Add(Pair.Key,Range); EventSystem->RunFrameScript(Pair.Key,TEXT("OnScrollRangeChanged"),{TEXT("0"),FString::SanitizeFloat(Range)}); }
        }
    }
	// Poll EditBox widgets for text changes
	for (auto& Pair : EditBoxWidgets)
	{
		if (!Pair.Value.IsValid()) continue;
		FString CurrentText = Pair.Value->GetText().ToString();
		if(const auto* Limit=EditBoxMaxLetters.Find(Pair.Key); Limit && *Limit>0 && CurrentText.Len()>*Limit) CurrentText.LeftInline(*Limit);
		if(const auto* Limit=EditBoxMaxBytes.Find(Pair.Key); Limit && *Limit>0)
			while(CurrentText.Len() && FTCHARToUTF8(*CurrentText).Length()>*Limit) CurrentText.LeftChopInline(1);
		if(CurrentText!=Pair.Value->GetText().ToString()) Pair.Value->SetText(FText::FromString(CurrentText));
		FString* LastText = EditBoxLastText.Find(Pair.Key);
		if (!LastText)
		{
			EditBoxLastText.Add(Pair.Key, CurrentText);
		}
		else if (*LastText != CurrentText)
		{
			*LastText = CurrentText;
			if (EventSystem)
			{
				EventSystem->RunFrameScript(Pair.Key, TEXT("OnTextChanged"),{TEXT("1")});
			}
		}
	}

	// Poll Slider widgets for value changes
	for (auto& Pair : SliderWidgets)
	{
		if (!Pair.Value.IsValid()) continue;
		float CurrentValue = Pair.Value->GetValue();
		float* LastValue = SliderLastValue.Find(Pair.Key);
		if (!LastValue)
		{
			SliderLastValue.Add(Pair.Key, CurrentValue);
		}
		else if (!FMath::IsNearlyEqual(*LastValue, CurrentValue))
		{
			*LastValue = CurrentValue;
			if (EventSystem)
			{
				EventSystem->RunFrameScript(Pair.Key, TEXT("OnValueChanged"),
					{FString::SanitizeFloat(CurrentValue)});
			}
		}
	}
}

bool FWowFrameManager::DispatchEditBoxEnterPressed()
{
	for (const auto& Pair : EditBoxWidgets)
	{
		if (!Pair.Value.IsValid()) continue;
		if (IsFrameHierarchyShown(Pair.Key) && Pair.Value->HasKeyboardFocus())
		{
			if (EventSystem)
			{
				EventSystem->RunFrameScript(Pair.Key, TEXT("OnEnterPressed"));
			}
			return true;
		}
	}
	return false;
}

bool FWowFrameManager::DispatchEditBoxEscapePressed()
{
	for (const auto& Pair : EditBoxWidgets)
	{
		if (!Pair.Value.IsValid()) continue;
		if (IsFrameHierarchyShown(Pair.Key) && Pair.Value->HasKeyboardFocus())
		{
			if (EventSystem)
			{
				EventSystem->RunFrameScript(Pair.Key, TEXT("OnEscapePressed"));
			}
			return true;
		}
	}
	return false;
}

void FWowFrameManager::UpdateMouseHover(float ScreenX, float ScreenY)
{
	LastPointerPosition=FVector2D(ScreenX,ScreenY);
	int64 NewHover = HitTestFrames(ScreenX, ScreenY);

	if (NewHover != HoverFrameHandle)
	{
		// Dispatch OnLeave for the old frame
		if (HoverFrameHandle >= 0 && EventSystem)
		{
			EventSystem->RunFrameScript(HoverFrameHandle, TEXT("OnLeave"), {TEXT("0")});
		}

		const int64 OldHover=HoverFrameHandle;
		HoverFrameHandle = NewHover;
		RefreshButtonVisuals(OldHover); RefreshButtonVisuals(NewHover);

		// Dispatch OnEnter for the new frame
		if (HoverFrameHandle >= 0 && EventSystem)
		{
			EventSystem->RunFrameScript(HoverFrameHandle, TEXT("OnEnter"), {TEXT("0")});
		}
	}
}

void FWowFrameManager::DispatchMouseDown(int64 Handle, const FString& Button)
{
	if (!EventSystem || Handle < 0) return;
	if(auto* Widget=GetButtonWidget(Handle))
	{
		if(!Widget->GetIsEnabled()) return;
		ButtonVisualStates.FindOrAdd(Handle).MousePressed=true; RefreshButtonVisuals(Handle);
	}
	EventSystem->RunFrameScript(Handle, TEXT("OnMouseDown"), {Button});
}

bool FWowFrameManager::DispatchDragStart(int64 Handle,const FString& Button)
{
    return EventSystem && EventSystem->GetFrameAttribute(Handle,TEXT("drag_")+Button)==TEXT("1") && EventSystem->RunFrameScript(Handle,TEXT("OnDragStart"),{Button});
}

void FWowFrameManager::DispatchDragStop(int64 Handle,const FString& Button)
{
    if(EventSystem) EventSystem->RunFrameScript(Handle,TEXT("OnDragStop"),{Button});
}

bool FWowFrameManager::DispatchMouseWheel(int64 Handle,float Delta)
{
    for(int32 Depth=0; Handle>=0 && Depth<64; ++Depth)
    {
        if(IsFrameMouseWheelEnabled(Handle) && EventSystem)
            return EventSystem->RunFrameScript(Handle,TEXT("OnMouseWheel"),{FString::SanitizeFloat(Delta)});
        Handle=GetParentHandle(Handle);
    }
    return false;
}

void FWowFrameManager::DispatchMouseUp(int64 Handle, const FString& Button)
{
	if (!EventSystem || Handle < 0) return;
	if(auto* Visual=ButtonVisualStates.Find(Handle)) { Visual->MousePressed=false; if(!Visual->PressLocked) Visual->ForcedPressed=false; RefreshButtonVisuals(Handle); }
	if(auto* Widget=GetButtonWidget(Handle); Widget && !Widget->GetIsEnabled()) return;
	EventSystem->RunFrameScript(Handle, TEXT("OnMouseUp"), {Button});
}

void FWowFrameManager::DispatchClick(int64 Handle, const FString& Button, bool bMouseDown)
{
	if (!EventSystem || Handle < 0) return;
	if(const auto* Widget=GetButtonWidget(Handle); Widget && !Widget->GetIsEnabled()) return;

	const FFrameEntry* Entry = Frames.Find(Handle);
	if(Entry && Entry->Def.Type==EWowFrameType::CheckButton && Button==TEXT("LeftButton")) SetChecked(Handle,!GetChecked(Handle));

	// Native secure action dispatch — emulates WoW's C++ SecureActionButton behavior.
	// The FrameXML OnLoad for SecureActionButtonTemplate sets type="action" via Lua,
	// but this often fails in our environment. So we also detect action buttons by
	// name pattern (ActionButton1-12, MultiBarBottomLeftButton1-12, etc.) and by
	// checking the Lua attribute as a fallback.
	if (Entry && (Entry->Def.Type == EWowFrameType::Button || Entry->Def.Type == EWowFrameType::CheckButton))
	{
		int32 ActionSlot = -1;

		// Method 1: Detect action buttons by name pattern
		// ActionButton1-12 → slots 1-12 (page 1)
		// MultiBarBottomLeftButton1-12 → slots 61-72
		// MultiBarBottomRightButton1-12 → slots 49-60
		// MultiBarRightButton1-12 → slots 25-36
		// MultiBarLeftButton1-12 → slots 37-48
		// BonusActionButton1-12 → slots 73-84
		const FString& Name = Entry->Def.Name;
		int32 ButtonNum = 0;

		if (Name.StartsWith(TEXT("ActionButton")) && Name.Len() <= 14)
		{
			ButtonNum = FCString::Atoi(*Name.Mid(12));
			if (ButtonNum >= 1 && ButtonNum <= 12) ActionSlot = ButtonNum;
		}
		else if (Name.StartsWith(TEXT("MultiBarBottomLeftButton")))
		{
			ButtonNum = FCString::Atoi(*Name.Mid(24));
			if (ButtonNum >= 1 && ButtonNum <= 12) ActionSlot = 60 + ButtonNum;
		}
		else if (Name.StartsWith(TEXT("MultiBarBottomRightButton")))
		{
			ButtonNum = FCString::Atoi(*Name.Mid(25));
			if (ButtonNum >= 1 && ButtonNum <= 12) ActionSlot = 48 + ButtonNum;
		}
		else if (Name.StartsWith(TEXT("MultiBarRightButton")))
		{
			ButtonNum = FCString::Atoi(*Name.Mid(19));
			if (ButtonNum >= 1 && ButtonNum <= 12) ActionSlot = 24 + ButtonNum;
		}
		else if (Name.StartsWith(TEXT("MultiBarLeftButton")))
		{
			ButtonNum = FCString::Atoi(*Name.Mid(18));
			if (ButtonNum >= 1 && ButtonNum <= 12) ActionSlot = 36 + ButtonNum;
		}
		else if (Name.StartsWith(TEXT("BonusActionButton")))
		{
			ButtonNum = FCString::Atoi(*Name.Mid(17));
			if (ButtonNum >= 1 && ButtonNum <= 12) ActionSlot = 72 + ButtonNum;
		}

		// Method 2: Check Lua attribute (if OnLoad managed to set it)
		if (ActionSlot < 0)
		{
			FString ActionType = EventSystem->GetFrameAttribute(Handle, TEXT("type"));
			if (ActionType.Equals(TEXT("action"), ESearchCase::IgnoreCase))
			{
				FString ActionAttr = EventSystem->GetFrameAttribute(Handle, TEXT("action"));
				if (!ActionAttr.IsEmpty())
				{
					ActionSlot = FCString::Atoi(*ActionAttr);
				}
				else
				{
					int32 FrameID = EventSystem->GetFrameID(Handle);
					if (FrameID > 0) ActionSlot = FrameID;
				}
			}
			else if (ActionType.Equals(TEXT("spell"), ESearchCase::IgnoreCase))
			{
				FString SpellAttr = EventSystem->GetFrameAttribute(Handle, TEXT("spell"));
				if (!SpellAttr.IsEmpty())
				{
					int32 SpellId = FCString::Atoi(*SpellAttr);
					if (SpellId > 0 && OnSecureSpellDispatch.IsBound())
					{
						OnSecureSpellDispatch.Execute(SpellId);
					}
				}
			}
		}

		// Stance/page scripts assign the actual slot. Prefer that live value to
		// the name fallback, which only represents the button's first page.
		const FString LiveAction=EventSystem->GetFrameAttribute(Handle,TEXT("action"));
		if (!LiveAction.IsEmpty()) ActionSlot=FCString::Atoi(*LiveAction);
		if (ActionSlot > 0 && OnSecureActionDispatch.IsBound())
		{
			OnSecureActionDispatch.Execute(ActionSlot - 1); // Convert to 0-based
			UE_LOG(LogWowFrame, Verbose, TEXT("Secure action dispatch: slot %d (frame %s)"),
				ActionSlot, *Entry->Def.Name);
		}
	}

	EventSystem->RunFrameClickScript(Handle, TEXT("OnClick"), Button, bMouseDown);
}

bool FWowFrameManager::DispatchReceiveDrag(int64 Handle)
{
	if (!EventSystem || Handle < 0)
	{
		return false;
	}

	return EventSystem->RunFrameScript(Handle, TEXT("OnReceiveDrag"));
}

// ── ScrollFrame Implementation ──────────────────────────────────────────────

void FWowFrameManager::SetScrollChild(int64 ScrollFrameHandle,int64 ChildHandle)
{
    auto* Scroll=Frames.Find(ScrollFrameHandle); auto* Child=Frames.Find(ChildHandle);
    if(!Scroll || !Child || ScrollFrameHandle==ChildHandle || Scroll->Def.Type!=EWowFrameType::ScrollFrame) return;
    if(!ReparentFrame(ChildHandle,ScrollFrameHandle)) return;
    ScrollChildHandles.Add(ScrollFrameHandle,ChildHandle);
    Child=Frames.Find(ChildHandle);
    if(Child->Def.Anchors.IsEmpty()) { FWowAnchor A; A.Point=A.RelativePoint=EWowAnchorPoint::TOPLEFT; Child->Def.Anchors.Add(A); }
    if(Scroll->Widget.IsValid()) Scroll->Widget->SetClipping(EWidgetClipping::ClipToBoundsAlways);
    FrameRects.Reset(); bLayoutDirty=true;
}
int64 FWowFrameManager::GetScrollChild(int64 ScrollFrameHandle) const
{
	if (ScrollFrameHandle < 0)
		return -1;

	const auto* Child=ScrollChildHandles.Find(ScrollFrameHandle); return Child?*Child:-1;
}

void FWowFrameManager::SetVerticalScroll(int64 Handle,float Offset)
{
    if(!FMath::IsFinite(Offset)) return;
    const float Value=FMath::Clamp(Offset,0.f,GetVerticalScrollRange(Handle));
    if(FMath::IsNearlyEqual(Value,ScrollVerticalOffsets.FindRef(Handle))) return;
    ScrollVerticalOffsets.Add(Handle,Value); FrameRects.Reset(); bLayoutDirty=true;
    if(EventSystem) EventSystem->RunFrameScript(Handle,TEXT("OnVerticalScroll"),{FString::SanitizeFloat(Value)});
}
float FWowFrameManager::GetVerticalScroll(int64 ScrollFrameHandle) const
{
	if (ScrollFrameHandle < 0)
		return 0.0f;

	return ScrollVerticalOffsets.FindRef(ScrollFrameHandle);
}

void FWowFrameManager::SetHorizontalScroll(int64 Handle,float Offset)
{
    if(!FMath::IsFinite(Offset)) return;
    const float Value=FMath::Clamp(Offset,0.f,GetHorizontalScrollRange(Handle));
    if(FMath::IsNearlyEqual(Value,ScrollHorizontalOffsets.FindRef(Handle))) return;
    ScrollHorizontalOffsets.Add(Handle,Value); FrameRects.Reset(); bLayoutDirty=true;
    if(EventSystem) EventSystem->RunFrameScript(Handle,TEXT("OnHorizontalScroll"),{FString::SanitizeFloat(Value)});
}
float FWowFrameManager::GetHorizontalScroll(int64 ScrollFrameHandle) const
{
	if (ScrollFrameHandle < 0)
		return 0.0f;

	return ScrollHorizontalOffsets.FindRef(ScrollFrameHandle);
}

float FWowFrameManager::GetVerticalScrollRange(int64 Handle) const
{
    const auto* Scroll=Frames.Find(Handle); const int64 Child=GetScrollChild(Handle);
    if(!Scroll || Child<0 || Scroll->Def.Type!=EWowFrameType::ScrollFrame) return 0;
    auto* Layout=const_cast<FWowFrameManager*>(this); if(bLayoutDirty) Layout->RefreshLayout();
    const auto ChildRect=Layout->GetLayoutRect(Child),ScrollRect=Layout->GetLayoutRect(Handle);
    const auto Descendant=[&](int64 Owner) { for(int32 Depth=0;Owner>=0 && Depth<64;++Depth,Owner=GetParentHandle(Owner)) if(Owner==Child) return true; return false; };
    float Bottom=ChildRect.Y+ChildRect.W;
    for(const auto& Pair:Frames) if(Descendant(Pair.Key) && IsFrameDrawn(Pair.Key)) { const auto R=Layout->GetLayoutRect(Pair.Key); Bottom=FMath::Max(Bottom,float(R.Y+R.W)); }
    for(const auto& Region:RegionLayouts)
    {
        const int64 Owner=FindFrame(Region.Owner);
        if(!Descendant(Owner) || !IsFrameDrawn(Owner) || !Region.Widget.IsValid() || Region.Widget->GetVisibility()==ESlateVisibility::Collapsed) continue;
        const auto* Slot=Cast<UCanvasPanelSlot>(Region.Widget->Slot); if(!Slot) continue;
        const auto R=Layout->GetLayoutRect(Owner); const float Scale=GetEffectiveFrameScale(Owner)/UIScale;
        Bottom=FMath::Max(Bottom,float(R.Y+(Slot->GetPosition().Y+Slot->GetSize().Y)*Scale));
    }
    return FMath::Max(0.f,(Bottom-ChildRect.Y-ScrollRect.W)/GetEffectiveFrameScale(Handle));
}
float FWowFrameManager::GetHorizontalScrollRange(int64 ScrollFrameHandle) const
{
	if (ScrollFrameHandle < 0)
		return 0.0f;

	const FFrameEntry* ScrollFrameEntry = Frames.Find(ScrollFrameHandle);
	if (!ScrollFrameEntry || ScrollFrameEntry->Def.Type != EWowFrameType::ScrollFrame)
		return 0.0f;

	int64 ChildHandle = GetScrollChild(ScrollFrameHandle);
	if (ChildHandle < 0)
		return 0.0f;

	const FFrameEntry* ChildEntry = Frames.Find(ChildHandle);
	if (!ChildEntry || !ChildEntry->Widget.IsValid() || !ScrollFrameEntry->Widget.IsValid())
		return 0.0f;

	// Get frame sizes in WoW coordinates (pre-scaling)
	float ScrollFrameWidth = ScrollFrameEntry->Def.Width;
	float ChildWidth = ChildEntry->Def.Width;

	// If frame width is 0 (auto-sized), get actual widget size
	if (ScrollFrameWidth <= 0.0f)
	{
		if (UCanvasPanelSlot* ScrollSlot = Cast<UCanvasPanelSlot>(ScrollFrameEntry->Widget->Slot))
		{
			ScrollFrameWidth = ScrollSlot->GetSize().X / UIScale;
		}
	}

	if (ChildWidth <= 0.0f)
	{
		if (UCanvasPanelSlot* ChildSlot = Cast<UCanvasPanelSlot>(ChildEntry->Widget->Slot))
		{
			ChildWidth = ChildSlot->GetSize().X / UIScale;
		}
	}

	// Maximum scroll is the difference between child width and frame width
	return FMath::Max(0.0f, ChildWidth - ScrollFrameWidth);
}

// ── Tooltip Implementation ──────────────────────────────────────────────────

void FWowFrameManager::TooltipSetOwner(int64 TooltipHandle, int64 OwnerHandle, const FString& AnchorType)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	State->OwnerHandle = OwnerHandle;
	State->AnchorType = AnchorType;

	// Position the tooltip relative to owner frame if valid
	if (OwnerHandle > 0)
	{
		const FWowFrameDef* OwnerDef = GetFrameDef(OwnerHandle);
		if (OwnerDef)
		{
			// Create anchor based on anchor type
			TArray<FWowAnchor> NewAnchors;
			FWowAnchor TooltipAnchor;

			if (AnchorType == TEXT("ANCHOR_LEFT"))
			{
				TooltipAnchor.Point = EWowAnchorPoint::BOTTOMRIGHT;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::BOTTOMLEFT;
			}
			else if (AnchorType == TEXT("ANCHOR_RIGHT"))
			{
				TooltipAnchor.Point = EWowAnchorPoint::BOTTOMLEFT;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::BOTTOMRIGHT;
			}
			else if (AnchorType == TEXT("ANCHOR_TOP"))
			{
				TooltipAnchor.Point = EWowAnchorPoint::BOTTOM;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::TOP;
			}
			else if (AnchorType == TEXT("ANCHOR_BOTTOM"))
			{
				TooltipAnchor.Point = EWowAnchorPoint::TOP;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::BOTTOM;
			}
			else // ANCHOR_CURSOR or default
			{
				TooltipAnchor.Point = EWowAnchorPoint::BOTTOMLEFT;
				TooltipAnchor.RelativePoint = EWowAnchorPoint::BOTTOMLEFT;
				TooltipAnchor.OffsetX = 5.f;
				TooltipAnchor.OffsetY = 5.f;
			}

			TooltipAnchor.RelativeTo = OwnerDef->Name;
			NewAnchors.Add(TooltipAnchor);
			SetFrameAnchors(TooltipHandle, NewAnchors);
		}
	}
}

void FWowFrameManager::TooltipAddLine(int64 TooltipHandle, const FString& Text, float R, float G, float B, bool bWrapText)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	FTooltipLine NewLine;
	NewLine.Text = Text;
	NewLine.Color = FLinearColor(R, G, B, 1.0f);
	NewLine.bWrapText = bWrapText;
	State->Lines.Add(NewLine);

	TooltipUpdateDisplay(TooltipHandle);
}

void FWowFrameManager::TooltipAddDoubleLine(int64 TooltipHandle, const FString& LeftText, const FString& RightText,
											 float LR, float LG, float LB, float RR, float RG, float RB)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	FTooltipDoubleLine NewLine;
	NewLine.LeftText = LeftText;
	NewLine.RightText = RightText;
	NewLine.LeftColor = FLinearColor(LR, LG, LB, 1.0f);
	NewLine.RightColor = FLinearColor(RR, RG, RB, 1.0f);
	State->DoubleLines.Add(NewLine);

	TooltipUpdateDisplay(TooltipHandle);
}

void FWowFrameManager::TooltipClearLines(int64 TooltipHandle)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	State->Lines.Empty();
	State->DoubleLines.Empty();
	TooltipUpdateDisplay(TooltipHandle);
}

int32 FWowFrameManager::TooltipNumLines(int64 TooltipHandle) const
{
	const FTooltipState* State = GetTooltipState(TooltipHandle);
	return State ? State->GetTotalLines() : 0;
}

void FWowFrameManager::TooltipSetText(int64 TooltipHandle, const FString& Text)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	// SetText replaces line 1 or creates it
	State->Lines.Empty();
	State->DoubleLines.Empty();

	FTooltipLine NewLine;
	NewLine.Text = Text;
	NewLine.Color = FLinearColor::White;
	State->Lines.Add(NewLine);

	TooltipUpdateDisplay(TooltipHandle);
}

void FWowFrameManager::TooltipAppendText(int64 TooltipHandle, const FString& Text)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	if (State->Lines.Num() > 0)
	{
		// Append to the last line
		State->Lines.Last().Text += Text;
	}
	else
	{
		// Create new line if none exist
		FTooltipLine NewLine;
		NewLine.Text = Text;
		NewLine.Color = FLinearColor::White;
		State->Lines.Add(NewLine);
	}

	TooltipUpdateDisplay(TooltipHandle);
}

void FWowFrameManager::TooltipUpdateDisplay(int64 TooltipHandle)
{
	const FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	const FWowFrameDef* TooltipDef = GetFrameDef(TooltipHandle);
	if (!TooltipDef) return;
	const FString Prefix=TooltipDef->Name;
	float TextY=8;
	const auto EnsureText=[&](int32 Line,bool Right)->UTextBlock*
	{
		const FString Name=FString::Printf(TEXT("%sText%s%d"),*Prefix,Right?TEXT("Right"):TEXT("Left"),Line);
		UTextBlock* Text=GetNamedFontString(Name);
		if(!Text) if(auto* Canvas=Cast<UCanvasPanel>(GetWidgetForHandle(TooltipHandle)))
		{
			Text=NewObject<UTextBlock>(Canvas); Canvas->AddChildToCanvas(Text);
			NamedFontStringWidgets.Add(Name,Text); RegionOwners.Add(Name,Prefix);
			if(EventSystem) EventSystem->CreateTextureRegionGlobal(Name,TooltipHandle,TEXT("FontString"));
		}
		if(Text)
		{
			if(FontManager) Text->SetFont(FontManager->GetDefaultFont(FMath::Max(1,FMath::RoundToInt(12*UIScale))));
			Text->SetJustification(Right?ETextJustify::Right:ETextJustify::Left);
			if(auto* Slot=Cast<UCanvasPanelSlot>(Text->Slot))
			{
				Slot->SetAnchors(FAnchors(Right?1.f:0.f,0)); Slot->SetAlignment(FVector2D(Right?1.f:0.f,0));
				Slot->SetPosition(FVector2D((Right?-8:8)*UIScale,TextY*UIScale)); Slot->SetAutoSize(true); Slot->SetZOrder(100);
			}
		}
		return Text;
	};

	// Update FontString widgets with tooltip lines
	int32 LineIndex = 0;

	// Process regular lines
	for (const FTooltipLine& Line : State->Lines)
	{
		LineIndex++;

		// Find the FontString widget for this line
		UTextBlock* LeftTextBlock = EnsureText(LineIndex,false);
		if(auto* Right=GetNamedFontString(FString::Printf(TEXT("%sTextRight%d"),*Prefix,LineIndex))) Right->SetVisibility(ESlateVisibility::Collapsed);

		if (LeftTextBlock)
		{
			LeftTextBlock->SetText(FText::FromString(WowUiPlainText(Line.Text)));
			LeftTextBlock->SetAutoWrapText(Line.bWrapText); LeftTextBlock->SetWrapTextAt(Line.bWrapText?300*UIScale:0);
			LeftTextBlock->SetColorAndOpacity(FSlateColor(Line.Color));
			LeftTextBlock->SetVisibility(ESlateVisibility::Visible);
			LeftTextBlock->ForceLayoutPrepass();
			TextY+=FMath::Max(14.f,float(LeftTextBlock->GetDesiredSize().Y/UIScale));
		}
	}

	// Process double lines
	for (const FTooltipDoubleLine& DoubleLine : State->DoubleLines)
	{
		LineIndex++;

		// Left side
		UTextBlock* LeftTextBlock = EnsureText(LineIndex,false);
		if (LeftTextBlock)
		{
			LeftTextBlock->SetText(FText::FromString(DoubleLine.LeftText));
			LeftTextBlock->SetColorAndOpacity(FSlateColor(DoubleLine.LeftColor));
			LeftTextBlock->SetVisibility(ESlateVisibility::Visible);
		}

		// Right side
		UTextBlock* RightTextBlock = EnsureText(LineIndex,true);
		if (RightTextBlock)
		{
			RightTextBlock->SetText(FText::FromString(DoubleLine.RightText));
			RightTextBlock->SetColorAndOpacity(FSlateColor(DoubleLine.RightColor));
			RightTextBlock->SetVisibility(ESlateVisibility::Visible);
		}
		TextY+=14;
	}

	// Hide unused FontString widgets
	for (int32 HideIndex = LineIndex + 1; HideIndex <= 30; HideIndex++) // WoW usually has up to 30 tooltip lines
	{
		FString LeftFontStringName = FString::Printf(TEXT("%sTextLeft%d"),*Prefix,HideIndex);
		UTextBlock* LeftTextBlock = GetNamedFontString(LeftFontStringName);
		if (LeftTextBlock)
		{
			LeftTextBlock->SetVisibility(ESlateVisibility::Collapsed);
		}

		FString RightFontStringName = FString::Printf(TEXT("%sTextRight%d"),*Prefix,HideIndex);
		UTextBlock* RightTextBlock = GetNamedFontString(RightFontStringName);
		if (RightTextBlock)
		{
			RightTextBlock->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Calculate and set tooltip size based on content
	float TooltipWidth = State->MinimumWidth;
	float TooltipHeight = 0.f;
	const float LineHeight = 14.f;
	const float Padding = 16.f; // Padding around tooltip content

	// Calculate width based on text content
	for (const FTooltipLine& Line : State->Lines)
	{
		// Estimate text width (this is a simplification)
		float EstimatedWidth = WowUiPlainText(Line.Text).Len() * 6.f;
		if(Line.bWrapText) EstimatedWidth=FMath::Min(300.f,EstimatedWidth);
		TooltipWidth = FMath::Max(TooltipWidth, EstimatedWidth);
	}

	for (const FTooltipDoubleLine& DoubleLine : State->DoubleLines)
	{
		float EstimatedWidth = (DoubleLine.LeftText.Len() + DoubleLine.RightText.Len()) * 7.f;
		TooltipWidth = FMath::Max(TooltipWidth, EstimatedWidth);
	}

	TooltipHeight = TextY+8;
	TooltipWidth += Padding;

	// Set the tooltip frame size
	SetFrameSize(TooltipHandle, TooltipWidth, TooltipHeight);
	if(State->AnchorType!=TEXT("ANCHOR_NONE"))
	{
		const auto Root=GetFrameRect(TEXT("UIParent")); const auto Owner=GetLayoutRect(State->OwnerHandle);
		FVector2D P=LastPointerPosition/RawViewportScale+FVector2D(16,-TooltipHeight-12);
		if(State->OwnerHandle>=0 && !State->AnchorType.Contains(TEXT("CURSOR")))
		{
			P=FVector2D(Owner.X+Owner.Z+8,Owner.Y);
			if(State->AnchorType.Contains(TEXT("LEFT"))) P.X=Owner.X-TooltipWidth-8;
			if(State->AnchorType==TEXT("ANCHOR_TOP")) P=FVector2D(Owner.X+(Owner.Z-TooltipWidth)*.5,Owner.Y-TooltipHeight-8);
			if(State->AnchorType==TEXT("ANCHOR_BOTTOM")) P=FVector2D(Owner.X,Owner.Y+Owner.W+8);
		}
		FWowAnchor Anchor; Anchor.Point=Anchor.RelativePoint=EWowAnchorPoint::TOPLEFT; Anchor.RelativeTo=TEXT("UIParent");
		Anchor.OffsetX=FMath::Clamp(float(P.X),4.f,FMath::Max(4.f,Root.W-TooltipWidth-4)); Anchor.OffsetY=-FMath::Clamp(float(P.Y),4.f,FMath::Max(4.f,Root.H-TooltipHeight-4));
		SetFrameAnchors(TooltipHandle,{Anchor});
	}
}

void FWowFrameManager::TooltipSetItem(int64 TooltipHandle, const FString& ItemLink)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	State->ItemLink = ItemLink;
	State->SpellId = 0; // Clear other contexts
	State->UnitToken.Empty();

	// TODO: Parse item link and populate tooltip with item data
	// For now, just add a placeholder line
	TooltipClearLines(TooltipHandle);
	TooltipAddLine(TooltipHandle, FString::Printf(TEXT("Item: %s"), *ItemLink), 1.0f, 1.0f, 1.0f, false);
}

void FWowFrameManager::TooltipSetSpell(int64 TooltipHandle, int32 SpellId,int32 Level,bool Aura)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	State->SpellId = SpellId;
	State->ItemLink.Empty(); // Clear other contexts
	State->UnitToken.Empty();

	TooltipClearLines(TooltipHandle);
	const auto& Spells=FDbcStore::Get().Spells(); const auto* Spell=Spells.GetById(SpellId);
	if(!Spell) { SetFrameVisible(TooltipHandle,false); return; }
	TooltipAddLine(TooltipHandle,Spell->SpellName,1,1,1,false);
	if(!Spell->Rank.IsEmpty()) TooltipAddLine(TooltipHandle,Spell->Rank,.7f,.7f,.7f,false);
	if(!Aura)
	{
		if(Spell->ManaCost || Spell->ManaCostPercentage)
		{
			static const TCHAR* Power[]={TEXT("Mana"),TEXT("Rage"),TEXT("Focus"),TEXT("Energy"),TEXT("Happiness"),TEXT("Runes"),TEXT("Runic Power")};
			const FString Cost=Spell->ManaCostPercentage?FString::Printf(TEXT("%u%% of base mana"),Spell->ManaCostPercentage):FString::Printf(TEXT("%g %s"),Spell->ManaCost/(Spell->PowerType==1 || Spell->PowerType==6?10.f:1.f),Spell->PowerType<UE_ARRAY_COUNT(Power)?Power[Spell->PowerType]:TEXT(""));
			TooltipAddLine(TooltipHandle,Cost,1,1,1,false);
		}
		if(const auto* Range=Spells.GetRange(Spell->RangeIndex); Range && Range->Max[0]>0) TooltipAddLine(TooltipHandle,FString::Printf(TEXT("%g yd range"),Range->Max[0]),1,1,1,false);
		const float Cast=Spells.CastTime(Spell->CastingTimeIndex);
		TooltipAddLine(TooltipHandle,Cast>0?FString::Printf(TEXT("%g sec cast"),Cast):TEXT("Instant"),1,1,1,false);
		if(Spell->RecoveryTime) TooltipAddLine(TooltipHandle,FString::Printf(TEXT("%g sec cooldown"),Spell->RecoveryTime/1000.f),1,1,1,false);
	}
	const FString Description=Spells.Description(SpellId,Level,Aura);
	if(!Description.IsEmpty()) TooltipAddLine(TooltipHandle,Description,1,.82f,0,true);
	SetFrameVisible(TooltipHandle,true);
}

void FWowFrameManager::TooltipSetUnit(int64 TooltipHandle, const FString& UnitToken)
{
	FTooltipState* State = GetTooltipState(TooltipHandle);
	if (!State) return;

	State->UnitToken = UnitToken;
	State->ItemLink.Empty(); // Clear other contexts
	State->SpellId = 0;

	// TODO: Lookup unit data and populate tooltip
	// For now, just add a placeholder line
	TooltipClearLines(TooltipHandle);
	TooltipAddLine(TooltipHandle, FString::Printf(TEXT("Unit: %s"), *UnitToken), 1.0f, 1.0f, 1.0f, false);
}

FTooltipState* FWowFrameManager::GetTooltipState(int64 TooltipHandle)
{
	// Ensure the tooltip state exists
	if (!TooltipStates.Contains(TooltipHandle))
	{
		TooltipStates.Add(TooltipHandle, FTooltipState());
	}
	return &TooltipStates[TooltipHandle];
}

const FTooltipState* FWowFrameManager::GetTooltipState(int64 TooltipHandle) const
{
	return TooltipStates.Find(TooltipHandle);
}
