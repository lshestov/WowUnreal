#include "Widgets/SWowQuestDialog.h"
#include "WowQuestItemTooltip.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Brushes/SlateNoResource.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Styling/CoreStyle.h"
#include "Fonts/FontCache.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/Image.h"
#include "WowFrameManager.h"
#include "WowConnectionManager.h"
#include "WowUIManager.h"
#include "LuaApi/LuaApiRegistry.h"
#include "Formats/Dbc/DbcStore.h"
#include "Slate/DeferredCleanupSlateBrush.h"
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
    OnQuestContinue = InArgs._OnQuestContinue;
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
    StackNumber = RewardNumber;
    SmallQuestText = BodyText;
    RowStyle.SetNormal(FSlateNoResource()).SetHovered(FSlateNoResource()).SetPressed(FSlateNoResource()).SetDisabled(FSlateNoResource());
    ItemStyle = RowStyle;
    ItemStyle.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
    RowStyle.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(1,1,0,0));
    PanelStyle.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(1,1,0,0));
    CloseStyle.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
    const auto Canvas=SNew(SConstraintCanvas);
    ChildSlot[Canvas];
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(7,6,60,60))
        [SNew(SImage).Image_Lambda([this]() -> const FSlateBrush*
        {
            return Portrait.IsValid() && Portrait->GetVisibility()!=ESlateVisibility::Collapsed && Portrait->GetBrush().GetResourceObject()
                ? &Portrait->GetBrush() : &Book;
        })];
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
    auto TurnIn = [this] { return bIsRewardDialog || CurrentQuest.RequestedItems.IsSet(); };
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(325,81,19,334))
        [SNew(SBox).Visibility_Lambda([TurnIn] { return TurnIn() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible; })[ScrollBar]];
    // UIPanelScrollFrameTemplate keeps both arrows visible at zero range and hides its fixed-size thumb.
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const auto Arrow = SNew(SButton).ButtonStyle(&ScrollArrowStyle[Index]).ContentPadding(0)
            .Visibility_Lambda([TurnIn] { return TurnIn() ? EVisibility::Visible : EVisibility::Collapsed; })
            .IsEnabled_Lambda([this,Index]
            {
                if (Index == 0) return ContentArea->GetScrollOffset() > .005f;
                return ContentArea->GetScrollOffsetOfEnd() - ContentArea->GetScrollOffset() > .005f;
            })
            .OnClicked_Lambda([this,Index]
            {
                const float Offset = ContentArea->GetScrollOffset() + (Index == 0 ? -151.f : 151.f);
                ContentArea->SetScrollOffset(FMath::Clamp(Offset,0.f,ContentArea->GetScrollOffsetOfEnd()));
                return FReply::Handled();
            });
        const TWeakPtr<SButton> Weak = Arrow;
        Arrow->SetContent(SNew(SImage).Image(&ScrollArrowHighlight[Index]).Visibility_Lambda([Weak]
        {
            const auto Button = Weak.Pin();
            return Button && Button->IsEnabled() && Button->IsHovered() ? EVisibility::HitTestInvisible : EVisibility::Hidden;
        }));
        Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(328,Index == 0 ? 81 : 399,18,16))[Arrow];
    }
    auto HasRange = [this,TurnIn] { return TurnIn() && ContentArea->GetScrollOffsetOfEnd() >= 1.f; };
    auto Fraction = [this] { return FMath::Clamp(ContentArea->GetScrollOffset() / FMath::Max(1.f,ContentArea->GetScrollOffsetOfEnd()),0.f,1.f); };
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset_Lambda([Fraction] { return FMargin(328,97 + Fraction() * 278,18,24); })
        [SNew(SImage).Image(&ScrollKnob).Visibility_Lambda([this,HasRange]
            { return CurrentQuest.RequestedItems.IsSet() || HasRange() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })];
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(328,109,18,278))
        [SNew(SSlider).Orientation(Orient_Vertical).IndentHandle(false).IsFocusable(false).RenderOpacity(0)
            .Visibility_Lambda([HasRange] { return HasRange() ? EVisibility::Visible : EVisibility::Hidden; })
            .Value_Lambda([Fraction] { return 1.f - Fraction(); })
            .OnValueChanged_Lambda([this](float Value) { ContentArea->SetScrollOffset((1.f - Value) * ContentArea->GetScrollOffsetOfEnd()); })];
    // QuestFrame.xml positions the footer controls over cut-outs in the panel art.
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset_Lambda([this](){
        if (bIsRewardDialog || CurrentQuest.RequestedItems.IsSet()) return FMargin(22,417,323,23);
        return FMargin(23,CurrentQuest.QuestId ? 418 : 417,322,22);
    })
        [SAssignNew(ButtonArea,SHorizontalBox)];

    SetVisibility(EVisibility::Collapsed);
}

void SWowQuestDialog::SetPresentation(FWowFrameManager& Frames, const FString& Name, uint64 QuestUnitGuid)
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
    Load(ItemNameFrame,TEXT("Interface\\QuestFrame\\UI-QuestItemNameFrame"));
    Load(SelectedItemHighlight,TEXT("Interface\\QuestFrame\\UI-QuestItemHighlight"),false,true);
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const FString Prefix = Index == 0 ? TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-") : TEXT("Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-");
        auto ArrowArt = [&](FSlateBrush& Brush,const TCHAR* Suffix,bool bAdditive = false)
        {
            Load(Brush, *(Prefix + Suffix), false, bAdditive);
            Brush.SetUVRegion(FBox2f(FVector2f(.2f,.25f),FVector2f(.8f,.75f)));
        };
        auto& Style = ScrollArrowStyle[Index];
        ArrowArt(Style.Normal,TEXT("Up"));
        Style.Hovered = Style.Normal;
        ArrowArt(Style.Pressed,TEXT("Down"));
        ArrowArt(Style.Disabled,TEXT("Disabled"));
        ArrowArt(ScrollArrowHighlight[Index],TEXT("Highlight"),true);
        Style.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
    }
    Load(ScrollKnob,TEXT("Interface\\Buttons\\UI-ScrollBar-Knob"));
    ScrollKnob.SetUVRegion(FBox2f(FVector2f(.2f,.125f),FVector2f(.8f,.875f)));
    for (int32 Index = 0; Index < 3; ++Index)
    {
        Load(MoneyIcons[Index],TEXT("Interface\\MoneyFrame\\UI-MoneyIcons"));
        MoneyIcons[Index].SetUVRegion(FBox2f(FVector2f(Index * .25f,0), FVector2f((Index+1) * .25f,1)));
    }
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
        StackNumber.SetFont(Font(TEXT("NumberFontNormal")));
        StackNumber.Font.OutlineSettings.OutlineSize = 1;
        SmallQuestText.SetFont(WithQuestTracking(Font(TEXT("QuestFontNormalSmall"))));
        NpcName->SetFont(CaptionText.Font);
    }
    // QuestFrame_SetPortrait uses "questnpc". The target may change while the
    // conversation stays open, and object/item quests use the book artwork.
    if (QuestUnitGuid && Frames.OnPortrait)
    {
        if (!Portrait.IsValid()) Portrait.Reset(NewObject<UImage>());
        Frames.OnPortrait(TEXT("NativeQuestGiverPortrait"), Portrait.Get(), QuestUnitGuid);
    }
    else if (Portrait.IsValid()) Portrait->SetVisibility(ESlateVisibility::Collapsed);
    NpcName->SetText(FText::FromString(Name));
}

void SWowQuestDialog::PrepareItems(UWowUIManager& UI, UWowConnectionManager* Connection, const FWowQuestDetails& Quest)
{
    ItemUI = &UI;
    ItemConnection = Connection;
    ItemIcons.Reset();
    auto Prepare = [&](const TArray<FWowQuestRewardItem>& Items)
    {
        for (const auto& Item : Items)
        {
            if (Connection) Connection->RequestItemTemplate(Item.ItemId);
            if (ItemIcons.Contains(Item.DisplayId)) continue;
            const auto* Display = FDbcStore::Get().ItemDisplayInfo().GetById(Item.DisplayId);
            if (Display && !Display->Icon1.IsEmpty())
                if (auto* Texture = UI.GetFrameManager()->LoadUITexture(TEXT("Interface\\Icons\\") + Display->Icon1))
                    ItemIcons.Add(Item.DisplayId, FDeferredCleanupSlateBrush::CreateBrush(Texture));
        }
    };
    if (Quest.RequestedItems.IsSet()) Prepare(Quest.RequestedItems->Items);
    Prepare(Quest.RewardItems);
    Prepare(Quest.ChoiceRewards);
}

FString SWowQuestDialog::Label(const TCHAR* Key, const TCHAR* Fallback) const
{
    if (ItemUI.IsValid() && ItemUI->GetEventSystem())
    {
        const FString Localized = ItemUI->GetEventSystem()->ResolveText(Key);
        if (!Localized.IsEmpty() && Localized != Key) return Localized;
    }
    return Fallback;
}

TSharedRef<SWidget> SWowQuestDialog::ItemButton(const FWowQuestRewardItem& Item, EItemGroup Group, int32 Index)
{
    const TCHAR* Prefix = Group == EItemGroup::Required ? TEXT("QuestRequiredItem") :
        Group == EItemGroup::Choice ? TEXT("QuestChoice") : TEXT("QuestFixedItem");
    auto ItemTag = [Prefix,Index](const TCHAR* Part) { return FName(*FString::Printf(TEXT("%s%s%d"),Prefix,Part,Index)); };
    const auto Canvas = SNew(SConstraintCanvas);
    const auto Icon = ItemIcons.FindRef(Item.DisplayId);
    // The stock LargeItemButtonTemplate leaves transparent margins in its 128x64 name plate.
    const auto Tint = TAttribute<FSlateColor>::CreateLambda([this, Id = Item.ItemId, Group]() -> FSlateColor
    {
        if (Group != EItemGroup::Required && ItemUI.IsValid() && ItemUI->GetLuaVM() && ItemUI->GetLuaVM()->IsInitialized())
        {
            auto* Context = WowLuaApi::GetContext(ItemUI->GetLuaVM()->GetState());
            const auto* Data = ItemConnection.IsValid() ? ItemConnection->PacketHandler.ItemTemplates.Find(Id) : nullptr;
            if (Context && Data && !WowLuaApi::CanUseItem(Context,*Data))
                return FLinearColor::FromSRGBColor(FColor(230,0,0));
        }
        return FLinearColor::White;
    });
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(0,0,39,39))
        [SNew(SImage).Tag(ItemTag(TEXT("Icon"))).Image_Lambda([Icon] { return FDeferredCleanupSlateBrush::TrySlateBrush(Icon); }).ColorAndOpacity(Tint)];
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(29,-12.5f,128,64))
        [SNew(SImage).Tag(ItemTag(TEXT("NameFrame"))).Image(&ItemNameFrame).ColorAndOpacity(Tint)];
    const auto Name = WrappedText(FString(), CaptionText, 90.f);
    Name->SetTag(ItemTag(TEXT("Name")));
    Name->SetText(TAttribute<FText>::CreateLambda([this, Id = Item.ItemId]
    {
        const auto* Data = ItemConnection.IsValid() ? ItemConnection->PacketHandler.ItemTemplates.Find(Id) : nullptr;
        return FText::FromString(Data ? Data->Name : Label(TEXT("RETRIEVING_ITEM_INFO"),TEXT("Retrieving item information...")));
    }));
    // LargeItemButtonTemplate gives the name 90x36 units. Wrap at the full
    // width first. A long localized name can scale down uniformly to fit.
    Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(46,1.5f,90,36))
        [SNew(SScaleBox).HAlign(HAlign_Left).VAlign(VAlign_Center)
            .Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[Name]];
    if (Item.Count > 1)
        Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(0,0,38,38))
            [SNew(SBox).HAlign(HAlign_Right).VAlign(VAlign_Bottom)
                [SNew(STextBlock).Tag(ItemTag(TEXT("Count"))).TextStyle(&StackNumber).Text(FText::AsNumber(Item.Count))]];

    if (Group == EItemGroup::Choice)
    {
        Canvas->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(-8,-7,256,64))
            [SNew(SImage).Tag(ItemTag(TEXT("Selected"))).Image(&SelectedItemHighlight).Visibility_Lambda([this, Index]
                { return SelectedRewardChoice == Index ? EVisibility::HitTestInvisible : EVisibility::Hidden; })];
    }
    const auto Button = SNew(SButton).Tag(ItemTag(TEXT(""))).ButtonStyle(&ItemStyle).ContentPadding(FMargin(0))
        .HAlign(HAlign_Fill).VAlign(VAlign_Fill)[Canvas];
    const TWeakPtr<SButton> Weak = Button;
    Button->SetOnHovered(FSimpleDelegate::CreateLambda([this,Weak,Id = Item.ItemId]
    {
        const auto Card = Weak.Pin();
        if (Card && ItemUI.IsValid()) WowQuestItemTooltip::Show(*ItemUI.Get(),Id,Card->GetCachedGeometry());
    }));
    Button->SetOnUnhovered(FSimpleDelegate::CreateLambda([this] { if (ItemUI.IsValid()) WowQuestItemTooltip::Hide(*ItemUI.Get()); }));
    if (Group == EItemGroup::Choice)
        Button->SetOnClicked(FOnClicked::CreateSP(this,&SWowQuestDialog::OnRewardItemClicked,Index));
    return SNew(SBox).WidthOverride(147).HeightOverride(41)[Button];
}

void SWowQuestDialog::AddItemGrid(const TArray<FWowQuestRewardItem>& Items, EItemGroup Group, float Left, float Top)
{
    const bool bRequired = Group == EItemGroup::Required;
    for (int32 Index = 0; Index < Items.Num(); Index += 2)
    {
        const auto Row = SNew(SHorizontalBox);
        Row->AddSlot().AutoWidth()[ItemButton(Items[Index],Group,Index)];
        if (Items.IsValidIndex(Index + 1))
            Row->AddSlot().AutoWidth().Padding(bRequired ? 2.f : 1.f,0,0,0)[ItemButton(Items[Index+1],Group,Index+1)];
        ContentArea->AddSlot().HAlign(HAlign_Left).Padding(Left,Index == 0 ? Top : (bRequired ? 3.f : 2.f),0,0)[Row];
    }
}

TSharedRef<SWidget> SWowQuestDialog::MoneyAmount(uint32 Amount, bool bRequired)
{
    const auto Row = SNew(SHorizontalBox);
    const uint32 Coins[] = {Amount / 10000, Amount / 100 % 100, Amount % 100};
    const int32 IconSize = bRequired ? 13 : 19;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        if (!Coins[Index]) continue;
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
            [SNew(STextBlock).TextStyle(bRequired ? &StackNumber : &RewardNumber).Text(FText::AsNumber(Coins[Index]))
                .ColorAndOpacity_Lambda([this,bRequired,Amount]() -> FSlateColor
                {
                    const auto* Player = ItemConnection.IsValid() ? ItemConnection->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
                    return bRequired && Player && Player->GetCoinage() < Amount ? FLinearColor::Red : FLinearColor::White;
                })];
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
            [SNew(SBox).WidthOverride(IconSize).HeightOverride(IconSize)[SNew(SImage).Image(&MoneyIcons[Index])]];
    }
    return Row;
}

TSharedRef<SWidget> SWowQuestDialog::PanelButton(const FString& Label,FOnClicked Action,float Width,bool bClose,TAttribute<bool> Enabled)
{
    const auto Button=SNew(SButton).Tag(bClose ? FName(TEXT("CloseButton")) : NAME_None).ButtonStyle(bClose?&CloseStyle:&PanelStyle)
        .ContentPadding(FMargin(0)).HAlign(HAlign_Fill).VAlign(VAlign_Fill).OnClicked(Action).IsEnabled(Enabled);
    const TWeakPtr<SButton> Weak=Button;
    const auto Content=SNew(SOverlay);
    Content->AddSlot()
        [SNew(SImage).Image(bClose?&CloseHighlight:&PanelHighlight).ColorAndOpacity(FLinearColor(1,1,1,.55f))
            .Visibility_Lambda([Weak]() {const auto B=Weak.Pin(); return B && B->IsEnabled() && B->IsHovered()?EVisibility::HitTestInvisible:EVisibility::Hidden;})];
    if (!bClose)
        Content->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [SNew(STextBlock).TextStyle(&CaptionText).Text(FText::FromString(Label))
                .TextShapingMethod(ETextShapingMethod::FullShaping)
                .ColorAndOpacity_Lambda([Weak]() -> FSlateColor {
                    const auto Current = Weak.Pin();
                    if (Current && !Current->IsEnabled()) return FLinearColor::FromSRGBColor(FColor(128,128,128));
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
    SelectedRewardChoice = QuestDetails.ChoiceRewards.IsEmpty() ? 0 : INDEX_NONE;

    RefreshContent();
    SetVisibility(EVisibility::Visible);

    UE_LOG(LogWowQuestDialog, Log, TEXT("Showing quest reward: %s"), *QuestDetails.Title);
}

void SWowQuestDialog::CloseDialog()
{
    if (Portrait.IsValid()) Portrait->SetVisibility(ESlateVisibility::Collapsed);
    if (ItemUI.IsValid()) WowQuestItemTooltip::Hide(*ItemUI.Get());
    GreetingBlock.Reset();
    GreetingText.Reset();
    CurrentQuest = FWowQuestDetails();
    SetVisibility(EVisibility::Collapsed);

    UE_LOG(LogWowQuestDialog, Log, TEXT("Closed quest dialog"));
}

void SWowQuestDialog::AcceptCurrentQuest()
{
    if (!bIsRewardDialog && !CurrentQuest.RequestedItems.IsSet() && CurrentQuest.QuestId && GetVisibility()==EVisibility::Visible) OnAcceptClicked();
}

void SWowQuestDialog::CompleteCurrentQuest()
{
    if (bIsRewardDialog && CurrentQuest.QuestId && GetVisibility()==EVisibility::Visible) OnCompleteClicked();
}

void SWowQuestDialog::ShowGossip(const FWowGossipMenu& Menu)
{
    if (ItemUI.IsValid()) WowQuestItemTooltip::Hide(*ItemUI.Get());
    ContentArea->ScrollToStart();
    CurrentQuest=FWowQuestDetails();
    bIsRewardDialog = false;
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
    if (ItemUI.IsValid()) WowQuestItemTooltip::Hide(*ItemUI.Get());
    bUsesGossipFrame = false;
    GreetingBlock.Reset();
    GreetingText.Reset();
    ContentArea->ClearChildren();
    ButtonArea->ClearChildren();

    const bool bProgress = CurrentQuest.RequestedItems.IsSet();
    auto AddParagraph = [&](const FString& Text, const FTextBlockStyle& Style, float Top, float Width = 285.f)
    {
        const auto Paragraph = WrappedText(Text,Style,Width);
        if (bProgress || bIsRewardDialog)
        {
            // Align Slate's ink with the original parchment FontString baseline.
            // Keep the FrameXML line box and the item anchors below it unchanged.
            Paragraph->SetRenderTransform(FSlateRenderTransform(FVector2f(2.f, &Style == &TitleText ? -3.f : -2.f)));
        }
        ContentArea->AddSlot().HAlign(HAlign_Left).Padding(5,Top,0,0)[Paragraph];
    };
    AddParagraph(CurrentQuest.Title, TitleText, 10);
    if (!CurrentQuest.Details.IsEmpty()) AddParagraph(CurrentQuest.Details, BodyText, 5, bProgress ? 275.f : 285.f);

    if (bProgress)
    {
        const auto& Request = CurrentQuest.RequestedItems.GetValue();
        if (!Request.Items.IsEmpty() || Request.Money)
            AddParagraph(Label(TEXT("TURN_IN_ITEMS"),TEXT("Required items:")), TitleText, 10, 295);
        if (Request.Money)
        {
            const auto* Player = ItemConnection.IsValid() ? ItemConnection->PacketHandler.EntityManager.GetLocalPlayer() : nullptr;
            const bool bHaveMoney = Player && Player->GetCoinage() >= Request.Money;
            ContentArea->AddSlot().HAlign(HAlign_Left).Padding(5,10,0,0)
                [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(STextBlock).TextStyle(&SmallQuestText).Text(FText::FromString(Label(TEXT("REQUIRED_MONEY"),TEXT("Required Money:"))))
                            .ColorAndOpacity(bHaveMoney ? FLinearColor::FromSRGBColor(FColor(51,51,51)) : FLinearColor::Black)]
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10,0,0,0)
                        [MoneyAmount(Request.Money,true)]];
        }
        AddItemGrid(Request.Items,EItemGroup::Required,Request.Money ? 5.f : 2.f,Request.Money ? 10.f : 5.f);
    }
    else
    {
        if (!CurrentQuest.Objectives.IsEmpty())
        {
            AddParagraph(Label(TEXT("QUEST_OBJECTIVES"),TEXT("Quest Objectives")),TitleText,10);
            AddParagraph(CurrentQuest.Objectives,BodyText,5);
        }
        const bool bChoices = !CurrentQuest.ChoiceRewards.IsEmpty();
        const bool bReceives = CurrentQuest.RewardMoney || CurrentQuest.RewardXP || !CurrentQuest.RewardItems.IsEmpty();
        if (bChoices || bReceives)
            AddParagraph(Label(TEXT("QUEST_REWARDS"),TEXT("Rewards")),TitleText,bIsRewardDialog ? 10 : 15);
        if (bChoices)
        {
            const FString ChooseText = bIsRewardDialog ? Label(TEXT("REWARD_CHOOSE"),TEXT("Choose your reward:")) :
                Label(TEXT("REWARD_CHOICES"),TEXT("You will be able to choose one of these rewards:"));
            AddParagraph(ChooseText,BodyText,5);
            AddItemGrid(CurrentQuest.ChoiceRewards,bIsRewardDialog ? EItemGroup::Choice : EItemGroup::Fixed,2,5);
        }
        if (bReceives)
        {
            const FString ReceiveText = bChoices ? Label(TEXT("REWARD_ITEMS"),TEXT("You will also receive:")) :
                Label(TEXT("REWARD_ITEMS_ONLY"),TEXT("You will receive:"));
            // QuestInfo.lua anchors the receive line to the last left-hand card, or directly to the header.
            const float Left = bChoices ? 5.f : 8.f;
            // These two FontStrings are vertically centered beside a MoneyFrame
            // or a number. Align their ink without moving the coin and XP widgets.
            auto RewardLabel = [&](const FString& Text, float Width)
            {
                const auto Block = WrappedText(Text, BodyText, Width);
                if (bIsRewardDialog) Block->SetRenderTransform(FSlateRenderTransform(FVector2f(2.f,-3.f)));
                return Block;
            };
            const auto Receive = SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[RewardLabel(ReceiveText,250)];
            if (CurrentQuest.RewardMoney)
                Receive->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(15,0,0,0)[MoneyAmount(CurrentQuest.RewardMoney,false)];
            ContentArea->AddSlot().HAlign(HAlign_Left).Padding(Left,5,0,0)[Receive];
            if (CurrentQuest.RewardXP)
            {
                ContentArea->AddSlot().HAlign(HAlign_Left).Padding(Left,5,0,0)
                    [SNew(SBox).WidthOverride(280).HeightOverride(20)
                        [SNew(SHorizontalBox)
                            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                [RewardLabel(Label(TEXT("EXPERIENCE_COLON"),TEXT("Experience:")),180)]
                            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(15,0,0,0)
                                [SNew(STextBlock).Tag(TEXT("QuestRewardXP")).TextStyle(&RewardNumber)
                                    .Text(FText::AsNumber(CurrentQuest.RewardXP))]]];
            }
            AddItemGrid(CurrentQuest.RewardItems,EItemGroup::Fixed,Left-3,5);
        }
        ContentArea->AddSlot().Padding(0,10,0,0)[SNew(SSpacer).Size(FVector2D(5,5))];
    }

    if (bIsRewardDialog || bProgress)
    {
        const auto Action = bProgress ? FOnClicked::CreateSP(this,&SWowQuestDialog::OnContinueClicked) :
            FOnClicked::CreateSP(this,&SWowQuestDialog::OnCompleteClicked);
        const FString Text = bProgress ? Label(TEXT("CONTINUE"),TEXT("Continue")) : Label(TEXT("COMPLETE_QUEST"),TEXT("Complete Quest"));
        ButtonArea->AddSlot().AutoWidth().VAlign(VAlign_Bottom)
            [PanelButton(Text,Action,120,false,!bProgress || CurrentQuest.RequestedItems->bCanComplete)];
        ButtonArea->AddSlot().FillWidth(1)[SNew(SSpacer)];
        ButtonArea->AddSlot().AutoWidth().VAlign(VAlign_Top)
            [PanelButton(Label(TEXT("CANCEL"),TEXT("Cancel")),FOnClicked::CreateSP(this,&SWowQuestDialog::OnDeclineClicked))];
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

FReply SWowQuestDialog::OnContinueClicked()
{
    if (CurrentQuest.RequestedItems.IsSet() && CurrentQuest.RequestedItems->bCanComplete)
        OnQuestContinue.ExecuteIfBound(CurrentQuest.QuestGiverGuid, CurrentQuest.QuestId);
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
    if (!CurrentQuest.ChoiceRewards.IsEmpty() && !CurrentQuest.ChoiceRewards.IsValidIndex(SelectedRewardChoice))
    {
        if (ItemUI.IsValid() && ItemUI->GetEventSystem())
            ItemUI->GetEventSystem()->FireEvent(TEXT("UI_ERROR_MESSAGE"),{Label(TEXT("ERR_QUEST_MUST_CHOOSE"),TEXT("You must choose a reward."))});
        return FReply::Handled();
    }
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
    return FReply::Handled();
}
