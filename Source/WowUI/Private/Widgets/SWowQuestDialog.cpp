#include "Widgets/SWowQuestDialog.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Brushes/SlateNoResource.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Styling/CoreStyle.h"
#include "Fonts/FontCache.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/Image.h"
#include "WowFrameManager.h"
#include "WowFontManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SSpacer.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowQuestDialog, Log, All);

static FSlateFontInfo WithQuestTracking(FSlateFontInfo Font)
{
    // The original client rounds unhinted glyph advances upwards. Slate's
    // full shaper adds trunc(LetterSpacing * point size / 1000) to each advance.
    Font.LetterSpacing = FMath::CeilToInt(1000.f / FMath::Max(1.f, Font.Size));
    return Font;
}

void SWowQuestDialog::Construct(const FArguments& InArgs)
{
    OnQuestAccept = InArgs._OnQuestAccept;
    OnQuestComplete = InArgs._OnQuestComplete;
    OnQuestDecline = InArgs._OnQuestDecline;
    OnGossipQuest = InArgs._OnGossipQuest;
    OnGossipOption = InArgs._OnGossipOption;

    // FontStyles.xml declares the heading's brown shadow; Constants.lua supplies
    // parchment ink. Slate takes linear colours, while FrameXML specifies sRGB.
    TitleText.SetFont(WithQuestTracking(FCoreStyle::GetDefaultFontStyle("Regular",13.5f))).SetColorAndOpacity(FLinearColor::Black)
        .SetShadowOffset(FVector2D(1,1)).SetShadowColorAndOpacity(FLinearColor::FromSRGBColor(FColor(125,89,13)));
    BodyText.SetFont(WithQuestTracking(FCoreStyle::GetDefaultFontStyle("Regular",9.75f))).SetColorAndOpacity(FLinearColor::FromSRGBColor(FColor(46,31,15)))
        .SetShadowOffset(FVector2D::ZeroVector);
    RowText = BodyText;
    RowText.SetColorAndOpacity(FLinearColor::Black);
    CaptionText.SetFont(WithQuestTracking(FCoreStyle::GetDefaultFontStyle("Regular",9.f))).SetColorAndOpacity(FLinearColor::White)
        .SetShadowOffset(FVector2D(1,1)).SetShadowColorAndOpacity(FLinearColor::Black);
    RewardNumber.SetFont(FCoreStyle::GetDefaultFontStyle("Regular",12.f)).SetColorAndOpacity(FLinearColor::White);
    RowStyle.SetNormal(FSlateNoResource()).SetHovered(FSlateNoResource()).SetPressed(FSlateNoResource()).SetDisabled(FSlateNoResource());
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
            [SNew(SImage).Image_Lambda([this,I]() -> const FSlateBrush* {
                return I == 3 && CurrentQuest.bAutoAccepted && !bIsRewardDialog && AutoAcceptBottomRight.GetResourceObject() ? &AutoAcceptBottomRight : &Background[I];
            }).Visibility(EVisibility::HitTestInvisible)];
    }
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset_Lambda([this]() {
        return bUsesGossipFrame ? FMargin(77,20,249,20) : FMargin(74.5f,20,235,20);
    })
        [SNew(SBox).VAlign(VAlign_Center).Clipping(EWidgetClipping::ClipToBounds)
            [SAssignNew(NpcName,STextBlock).TextStyle(&CaptionText).Justification(ETextJustify::Center)
                .TextShapingMethod(ETextShapingMethod::FullShaping)
                .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]];
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(326,15,32,32))
        [PanelButton(FString(),FOnClicked::CreateSP(this,&SWowQuestDialog::OnDeclineClicked),32,true)];
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(22,380,128,64))
        [SNew(SImage).Image(&GreetingFooter).Visibility_Lambda([this](){return CurrentQuest.QuestId?EVisibility::Hidden:EVisibility::HitTestInvisible;})];
    // UIPanelScrollFrameTemplate keeps the scrollbar outside the parchment text column.
    const auto ScrollBar = SNew(SScrollBar).Orientation(Orient_Vertical).Thickness(FVector2D(12,12));
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(23,81,300,334))
        [SAssignNew(ContentArea,SScrollBox).Orientation(Orient_Vertical)
            .ExternalScrollbar(ScrollBar).Clipping(EWidgetClipping::ClipToBounds)];
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(325,81,19,334))[ScrollBar];
    // QuestFrame.xml positions the footer controls over cut-outs in the panel art.
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset_Lambda([this](){
        return FMargin(23,CurrentQuest.QuestId ? 418 : 417,322,22);
    })
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
    auto Load=[&](FSlateBrush& Brush,const TCHAR* Path,bool bPanel=false,bool bAdditive=false)
    {
        if (Brush.GetResourceObject()) return;
        if (auto* Texture=Frames.LoadUITexture(Path))
        {
            UObject* Resource = Texture;
            if (bAdditive)
            {
                // These BLPs have black backgrounds. FrameXML renders them with alphaMode="ADD".
                auto* Base = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowUiAdditive.M_WowUiAdditive"));
                if (!Base) return;
                auto* Highlight = UMaterialInstanceDynamic::Create(Base,GetTransientPackage());
                Highlight->SetTextureParameterValue(TEXT("Image"),Texture);
                Resource = Highlight;
            }
            Artwork.Emplace(Resource);
            Brush.DrawAs=ESlateBrushDrawType::Image;
            Brush.SetResourceObject(Resource);
            if (bPanel) Brush.SetUVRegion(FBox2f(FVector2f::ZeroVector,FVector2f(.625f,.6875f)));
        }
    };
    Load(PanelStyle.Normal,TEXT("Interface\\Buttons\\UI-Panel-Button-Up"),true);
    PanelStyle.Hovered=PanelStyle.Normal;
    Load(PanelStyle.Pressed,TEXT("Interface\\Buttons\\UI-Panel-Button-Down"),true);
    Load(PanelStyle.Disabled,TEXT("Interface\\Buttons\\UI-Panel-Button-Disabled"),true);
    Load(PanelHighlight,TEXT("Interface\\Buttons\\UI-Panel-Button-Highlight"),true,true);
    Load(CloseStyle.Normal,TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Up"));
    CloseStyle.Hovered=CloseStyle.Normal;
    Load(CloseStyle.Pressed,TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Down"));
    Load(CloseHighlight,TEXT("Interface\\Buttons\\UI-Panel-MinimizeButton-Highlight"),false,true);
    Load(RowStyle.Hovered,TEXT("Interface\\QuestFrame\\UI-QuestTitleHighlight"),false,true);
    Load(AvailableIcon,TEXT("Interface\\GossipFrame\\AvailableQuestIcon"));
    Load(ActiveIcon,TEXT("Interface\\GossipFrame\\ActiveQuestIcon"));
    Load(GossipIcon,TEXT("Interface\\GossipFrame\\GossipGossipIcon"));
    Load(Book,TEXT("Interface\\QuestFrame\\UI-QuestLog-BookIcon"));
    Load(GreetingFooter,TEXT("Interface\\QuestFrame\\UI-Quest-BotLeftPatch"));
    Load(AutoAcceptBottomRight,TEXT("Interface\\QuestFrame\\UI-QuestGreeting-BotRight-blank"));
    AutoAcceptBottomRight.SetUVRegion(static_cast<FBox2f>(Background[3].GetUVRegion()));
    Load(SectionDivider,TEXT("Interface\\QuestFrame\\UI-HorizontalBreak"));
    if (auto* Fonts=Frames.GetFontManager(); Fonts && Fonts->IsInitialized())
    {
        auto Font = [Fonts](const TCHAR* Role)
        {
            return Fonts->GetFont(Role, Fonts->GetFontSize(Role), EFontHinting::None);
        };
        TitleText.SetFont(WithQuestTracking(Font(TEXT("QuestTitleFont"))));
        BodyText.SetFont(WithQuestTracking(Font(TEXT("QuestFont"))));
        RowText.SetFont(WithQuestTracking(Font(TEXT("QuestFontLeft"))));
        CaptionText.SetFont(WithQuestTracking(Font(TEXT("GameFontHighlight"))));
        RewardNumber.SetFont(Font(TEXT("NumberFontNormalLarge")));
        RewardNumber.Font.OutlineSettings.OutlineSize = 1;
        NpcName->SetFont(CaptionText.Font);
    }
    Portrait=bShowTargetPortrait?Frames.GetNamedTexture(TEXT("TargetFramePortrait")):nullptr;
    NpcName->SetText(FText::FromString(Name));
}

TSharedRef<SWidget> SWowQuestDialog::PanelButton(const FString& Label,FOnClicked Action,float Width,bool bClose)
{
    const auto Button=SNew(SButton).Tag(bClose ? FName(TEXT("CloseButton")) : NAME_None).ButtonStyle(bClose?&CloseStyle:&PanelStyle)
        .ContentPadding(FMargin(0)).HAlign(HAlign_Fill).VAlign(VAlign_Fill).OnClicked(Action);
    const TWeakPtr<SButton> Weak=Button;
    const auto Content=SNew(SOverlay);
    Content->AddSlot()
        [SNew(SImage).Image(bClose?&CloseHighlight:&PanelHighlight).ColorAndOpacity(FLinearColor(1,1,1,.55f))
            .Visibility_Lambda([Weak]() {const auto B=Weak.Pin(); return B && B->IsHovered()?EVisibility::HitTestInvisible:EVisibility::Hidden;})];
    if (!bClose)
        Content->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [SNew(STextBlock).TextStyle(&CaptionText).Text(FText::FromString(Label))
                .TextShapingMethod(ETextShapingMethod::FullShaping)
                .ColorAndOpacity_Lambda([Weak]() -> FSlateColor {
                    const auto Current = Weak.Pin();
                    return Current && Current->IsHovered() ? FLinearColor::White : FLinearColor::FromSRGBColor(FColor(255,209,0));
                })];
    Button->SetContent(Content);
    return SNew(SBox).WidthOverride(Width).HeightOverride(bClose?32.f:22.f)[Button];
}

TSharedRef<STextBlock> SWowQuestDialog::WrappedText(const FString& Text,const FTextBlockStyle& Style,float Width) const
{
    return SNew(STextBlock).TextStyle(&Style).Text(FText::FromString(Text))
        .TextShapingMethod(ETextShapingMethod::FullShaping)
        .WrapTextAt(Width).WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
        .Justification(ETextJustify::Left)
        .LineHeightPercentage_Lambda([this,&Style]()
        {
            // 3.3.5a advances by the requested pixel height, whereas Slate uses
            // the font's ascent and descent. Account for pixel rounding on resize.
            const float Scale = GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
            const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
            const int32 Height = Measure->GetMaxCharacterHeight(Style.Font,Scale)
                + FMath::RoundToInt(FMath::Abs(Style.ShadowOffset.Y) * Scale);
            const int32 Advance = FMath::Max(1,FMath::RoundToInt(Style.Font.Size * (96.f / 72.f) * Scale));
            return Height > 0 ? float(Advance) / Height : 1.f;
        });
}

TSharedRef<SWidget> SWowQuestDialog::GossipRow(const FString& Label,const FSlateBrush* Icon,FOnClicked Action,bool bEnabled)
{
    return SNew(SButton).ButtonStyle(&RowStyle).ContentPadding(FMargin(0,1)).IsEnabled(bEnabled)
        .HAlign(HAlign_Fill).VAlign(VAlign_Center).OnClicked(Action)
        [SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(3,0,1,0)
                [SNew(SBox).WidthOverride(16).HeightOverride(16)[SNew(SImage).Image(Icon)]]
            +SHorizontalBox::Slot().FillWidth(1.f)
                [WrappedText(Label,RowText,275)]];
}

void SWowQuestDialog::ShowQuestDetails(const FWowQuestDetails& QuestDetails)
{
    ContentArea->ScrollToStart();
    CurrentQuest = QuestDetails;
    bIsRewardDialog = false;
    SelectedRewardChoice = 0;

    RefreshContent();
    SetVisibility(EVisibility::Visible);

    UE_LOG(LogWowQuestDialog, Log, TEXT("Showing quest details: %s"), *QuestDetails.Title);
}

void SWowQuestDialog::ShowQuestReward(const FWowQuestDetails& QuestDetails)
{
    ContentArea->ScrollToStart();
    CurrentQuest = QuestDetails;
    bIsRewardDialog = true;
    SelectedRewardChoice = 0;

    RefreshContent();
    SetVisibility(EVisibility::Visible);

    UE_LOG(LogWowQuestDialog, Log, TEXT("Showing quest reward: %s"), *QuestDetails.Title);
}

void SWowQuestDialog::CloseDialog()
{
    GreetingBlock.Reset();
    GreetingText.Reset();
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
    ContentArea->ScrollToStart();
    CurrentQuest=FWowQuestDetails();
    bUsesGossipFrame = !Menu.bQuestList;
    ContentArea->ClearChildren();  ButtonArea->ClearChildren();
    ContentArea->AddSlot()[SNew(SSpacer).Size(FVector2D(1,10))];
    GreetingText = WrappedText(Menu.Greeting, Menu.bQuestList ? BodyText : RowText, 270);
    ContentArea->AddSlot()
        [SAssignNew(GreetingBlock,SBox).Padding(FMargin(10,0,20,Menu.bQuestList ? 0 : 20))
            [GreetingText.ToSharedRef()]];
    UpdateGossipGreeting(Menu.Greeting);

    auto AddQuestRow = [&](const FWowGossipQuest& Quest)
    {
        ContentArea->AddSlot().Padding(0,0,0,Menu.bQuestList ? 2 : 3)
            [GossipRow(Quest.Title,Quest.Icon==4?&ActiveIcon:&AvailableIcon,
            FOnClicked::CreateLambda([this,Id=Quest.Id](){OnGossipQuest.ExecuteIfBound(Id); return FReply::Handled();}))];
    };
    if (Menu.bQuestList)
    {
        auto AddSection = [&](const TCHAR* Heading,bool bActive)
        {
            auto IsInSection = [bActive](const FWowGossipQuest& Quest){ return (Quest.Icon == 4) == bActive; };
            if (!Menu.Quests.ContainsByPredicate(IsInSection)) return false;
            ContentArea->AddSlot().Padding(10,10,20,5)
                [WrappedText(Heading,TitleText,270)];
            for (const auto& Quest : Menu.Quests)
                if (IsInSection(Quest)) AddQuestRow(Quest);
            return true;
        };
        const bool bHasActive = AddSection(TEXT("Current Quests"),true);
        if (bHasActive && Menu.Quests.ContainsByPredicate([](const FWowGossipQuest& Quest){ return Quest.Icon != 4; }))
            ContentArea->AddSlot().HAlign(HAlign_Left).Padding(22,10,0,0)
                [SNew(SBox).WidthOverride(256).HeightOverride(32)[SNew(SImage).Image(&SectionDivider)]];
        AddSection(TEXT("Available Quests"),false);
    }
    else
    {
        for (const auto& Quest : Menu.Quests) AddQuestRow(Quest);
    }
    for (const auto& Option : Menu.Options)
        ContentArea->AddSlot().Padding(0,0,0,3)[GossipRow(Option.Text,&GossipIcon,
            FOnClicked::CreateLambda([this,Id=Option.Id](){OnGossipOption.ExecuteIfBound(Id); return FReply::Handled();}),
            !Option.bCoded && Option.Money==0 && Option.Confirmation.IsEmpty())];
    ButtonArea->AddSlot().FillWidth(1)[SNew(SSpacer)];
    ButtonArea->AddSlot().AutoWidth()[PanelButton(TEXT("Goodbye"),FOnClicked::CreateSP(this,&SWowQuestDialog::OnDeclineClicked))];
    SetVisibility(EVisibility::Visible);
}

void SWowQuestDialog::UpdateGossipGreeting(const FString& Greeting)
{
    if (GreetingText && GreetingBlock)
    {
        GreetingText->SetText(FText::FromString(Greeting));
        GreetingBlock->SetVisibility(Greeting.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
    }
}

void SWowQuestDialog::RefreshContent()
{
    bUsesGossipFrame = false;
    GreetingBlock.Reset();
    GreetingText.Reset();
    ContentArea->ClearChildren();
    ButtonArea->ClearChildren();

    // QuestInfo.xml gives the title, description and objectives a shared 285px column.
    auto AddParagraph = [&](const FString& Text,const FTextBlockStyle& Style,float TopMargin)
    {
        ContentArea->AddSlot().Padding(5,TopMargin,10,0)
            [WrappedText(Text,Style,285)];
    };
    AddParagraph(CurrentQuest.Title,TitleText,10);

    // Quest details text
    if (!CurrentQuest.Details.IsEmpty())
    {
        AddParagraph(CurrentQuest.Details,BodyText,5);
    }

    // Quest objectives
    if (!CurrentQuest.Objectives.IsEmpty())
    {
        AddParagraph(TEXT("Quest Objectives"),TitleText,10);
        AddParagraph(CurrentQuest.Objectives,BodyText,5);
    }

    // Rewards section
    if (CurrentQuest.RewardMoney > 0 || CurrentQuest.RewardXP > 0 || CurrentQuest.RewardItems.Num() > 0)
    {
        AddParagraph(TEXT("Rewards"),TitleText,bIsRewardDialog ? 10 : 15);

        // XP reward
        if (CurrentQuest.RewardXP > 0)
        {
            ContentArea->AddSlot().Padding(5,5,10,0)
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(STextBlock).TextStyle(&BodyText).Text(FText::FromString(TEXT("Experience:")))]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(15,0,0,0)
                        [SNew(STextBlock).TextStyle(&RewardNumber).Text(FText::AsNumber(CurrentQuest.RewardXP))]];
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

            ContentArea->AddSlot().Padding(5,5,10,0)
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(STextBlock).TextStyle(&BodyText).Text(FText::FromString(TEXT("Money:")))]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(15,0,0,0)
                        [SNew(STextBlock).TextStyle(&RewardNumber).Text(FText::FromString(MoneyText.TrimEnd()))]];
        }
    }

    // Choice rewards (for reward dialog)
    if (bIsRewardDialog && CurrentQuest.ChoiceRewards.Num() > 0)
    {
        AddParagraph(TEXT("Choose your reward:"),BodyText,5);

        for (int32 i = 0; i < CurrentQuest.ChoiceRewards.Num(); ++i)
        {
            const FWowQuestRewardItem& RewardItem = CurrentQuest.ChoiceRewards[i];

            ContentArea->AddSlot().Padding(5,5,10,0)
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
                                .TextStyle(&CaptionText)
                                .Text(FText::FromString(TEXT("?")))
                                .Justification(ETextJustify::Center)
                            ]
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    [
                        SNew(STextBlock)
                        .TextStyle(&CaptionText)
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
        if (!CurrentQuest.bAutoAccepted)
            ButtonArea->AddSlot().AutoWidth()[PanelButton(TEXT("Decline"),FOnClicked::CreateSP(this,&SWowQuestDialog::OnDeclineClicked))];
    }

}

FReply SWowQuestDialog::OnAcceptClicked()
{
    // QuestFrameDetailPanel uses its single Accept button to dismiss an
    // auto-accepted quest. The server has already written the journal slot.
    if (!CurrentQuest.bAutoAccepted && OnQuestAccept.IsBound())
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
