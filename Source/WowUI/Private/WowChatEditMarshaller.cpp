#include "WowChatEditMarshaller.h"
#include "WowUiText.h"
#include "Framework/Text/TextLayout.h"
#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/SlateWidgetRun.h"
#include "Widgets/Text/STextBlock.h"

FWowChatEditDocument FWowChatEditDocument::Build(TArray<FWowChatEditSpan> Values)
{
    FWowChatEditDocument D;
    for(auto& S:Values)
    {
        if(S.Label.IsEmpty() && !S.Atomic()) continue;
        S.ModelBegin=D.Model.Len(); S.RawBegin=D.Raw.Len();
        if(!S.Color.IsEmpty()) D.Raw+=TEXT("|c")+S.Color;
        S.BodyBegin=D.Raw.Len(); D.Raw+=S.Atomic()?S.Wire:S.Label; S.BodyEnd=D.Raw.Len();
        if(!S.Color.IsEmpty()) D.Raw+=TEXT("|r");
        S.RawEnd=D.Raw.Len(); D.Display+=S.Label;
        if(S.Atomic()) D.Model.AppendChar(TCHAR(0xfffc)); else D.Model+=S.Label;
        S.ModelEnd=D.Model.Len(); D.Spans.Add(MoveTemp(S));
    }
    return D;
}
FWowChatEditDocument FWowChatEditDocument::Parse(const FString& Source)
{
    TArray<FWowChatEditSpan> Values; FString Pending,Color;
    const auto Flush=[&]() {if(!Pending.IsEmpty()) {FWowChatEditSpan S; S.Label=MoveTemp(Pending); S.Color=Color; Values.Add(MoveTemp(S)); Pending.Empty();}};
    int32 Atoms=0;
    for(int32 I=0;I<Source.Len();++I)
    {
        if(Source[I]=='\r' || Source[I]=='\n') continue;
        if(Source[I]!='|' || I+1>=Source.Len()) {Pending.AppendChar(Source[I]); continue;}
        const TCHAR Code=Source[I+1];
        if(Code=='|' && Atoms<128)
        {
            Flush(); FWowChatEditSpan S; S.Label=TEXT("|"); S.Wire=TEXT("||"); S.Color=Color; Values.Add(MoveTemp(S)); ++Atoms; ++I; continue;
        }
        if(Code=='c' && I+9<Source.Len())
        {
            const FString Hex=Source.Mid(I+2,8); bool Valid=true; for(TCHAR C:Hex) Valid&=FChar::IsHexDigit(C);
            if(Valid) {Flush(); Color=Hex; I+=9; continue;}
        }
        if(Code=='r') {Flush(); Color.Empty(); ++I; continue;}
        if(Code=='H' && Atoms<128)
        {
            const int32 Label=Source.Find(TEXT("|h"),ESearchCase::CaseSensitive,ESearchDir::FromStart,I+2);
            const int32 End=Label>=0?Source.Find(TEXT("|h"),ESearchCase::CaseSensitive,ESearchDir::FromStart,Label+2):INDEX_NONE;
            if(Label>I+2 && Label-I<=2048 && End>=0)
            {
                Flush(); FWowChatEditSpan S; S.Label=WowUiPlainText(Source.Mid(Label+2,End-Label-2)); S.Wire=Source.Mid(I,End+2-I); S.Color=Color;
                Values.Add(MoveTemp(S)); ++Atoms; I=End+1; continue;
            }
        }
        Pending.AppendChar(Source[I]);
    }
    Flush(); return Build(MoveTemp(Values));
}
int32 FWowChatEditDocument::RawOffset(int32 Offset) const
{
    Offset=FMath::Clamp(Offset,0,Model.Len());
    if(Offset==0) return 0;
    for(const auto& S:Spans)
    {
        if(Offset<=S.ModelBegin) return S.RawBegin;
        if(Offset<S.ModelEnd) return S.Atomic()?S.RawEnd:S.BodyBegin+Offset-S.ModelBegin;
        if(Offset==S.ModelEnd) return S.RawEnd;
    }
    return Raw.Len();
}
int32 FWowChatEditDocument::ModelOffset(int32 Offset) const
{
    Offset=FMath::Clamp(Offset,0,Raw.Len());
    for(const auto& S:Spans)
    {
        if(Offset<=S.BodyBegin) return S.ModelBegin;
        if(Offset<S.BodyEnd) return S.Atomic()?S.ModelEnd:S.ModelBegin+Offset-S.BodyBegin;
        if(Offset<=S.RawEnd) return S.ModelEnd;
    }
    return Model.Len();
}
void FWowChatEditMarshaller::SetText(const FString& Source,FTextLayout& Layout)
{
    CurrentLayout=&Layout; const auto D=FWowChatEditDocument::Parse(Source);
    auto Model=MakeShared<FString>(D.Model); TArray<TSharedRef<IRun>> Runs;
    for(const auto& S:D.Spans)
    {
        FRunInfo Info(S.Atomic()?TEXT("wowchat_atom"):TEXT("wowchat_text"));
        Info.MetaData.Add(TEXT("color"),S.Color);
        FTextBlockStyle RunStyle=*Style;
        if(S.Color.Len()==8)
        {
            const uint32 C=FCString::Strtoui64(*S.Color,nullptr,16);
            RunStyle.SetColorAndOpacity(FLinearColor(float((C>>16)&255)/255,float((C>>8)&255)/255,float(C&255)/255,float(C>>24)/255));
        }
        const FTextRange Range(S.ModelBegin,S.ModelEnd);
        if(S.Atomic())
        {
            Info.MetaData.Add(TEXT("wire"),S.Wire); Info.MetaData.Add(TEXT("label"),S.Label);
            const auto Label=SNew(STextBlock).Text(FText::FromString(S.Label)).Font(RunStyle.Font).ColorAndOpacity(RunStyle.ColorAndOpacity);
            const int16 Baseline=FSlateTextRun::Create(FRunInfo(),Model,RunStyle,Range)->GetBaseLine(1.f);
            Runs.Add(FSlateWidgetRun::Create(Layout.AsShared(),Info,Model,FSlateWidgetRun::FWidgetRunInfo(Label,TAttribute<int16>(Baseline)),Range));
        }
        else Runs.Add(FSlateTextRun::Create(Info,Model,RunStyle,Range));
    }
    if(Runs.IsEmpty()) Runs.Add(FSlateTextRun::Create(FRunInfo(TEXT("wowchat_text")),Model,*Style));
    Layout.ClearLines(); Layout.AddLine(FTextLayout::FNewLineData(Model,MoveTemp(Runs))); ClearDirty();
}
FWowChatEditDocument FWowChatEditMarshaller::Read(const FTextLayout& Layout) const
{
    TArray<FWowChatEditSpan> Values;
    for(const auto& Line:Layout.GetLineModels()) for(const auto& Model:Line.Runs)
    {
        const auto Run=Model.GetRun(); const auto& Info=Run->GetRunInfo(); FWowChatEditSpan S;
        S.Color=Info.MetaData.FindRef(TEXT("color"));
        if(Info.Name==TEXT("wowchat_atom")) {S.Wire=Info.MetaData.FindRef(TEXT("wire")); S.Label=Info.MetaData.FindRef(TEXT("label"));}
        else Run->AppendTextTo(S.Label);
        Values.Add(MoveTemp(S));
    }
    return FWowChatEditDocument::Build(MoveTemp(Values));
}
void FWowChatEditMarshaller::GetText(FString& Text,const FTextLayout& Layout) {Text=Read(Layout).Raw;}
FWowChatEditDocument FWowChatEditMarshaller::CurrentDocument() const {return CurrentLayout?Read(*CurrentLayout):FWowChatEditDocument();}
