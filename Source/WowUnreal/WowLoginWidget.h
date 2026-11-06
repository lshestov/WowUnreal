#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "WowExpansionTypes.h"
#include "Styling/SlateTypes.h"
#include "UObject/StrongObjectPtr.h"

DECLARE_DELEGATE_FourParams(FOnLoginSubmit, const FString& /*Server*/, int32 /*Port*/, const FString& /*User*/, const FString& /*Pass*/);
DECLARE_DELEGATE_OneParam(FOnExpansionChanged, uint8 /*ExpansionIndex*/);

class SBox;
class AWowLoginScene;
class AWowAudioManager;
class FMpqManager;
class FWowCharacterCreationArt;
class UWowBackdropWidget;

/**
 * Wrath login form and shared expansion selector.
 */
class SWowLoginWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowLoginWidget) : _PersistPreferences(true) {}
    SLATE_ARGUMENT(bool, PersistPreferences)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    SWowLoginWidget();
    virtual ~SWowLoginWidget();
    void SetScene(AWowLoginScene* Scene,FMpqManager* Mpq,AWowAudioManager* Audio);
    bool PressStockControl(FName Name);
    virtual FReply OnPreviewKeyDown(const FGeometry&,const FKeyEvent&) override;
    void SetStatusText(const FString& Text);

    /** Prefill the login fields */
    void SetCredentials(const FString& Server, const FString& Username, const FString& Password);

    /** Programmatically click the login button */
    void AutoSubmit();

    FOnLoginSubmit OnLoginSubmit;
    FOnExpansionChanged OnExpansionChanged;

    EWowExpansion GetCurrentExpansion() const { return CurrentExpansion; }

private:
    void BuildStockLayout();
    void InitializeStyles();
    TSharedRef<SButton> MakeButton(FName Id,const FString& Text,FOnClicked Action,int32 Size=15,TAttribute<bool> Selected=false);
    TSharedRef<SButton> MakeCheckBox(FName Id,const FString& Text,TAttribute<bool> Checked,FSimpleDelegate Toggle);
    void SavePreferences();
    TUniquePtr<FWowCharacterCreationArt> Art;
    TWeakObjectPtr<AWowLoginScene> Scene;
    TWeakObjectPtr<AWowAudioManager> Audio;
    TArray<TStrongObjectPtr<UWowBackdropWidget>> Backdrops;
    TSharedPtr<FButtonStyle> BlueButton;
    FButtonStyle PlainButton;
    TSharedPtr<FSliderStyle> GlueSlider;
    FEditableTextBoxStyle StockEditStyle;
    TMap<FName,TWeakPtr<SButton>> StockControls;
    bool bOptions=false;
    bool bRememberAccount=true;
    bool bPersistPreferences=true;
    FString StatusText;
    TSharedPtr<SBox> ScreenBody;
    TSharedPtr<SBox> ExpansionFooter;
    FReply OnLoginClicked();
    void SetExpansion(EWowExpansion Expansion);

    /** Build the expansion tab bar at the bottom */
    TSharedRef<SWidget> BuildExpansionTabs();

    /** Get colors for the current expansion */
    FLinearColor GetBackgroundColor() const;
    FLinearColor GetAccentColor() const;
    FString GetExpansionTitle() const;

    EWowExpansion CurrentExpansion = EWowExpansion::WrathOfTheLichKing;

    TSharedPtr<SEditableTextBox> ServerBox;
    TSharedPtr<SEditableTextBox> UsernameBox;
    TSharedPtr<SEditableTextBox> PasswordBox;
    TSharedPtr<STextBlock> StatusLabel;
};
