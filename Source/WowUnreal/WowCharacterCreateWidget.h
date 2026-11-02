#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Formats/Dbc/CharacterCreationDbc.h"
#include "UObject/StrongObjectPtr.h"

class AWowCharacterPreview;
class SEditableTextBox;
class STextBlock;
DECLARE_DELEGATE_NineParams(FOnCharacterCreated,const FString&,uint8,uint8,uint8,uint8,uint8,uint8,uint8,uint8);
DECLARE_DELEGATE(FOnBackToCharSelect);

class SWowCharacterCreateWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowCharacterCreateWidget) {} SLATE_END_ARGS()
    void Construct(const FArguments& InArgs);
    virtual void Tick(const FGeometry&,double,float) override;
    virtual bool SupportsKeyboardFocus() const override {return true;}
    virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent&) override;
    void SetPreview(AWowCharacterPreview* InPreview);
    void SetStatusText(const FString& Text);
    void CompleteRequest(const FString& Text,bool Success=false);
    void SetAppearance(FWowCharacterAppearance Value);
    const FWowCharacterAppearance& GetAppearance() const { return Appearance; }
    TArray<uint8> GetOptions(EWowAppearanceOption Option) const;
    bool IsSubmitting() const { return bSubmitting; }
    bool IsDataReady() const { return bDataReady; }
    bool IsAppearanceValid() const { return bValid; }
    void SetName(const FString& Name);
    void Submit();
    FOnCharacterCreated OnCharacterCreated;
    FOnBackToCharSelect OnBackToCharSelect;
private:
    friend struct FWowCharacterCreationProbe;
    friend class FCharacterCreationSelectionMemory;
    friend class AWowCharacterCreationTestGameMode;
    void Refresh();
    void BuildStockLayout();
    bool PressStockControl(FName Name);
    void ChangeIdentity(int32 Field,int32 Delta);
    void SelectIdentity(int32 Field,uint8 Selected);
    void ChangeOption(EWowAppearanceOption Option,int32 Delta);
    uint8& Value(EWowAppearanceOption Option);
    uint32 MemoryKey() const;
    FWowCharacterAppearance Appearance;
    TMap<uint32,FWowCharacterAppearance> Remembered;
    TWeakObjectPtr<AWowCharacterPreview> Preview;
    TSharedPtr<SEditableTextBox> NameBox;
    TSharedPtr<STextBlock> StatusLabel;
    TSharedPtr<SBox> PreviewBox;
    TSharedPtr<class FWowCharacterCreationArt> Art;
    TSharedPtr<struct FEditableTextBoxStyle> NameStyle;
    TArray<TStrongObjectPtr<class UWowBackdropWidget>> Backdrops;
    TMap<FName,TWeakPtr<class SButton>> StockControls;
    bool bSubmitting=false,bDataReady=false,bValid=false;
};
