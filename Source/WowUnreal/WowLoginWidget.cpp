#include "WowLoginWidget.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "MediaTexture.h"
#include "WowLoginScene.h"
#include "WowAudioManager.h"
#include "WowCharacterCreationArt.h"
#include "WowBackdropWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"

SWowLoginWidget::SWowLoginWidget()=default;
SWowLoginWidget::~SWowLoginWidget()=default;
namespace
{
class SLoginViewport : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SLoginViewport){} SLATE_ARGUMENT(AWowLoginScene*,Scene) SLATE_END_ARGS()
    void Construct(const FArguments& A){Scene=A._Scene;Brush.SetResourceObject(A._Scene->GetDisplayMaterial());Brush.DrawAs=ESlateBrushDrawType::Image;ChildSlot[SNew(SImage).Image(&Brush)];}
    virtual void Tick(const FGeometry& G,double T,float DT) override{SCompoundWidget::Tick(G,T,DT);if(Scene.IsValid() && G.GetLocalSize().Y>0) Scene->SetViewAspect(G.GetLocalSize().X/G.GetLocalSize().Y);}
private:TWeakObjectPtr<AWowLoginScene> Scene;FSlateBrush Brush;
};
}
void SWowLoginWidget::SetScene(AWowLoginScene* InScene,FMpqManager* Mpq,AWowAudioManager* InAudio)
{
    Scene=InScene;Audio=InAudio;
    if(InScene){Art=MakeUnique<FWowCharacterCreationArt>(Mpq);BuildStockLayout();}
    else
    {
        const FString Server=ServerBox?ServerBox->GetText().ToString():FString(),User=UsernameBox?UsernameBox->GetText().ToString():FString(),Pass=PasswordBox?PasswordBox->GetText().ToString():FString();
        Construct(FArguments());SetCredentials(Server,User,Pass);Art.Reset();
    }
}
void SWowLoginWidget::BuildStockLayout()
{
    const FString Server=ServerBox?ServerBox->GetText().ToString():TEXT("127.0.0.1:3724"),User=UsernameBox?UsernameBox->GetText().ToString():FString(),Pass=PasswordBox?PasswordBox->GetText().ToString():FString();
    Backdrops.Reset();StockControls.Reset();const FLinearColor Gold(1,.82f,0);auto Canvas=SNew(SConstraintCanvas);
    auto Put=[&](TSharedRef<SWidget> W,float X,float Y,float Width,float Height,float AX=0,float AY=0){Canvas->AddSlot().Anchors(FAnchors(AX,AY)).Alignment(FVector2D::ZeroVector).Offset(FMargin(X,Y,Width,Height))[W];};
    auto Font=[this](int32 Size){auto F=Art->Font(Size);F.OutlineSettings=FFontOutlineSettings(1);return F;};
    auto Label=[&](const FString& Text,int32 Size=16){return SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(Size)).ColorAndOpacity(Gold).Justification(ETextJustify::Center);};
    if(!BlueButton) BlueButton=MakeShared<FButtonStyle>();const FString Base=TEXT("Interface\\Glues\\Common\\Glue-Panel-Button");const FVector2f Max(.578125f,.75f);
    BlueButton->SetNormal(*Art->Brush(Base+TEXT("-Up-Blue"),FVector2f(0),Max));BlueButton->SetHovered(BlueButton->Normal);
    BlueButton->SetPressed(*Art->Brush(Base+TEXT("-Down-Blue"),FVector2f(0),Max));BlueButton->SetDisabled(*Art->Brush(Base+TEXT("-Disabled"),FVector2f(0),Max));
    BlueButton->SetNormalPadding(FMargin(0));BlueButton->SetPressedPadding(FMargin(1,1,0,0));
    auto Click=[&](FName Id,const FString& Text,FOnClicked Action,int32 Size=16)
    {auto B=SNew(SButton).ButtonStyle(BlueButton.Get()).ContentPadding(FMargin(0,0,0,6)).HAlign(HAlign_Center).VAlign(VAlign_Center).OnClicked(Action)[Label(Text,Size)];StockControls.Add(Id,B);return B;};
    auto Backdrop=[&](FWowBackdrop D)
    {auto* B=NewObject<UWowBackdropWidget>();Backdrops.Add(TStrongObjectPtr<UWowBackdropWidget>(B));B->Configure(D,Cast<UTexture2D>(Art->Brush(D.BgFile)->GetResourceObject()),Cast<UTexture2D>(Art->Brush(D.EdgeFile)->GetResourceObject()),1);return B->TakeWidget();};
    Canvas->AddSlot().Anchors(FAnchors(0,0,1,1)).Offset(FMargin(0))[SNew(SLoginViewport).Scene(Scene.Get())];
    Put(SNew(SImage).Image(Art->Brush(TEXT("Interface\\Glues\\Common\\Glues-WoW-WotLKLogo"))),3,7,256,128);
    Put(SNew(SImage).Image(Art->Brush(TEXT("Interface\\Glues\\Mainmenu\\Glues-BlizzardLogo"))),-50,-108,100,100,.5f,1);
    Put(Label(Art->String(TEXT("BLIZZ_DISCLAIMER")),12),-360,-24,720,18,.5f,1);
    Put(Label(TEXT("Version 3.3.5 (12340)"),12),0,-24,230,18,0,1);
    StockEditStyle=FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");FSlateBrush Empty;Empty.DrawAs=ESlateBrushDrawType::NoDrawType;
    StockEditStyle.SetBackgroundImageNormal(Empty).SetBackgroundImageHovered(Empty).SetBackgroundImageFocused(Empty).SetBackgroundImageReadOnly(Empty).SetPadding(FMargin(12,0,5,5));
    auto Field=[&](TSharedPtr<SEditableTextBox>& Box,const FString& Text,bool Password)
    {
        FWowBackdrop D;D.BgFile=TEXT("Interface\\Tooltips\\UI-Tooltip-Background");D.EdgeFile=TEXT("Interface\\Glues\\Common\\Glue-Tooltip-Border");D.EdgeSize=16;D.TileSize=16;D.Tile=true;
        D.InsetLeft=10;D.InsetRight=5;D.InsetTop=4;D.InsetBottom=9;D.Color=FLinearColor(.09f,.09f,.09f,1);D.BorderColor=FLinearColor(.8f,.8f,.8f,1);
        return SNew(SOverlay)+SOverlay::Slot()[Backdrop(D)]+SOverlay::Slot()[SAssignNew(Box,SEditableTextBox).Style(&StockEditStyle).Text(FText::FromString(Text)).Font(Font(16)).IsPassword(Password)
            .OnTextCommitted_Lambda([this](const FText&,ETextCommit::Type Type){if(Type==ETextCommit::OnEnter) OnLoginClicked();})];
    };
    Put(Label(Art->String(TEXT("ACCOUNT_NAME"))),-300,-416,600,34,.5f,1);Put(Field(UsernameBox,User,false),-100,-382,200,37,.5f,1);
    Put(Label(Art->String(TEXT("PASSWORD"))),-300,-346,600,34,.5f,1);Put(Field(PasswordBox,Pass,true),-100,-312,200,37,.5f,1);
    Put(Click(TEXT("Login"),Art->String(TEXT("LOGIN")),FOnClicked::CreateSP(this,&SWowLoginWidget::OnLoginClicked)), -110,-215,220,45,.5f,1);
    Put(SAssignNew(StatusLabel,STextBlock).Font(Font(12)).ColorAndOpacity(FLinearColor(1,.25f,.15f)).Justification(ETextJustify::Center).AutoWrapText(true),-300,-159,600,42,.5f,1);
    Put(Click(TEXT("Options"),Art->String(TEXT("OPTIONS")),FOnClicked::CreateLambda([this]{bOptions=!bOptions;BuildStockLayout();return FReply::Handled();}),12),-155,-219,150,38,1,1);
    Put(Click(TEXT("Quit"),Art->String(TEXT("QUIT")),FOnClicked::CreateLambda([]{FPlatformMisc::RequestExit(false);return FReply::Handled();}),12),-155,-67,150,38,1,1);
    // Connection/expansion controls remain available inside Options.
    // Keep the options controls between the centered login button and the
    // right-side buttons even at the original 1024x768 viewport.
    constexpr float PanelLeft=-385,PanelWidth=210;
    auto ServerField=Field(ServerBox,Server,false);ServerField->SetVisibility(bOptions?EVisibility::Visible:EVisibility::Collapsed);Put(ServerField,PanelLeft,-294,PanelWidth,37,1,1);
    if(bOptions)
    {
        Put(Label(TEXT("Login server"),12),PanelLeft,-320,PanelWidth,24,1,1);
        const TCHAR* Titles[]={TEXT("Classic"),TEXT("The Burning Crusade"),TEXT("Wrath of the Lich King")};
        for(uint8 I=0;I<3;++I) Put(Click(FName(*FString::Printf(TEXT("Expansion_%u"),I)),Titles[I],FOnClicked::CreateLambda([this,I]{SetExpansion(EWowExpansion(I));return FReply::Handled();}),12),-330+I*220,140,220,38,.5f,0);
        if(!GlueCheck) GlueCheck=MakeShared<FCheckBoxStyle>();
        const auto* Up=Art->Brush(TEXT("Interface\\Buttons\\UI-CheckBox-Up"));const auto* Checked=Art->Brush(TEXT("Interface\\Buttons\\UI-CheckBox-Check"));
        GlueCheck->SetUncheckedImage(*Up).SetUncheckedHoveredImage(*Up).SetUncheckedPressedImage(*Art->Brush(TEXT("Interface\\Buttons\\UI-CheckBox-Down")));
        GlueCheck->SetCheckedImage(*Checked).SetCheckedHoveredImage(*Checked).SetCheckedPressedImage(*Checked);
        auto Check=[&](const TCHAR* Name,const FString& Text){return SNew(SCheckBox).Style(GlueCheck.Get()).IsChecked_Lambda([this,Name]{FString V;return Audio.IsValid() && Audio->GetUiOption(Name,V) && V==TEXT("1")?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this,Name](ECheckBoxState S){if(Audio.IsValid()) Audio->SetUiOption(Name,S==ECheckBoxState::Checked?TEXT("1"):TEXT("0"));})[Label(Text,12)];};
        Put(Check(TEXT("sound_enablemusic"),Art->String(TEXT("ENABLE_MUSIC"))),PanelLeft,-252,PanelWidth,28,1,1);
        if(!GlueSlider) GlueSlider=MakeShared<FSliderStyle>();FSlateBrush NoBar;NoBar.DrawAs=ESlateBrushDrawType::NoDrawType;
        GlueSlider->SetNormalBarImage(NoBar).SetHoveredBarImage(NoBar).SetDisabledBarImage(NoBar);
        FSlateBrush Thumb=*Art->Brush(TEXT("Interface\\Buttons\\UI-SliderBar-Button-Horizontal"));Thumb.ImageSize=FVector2D(32,32);
        GlueSlider->SetNormalThumbImage(Thumb).SetHoveredThumbImage(Thumb).SetDisabledThumbImage(Thumb);
        FWowBackdrop SliderBack;SliderBack.BgFile=TEXT("Interface\\Buttons\\UI-SliderBar-Background");SliderBack.EdgeFile=TEXT("Interface\\Buttons\\UI-SliderBar-Border");SliderBack.Tile=true;SliderBack.TileSize=8;SliderBack.EdgeSize=8;SliderBack.InsetLeft=3;SliderBack.InsetRight=3;SliderBack.InsetTop=6;SliderBack.InsetBottom=6;
        Put(Backdrop(SliderBack),PanelLeft,-215,PanelWidth,17,1,1);
        Put(SNew(SSlider).Style(GlueSlider.Get()).Value_Lambda([this]{return Audio.IsValid()?Audio->MusicVolume:.5f;}).OnValueChanged_Lambda([this](float V){if(Audio.IsValid()) Audio->SetUiOption(TEXT("sound_musicvolume"),FString::SanitizeFloat(V));}),PanelLeft,-222,PanelWidth,32,1,1);
    }
    else ServerBox->SetVisibility(EVisibility::Collapsed);
    ChildSlot[SNew(SDPIScaler).DPIScale_Lambda([this]{return GetCachedGeometry().GetLocalSize().Y>0?float(GetCachedGeometry().GetLocalSize().Y/768):1.f;})[Canvas]];
}
bool SWowLoginWidget::PressStockControl(FName Name)
{
    const auto Keep=AsShared();const auto* Weak=StockControls.Find(Name);const auto B=Weak?Weak->Pin():nullptr;if(!B || !B->IsEnabled()) return false;
    const FKeyEvent E(EKeys::SpaceBar,FModifierKeysState(),0,false,0,0);B->OnKeyDown(B->GetCachedGeometry(),E);B->OnKeyUp(B->GetCachedGeometry(),E);return true;
}
FReply SWowLoginWidget::OnPreviewKeyDown(const FGeometry&,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Escape && bOptions){bOptions=false;BuildStockLayout();return FReply::Handled();}
    if(E.GetKey()==EKeys::Enter) return OnLoginClicked();return FReply::Unhandled();
}

// ─── Expansion-specific theming ─────────────────────────────────────────────

FLinearColor SWowLoginWidget::GetBackgroundColor() const
{
    switch (CurrentExpansion)
    {
    case EWowExpansion::Classic:
        return FLinearColor(0.02f, 0.02f, 0.08f, 0.95f); // Deep dark blue
    case EWowExpansion::BurningCrusade:
        return FLinearColor(0.06f, 0.02f, 0.02f, 0.95f); // Dark red/fel green tint
    case EWowExpansion::WrathOfTheLichKing:
    default:
        return FLinearColor(0.02f, 0.04f, 0.08f, 0.95f); // Icy dark blue
    }
}

FLinearColor SWowLoginWidget::GetAccentColor() const
{
    switch (CurrentExpansion)
    {
    case EWowExpansion::Classic:
        return FLinearColor(1.0f, 0.84f, 0.0f); // Classic gold
    case EWowExpansion::BurningCrusade:
        return FLinearColor(0.4f, 1.0f, 0.2f); // Fel green
    case EWowExpansion::WrathOfTheLichKing:
    default:
        return FLinearColor(0.6f, 0.85f, 1.0f); // Icy blue
    }
}

FString SWowLoginWidget::GetExpansionTitle() const
{
    switch (CurrentExpansion)
    {
    case EWowExpansion::Classic:
        return TEXT("World of Warcraft");
    case EWowExpansion::BurningCrusade:
        return TEXT("The Burning Crusade");
    case EWowExpansion::WrathOfTheLichKing:
    default:
        return TEXT("Wrath of the Lich King");
    }
}

FString SWowLoginWidget::GetExpansionSubtitle() const
{
    switch (CurrentExpansion)
    {
    case EWowExpansion::Classic:
        return TEXT("");
    case EWowExpansion::BurningCrusade:
        return TEXT("World of Warcraft");
    case EWowExpansion::WrathOfTheLichKing:
    default:
        return TEXT("World of Warcraft");
    }
}

// ─── UI Construction ────────────────────────────────────────────────────────

void SWowLoginWidget::Construct(const FArguments& InArgs)
{
    // WoW gold color palette
    const FLinearColor WowGold(1.0f, 0.84f, 0.0f);
    const FLinearColor WowGoldDim(0.7f, 0.6f, 0.1f);
    const FLinearColor FieldBg(0.0f, 0.0f, 0.0f, 0.6f);
    const FLinearColor FieldBorder(0.35f, 0.3f, 0.1f, 0.8f);
    const FLinearColor PanelBorder(0.4f, 0.35f, 0.1f, 0.6f);

    // Styled text field builder
    auto MakeField = [&](TSharedPtr<SEditableTextBox>& OutBox, const FString& HintText, bool bPassword = false) -> TSharedRef<SWidget>
    {
        return SNew(SBorder)
            .BorderBackgroundColor(FieldBg)
            .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
            .Padding(FMargin(2))
            [
                SNew(SBorder)
                .BorderBackgroundColor(FieldBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
                .Padding(FMargin(1))
                [
                    SNew(SBox).MinDesiredWidth(280).MaxDesiredHeight(30)
                    [
                        SAssignNew(OutBox, SEditableTextBox)
                        .IsPassword(bPassword)
                        .HintText(FText::FromString(HintText))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                        .BackgroundColor(FLinearColor(0.05f, 0.05f, 0.1f, 1.0f))
                        .ForegroundColor(FSlateColor(FLinearColor::White))
                    ]
                ]
            ];
    };

    ChildSlot
    [
        SNew(SOverlay)

        // Layer 0: Cinematic video background (hidden until texture is set)
        + SOverlay::Slot()
        [
            SAssignNew(CinematicImage, SImage)
            .Visibility(EVisibility::Collapsed)
        ]

        // Layer 1: Color overlay + UI
        + SOverlay::Slot()
        [
        SAssignNew(BackgroundBorder, SBorder)
        .BorderBackgroundColor(GetBackgroundColor())
        .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
        [
            SNew(SVerticalBox)

            // ═══ Top spacer ═══
            + SVerticalBox::Slot().FillHeight(1.5f)

            // ═══ Title area ═══
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 5)
            [
                SAssignNew(SubtitleText, STextBlock)
                .Text(FText::FromString(GetExpansionSubtitle()))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                .ColorAndOpacity(FLinearColor(0.8f, 0.75f, 0.5f))
                .Justification(ETextJustify::Center)
            ]

            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 30)
            [
                SAssignNew(TitleText, STextBlock)
                .Text(FText::FromString(GetExpansionTitle()))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 36))
                .ColorAndOpacity(GetAccentColor())
                .Justification(ETextJustify::Center)
            ]

            // ═══ Login panel (centered, bordered) ═══
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.4f))
                .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
                .Padding(FMargin(30, 25))
                [
                    SNew(SBorder)
                    .BorderBackgroundColor(PanelBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
                    .Padding(FMargin(1))
                    [
                        SNew(SBorder)
                        .BorderBackgroundColor(FLinearColor(0.03f, 0.03f, 0.06f, 0.95f))
                        .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
                        .Padding(FMargin(25, 20))
                        [
                            SNew(SVerticalBox)

                            // Server field
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
                                [
                                    SNew(STextBlock)
                                    .Text(FText::FromString(TEXT("Login server")))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                                    .ColorAndOpacity(WowGoldDim)
                                ]
                                + SVerticalBox::Slot().AutoHeight()
                                [
                                    MakeField(ServerBox, TEXT("127.0.0.1:3724"))
                                ]
                            ]

                            // Username field
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
                                [
                                    SNew(STextBlock)
                                    .Text(FText::FromString(TEXT("Account Name")))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                                    .ColorAndOpacity(WowGoldDim)
                                ]
                                + SVerticalBox::Slot().AutoHeight()
                                [
                                    MakeField(UsernameBox, TEXT(""))
                                ]
                            ]

                            // Password field
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
                                [
                                    SNew(STextBlock)
                                    .Text(FText::FromString(TEXT("Password")))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                                    .ColorAndOpacity(WowGoldDim)
                                ]
                                + SVerticalBox::Slot().AutoHeight()
                                [
                                    MakeField(PasswordBox, TEXT(""), true)
                                ]
                            ]

                            // Login button
                            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 18, 0, 8)
                            [
                                SNew(SBorder)
                                .BorderBackgroundColor(FLinearColor(0.35f, 0.28f, 0.05f, 1.0f))
                                .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
                                .Padding(FMargin(1))
                                [
                                    SNew(SButton)
                                    .ButtonColorAndOpacity(FLinearColor(0.15f, 0.12f, 0.02f, 1.0f))
                                    .OnClicked_Raw(this, &SWowLoginWidget::OnLoginClicked)
                                    [
                                        SNew(SBox).Padding(FMargin(40, 6)).HAlign(HAlign_Center)
                                        [
                                            SNew(STextBlock)
                                            .Text(FText::FromString(TEXT("Login")))
                                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                                            .ColorAndOpacity(WowGold)
                                            .Justification(ETextJustify::Center)
                                        ]
                                    ]
                                ]
                            ]

                            // Status label
                            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4)
                            [
                                SAssignNew(StatusLabel, STextBlock)
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                                .ColorAndOpacity(FLinearColor(0.9f, 0.7f, 0.3f))
                            ]
                        ]
                    ]
                ]
            ]

            // ═══ Bottom spacer ═══
            + SVerticalBox::Slot().FillHeight(2.0f)

            // ═══ Expansion tabs ═══
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 0)
            [
                BuildExpansionTabs()
            ]
        ]
        ] // SOverlay Layer 1
    ];
    ServerBox->SetText(FText::FromString(TEXT("127.0.0.1:3724")));
}

TSharedRef<SWidget> SWowLoginWidget::BuildExpansionTabs()
{
    const FLinearColor TabBarBg(0.0f, 0.0f, 0.0f, 0.7f);
    const FLinearColor TabBorderColor(0.3f, 0.25f, 0.08f, 0.8f);
    const FLinearColor InactiveTab(0.06f, 0.05f, 0.02f, 0.9f);
    const FLinearColor ActiveClassic(0.12f, 0.1f, 0.03f, 0.95f);
    const FLinearColor ActiveBC(0.05f, 0.12f, 0.03f, 0.95f);
    const FLinearColor ActiveWotLK(0.03f, 0.06f, 0.12f, 0.95f);

    auto MakeTab = [&](const FString& Label, EWowExpansion Exp, TSharedPtr<SBorder>& OutBorder) -> TSharedRef<SWidget>
    {
        bool bActive = (CurrentExpansion == Exp);
        FLinearColor ActiveColor;
        switch (Exp)
        {
        case EWowExpansion::Classic: ActiveColor = ActiveClassic; break;
        case EWowExpansion::BurningCrusade: ActiveColor = ActiveBC; break;
        default: ActiveColor = ActiveWotLK; break;
        }

        return SNew(SBox).Padding(FMargin(2, 0))
        [
            SAssignNew(OutBorder, SBorder)
            .BorderBackgroundColor(TabBorderColor)
            .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
            .Padding(FMargin(1))
            [
                SNew(SButton)
                .ButtonColorAndOpacity(bActive ? ActiveColor : InactiveTab)
                .OnClicked_Lambda([this, Exp]() -> FReply
                {
                    SetExpansion(Exp);
                    return FReply::Handled();
                })
                [
                    SNew(SBox).Padding(FMargin(20, 8)).HAlign(HAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(Label))
                        .Font(FCoreStyle::GetDefaultFontStyle(bActive ? "Bold" : "Regular", 13))
                        .ColorAndOpacity(bActive ? GetAccentColor() : FLinearColor(0.5f, 0.45f, 0.3f))
                    ]
                ]
            ]
        ];
    };

    return SNew(SBorder)
        .BorderBackgroundColor(TabBarBg)
        .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
        .Padding(FMargin(0, 4))
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f)
            + SHorizontalBox::Slot().AutoWidth()
            [
                MakeTab(TEXT("Classic"), EWowExpansion::Classic, ClassicTab)
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                MakeTab(TEXT("The Burning Crusade"), EWowExpansion::BurningCrusade, BCTab)
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                MakeTab(TEXT("Wrath of the Lich King"), EWowExpansion::WrathOfTheLichKing, WotLKTab)
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
        ];
}

void SWowLoginWidget::SetCinematicTexture(UMediaTexture* InTexture)
{
    if (!CinematicImage.IsValid()) return;

    if (InTexture)
    {
        // Create a brush from the media texture
        CinematicBrush = MakeShared<FSlateBrush>();
        CinematicBrush->SetResourceObject(InTexture);
        CinematicBrush->ImageSize = FVector2D(1920, 1080);
        CinematicBrush->DrawAs = ESlateBrushDrawType::Image;
        CinematicBrush->Tiling = ESlateBrushTileType::NoTile;

        CinematicImage->SetImage(CinematicBrush.Get());
        CinematicImage->SetVisibility(EVisibility::SelfHitTestInvisible);

        // Make the color overlay semi-transparent so cinematic shows through
        if (BackgroundBorder.IsValid())
        {
            FLinearColor BgColor = GetBackgroundColor();
            BgColor.A = 0.3f; // Let video show through
            BackgroundBorder->SetBorderBackgroundColor(BgColor);
        }
    }
    else
    {
        CinematicImage->SetVisibility(EVisibility::Collapsed);
        CinematicBrush.Reset();

        // Restore opaque background
        if (BackgroundBorder.IsValid())
        {
            BackgroundBorder->SetBorderBackgroundColor(GetBackgroundColor());
        }
    }
}

void SWowLoginWidget::SetExpansion(EWowExpansion Expansion)
{
    CurrentExpansion = Expansion;

    // Update visuals
    if (BackgroundBorder.IsValid())
    {
        // If cinematic is playing, keep semi-transparent; otherwise opaque
        FLinearColor BgColor = GetBackgroundColor();
        if (CinematicImage.IsValid() && CinematicImage->GetVisibility() != EVisibility::Collapsed)
        {
            BgColor.A = 0.3f;
        }
        BackgroundBorder->SetBorderBackgroundColor(BgColor);
    }
    if (TitleText.IsValid())
    {
        TitleText->SetText(FText::FromString(GetExpansionTitle()));
        TitleText->SetColorAndOpacity(GetAccentColor());
    }
    if (SubtitleText.IsValid())
    {
        SubtitleText->SetText(FText::FromString(GetExpansionSubtitle()));
    }

    // Notify controller to switch cinematic
    OnExpansionChanged.ExecuteIfBound(static_cast<uint8>(Expansion));
}

// ─── Login logic ────────────────────────────────────────────────────────────

FReply SWowLoginWidget::OnLoginClicked()
{
    FString ServerStr = ServerBox.IsValid() ? ServerBox->GetText().ToString().TrimStartAndEnd() : TEXT("127.0.0.1:3724");
    if (ServerStr.IsEmpty()) ServerStr = TEXT("127.0.0.1:3724");
    FString User = UsernameBox.IsValid() ? UsernameBox->GetText().ToString().TrimStartAndEnd() : TEXT("");
    FString Pass = PasswordBox.IsValid() ? PasswordBox->GetText().ToString().TrimStartAndEnd() : TEXT("");

    FString Host = ServerStr;
    int32 Port = 3724;
    int32 ColonIdx;
    if (ServerStr.FindChar(TEXT(':'), ColonIdx))
    {
        Host = ServerStr.Left(ColonIdx);
        Port = FCString::Atoi(*ServerStr.Mid(ColonIdx + 1));
    }

    if (Host.IsEmpty() || Port < 1 || Port > 65535)
    {
        SetStatusText(TEXT("Please enter a valid login server address and port"));
        return FReply::Handled();
    }
    if ((Host == TEXT("127.0.0.1") || Host.Equals(TEXT("localhost"), ESearchCase::IgnoreCase)) && Port == 8085)
    {
        SetStatusText(TEXT("Use 127.0.0.1:3724 to log in. Port 8085 is the world server."));
        return FReply::Handled();
    }
    if (User.IsEmpty())
    {
        SetStatusText(TEXT("Please enter your account name"));
        return FReply::Handled();
    }

    SetStatusText(TEXT("Connecting..."));
    OnLoginSubmit.ExecuteIfBound(Host, Port, User, Pass);
    return FReply::Handled();
}

void SWowLoginWidget::SetCredentials(const FString& Server, const FString& Username, const FString& Password)
{
    if (ServerBox.IsValid()) ServerBox->SetText(FText::FromString(Server));
    if (UsernameBox.IsValid()) UsernameBox->SetText(FText::FromString(Username));
    if (PasswordBox.IsValid()) PasswordBox->SetText(FText::FromString(Password));
}

void SWowLoginWidget::AutoSubmit()
{
    OnLoginClicked();
}

void SWowLoginWidget::SetStatusText(const FString& Text)
{
    if (StatusLabel.IsValid())
    {
        StatusLabel->SetText(FText::FromString(Text));
    }
}
