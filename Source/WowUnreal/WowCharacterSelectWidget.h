#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "WowSessionState.h"
#include "UObject/StrongObjectPtr.h"

DECLARE_DELEGATE_OneParam(FOnCharacterSelected,int64);
DECLARE_DELEGATE(FOnCreateCharacterRequest);
DECLARE_DELEGATE_OneParam(FOnDeleteCharacterRequest,int64);
DECLARE_DELEGATE_OneParam(FOnCharacterHighlighted,int64);
class AWowCharacterPreview;
class FWowCharacterCreationArt;
class UWowBackdropWidget;
class SButton;
class SEditableTextBox;
struct FEditableTextBoxStyle;

/** Stock Wrath selection layout, backed by the current server enumeration. */
class SWowCharacterSelectWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowCharacterSelectWidget) {} SLATE_END_ARGS()
    void Construct(const FArguments&);
    virtual ~SWowCharacterSelectWidget();
    virtual void Tick(const FGeometry&,double,float) override;
    virtual bool SupportsKeyboardFocus() const override {return true;}
    virtual FReply OnPreviewKeyDown(const FGeometry&,const FKeyEvent&) override;
    void SetPreview(AWowCharacterPreview* Preview,const FString& Realm);
    void PopulateCharacters(const TArray<FWowCharacterInfo>& Characters,int64 PreferredGuid=0);
    bool HighlightCharacter(int64 Guid);
    bool EnterSelected();
    bool PressStockControl(FName Name,bool DoubleClick=false);
    int64 GetSelectedGuid() const {return SelectedGuid;}
    bool IsDeleteDialogOpen() const {return DeleteGuid!=0;}
    bool IsSubmitting() const {return bBusy;}
    void SetDeleteConfirmationText(const FString& Text);
    void SetStatusText(const FString& Text);
    void CompleteDeletion(bool Success,const FString& Message);
    FOnCharacterSelected OnCharacterSelected;
    FOnCreateCharacterRequest OnCreateCharacterRequest;
    FOnDeleteCharacterRequest OnDeleteCharacterRequest;
    FOnCharacterHighlighted OnCharacterHighlighted;
    FSimpleDelegate OnBackRequest,OnChangeRealmRequest;
private:
    void BuildStockLayout();
    void RefreshPreview();
    void OpenDeleteDialog();
    void ConfirmDelete();
    bool CanConfirmDelete() const;
    const FWowCharacterInfo* Selected() const;
    TSharedRef<SWidget> Backdrop(float Width,float Height,bool CharacterList=false);
    TWeakObjectPtr<AWowCharacterPreview> Preview;
    TUniquePtr<FWowCharacterCreationArt> Art;
    TArray<TStrongObjectPtr<UWowBackdropWidget>> Backdrops;
    TSharedPtr<SEditableTextBox> DeleteBox;
    TSharedPtr<FEditableTextBoxStyle> DeleteStyle;
    TSharedPtr<FButtonStyle> RotationStyle;
    TMap<FName,TWeakPtr<SButton>> StockControls;
    TArray<FWowCharacterInfo> CachedCharacters;
    FString RealmName,Status;
    int64 SelectedGuid=0,DeleteGuid=0;
    bool bBusy=false,bDataReady=false;
};
