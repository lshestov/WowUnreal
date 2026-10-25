#include "WowMultiLineEditBox.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Framework/Text/SlateTextLayoutFactory.h"

TSharedRef<SWidget> UWowMultiLineEditBox::RebuildWidget()
{
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
    MeasuredLayout.Reset(); Super::ReleaseSlateResources(Children);
}
bool UWowMultiLineEditBox::HasEditFocus() const
{
    return MyMultiLineEditableText.IsValid() && MyMultiLineEditableText->HasAnyUserFocusOrFocusedDescendants();
}
int32 UWowMultiLineEditBox::CursorOffset() const
{
    if(!MyMultiLineEditableText) return 0;
    const auto Position=MyMultiLineEditableText->GetCursorLocation(); const FString Content=LuaText();
    int32 Line=0,I=0; for(;I<Content.Len() && Line<Position.GetLineIndex();++I) if(Content[I]=='\n') ++Line;
    return FMath::Clamp(I+Position.GetOffset(),0,Content.Len());
}
void UWowMultiLineEditBox::SetCursorOffset(int32 Offset)
{
    TakeWidget(); if(!MyMultiLineEditableText) return;
    const FString Content=LuaText(); Offset=FMath::Clamp(Offset,0,Content.Len());
    int32 Line=0,Column=0; for(int32 I=0;I<Offset;++I) { if(Content[I]=='\n') { ++Line; Column=0; } else ++Column; }
    MyMultiLineEditableText->GoTo(FTextLocation(Line,Column));
}
void UWowMultiLineEditBox::InsertAtCursor(const FString& InText)
{
    TakeWidget(); if(MyMultiLineEditableText) MyMultiLineEditableText->InsertTextAtCursor(InText);
}
bool UWowMultiLineEditBox::MeasureEditor(FVector4& OutCursor,float& TextHeight)
{
    TakeWidget()->Invalidate(EInvalidateWidgetReason::Prepass); ForceLayoutPrepass();
    if(!MeasuredLayout || !MyMultiLineEditableText) return false;
    const float Scale=FMath::Max(MeasuredLayout->GetScale(),.001f);
    const auto Position=MyMultiLineEditableText->GetCursorLocation();
    const auto Point=MeasuredLayout->GetLocationAt(Position,true)/Scale;
    float Height=GetFont().Size;
    for(const auto& Line:MeasuredLayout->GetLineViews()) if(Line.ModelIndex==Position.GetLineIndex() && Position.GetOffset()>=Line.Range.BeginIndex && Position.GetOffset()<=Line.Range.EndIndex) { Height=Line.Size.Y/Scale; break; }
    OutCursor=FVector4(Point.X,-Point.Y,1,Height); TextHeight=MeasuredLayout->GetSize().Y/Scale; return true;
}
