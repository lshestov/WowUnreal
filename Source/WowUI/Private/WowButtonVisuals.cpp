#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

FString FWowFrameManager::EnsureButtonTexture(int64 Handle,const FString& State)
{
    const auto* Entry=Frames.Find(Handle);
    if(!Entry || !GetButtonWidget(Handle)) return {};
    if(const auto* Existing=ButtonVisualStates.FindOrAdd(Handle).Regions.Find(State)) return *Existing;
    auto* Canvas=Cast<UCanvasPanel>(Entry->Widget.Get()); if(!Canvas) return {};
    FWowTextureElement Texture;
    if(const auto* Xml=Entry->Def.ButtonTextureRegions.Find(State)) Texture=*Xml;
    else
    {
        if(State==TEXT("Normal")) { Texture.File=Entry->Def.NormalTexture; Texture.Name=Entry->Def.NormalTextureName; }
        if(State==TEXT("Pushed")) { Texture.File=Entry->Def.PushedTexture; Texture.Name=Entry->Def.PushedTextureName; }
        if(State==TEXT("Disabled")) { Texture.File=Entry->Def.DisabledTexture; Texture.Name=Entry->Def.DisabledTextureName; }
        if(State==TEXT("Highlight")) { Texture.File=Entry->Def.HighlightTexture; Texture.Name=Entry->Def.HighlightTextureName; }
    }
    if(Texture.Name.IsEmpty()) Texture.Name=Entry->Def.Name+State+TEXT("Texture");
    Texture.Name.ReplaceInline(TEXT("$parent"),*Entry->Def.Name);
    for(auto& Anchor:Texture.Anchors) Anchor.RelativeTo.ReplaceInline(TEXT("$parent"),*Entry->Def.Name);
    if(State==TEXT("Highlight")) Texture.bAdditive=true;
    const bool Hidden=Texture.bHidden; Texture.bHidden=false;
    FWowFrameDef RegionOwner; RegionOwner.Name=Entry->Def.Name;
    const auto Size=GetLayoutRect(Handle); RegionOwner.Width=Size.Z; RegionOwner.Height=Size.W;
    FWowLayer Layer; Layer.Textures.Add(Texture); RegionOwner.Layers.Add(Layer);
    ButtonVisualStates.FindOrAdd(Handle).Regions.Add(State,Texture.Name);
    ButtonRegionHidden.Add(Texture.Name,Hidden);
    CreateLayerContent(Canvas,RegionOwner,Handle);
    if(auto* Image=GetNamedTexture(Texture.Name)) if(auto* Slot=Cast<UCanvasPanelSlot>(Image->Slot))
        Slot->SetZOrder(State==TEXT("Highlight")?450:200);
    RefreshButtonVisuals(Handle);
    return Texture.Name;
}

FString FWowFrameManager::GetButtonState(int64 Handle) const
{
    const auto* Button=GetButtonWidget(Handle);
    if(Button && !Button->GetIsEnabled()) return TEXT("DISABLED");
    const auto* State=ButtonVisualStates.Find(Handle);
    return State && (State->ForcedPressed || State->MousePressed)?TEXT("PUSHED"):TEXT("NORMAL");
}

void FWowFrameManager::RefreshButtonVisuals(int64 Handle)
{
    const auto* Visual=ButtonVisualStates.Find(Handle); if(!Visual) return;
    const FString State=GetButtonState(Handle);
    const bool Pressed=State==TEXT("PUSHED"),Disabled=State==TEXT("DISABLED");
    const auto HasResource=[&](const FString& Key) { const auto* Name=Visual->Regions.Find(Key); const auto* Image=Name?GetNamedTexture(*Name):nullptr; return Image && Image->GetBrush().DrawAs!=ESlateBrushDrawType::NoDrawType && Image->GetRenderOpacity()>0; };
    for(const auto& Pair:Visual->Regions) if(auto* Image=GetNamedTexture(Pair.Value))
    {
        bool Show=Pair.Key==TEXT("Normal")?!((Pressed && HasResource(TEXT("Pushed"))) || (Disabled && HasResource(TEXT("Disabled")))):
            Pair.Key==TEXT("Pushed")?Pressed:Pair.Key==TEXT("Disabled")?Disabled:
            Pair.Key==TEXT("Highlight")?!Disabled && (HoverFrameHandle==Handle || Visual->HighlightLocked):false;
        if(Pair.Key==TEXT("Highlight")) if(auto* Texture=Cast<UTexture2D>(Image->GetBrush().GetResourceObject()))
            if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowUiAdditive.M_WowUiAdditive")))
            {
                auto* Material=UMaterialInstanceDynamic::Create(Base,Image); Material->SetTextureParameterValue(TEXT("Image"),Texture);
                auto Brush=Image->GetBrush(); Brush.SetResourceObject(Material); Image->SetBrush(Brush);
            }
        Image->SetVisibility(Show && !ButtonRegionHidden.FindRef(Pair.Value)?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
}

void FWowFrameManager::SetButtonState(int64 Handle,const FString& State,bool Locked)
{
    if(!GetButtonWidget(Handle)) return;
    auto& Visual=ButtonVisualStates.FindOrAdd(Handle);
    Visual.ForcedPressed=State.Equals(TEXT("PUSHED"),ESearchCase::IgnoreCase);
    Visual.PressLocked=Visual.ForcedPressed && Locked;
    RefreshButtonVisuals(Handle);
}

void FWowFrameManager::SetButtonEnabled(int64 Handle,bool Enabled)
{
    auto* Button=GetButtonWidget(Handle); if(!Button || Button->GetIsEnabled()==Enabled) return;
    Button->SetIsEnabled(Enabled); RefreshButtonVisuals(Handle);
    if(EventSystem) EventSystem->RunFrameScript(Handle,Enabled?TEXT("OnEnable"):TEXT("OnDisable"));
}

void FWowFrameManager::LockButtonHighlight(int64 Handle,bool Locked)
{
    ButtonVisualStates.FindOrAdd(Handle).HighlightLocked=Locked; RefreshButtonVisuals(Handle);
}

bool FWowFrameManager::SetButtonRegionShown(const FString& Name,bool Shown)
{
    if(!ButtonRegionHidden.Contains(Name)) return false;
    ButtonRegionHidden[Name]=!Shown;
    for(const auto& Pair:ButtonVisualStates) for(const auto& Region:Pair.Value.Regions) if(Region.Value==Name) { RefreshButtonVisuals(Pair.Key); return true; }
    return true;
}
