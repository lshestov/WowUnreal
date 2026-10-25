#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Slider.h"

float FWowFrameManager::GetSliderValue(int64 Handle) const
{
    const auto* Slider=GetSliderWidget(Handle); if(!Slider) return 0;
    return Slider->GetOrientation()==EOrientation::Orient_Vertical?Slider->GetMinValue()+Slider->GetMaxValue()-Slider->GetValue():Slider->GetValue();
}
void FWowFrameManager::SetSliderValue(int64 Handle,float Value)
{
    auto* Slider=GetSliderWidget(Handle); if(!Slider || !FMath::IsFinite(Value)) return;
    const float Min=Slider->GetMinValue(),Max=Slider->GetMaxValue(); Value=FMath::Clamp(Value,Min,Max);
    const auto* Def=GetFrameDef(Handle); const float Step=Def?FMath::Max(Def->ValueStep,FLT_EPSILON):.001f;
    const float Snapped=Min+FMath::RoundToFloat((Value-Min)/Step)*Step;
    if(!FMath::IsNearlyEqual(Value,Snapped,FMath::Max(FLT_EPSILON,FMath::Abs(Value)*FLT_EPSILON))) Value=Snapped;
    if(Value+Step>Max) Value=Max; if(Value-Step<Min) Value=Min;
    Value=FMath::Clamp(Value,Min,Max);
    Slider->SetValue(Slider->GetOrientation()==EOrientation::Orient_Vertical?Min+Max-Value:Value);
    SliderLastValue.Add(Handle,Value); UpdateSliderThumb(Handle); FrameRects.Reset(); bLayoutDirty=true;
}
void FWowFrameManager::SetSliderRange(int64 Handle,float Minimum,float Maximum)
{
    auto* Slider=GetSliderWidget(Handle); if(!Slider || !FMath::IsFinite(Minimum) || !FMath::IsFinite(Maximum)) return;
    Maximum=FMath::Max(Minimum,Maximum); const float Before=GetSliderValue(Handle);
    const bool Changed=Minimum!=Slider->GetMinValue() || Maximum!=Slider->GetMaxValue();
    Slider->SetMinValue(Minimum); Slider->SetMaxValue(Maximum); SetSliderValue(Handle,Before);
    if(!FMath::IsNearlyEqual(Before,GetSliderValue(Handle)) && EventSystem) EventSystem->RunFrameScript(Handle,TEXT("OnValueChanged"),{FString::Printf(TEXT("%.17g"),double(GetSliderValue(Handle)))});
    if(Changed && EventSystem) EventSystem->RunFrameScript(Handle,TEXT("OnMinMaxChanged"),{FString::Printf(TEXT("%.17g"),double(Minimum)),FString::Printf(TEXT("%.17g"),double(Maximum))});
}
void FWowFrameManager::SetSliderStep(int64 Handle,float Step)
{
    if(!FMath::IsFinite(Step)) return;
    const float Before=GetSliderValue(Handle); if(auto* Def=GetMutableFrameDef(Handle)) Def->ValueStep=FMath::Max(Step,FLT_EPSILON);
    if(auto* Slider=GetSliderWidget(Handle)) Slider->SetStepSize(FMath::Max(Step,FLT_EPSILON));
    SetSliderValue(Handle,Before);
    if(!FMath::IsNearlyEqual(Before,GetSliderValue(Handle)) && EventSystem) EventSystem->RunFrameScript(Handle,TEXT("OnValueChanged"),{FString::Printf(TEXT("%.17g"),double(GetSliderValue(Handle)))});
}
void FWowFrameManager::SetSliderOrientation(int64 Handle,bool Vertical)
{
    auto* Slider=GetSliderWidget(Handle); if(!Slider) return; const float Before=GetSliderValue(Handle);
    Slider->SetOrientation(Vertical?EOrientation::Orient_Vertical:EOrientation::Orient_Horizontal);
    if(auto* Def=GetMutableFrameDef(Handle)) Def->Orientation=Vertical?TEXT("VERTICAL"):TEXT("HORIZONTAL");
    SetSliderValue(Handle,Before);
}
FString FWowFrameManager::EnsureSliderThumb(int64 Handle)
{
    if(!GetSliderWidget(Handle)) return {};
    if(const auto* Name=SliderThumbRegions.Find(Handle)) return *Name;
    const FString Name=CreateTextureRegion(Handle,GetFrameName(Handle)+TEXT("ThumbTexture"),TEXT("ARTWORK"),{});
    SliderThumbRegions.Add(Handle,Name); return Name;
}
void FWowFrameManager::UpdateSliderThumb(int64 Handle)
{
    auto* Slider=GetSliderWidget(Handle); const auto* Name=SliderThumbRegions.Find(Handle);
    auto* Image=Name?GetNamedTexture(*Name):nullptr; if(!Slider || !Image) return;
    Slider->SetLocked(false);
    const auto Rect=GetLayoutRect(Handle); const float Scale=GetEffectiveFrameScale(Handle);
    const float Width=Rect.Z/Scale,Height=Rect.W/Scale;
    const float Range=Slider->GetMaxValue()-Slider->GetMinValue(),Fraction=Range>0?FMath::Clamp((GetSliderValue(Handle)-Slider->GetMinValue())/Range,0.f,1.f):0;
    for(auto& Region:RegionLayouts) if(Region.Widget.Get()==Image)
    {
        const float W=Region.Width>0?Region.Width:Image->GetBrush().ImageSize.X,H=Region.Height>0?Region.Height:Image->GetBrush().ImageSize.Y;
        const bool Vertical=Slider->GetOrientation()==EOrientation::Orient_Vertical;
        Region.bAllPoints=false; Region.bUnanchored=false;
        Region.Anchors={{Vertical?EWowAnchorPoint::TOP:EWowAnchorPoint::LEFT,GetFrameName(Handle),Vertical?EWowAnchorPoint::TOP:EWowAnchorPoint::LEFT,Vertical?0:(Width-W)*Fraction,Vertical?-(Height-H)*Fraction:0}};
        UnanchoredRegions.Remove(*Name);
        if(Image->GetVisibility()==ESlateVisibility::Hidden) Image->SetVisibility(ESlateVisibility::HitTestInvisible);
        // Keep native slider hit geometry, and paint its canonical WoW region.
        auto Style=Slider->GetWidgetStyle(); FSlateBrush Empty; Empty.DrawAs=ESlateBrushDrawType::NoDrawType; Empty.ImageSize=FVector2D(W,H)*UIScale;
        Style.SetNormalThumbImage(Empty); Style.SetHoveredThumbImage(Empty); Style.SetDisabledThumbImage(Empty); Slider->SetWidgetStyle(Style);
        break;
    }
}
void FWowFrameManager::UpdateSliderPointer()
{
    const int64 Handle=DraggingSlider; auto* Slider=GetSliderWidget(Handle);
    if(!Slider || !SliderThumbRegions.Contains(Handle) || !Slider->GetIsEnabled() || !IsFrameDrawn(Handle)) { DraggingSlider=-1; return; }
    const auto R=GetLayoutRect(Handle); const bool Vertical=Slider->GetOrientation()==EOrientation::Orient_Vertical;
    float Thumb=0; if(const auto* Name=SliderThumbRegions.Find(Handle)) for(const auto& Region:RegionLayouts) if(Region.Widget.Get()==GetNamedTexture(*Name))
    { const auto* Image=GetNamedTexture(*Name); Thumb=(Vertical?(Region.Height>0?Region.Height:Image->GetBrush().ImageSize.Y):(Region.Width>0?Region.Width:Image->GetBrush().ImageSize.X))*GetEffectiveFrameScale(Handle); break; }
    const float Length=(Vertical?R.W:R.Z)-Thumb,Offset=(Vertical?LastPointerPosition.Y:LastPointerPosition.X)/RawViewportScale-(Vertical?R.Y:R.X)-Thumb*.5f;
    const float Before=GetSliderValue(Handle),Fraction=Length>0?FMath::Clamp(Offset/Length,0.f,1.f):0;
    SetSliderValue(Handle,Slider->GetMinValue()+(Slider->GetMaxValue()-Slider->GetMinValue())*Fraction);
    if(!FMath::IsNearlyEqual(Before,GetSliderValue(Handle)) && EventSystem) EventSystem->RunFrameScript(Handle,TEXT("OnValueChanged"),{FString::Printf(TEXT("%.17g"),double(GetSliderValue(Handle)))});
}
