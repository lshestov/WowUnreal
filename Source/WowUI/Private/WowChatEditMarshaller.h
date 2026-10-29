#pragma once
#include "CoreMinimal.h"
#include "Framework/Text/BaseTextLayoutMarshaller.h"
#include "Styling/SlateTypes.h"

struct FWowChatEditSpan
{
    FString Label,Wire,Color;
    int32 ModelBegin=0,ModelEnd=0,RawBegin=0,BodyBegin=0,BodyEnd=0,RawEnd=0;
    bool Atomic() const {return !Wire.IsEmpty();}
};

/** An editable layout uses one character per link; Lua and chat retain its wire code. */
struct FWowChatEditDocument
{
    FString Raw,Model,Display;
    TArray<FWowChatEditSpan> Spans;
    static FWowChatEditDocument Parse(const FString& Raw);
    static FWowChatEditDocument Build(TArray<FWowChatEditSpan> Spans);
    int32 RawOffset(int32 ModelOffset) const;
    int32 ModelOffset(int32 RawOffset) const;
};

class FWowChatEditMarshaller : public FBaseTextLayoutMarshaller
{
public:
    explicit FWowChatEditMarshaller(const FTextBlockStyle* InStyle):Style(InStyle) {}
    virtual void SetText(const FString& Source,FTextLayout& Layout) override;
    virtual void GetText(FString& Text,const FTextLayout& Layout) override;
    virtual bool RequiresLiveUpdate() const override {return true;}
    FWowChatEditDocument CurrentDocument() const;
private:
    const FTextBlockStyle* Style;
    FTextLayout* CurrentLayout=nullptr;
    FWowChatEditDocument Read(const FTextLayout& Layout) const;
};
