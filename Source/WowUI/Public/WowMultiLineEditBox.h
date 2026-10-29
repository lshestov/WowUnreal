#pragma once
#include "CoreMinimal.h"
#include "Components/MultiLineEditableText.h"
#include "WowMultiLineEditBox.generated.h"
class FSlateTextLayout;
class FWowChatEditMarshaller;
UCLASS()
class WOWUI_API UWowMultiLineEditBox : public UMultiLineEditableText
{
    GENERATED_BODY()
public:
    // Slate uses platform newlines; the WoW Lua API uses a single LF per line.
    FString LuaText() const;
    void SetLuaText(const FString& Text);
    void EnableChatMarkup() {bChatMarkup=true;}
    bool IsChatEditor() const {return bChatMarkup;}
    bool ChatKey(const FKey& Key);
    void Highlight(int32 Begin,int32 End);
    FString DisplayText() const;
    FString SelectedRawText() const;
    void SetEditorInsets(const FMargin& Insets);
    FMargin EditorMargin() const {return Margin;}
    void CaptureChatSelection(int32& Anchor,int32& Caret,bool& Selected) const;
    void RestoreChatSelection(int32 Anchor,int32 Caret,bool Selected);
    TFunction<void(const FString&,const FString&)> ChatCommand;
    int32 CursorOffset() const;
    void SetCursorOffset(int32 Offset);
    void InsertAtCursor(const FString& InText);
    bool HasEditFocus() const;
    bool MeasureEditor(FVector4& OutCursor,float& TextHeight);
    virtual void ReleaseSlateResources(bool ReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    TSharedPtr<FSlateTextLayout> MeasuredLayout;
    TSharedPtr<FWowChatEditMarshaller> ChatMarshaller;
    bool bChatMarkup=false,bChatUpdating=false;
    FMargin EditorInsets;
    void UpdateChatMargin();
    void SetChatText(const FString& Raw,int32 Cursor);
    bool DeleteChatAtom(bool Backward);
    void Edited(const FText& InText);
    void Committed(const FText& InText,ETextCommit::Type Type) { HandleOnTextCommitted(InText,Type); }
};
