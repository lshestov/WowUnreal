#include "WowMultiLineEditBox.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Framework/Text/SlateTextLayoutFactory.h"
#include "WowChatEditMarshaller.h"
#include "WowEditOffsets.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

namespace
{
class SWowChatEditableText : public SMultiLineEditableText
{
public:
    TWeakObjectPtr<UWowMultiLineEditBox> Owner;
    virtual void Tick(const FGeometry& Geometry,double Time,float Delta) override
    {
        int32 Anchor=0,Caret=0; bool Selected=false;
        if(Owner.IsValid()) Owner->CaptureChatSelection(Anchor,Caret,Selected);
        SMultiLineEditableText::Tick(Geometry,Time,Delta);
        if(Owner.IsValid()) Owner->RestoreChatSelection(Anchor,Caret,Selected);
    }
    virtual FReply OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override
    {
        if(Owner.IsValid() && Event.IsControlDown())
        {
            if(Event.GetKey()==EKeys::C || Event.GetKey()==EKeys::X)
            {
                const FString Value=Owner->SelectedRawText();
                if(!Value.IsEmpty()) {FPlatformApplicationMisc::ClipboardCopy(*Value); if(Event.GetKey()==EKeys::X) Owner->InsertAtCursor(TEXT(""));}
                return FReply::Handled();
            }
            if(Event.GetKey()==EKeys::V)
            {FString Value; FPlatformApplicationMisc::ClipboardPaste(Value); Owner->InsertAtCursor(Value); return FReply::Handled();}
        }
        if(Owner.IsValid() && Owner->ChatKey(Event.GetKey())) return FReply::Handled();
        return SMultiLineEditableText::OnKeyDown(Geometry,Event);
    }
protected:
    virtual bool CanInsertCarriageReturn() const override {return false;}
    virtual bool ShouldJumpCursorToEndWhenFocused() const override {return false;}
};
}

TSharedRef<SWidget> UWowMultiLineEditBox::RebuildWidget()
{
    if(bChatMarkup)
    {
        ChatMarshaller=MakeShared<FWowChatEditMarshaller>(&WidgetStyle);
        const auto Edit=SNew(SWowChatEditableText).Text(GetText()).TextStyle(&WidgetStyle).Marshaller(ChatMarshaller).AutoWrapText(false)
            .OnTextChanged(BIND_UOBJECT_DELEGATE(FOnTextChanged,Edited)).OnTextCommitted(BIND_UOBJECT_DELEGATE(FOnTextCommitted,Committed))
            .CreateSlateTextLayout(FCreateSlateTextLayout::CreateLambda([this](SWidget* Owner,const FTextBlockStyle& Style)
            {MeasuredLayout=FSlateTextLayout::Create(Owner,Style); return MeasuredLayout.ToSharedRef();}));
        Edit->Owner=this; MyMultiLineEditableText=Edit; return Edit;
    }
    MyMultiLineEditableText=SNew(SMultiLineEditableText)
        .Text(GetText()).TextStyle(&WidgetStyle)
        .OnTextChanged(BIND_UOBJECT_DELEGATE(FOnTextChanged,Edited))
        .OnTextCommitted(BIND_UOBJECT_DELEGATE(FOnTextCommitted,Committed))
        .CreateSlateTextLayout(FCreateSlateTextLayout::CreateLambda([this](SWidget* Owner,const FTextBlockStyle& Style)
        { MeasuredLayout=FSlateTextLayout::Create(Owner,Style); return MeasuredLayout.ToSharedRef(); }));
    return MyMultiLineEditableText.ToSharedRef();
}
void UWowMultiLineEditBox::ReleaseSlateResources(bool Children)
{
    ChatMarshaller.Reset(); MeasuredLayout.Reset(); Super::ReleaseSlateResources(Children);
}
FString UWowMultiLineEditBox::LuaText() const
{
    if(ChatMarshaller && MyMultiLineEditableText)
        return ChatMarshaller->CurrentDocument().Raw;
    return GetText().ToString().Replace(TEXT("\r\n"),TEXT("\n")).Replace(TEXT("\r"),TEXT("\n"));
}
void UWowMultiLineEditBox::SetLuaText(const FString& InValue)
{
    if(bChatMarkup) SetChatText(InValue,InValue.Len()); else SetText(FText::FromString(InValue));
}
FString UWowMultiLineEditBox::DisplayText() const
{
    return ChatMarshaller?ChatMarshaller->CurrentDocument().Display:LuaText();
}
void UWowMultiLineEditBox::SetChatText(const FString& Raw,int32 InCursor)
{
    TGuardValue<bool> Guard(bChatUpdating,true);
    TakeWidget(); const auto D=FWowChatEditDocument::Parse(Raw); SetText(FText::FromString(D.Raw));
    if(MyMultiLineEditableText && ChatMarshaller)
    {ChatMarshaller->MakeDirty(); MyMultiLineEditableText->Refresh(); MyMultiLineEditableText->GoTo(FTextLocation(0,D.ModelOffset(InCursor)));}
}
void UWowMultiLineEditBox::Edited(const FText& InValue)
{
    HandleOnTextChanged(InValue);
}
bool UWowMultiLineEditBox::ChatKey(const FKey& Key)
{
    if(!bChatMarkup || !ChatCommand) return false;
    if(Key==EKeys::BackSpace || Key==EKeys::Delete) return DeleteChatAtom(Key==EKeys::BackSpace);
    if(Key==EKeys::Enter) ChatCommand(TEXT("OnEnterPressed"),TEXT(""));
    else if(Key==EKeys::Escape) ChatCommand(TEXT("OnEscapePressed"),TEXT(""));
    else if(Key==EKeys::Tab) ChatCommand(TEXT("OnTabPressed"),TEXT(""));
    else if(Key==EKeys::Up || Key==EKeys::Down) ChatCommand(TEXT("OnArrowPressed"),Key==EKeys::Up?TEXT("UP"):TEXT("DOWN"));
    else return false;
    return true;
}
void UWowMultiLineEditBox::Highlight(int32 Begin,int32 End)
{
    TakeWidget(); if(!MyMultiLineEditableText) return;
    if(bChatMarkup && ChatMarshaller)
    {
        const auto D=ChatMarshaller->CurrentDocument();
        MyMultiLineEditableText->SelectText(FTextLocation(0,D.ModelOffset(WowEditOffsets::CharacterIndex(D.Raw,Begin))),FTextLocation(0,D.ModelOffset(End<0?D.Raw.Len():WowEditOffsets::CharacterIndex(D.Raw,End)))); return;
    }
    const FString Content=LuaText();
    const auto Location=[&](int32 At){int32 Line=0,Column=0; for(int32 I=0;I<At;++I) {if(Content[I]=='\n') {++Line; Column=0;} else ++Column;} return FTextLocation(Line,Column);};
    MyMultiLineEditableText->SelectText(Location(WowEditOffsets::CharacterIndex(Content,Begin)),Location(End<0?Content.Len():WowEditOffsets::CharacterIndex(Content,End)));
}
bool UWowMultiLineEditBox::HasEditFocus() const
{
    return MyMultiLineEditableText.IsValid() && MyMultiLineEditableText->HasAnyUserFocusOrFocusedDescendants();
}
int32 UWowMultiLineEditBox::CursorOffset() const
{
    if(!MyMultiLineEditableText) return 0;
    if(ChatMarshaller) {const auto D=ChatMarshaller->CurrentDocument(); return FTCHARToUTF8(*D.Raw.Left(D.RawOffset(MyMultiLineEditableText->GetCursorLocation().GetOffset()))).Length();}
    const auto Position=MyMultiLineEditableText->GetCursorLocation(); const FString Content=LuaText();
    int32 Line=0,I=0; for(;I<Content.Len() && Line<Position.GetLineIndex();++I) if(Content[I]=='\n') ++Line;
    return WowEditOffsets::ByteIndex(Content,I+Position.GetOffset());
}
void UWowMultiLineEditBox::SetCursorOffset(int32 Offset)
{
    TakeWidget(); if(!MyMultiLineEditableText) return;
    if(ChatMarshaller)
    {const auto D=ChatMarshaller->CurrentDocument(); MyMultiLineEditableText->GoTo(FTextLocation(0,D.ModelOffset(WowEditOffsets::CharacterIndex(D.Raw,Offset)))); return;}
    const FString Content=LuaText(); Offset=WowEditOffsets::CharacterIndex(Content,Offset);
    int32 Line=0,Column=0; for(int32 I=0;I<Offset;++I) { if(Content[I]=='\n') { ++Line; Column=0; } else ++Column; }
    MyMultiLineEditableText->GoTo(FTextLocation(Line,Column));
}
void UWowMultiLineEditBox::InsertAtCursor(const FString& InText)
{
    TakeWidget(); if(!MyMultiLineEditableText) return;
    if(ChatMarshaller)
    {
        const auto D=ChatMarshaller->CurrentDocument();
        const auto Selection=MyMultiLineEditableText->GetSelection();
        int32 Begin=MyMultiLineEditableText->GetCursorLocation().GetOffset(),End=Begin;
        if(MyMultiLineEditableText->AnyTextSelected()) {Begin=Selection.GetBeginning().GetOffset(); End=Selection.GetEnd().GetOffset();}
        Begin=D.RawOffset(Begin); End=D.RawOffset(End);
        const FString Inserted=InText.Replace(TEXT("\r"),TEXT("")).Replace(TEXT("\n"),TEXT(""));
        SetChatText(D.Raw.Left(Begin)+Inserted+D.Raw.Mid(End),Begin+Inserted.Len()); return;
    }
    MyMultiLineEditableText->InsertTextAtCursor(InText);
}
FString UWowMultiLineEditBox::SelectedRawText() const
{
    if(!ChatMarshaller || !MyMultiLineEditableText || !MyMultiLineEditableText->AnyTextSelected()) return {};
    const auto D=ChatMarshaller->CurrentDocument(); const auto Range=MyMultiLineEditableText->GetSelection();
    const int32 Begin=D.RawOffset(Range.GetBeginning().GetOffset()),End=D.RawOffset(Range.GetEnd().GetOffset());
    return D.Raw.Mid(Begin,End-Begin);
}
bool UWowMultiLineEditBox::DeleteChatAtom(bool Backward)
{
    if(!ChatMarshaller || !MyMultiLineEditableText) return false;
    const auto D=ChatMarshaller->CurrentDocument();
    int32 Begin=MyMultiLineEditableText->GetCursorLocation().GetOffset(),End=Begin;
    if(MyMultiLineEditableText->AnyTextSelected())
    {const auto Range=MyMultiLineEditableText->GetSelection(); Begin=Range.GetBeginning().GetOffset(); End=Range.GetEnd().GetOffset();}
    else if(Backward) --Begin; else ++End;
    if(Begin<0 || End>D.Model.Len()) return false;
    bool ContainsAtom=false; for(const auto& S:D.Spans) ContainsAtom|=S.Atomic() && S.ModelBegin<End && S.ModelEnd>Begin;
    if(!ContainsAtom) return false;
    const int32 RawBegin=D.RawOffset(Begin),RawEnd=D.RawOffset(End);
    SetChatText(D.Raw.Left(RawBegin)+D.Raw.Mid(RawEnd),RawBegin); return true;
}
void UWowMultiLineEditBox::CaptureChatSelection(int32& Anchor,int32& Caret,bool& Selected) const
{
    if(!ChatMarshaller || !MyMultiLineEditableText) return;
    const auto D=ChatMarshaller->CurrentDocument(); const int32 Position=MyMultiLineEditableText->GetCursorLocation().GetOffset();
    Caret=D.RawOffset(Position); Anchor=Caret; Selected=MyMultiLineEditableText->AnyTextSelected();
    if(Selected)
    {const auto Range=MyMultiLineEditableText->GetSelection(); Anchor=D.RawOffset(Position==Range.GetBeginning().GetOffset()?Range.GetEnd().GetOffset():Range.GetBeginning().GetOffset());}
}
void UWowMultiLineEditBox::RestoreChatSelection(int32 Anchor,int32 Caret,bool Selected)
{
    if(!ChatMarshaller || !MyMultiLineEditableText || bChatUpdating) return;
    const auto D=ChatMarshaller->CurrentDocument(); const int32 Position=D.ModelOffset(Caret);
    if(Selected) MyMultiLineEditableText->SelectText(FTextLocation(0,D.ModelOffset(Anchor)),FTextLocation(0,Position));
    else if(MyMultiLineEditableText->GetCursorLocation().GetOffset()!=Position) MyMultiLineEditableText->GoTo(FTextLocation(0,Position));
}
bool UWowMultiLineEditBox::MeasureEditor(FVector4& OutCursor,float& TextHeight)
{
    if(bChatMarkup) UpdateChatMargin();
    TakeWidget()->Invalidate(EInvalidateWidgetReason::Prepass); ForceLayoutPrepass();
    if(!MeasuredLayout || !MyMultiLineEditableText) return false;
    const float Scale=FMath::Max(MeasuredLayout->GetScale(),.001f);
    const auto Position=MyMultiLineEditableText->GetCursorLocation();
    const auto Point=MeasuredLayout->GetLocationAt(Position,true)/Scale;
    float Height=GetFont().Size;
    for(const auto& Line:MeasuredLayout->GetLineViews()) if(Line.ModelIndex==Position.GetLineIndex() && Position.GetOffset()>=Line.Range.BeginIndex && Position.GetOffset()<=Line.Range.EndIndex) { Height=Line.Size.Y/Scale; break; }
    OutCursor=FVector4(Point.X,-Point.Y,1,Height); TextHeight=MeasuredLayout->GetSize().Y/Scale; return true;
}
void UWowMultiLineEditBox::SetEditorInsets(const FMargin& Insets)
{
    EditorInsets=Insets;
    if(bChatMarkup) UpdateChatMargin(); else SetMargin(Insets);
}
void UWowMultiLineEditBox::UpdateChatMargin()
{
    FMargin Desired=EditorInsets;
    if(MyMultiLineEditableText && FSlateApplication::IsInitialized())
    {
        const float Height=MyMultiLineEditableText->GetCachedGeometry().GetLocalSize().Y;
        const float GlyphHeight=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(TEXT("Ag"),WidgetStyle.Font).Y;
        const float Space=FMath::Max(0.f,(Height-EditorInsets.Top-EditorInsets.Bottom-GlyphHeight)*.5f);
        Desired.Top+=Space; Desired.Bottom+=Space;
    }
    const auto Current=EditorMargin();
    if(!FMath::IsNearlyEqual(Current.Left,Desired.Left,.1f) || !FMath::IsNearlyEqual(Current.Right,Desired.Right,.1f) ||
        !FMath::IsNearlyEqual(Current.Top,Desired.Top,.1f) || !FMath::IsNearlyEqual(Current.Bottom,Desired.Bottom,.1f)) SetMargin(Desired);
}
