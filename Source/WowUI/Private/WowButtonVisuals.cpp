#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

FWowTextureElement FWowFrameManager::ResolveTexture(const FWowTextureElement& Texture,int32 Depth) const
{
    FWowTextureElement Result=Texture;
    if(Depth>=16) return Result;
    TArray<FString> Names; Texture.Inherits.ParseIntoArray(Names,TEXT(","),true);
    for(auto Name:Names)
    {
        Name.TrimStartAndEndInline(); const auto* Source=TextureTemplates.Find(Name); if(!Source) continue;
        const auto Base=ResolveTexture(*Source,Depth+1);
        if(Result.File.IsEmpty()) Result.File=Base.File;
        if(Result.Width<=0) Result.Width=Base.Width; if(Result.Height<=0) Result.Height=Base.Height;
        if(Result.Anchors.IsEmpty()) Result.Anchors=Base.Anchors;
        if(!Result.bHasTexCoords && (Result.Left==0 && Result.Right==1 && Result.Top==0 && Result.Bottom==1))
        {
            Result.Left=Base.Left; Result.Right=Base.Right; Result.Top=Base.Top; Result.Bottom=Base.Bottom; Result.bHasTexCoords=Base.bHasTexCoords;
        }
        Result.bAdditive|=Base.bAdditive; Result.bSetAllPoints|=Base.bSetAllPoints;
    }
    Result.Inherits.Empty(); return Result;
}
void FWowFrameManager::LoadTextureDefinition(const FWowTextureElement& Texture,const FString& Parent)
{
    if(Texture.bVirtual) { TextureTemplates.Add(Texture.Name,Texture); return; }
    const FString Owner=Parent.IsEmpty()?TEXT("UIParent"):Parent; const int64 Handle=FindFrame(Owner);
    auto* Canvas=Cast<UCanvasPanel>(GetWidgetForHandle(Handle)); if(!Canvas && Owner==TEXT("UIParent")) Canvas=RootCanvas.Get();
    if(!Canvas) return;
    FWowFrameDef Definition; Definition.Name=Owner; FWowLayer Layer; Layer.Textures.Add(Texture); Definition.Layers.Add(Layer);
    CreateLayerContent(Canvas,Definition,Handle);
}
void FWowFrameManager::SetButtonFont(int64 Handle,const FString& State,const FString& Font)
{
    auto* Entry=Frames.Find(Handle); if(!Entry) return;
    if(State==TEXT("Normal")) Entry->Def.NormalFont=Font;
    if(State==TEXT("Highlight")) Entry->Def.HighlightFont=Font;
    if(State==TEXT("Disabled")) Entry->Def.DisabledFont=Font;
    ButtonVisualStates.FindOrAdd(Handle).AppliedFont.Empty(); RefreshButtonVisuals(Handle);
}

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
    if(State==TEXT("Checked") && Texture.File.IsEmpty()) Texture.File=Entry->Def.CheckedTexture;
    if(State==TEXT("Highlight") || State==TEXT("Checked")) Texture.bAdditive=true;
    const bool Hidden=Texture.bHidden; Texture.bHidden=false;
    FWowFrameDef RegionOwner; RegionOwner.Name=Entry->Def.Name;
    const auto Size=GetLayoutRect(Handle); RegionOwner.Width=Size.Z; RegionOwner.Height=Size.W;
    FWowLayer Layer; Layer.Textures.Add(Texture); RegionOwner.Layers.Add(Layer);
    ButtonVisualStates.FindOrAdd(Handle).Regions.Add(State,Texture.Name);
    ButtonRegionHidden.Add(Texture.Name,Hidden);
    CreateLayerContent(Canvas,RegionOwner,Handle);
    if(auto* Image=GetNamedTexture(Texture.Name)) if(auto* Slot=Cast<UCanvasPanelSlot>(Image->Slot))
        Slot->SetZOrder(State==TEXT("Highlight")?450:State.Contains(TEXT("Checked"))?400:200);
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
    auto* Visual=ButtonVisualStates.Find(Handle); if(!Visual) return;
    const FString State=GetButtonState(Handle);
    const bool Pressed=State==TEXT("PUSHED"),Disabled=State==TEXT("DISABLED");
    const auto HasResource=[&](const FString& Key) { const auto* Name=Visual->Regions.Find(Key); const auto* Image=Name?GetNamedTexture(*Name):nullptr; return Image && Image->GetBrush().DrawAs!=ESlateBrushDrawType::NoDrawType && Image->GetRenderOpacity()>0; };
    for(const auto& Pair:Visual->Regions) if(auto* Image=GetNamedTexture(Pair.Value))
    {
        bool Show=Pair.Key==TEXT("Normal")?!((Pressed && HasResource(TEXT("Pushed"))) || (Disabled && HasResource(TEXT("Disabled")))):
            Pair.Key==TEXT("Pushed")?Pressed:Pair.Key==TEXT("Disabled")?Disabled:
            Pair.Key==TEXT("Checked")?GetChecked(Handle) && !(Disabled && HasResource(TEXT("DisabledChecked"))):
            Pair.Key==TEXT("DisabledChecked")?GetChecked(Handle) && Disabled:
            Pair.Key==TEXT("Highlight")?!Disabled && (HoverFrameHandle==Handle || Visual->HighlightLocked):false;
        if(Pair.Key==TEXT("Highlight")) if(auto* Texture=Cast<UTexture2D>(Image->GetBrush().GetResourceObject()))
            if(auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowUiAdditive.M_WowUiAdditive")))
            {
                auto* Material=UMaterialInstanceDynamic::Create(Base,Image); Material->SetTextureParameterValue(TEXT("Image"),Texture);
                auto Brush=Image->GetBrush(); Brush.SetResourceObject(Material); Image->SetBrush(Brush);
            }
        Image->SetVisibility(Show && !ButtonRegionHidden.FindRef(Pair.Value)?(IsRegionAnchored(Pair.Value)?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden):ESlateVisibility::Collapsed);
    }
    if(const auto* Entry=Frames.Find(Handle); Entry && EventSystem)
    {
        FString Font=Disabled?Entry->Def.DisabledFont:(HoverFrameHandle==Handle || Visual->HighlightLocked)?Entry->Def.HighlightFont:Entry->Def.NormalFont;
        if(Font.IsEmpty()) Font=Entry->Def.NormalFont;
        if(!Font.IsEmpty() && Font!=Visual->AppliedFont)
        {
            Visual->AppliedFont=Font;
            const FString TextName=GetNamedFontString(Entry->Def.Name+TEXT("Text"))?Entry->Def.Name+TEXT("Text"):Entry->Def.Name+TEXT("NormalText");
            EventSystem->ApplyRegionFont(TextName,Font);
        }
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
    if(!Enabled) { auto& Visual=ButtonVisualStates.FindOrAdd(Handle); Visual.MousePressed=false; Visual.ForcedPressed=false; Visual.PressLocked=false; }
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
