#pragma once
#include "CoreMinimal.h"
#include "Components/MultiLineEditableText.h"
#include "WowMultiLineEditBox.generated.h"
class FSlateTextLayout;
UCLASS()
class WOWUI_API UWowMultiLineEditBox : public UMultiLineEditableText
{
    GENERATED_BODY()
public:
    // Slate uses platform newlines; the WoW Lua API uses a single LF per line.
    FString LuaText() const { return GetText().ToString().Replace(TEXT("\r\n"),TEXT("\n")).Replace(TEXT("\r"),TEXT("\n")); }
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
    void Edited(const FText& InText) { HandleOnTextChanged(InText); }
    void Committed(const FText& InText,ETextCommit::Type Type) { HandleOnTextCommitted(InText,Type); }
};
