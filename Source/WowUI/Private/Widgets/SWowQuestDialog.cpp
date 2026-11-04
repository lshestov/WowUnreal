#include "Widgets/SWowQuestDialog.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Brushes/SlateNoResource.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Styling/CoreStyle.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Components/Image.h"
#include "WowFrameManager.h"
#include "WowFontManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SSpacer.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowQuestDialog, Log, All);

void SWowQuestDialog::Construct(const FArguments& InArgs)
{
    OnQuestAccept = InArgs._OnQuestAccept;
    OnQuestComplete = InArgs._OnQuestComplete;
    OnQuestDecline = InArgs._OnQuestDecline;
    OnGossipQuest = InArgs._OnGossipQuest;
    OnGossipOption = InArgs._OnGossipOption;

    TitleFont=FCoreStyle::GetDefaultFontStyle("Bold",16);
    BodyFont=FCoreStyle::GetDefaultFontStyle("Regular",12);
    ButtonFont=BodyFont;
    RowStyle.SetNormal(FSlateNoResource()).SetPressed(FSlateNoResource()).SetDisabled(FSlateNoResource());
    RowStyle.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(1,1,0,0));
    PanelStyle.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(1,1,0,0));
    CloseStyle.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
    const auto Canvas=SNew(SConstraintCanvas);
    ChildSlot[Canvas];
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(7,6,60,60))
        [SNew(SImage).Image_Lambda([this]()->const FSlateBrush* {return Portrait.IsValid() && Portrait->GetBrush().GetResourceObject()?&Portrait->GetBrush():&Book;})];
    // QuestFramePanelTemplate has 256/128 pixel columns and two 256 pixel rows.
    // Its hit rect omits 30 transparent pixels at the right and 70 at the bottom.
    for (int32 I=0;I<4;++I)
    {
        const int32 W=(I%2)?98:256, H=(I/2)?186:256;
        Background[I].DrawAs=ESlateBrushDrawType::Image;
        Background[I].SetUVRegion(FBox2f(FVector2f::ZeroVector,FVector2f(W/float(I%2?128:256),H/256.f)));
        Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(I%2?256:0,I/2?256:0,W,H))
            [SNew(SImage).Image(&Background[I]).Visibility(EVisibility::HitTestInvisible)];
    }
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(76,19,240,22))
        [SAssignNew(NpcName,STextBlock).Justification(ETextJustify::Center).ColorAndOpacity(FLinearColor::White)];
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(326,15,32,32))
        [PanelButton(FString(),FOnClicked::CreateSP(this,&SWowQuestDialog::OnDeclineClicked),32,true)];
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(22,380,128,64))
        [SNew(SImage).Image(&GreetingFooter).Visibility_Lambda([this](){return CurrentQuest.QuestId?EVisibility::Hidden:EVisibility::HitTestInvisible;})];
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(26,82,294,319))
        [SAssignNew(ContentArea,SScrollBox).Orientation(Orient_Vertical)];
    // QuestFrame.xml positions the footer controls over cut-outs in the panel art.
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(23,418,322,22))
        [SAssignNew(ButtonArea,SHorizontalBox)];

    SetVisibility(EVisibility::Collapsed);
}

void SWowQuestDialog::SetPresentation(FWowFrameManager& Frames, const FString& Name, bool bShowTargetPortrait)
{
    static const TCHAR* Tiles[]={TEXT("TopLeft"),TEXT("TopRight"),TEXT("BotLeft"),TEXT("BotRight")};
    for (int32 I=0;I<4;++I)
    {
        if (Background[I].GetResourceObject()) continue;
        const FString Path=FString(TEXT("Interface\\QuestFrame\\UI-QuestGreeting-"))+Tiles[I];
        if (auto* Texture=Frames.LoadUITexture(Path))
        {
            Artwork.Emplace(Texture);
            Background[I].SetResourceObject(Texture);
        }
    }
    auto Load=[&](FSlateBrush& Brush,const TCHAR* Path,bool bPanel=false)
    {
        if (Brush.GetResourceObject()) return;
        if (auto* Texture=Frames.LoadUITexture(Path))
        {
            Artwork.Emplace(Texture);
            Brush.DrawAs=ESlateBrushDrawType::Image;
            Brush.SetResourceObject(Texture);
            if (bPanel) Brush.SetUVRegion(FBox2f(FVector2f::ZeroVector,FVector2f(.625f,.6875f)));
        }
    };
    Load(PanelStyle.Normal,TEXT("Interface\\Buttons\\UI-Panel-Button-Up"),true);
    PanelStyle.Hovered=PanelStyle.Normal;
    Load(PanelStyle.Pressed,TEXT("Interface\\Buttons\\UI-Panel-Button-Down"),true);
    Load(PanelStyle.Disabled,TEXT("Interface\\Buttons\\UI-Panel-Button-Disabled"),true);
    Load(PanelHighlight,TEXT("Interface\\Buttons\\UI-Panel-Button-Highlight"),true);
    Load(CloseStyle.Normal,TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"));
    CloseStyle.Hovered=CloseStyle.Normal;
    Load(CloseStyle.Pressed,TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Down"));
    Load(CloseHighlight,TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Highlight"));
    Load(RowStyle.Hovered,TEXT("Interface\\QuestFrame\\UI-QuestTitleHighlight"));
    Load(AvailableIcon,TEXT("Interface\\GossipFrame\\AvailableQuestIcon"));
    Load(ActiveIcon,TEXT("Interface\\GossipFrame\\ActiveQuestIcon"));
    Load(GossipIcon,TEXT("Interface\\GossipFrame\\GossipGossipIcon"));
    Load(Book,TEXT("Interface\\QuestFrame\\UI-QuestLog-BookIcon"));
    Load(GreetingFooter,TEXT("Interface\\QuestFrame\\UI-Quest-BotLeftPatch"));
    if (auto* Fonts=Frames.GetFontManager(); Fonts && Fonts->IsInitialized())
    {
        TitleFont=Fonts->GetFont(TEXT("QuestTitleFont"),Fonts->GetFontSize(TEXT("QuestTitleFont")));
        BodyFont=Fonts->GetFont(TEXT("QuestFont"),Fonts->GetFontSize(TEXT("QuestFont")));
        ButtonFont=Fonts->GetFont(TEXT("GameFontNormal"),12);
        NpcName->SetFont(Fonts->GetFont(TEXT("GameFontHighlight"),12));
    }
    Portrait=bShowTargetPortrait?Frames.GetNamedTexture(TEXT("TargetFramePortrait")):nullptr;
    NpcName->SetText(FText::FromString(Name));
}

TSharedRef<SWidget> SWowQuestDialog::PanelButton(const FString& Label,FOnClicked Action,float Width,bool bClose)
{
    const auto Button=SNew(SButton).ButtonStyle(bClose?&CloseStyle:&PanelStyle)
        .ContentPadding(FMargin(0)).HAlign(HAlign_Fill).VAlign(VAlign_Fill).OnClicked(Action);
    const TWeakPtr<SButton> Weak=Button;
    const auto Content=SNew(SOverlay);
    Content->AddSlot()
        [SNew(SImage).Image(bClose?&CloseHighlight:&PanelHighlight).ColorAndOpacity(FLinearColor(1,1,1,.55f))
            .Visibility_Lambda([Weak]() {const auto B=Weak.Pin(); return B && B->IsHovered()?EVisibility::HitTestInvisible:EVisibility::Hidden;})];
    if (!bClose)
        Content->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [SNew(STextBlock).Font(ButtonFont).Text(FText::FromString(Label))
                .ColorAndOpacity(FLinearColor(1,.82f,0)).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)];
    Button->SetContent(Content);
    return SNew(SBox).WidthOverride(Width).HeightOverride(bClose?32.f:22.f)[Button];
}

TSharedRef<SWidget> SWowQuestDialog::GossipRow(const FString& Label,const FSlateBrush* Icon,FOnClicked Action,bool bEnabled)
{
    return SNew(SButton).ButtonStyle(&RowStyle).ContentPadding(FMargin(0,3)).IsEnabled(bEnabled).OnClicked(Action)
        [SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0,0,5,0)
                [SNew(SBox).WidthOverride(18).HeightOverride(18)[SNew(SImage).Image(Icon)]]
            +SHorizontalBox::Slot().FillWidth(1.f)
                [SNew(STextBlock).Text(FText::FromString(Label)).Font(BodyFont)
                    .Justification(ETextJustify::Left).ColorAndOpacity(FLinearColor(.055f,.027f,.01f)).AutoWrapText(true)]];
}

void SWowQuestDialog::ShowQuestDetails(const FWowQuestDetails& QuestDetails)
{
    CurrentQuest = QuestDetails;
    bIsRewardDialog = false;
    SelectedRewardChoice = 0;

    RefreshContent();
    SetVisibility(EVisibility::Visible);

    UE_LOG(LogWowQuestDialog, Log, TEXT("Showing quest details: %s"), *QuestDetails.Title);
}

void SWowQuestDialog::ShowQuestReward(const FWowQuestDetails& QuestDetails)
{
    CurrentQuest = QuestDetails;
    bIsRewardDialog = true;
    SelectedRewardChoice = 0;

    RefreshContent();
    SetVisibility(EVisibility::Visible);

    UE_LOG(LogWowQuestDialog, Log, TEXT("Showing quest reward: %s"), *QuestDetails.Title);
}

void SWowQuestDialog::CloseDialog()
{
    CurrentQuest = FWowQuestDetails();
    SetVisibility(EVisibility::Collapsed);

    UE_LOG(LogWowQuestDialog, Log, TEXT("Closed quest dialog"));
}

void SWowQuestDialog::AcceptCurrentQuest()
{
    if (!bIsRewardDialog && CurrentQuest.QuestId && GetVisibility()==EVisibility::Visible) OnAcceptClicked();
}

void SWowQuestDialog::CompleteCurrentQuest()
{
    if (bIsRewardDialog && CurrentQuest.QuestId && GetVisibility()==EVisibility::Visible) OnCompleteClicked();
}

void SWowQuestDialog::ShowGossip(const FWowGossipMenu& Menu)
{
    CurrentQuest=FWowQuestDetails();
    ContentArea->ClearChildren();  ButtonArea->ClearChildren();
    ContentArea->AddSlot()[SNew(STextBlock).Font(BodyFont).Justification(ETextJustify::Left).ColorAndOpacity(FLinearColor(.055f,.027f,.01f)).Text(FText::FromString(Menu.Greeting.IsEmpty()?TEXT("Available quests and conversation"):Menu.Greeting)).AutoWrapText(true)];
    for (const auto& Quest : Menu.Quests)
        ContentArea->AddSlot()[GossipRow(Quest.Title,Quest.Icon==4?&ActiveIcon:&AvailableIcon,
            FOnClicked::CreateLambda([this,Id=Quest.Id](){OnGossipQuest.ExecuteIfBound(Id); return FReply::Handled();}))];
    for (const auto& Option : Menu.Options)
        ContentArea->AddSlot()[GossipRow(Option.Text,&GossipIcon,
            FOnClicked::CreateLambda([this,Id=Option.Id](){OnGossipOption.ExecuteIfBound(Id); return FReply::Handled();}),
            !Option.bCoded && Option.Money==0 && Option.Confirmation.IsEmpty())];
    ButtonArea->AddSlot().FillWidth(1)[SNew(SSpacer)];
    ButtonArea->AddSlot().AutoWidth()[PanelButton(TEXT("Goodbye"),FOnClicked::CreateSP(this,&SWowQuestDialog::OnDeclineClicked))];
    SetVisibility(EVisibility::Visible);
}

void SWowQuestDialog::RefreshContent()
{
    const FLinearColor Ink(.055f,.027f,.01f);
    // Clear existing content
    ContentArea->ClearChildren();

    ButtonArea->ClearChildren();

    // Quest title
    ContentArea->AddSlot().Padding(0,1,0,4)
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
        .Padding(FMargin(0,0,0,6))
        [
            SNew(STextBlock)
            .Text(FText::FromString(CurrentQuest.Title))
            .Font(TitleFont)
            .Justification(ETextJustify::Left)
            .ColorAndOpacity(Ink)
            .AutoWrapText(true)
        ]
    ];

    // Quest details text
    if (!CurrentQuest.Details.IsEmpty())
    {
        ContentArea->AddSlot().Padding(0,1,0,4)
        [
            SNew(STextBlock)
            .Text(FText::FromString(CurrentQuest.Details))
            .Font(BodyFont)
            .Justification(ETextJustify::Left)
            .ColorAndOpacity(Ink)
            .AutoWrapText(true)
        ];
    }

    // Quest objectives
    if (!CurrentQuest.Objectives.IsEmpty())
    {
        ContentArea->AddSlot().Padding(0,1,0,4)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Quest Objectives")))
            .Font(TitleFont)
            .Justification(ETextJustify::Left)
            .ColorAndOpacity(Ink)
        ];

        ContentArea->AddSlot().Padding(0,1,0,4)
        [
            SNew(STextBlock)
            .Text(FText::FromString(CurrentQuest.Objectives))
            .Font(BodyFont)
            .Justification(ETextJustify::Left)
            .ColorAndOpacity(Ink)
            .AutoWrapText(true)
        ];
    }

    // Rewards section
    if (CurrentQuest.RewardMoney > 0 || CurrentQuest.RewardXP > 0 || CurrentQuest.RewardItems.Num() > 0)
    {
        ContentArea->AddSlot().Padding(0,1,0,4)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Rewards")))
            .Font(TitleFont)
            .Justification(ETextJustify::Left)
            .ColorAndOpacity(Ink)
        ];

        // XP reward
        if (CurrentQuest.RewardXP > 0)
        {
            ContentArea->AddSlot().Padding(0,1,0,4)
            [
                SNew(STextBlock)
                .Text(FText::FromString(FString::Printf(TEXT("Experience: %u XP"), CurrentQuest.RewardXP)))
                .Font(BodyFont)
                .Justification(ETextJustify::Left)
                .ColorAndOpacity(Ink)
            ];
        }

        // Money reward
        if (CurrentQuest.RewardMoney > 0)
        {
            uint32 Gold = CurrentQuest.RewardMoney / 10000;
            uint32 Silver = (CurrentQuest.RewardMoney % 10000) / 100;
            uint32 Copper = CurrentQuest.RewardMoney % 100;

            FString MoneyText;
            if (Gold > 0) MoneyText += FString::Printf(TEXT("%ug "), Gold);
            if (Silver > 0) MoneyText += FString::Printf(TEXT("%us "), Silver);
            if (Copper > 0 || CurrentQuest.RewardMoney < 100) MoneyText += FString::Printf(TEXT("%uc"), Copper);

            ContentArea->AddSlot().Padding(0,1,0,4)
            [
                SNew(STextBlock)
                .Text(FText::FromString(FString::Printf(TEXT("Money: %s"), *MoneyText.TrimEnd())))
                .Font(BodyFont)
                .Justification(ETextJustify::Left)
                .ColorAndOpacity(Ink)
            ];
        }
    }

    // Choice rewards (for reward dialog)
    if (bIsRewardDialog && CurrentQuest.ChoiceRewards.Num() > 0)
    {
        ContentArea->AddSlot().Padding(0,1,0,4)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Choose your reward:")))
            .Font(TitleFont)
            .Justification(ETextJustify::Left)
            .ColorAndOpacity(Ink)
        ];

        for (int32 i = 0; i < CurrentQuest.ChoiceRewards.Num(); ++i)
        {
            const FWowQuestRewardItem& RewardItem = CurrentQuest.ChoiceRewards[i];

            ContentArea->AddSlot().Padding(0,1,0,4)
            [
                SNew(SButton)
                .OnClicked_Lambda([this, i]() { return OnRewardItemClicked(i); })
                .ButtonColorAndOpacity_Lambda([this, i]() {
                    return SelectedRewardChoice == i ? FLinearColor::Blue : FLinearColor::Gray;
                })
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SBox)
                        .WidthOverride(24.0f)
                        .HeightOverride(24.0f)
                        [
                            SNew(SBorder)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(TEXT("?")))
                                .Justification(ETextJustify::Center)
                            ]
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(FString::Printf(TEXT("Reward Item %u (x%u)"),
                            RewardItem.ItemId, RewardItem.Count)))
                    ]
                ]
            ];
        }
    }

    if (bIsRewardDialog)
    {
        ButtonArea->AddSlot().AutoWidth()[PanelButton(TEXT("Complete Quest"),FOnClicked::CreateSP(this,&SWowQuestDialog::OnCompleteClicked),120)];
        ButtonArea->AddSlot().FillWidth(1)[SNew(SSpacer)];
        ButtonArea->AddSlot().AutoWidth()[PanelButton(TEXT("Cancel"),FOnClicked::CreateSP(this,&SWowQuestDialog::OnDeclineClicked))];
    }
    else
    {
        ButtonArea->AddSlot().AutoWidth()[PanelButton(TEXT("Accept"),FOnClicked::CreateSP(this,&SWowQuestDialog::OnAcceptClicked),77)];
        ButtonArea->AddSlot().FillWidth(1)[SNew(SSpacer)];
        ButtonArea->AddSlot().AutoWidth()[PanelButton(TEXT("Decline"),FOnClicked::CreateSP(this,&SWowQuestDialog::OnDeclineClicked))];
    }

}

FReply SWowQuestDialog::OnAcceptClicked()
{
    if (OnQuestAccept.IsBound())
    {
        OnQuestAccept.Execute(CurrentQuest.QuestGiverGuid, CurrentQuest.QuestId);
    }
    CloseDialog();
    return FReply::Handled();
}

FReply SWowQuestDialog::OnDeclineClicked()
{
    if (OnQuestDecline.IsBound())
    {
        OnQuestDecline.Execute();
    }
    CloseDialog();
    return FReply::Handled();
}

FReply SWowQuestDialog::OnCompleteClicked()
{
    if (OnQuestComplete.IsBound())
    {
        OnQuestComplete.Execute(CurrentQuest.QuestGiverGuid, CurrentQuest.QuestId, SelectedRewardChoice);
    }
    CloseDialog();
    return FReply::Handled();
}

FReply SWowQuestDialog::OnRewardItemClicked(int32 RewardIndex)
{
    SelectedRewardChoice = RewardIndex;
    // Refresh to update button colors
    RefreshContent();
    return FReply::Handled();
}
