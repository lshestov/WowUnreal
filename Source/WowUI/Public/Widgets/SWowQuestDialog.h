#pragma once
#include "CoreMinimal.h"
#include "Components/Image.h"
#include "Widgets/SCompoundWidget.h"
#include "Framework/SlateDelegates.h"
#include "Styling/SlateTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "WowEntity.h"

class SScrollBox;
class SButton;
class SHorizontalBox;

DECLARE_DELEGATE_TwoParams(FOnQuestDialogAccept, uint64 /*QuestGiverGuid*/, uint32 /*QuestId*/);
DECLARE_DELEGATE_ThreeParams(FOnQuestDialogComplete, uint64 /*QuestGiverGuid*/, uint32 /*QuestId*/, int32 /*RewardChoice*/);
DECLARE_DELEGATE(FOnQuestDialogDecline);
DECLARE_DELEGATE_OneParam(FOnGossipQuestSelected, uint32);
DECLARE_DELEGATE_OneParam(FOnGossipOptionSelected, uint32);

/**
 * WoW-style quest dialog widget
 * Shows quest details with title, description, objectives, and rewards
 * Has Accept/Decline buttons or Complete Quest button depending on mode
 */
class WOWUI_API SWowQuestDialog : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowQuestDialog) {}
        SLATE_EVENT(FOnQuestDialogAccept, OnQuestAccept)
        SLATE_EVENT(FOnQuestDialogAccept, OnQuestContinue)
        SLATE_EVENT(FOnQuestDialogComplete, OnQuestComplete)
        SLATE_EVENT(FOnQuestDialogDecline, OnQuestDecline)
        SLATE_EVENT(FOnGossipQuestSelected, OnGossipQuest)
        SLATE_EVENT(FOnGossipOptionSelected, OnGossipOption)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetPresentation(class FWowFrameManager& Frames, const FString& Name, uint64 QuestUnitGuid);
    void PrepareItems(class UWowUIManager& UI, class UWowConnectionManager* Connection, const FWowQuestDetails& Quest);

    /** Show quest details dialog (for new quest) */
    void ShowQuestDetails(const FWowQuestDetails& QuestDetails);

    /** Show quest reward dialog (for quest completion) */
    void ShowQuestReward(const FWowQuestDetails& QuestDetails);

    /** Hide the quest dialog */
    void CloseDialog();
    void ShowGossip(const FWowGossipMenu& Menu);
    void UpdateGossipGreeting(const FString& Greeting);
    void AcceptCurrentQuest();
    void CompleteCurrentQuest();
    uint32 GetCurrentQuestId() const { return CurrentQuest.QuestId; }

private:
    FSlateBrush Background[4];
    FSlateBrush AutoAcceptBottomRight;
    FSlateBrush PanelHighlight, CloseHighlight, AvailableIcon, ActiveIcon, GossipIcon, Book, GreetingFooter, SectionDivider;
    FSlateBrush ItemNameFrame, SelectedItemHighlight, MoneyIcons[3];
    FSlateBrush ScrollKnob, ScrollArrowHighlight[2];
    FButtonStyle ScrollArrowStyle[2];
    FButtonStyle PanelStyle, CloseStyle, RowStyle, ItemStyle;
    TSharedRef<SWidget> PanelButton(const FString& Label, FOnClicked Action, float Width = 78.f, bool bClose = false, TAttribute<bool> Enabled = true);
    TSharedRef<SWidget> GossipRow(const FString& Label, const FSlateBrush* Icon, FOnClicked Action, bool bEnabled = true);
    enum class EItemGroup { Required, Choice, Fixed };
    TSharedRef<SWidget> ItemButton(const FWowQuestRewardItem& Item, EItemGroup Group, int32 Index);
    void AddItemGrid(const TArray<FWowQuestRewardItem>& Items, EItemGroup Group, float Left, float Top);
    TSharedRef<SWidget> MoneyAmount(uint32 Amount, bool bRequired);
    FString Label(const TCHAR* Key, const TCHAR* Fallback) const;
    TMap<uint32,TSharedPtr<class FDeferredCleanupSlateBrush>> ItemIcons;
    TWeakObjectPtr<class UWowUIManager> ItemUI;
    TWeakObjectPtr<class UWowConnectionManager> ItemConnection;
    TSharedRef<class STextBlock> WrappedText(const FString& Text,const FTextBlockStyle& Style,float Width) const;
    TArray<TStrongObjectPtr<UObject>> Artwork;
    FTextBlockStyle TitleText, BodyText, RowText, CaptionText, RewardNumber, StackNumber, SmallQuestText;
    TSharedPtr<class STextBlock> NpcName;
    TSharedPtr<class SBox> GreetingBlock;
    TSharedPtr<class STextBlock> GreetingText;
    TStrongObjectPtr<UImage> Portrait;
    FOnQuestDialogAccept OnQuestAccept;
    FOnQuestDialogAccept OnQuestContinue;
    FOnQuestDialogComplete OnQuestComplete;
    FOnQuestDialogDecline OnQuestDecline;
    FOnGossipQuestSelected OnGossipQuest;
    FOnGossipOptionSelected OnGossipOption;

    TSharedPtr<SScrollBox> ContentArea;
    TSharedPtr<SHorizontalBox> ButtonArea;

    FWowQuestDetails CurrentQuest;
    bool bUsesGossipFrame = false;
    bool bIsRewardDialog = false;
    int32 SelectedRewardChoice = 0;

    FReply OnAcceptClicked();
    FReply OnContinueClicked();
    FReply OnDeclineClicked();
    FReply OnCompleteClicked();
    FReply OnRewardItemClicked(int32 RewardIndex);

    /** Generate quest text content */
    void RefreshContent();
};
