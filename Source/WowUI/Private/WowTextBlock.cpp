#include "WowTextBlock.h"
#include "WowFrameManager.h"
#include "Engine/Texture2D.h"
#include "Framework/Text/ITextDecorator.h"
#include "Framework/Text/SlateImageRun.h"
#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/SlateWidgetRun.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/SRichTextBlock.h"

namespace
{
FString EscapeLabel(FString Text)
{
    return Text.Replace(TEXT("&"),TEXT("&amp;")).Replace(TEXT("<"),TEXT("&lt;")).Replace(TEXT(">"),TEXT("&gt;"));
}
}
FWowLabelMarkup UWowTextBlock::ParseMarkup(const FString& Text)
{
    FWowLabelMarkup Result; FString Pending,Color;
    auto Flush=[&]()
    {
        Result.Plain+=Pending;
        if(Color.IsEmpty()) Result.Rich+=EscapeLabel(Pending);
        else if(!Pending.IsEmpty()) Result.Rich+=FString::Printf(TEXT("<wowtext c=\"%s\">%s</>"),*Color,*EscapeLabel(Pending));
        Pending.Empty();
    };
    for(int32 I=0;I<Text.Len();++I)
    {
        if(Text[I]!='|' || I+1>=Text.Len()) { Pending.AppendChar(Text[I]); continue; }
        const TCHAR Code=Text[I+1];
        if(Code=='|') { Pending.AppendChar('|'); ++I; continue; }
        if(Code=='n') { Pending.AppendChar('\n'); ++I; continue; }
        if(Code=='4')
        {
            const int32 End=Text.Find(TEXT(";"),ESearchCase::CaseSensitive,ESearchDir::FromStart,I+2);
            if(End>=0)
            {
                TArray<FString> Forms; Text.Mid(I+2,End-I-2).ParseIntoArray(Forms,TEXT(":"),false);
                if(Forms.Num()==2 || Forms.Num()==3)
                {
                    // WoW grammar selects a form using the last visible number.
                    const FString Before=Result.Plain+Pending; int32 Last=Before.Len()-1;
                    while(Last>=0 && !FChar::IsDigit(Before[Last])) --Last;
                    int32 First=Last; while(First>=0 && (FChar::IsDigit(Before[First]) || Before[First]=='.')) --First;
                    const double Number=Last>=0?FCString::Atod(*Before.Mid(First+1,Last-First)):0;
                    int32 Form=Number==1?0:1;
                    if(Forms.Num()==3)
                    {
                        const int64 Whole=int64(FMath::Clamp(FMath::IsFinite(Number)?Number:0.,0.,double(MAX_int32))); const int32 Tens=int32(Whole%100),Ones=int32(Whole%10);
                        Form=Number!=double(Whole)?2:Ones==1 && Tens!=11?0:Ones>=2 && Ones<=4 && (Tens<12 || Tens>14)?1:2;
                    }
                    Pending+=Forms[Form]; I=End; continue;
                }
            }
        }
        if(Code=='c' && I+9<Text.Len())
        {
            const FString Hex=Text.Mid(I+2,8);
            bool Valid=true; for(TCHAR C:Hex) Valid&=FChar::IsHexDigit(C);
            if(Valid) { Flush(); Color=Hex; I+=9; Result.Formatted=true; continue; }
        }
        if(Code=='r') { Flush(); Color.Empty(); ++I; Result.Formatted=true; continue; }
        if(Code=='H') { const int32 End=Text.Find(TEXT("|h"),ESearchCase::CaseSensitive,ESearchDir::FromStart,I+2); if(End>=0) { I=End+1; continue; } }
        if(Code=='h') { ++I; continue; }
        if(Code=='T')
        {
            const int32 End=Text.Find(TEXT("|t"),ESearchCase::CaseSensitive,ESearchDir::FromStart,I+2);
            if(End>=0)
            {
                Flush(); TArray<FString> Parts; Text.Mid(I+2,End-I-2).ParseIntoArray(Parts,TEXT(":"),false);
                if(Parts.Num() && !Parts[0].IsEmpty() && Result.Icons.Num()<64)
                {
                    FWowInlineIcon Icon; Icon.Path=Parts[0];
                    auto Number=[&](int32 N) { const float V=Parts.IsValidIndex(N)?FCString::Atof(*Parts[N]):0; return FMath::IsFinite(V)?V:0.f; };
                    Icon.Height=Number(1); Icon.Width=Number(2); Icon.OffsetX=Number(3); Icon.OffsetY=Number(4);
                    const float W=Number(5),H=Number(6);
                    if(W>0 && H>0 && Parts.Num()>=11) Icon.Crop=FVector4f(Number(7)/W,Number(9)/H,Number(8)/W,Number(10)/H);
                    Result.Rich+=FString::Printf(TEXT("<wowimage i=\"%d\"/>"),Result.Icons.Num()); Result.Icons.Add(Icon);
                }
                Result.Formatted=true; I=End+1; continue;
            }
        }
        Pending.AppendChar(Text[I]);
    }
    Flush(); return Result;
}

struct FWowRichLabelState
{
    FTextBlockStyle Style;
    float Scale=0;
    float Wrap=-1;
    bool AutoWrap=false;
    ETextJustify::Type Justification=ETextJustify::Left;
    TArray<FSlateBrush> Brushes;
    TArray<int16> Baselines;
    TArray<float> Offsets;
};
class FWowLabelDecorator : public ITextDecorator
{
    TSharedRef<FWowRichLabelState> State;
public:
    explicit FWowLabelDecorator(TSharedRef<FWowRichLabelState> In):State(In) {}
    virtual bool Supports(const FTextRunParseResults& Run,const FString&) const override { return Run.Name==TEXT("wowimage") || Run.Name==TEXT("wowtext"); }
    virtual TSharedRef<ISlateRun> Create(const TSharedRef<FTextLayout>& Layout,const FTextRunParseResults& Run,const FString& Original,const TSharedRef<FString>& Model,const ISlateStyle*) override
    {
        auto Meta=[&](const TCHAR* Key) { const auto* Range=Run.MetaData.Find(Key); return Range?Original.Mid(Range->BeginIndex,Range->Len()):FString(); };
        const int32 Begin=Model->Len(); FRunInfo Info(Run.Name);
        if(Run.Name==TEXT("wowimage"))
        {
            const int32 Index=FCString::Atoi(*Meta(TEXT("i"))); Model->AppendChar(TCHAR(0x200B));
            if(State->Brushes.IsValidIndex(Index))
            {
                if(State->Offsets[Index]!=0)
                {
                    const auto Image=SNew(SImage).Image(&State->Brushes[Index]).RenderTransform(FSlateRenderTransform(FVector2D(State->Offsets[Index],0)));
                    return FSlateWidgetRun::Create(Layout,Info,Model,FSlateWidgetRun::FWidgetRunInfo(Image,TAttribute<int16>(State->Baselines[Index]),FVector2D(State->Brushes[Index].ImageSize)),FTextRange(Begin,Model->Len()));
                }
                return FSlateImageRun::Create(Info,Model,&State->Brushes[Index],TAttribute<int16>(State->Baselines[Index]),FTextRange(Begin,Model->Len()));
            }
        }
        else Model->Append(Original.Mid(Run.ContentRange.BeginIndex,Run.ContentRange.Len()));
        FTextBlockStyle Style=State->Style;
        const FString Hex=Meta(TEXT("c"));
        if(Hex.Len()==8)
        {
            const uint32 Value=FCString::Strtoui64(*Hex,nullptr,16);
            Style.SetColorAndOpacity(FLinearColor(float((Value>>16)&255)/255,float((Value>>8)&255)/255,float(Value&255)/255,float(Value>>24)/255));
        }
        return FSlateTextRun::Create(Info,Model,Style,FTextRange(Begin,Model->Len()));
    }
};

void UWowTextBlock::InitializeLabel(FWowFrameManager* InFrames,float Scale) { Frames=InFrames; UnitScale=Scale; }
void UWowTextBlock::SetText(FText InText)
{
    const FString Raw=InText.ToString(); if(HasOriginal && Raw==OriginalText) return;
    HasOriginal=true; OriginalText=Raw; Parsed=ParseMarkup(Raw);
    Super::SetText(FText::FromString(Parsed.Plain)); IconTextures.Reset();
    for(const auto& Icon:Parsed.Icons) IconTextures.Add(Frames?Frames->LoadUITexture(Icon.Path):nullptr);
    RebuildContent();
}
void UWowTextBlock::RebuildContent()
{
    if(!Host.IsValid()) return;
    if(!Parsed.Formatted) { RichWidget.Reset(); RichState.Reset(); Host->SetContent(PlainWidget.ToSharedRef()); return; }
    RichState=MakeShared<FWowRichLabelState>();
    TArray<TSharedRef<ITextDecorator>> Decorators; Decorators.Add(MakeShared<FWowLabelDecorator>(RichState.ToSharedRef()));
    RichWidget=SNew(SRichTextBlock).Text(FText::FromString(Parsed.Rich)).Decorators(Decorators).Clipping(EWidgetClipping::Inherit);
    RefreshRichStyle(); Host->SetContent(RichWidget.ToSharedRef()); SetNonSpaceWrapping(NonSpaceWrapping); SetPixelSpacing(PixelSpacing);
}
void UWowTextBlock::RefreshRichStyle(float Scale)
{
    if(Scale>0) UnitScale=Scale;
    if(!RichWidget.IsValid() || !RichState.IsValid()) return;
    FTextBlockStyle Style; Style.SetFont(GetFont()); Style.SetColorAndOpacity(GetColorAndOpacity());
    Style.SetShadowColorAndOpacity(GetShadowColorAndOpacity()); Style.SetShadowOffset(GetShadowOffset());
    const bool Changed=!Style.IsIdenticalTo(RichState->Style) || RichState->Scale!=UnitScale || RichState->Brushes.Num()!=Parsed.Icons.Num();
    if(Changed)
    {
        RichState->Style=Style; RichState->Scale=UnitScale; RichState->Brushes.Reset(); RichState->Baselines.Reset(); RichState->Offsets.Reset();
        for(int32 I=0;I<Parsed.Icons.Num();++I)
        {
            const auto& Icon=Parsed.Icons[I]; const float H=Icon.Height>0?Icon.Height*UnitScale:GetFont().Size*4.f/3;
            const float W=Icon.Width>0?Icon.Width*UnitScale:H;
            FSlateBrush Brush; Brush.SetResourceObject(IconTextures.IsValidIndex(I)?IconTextures[I]:nullptr); Brush.ImageSize=FVector2D(W,H);
            Brush.SetUVRegion(FBox2f(FVector2f(Icon.Crop.X,Icon.Crop.Y),FVector2f(Icon.Crop.Z,Icon.Crop.W)));
            if(!Brush.GetResourceObject()) Brush.DrawAs=ESlateBrushDrawType::NoDrawType;
            RichState->Brushes.Add(Brush); RichState->Baselines.Add(FMath::RoundToInt(-GetFont().Size*.2f+Icon.OffsetY*UnitScale));
            RichState->Offsets.Add(Icon.OffsetX*UnitScale);
        }
        RichWidget->SetTextStyle(Style); RichWidget->Refresh();
    }
    if(RichState->Wrap!=GetWrapTextAt()) { RichState->Wrap=GetWrapTextAt(); RichWidget->SetWrapTextAt(RichState->Wrap); }
    if(RichState->AutoWrap!=GetAutoWrapText()) { RichState->AutoWrap=GetAutoWrapText(); RichWidget->SetAutoWrapText(RichState->AutoWrap); }
    if(RichState->Justification!=LabelJustification) { RichState->Justification=LabelJustification; RichWidget->SetJustification(LabelJustification); }
}
void UWowTextBlock::SetVerticalJustification(const FString& Value)
{
    VerticalJustification=Value;
    if(Host) Host->SetVAlign(Value==TEXT("TOP")?VAlign_Top:Value==TEXT("BOTTOM")?VAlign_Bottom:VAlign_Center);
}
void UWowTextBlock::SetNonSpaceWrapping(bool Value)
{
    NonSpaceWrapping=Value; SetWrappingPolicy(Value?ETextWrappingPolicy::AllowPerCharacterWrapping:ETextWrappingPolicy::DefaultWrapping);
    if(RichWidget) RichWidget->SetWrappingPolicy(Value?ETextWrappingPolicy::AllowPerCharacterWrapping:ETextWrappingPolicy::DefaultWrapping);
}
void UWowTextBlock::SetPixelSpacing(float Value)
{
    PixelSpacing=Value; const float Percentage=FMath::Max(.1f,1+Value*UnitScale/FMath::Max(1.f,GetFont().Size*4.f/3));
    SetLineHeightPercentage(Percentage); if(RichWidget) RichWidget->SetLineHeightPercentage(Percentage);
}
TSharedRef<SWidget> UWowTextBlock::RebuildWidget()
{
    PlainWidget=Super::RebuildWidget(); Host=SNew(SBox); RebuildContent(); SetVerticalJustification(VerticalJustification); SetNonSpaceWrapping(NonSpaceWrapping); SetPixelSpacing(PixelSpacing); return Host.ToSharedRef();
}
void UWowTextBlock::SynchronizeProperties() { Super::SynchronizeProperties(); RefreshRichStyle(); }
void UWowTextBlock::ReleaseSlateResources(bool Children) { Super::ReleaseSlateResources(Children); Host.Reset(); PlainWidget.Reset(); RichWidget.Reset(); RichState.Reset(); }
void UWowTextBlock::OnWrapTextAtChanged(float Value) { Super::OnWrapTextAtChanged(Value); RefreshRichStyle(); }
void UWowTextBlock::OnAutoWrapTextChanged(bool Value) { Super::OnAutoWrapTextChanged(Value); RefreshRichStyle(); }
void UWowTextBlock::OnJustificationChanged(ETextJustify::Type Value) { Super::OnJustificationChanged(Value); LabelJustification=Value; RefreshRichStyle(); }
