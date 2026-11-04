#pragma once
#include "CoreMinimal.h"
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
        SLATE_EVENT(FOnQuestDialogComplete, OnQuestComplete)
        SLATE_EVENT(FOnQuestDialogDecline, OnQuestDecline)
        SLATE_EVENT(FOnGossipQuestSelected, OnGossipQuest)
        SLATE_EVENT(FOnGossipOptionSelected, OnGossipOption)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void SetPresentation(class FWowFrameManager& Frames, const FString& Name, bool bShowTargetPortrait);

    /** Show quest details dialog (for new quest) */
    void ShowQuestDetails(const FWowQuestDetails& QuestDetails);

    /** Show quest reward dialog (for quest completion) */
    void ShowQuestReward(const FWowQuestDetails& QuestDetails);

    /** Hide the quest dialog */
    void CloseDialog();
    void ShowGossip(const FWowGossipMenu& Menu);
    void AcceptCurrentQuest();
    void CompleteCurrentQuest();
    uint32 GetCurrentQuestId() const { return CurrentQuest.QuestId; }

private:
    FSlateBrush Background[4];
    FSlateBrush PanelHighlight, CloseHighlight, AvailableIcon, ActiveIcon, GossipIcon, Book, GreetingFooter;
    FButtonStyle PanelStyle, CloseStyle, RowStyle;
    TSharedRef<SWidget> PanelButton(const FString& Label, FOnClicked Action, float Width = 78.f, bool bClose = false);
    TSharedRef<SWidget> GossipRow(const FString& Label, const FSlateBrush* Icon, FOnClicked Action, bool bEnabled = true);
    TArray<TStrongObjectPtr<class UTexture2D>> Artwork;
    FSlateFontInfo TitleFont, BodyFont, ButtonFont;
    TSharedPtr<class STextBlock> NpcName;
    TWeakObjectPtr<class UImage> Portrait;
    FOnQuestDialogAccept OnQuestAccept;
    FOnQuestDialogComplete OnQuestComplete;
    FOnQuestDialogDecline OnQuestDecline;
    FOnGossipQuestSelected OnGossipQuest;
    FOnGossipOptionSelected OnGossipOption;

    TSharedPtr<SScrollBox> ContentArea;
    TSharedPtr<SHorizontalBox> ButtonArea;

    FWowQuestDetails CurrentQuest;
    bool bIsRewardDialog = false;
    int32 SelectedRewardChoice = 0;

    FReply OnAcceptClicked();
    FReply OnDeclineClicked();
    FReply OnCompleteClicked();
    FReply OnRewardItemClicked(int32 RewardIndex);

    /** Generate quest text content */
    void RefreshContent();
};
