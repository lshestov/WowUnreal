#include "WowLoginWidget.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"

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
    if (InScene)
    {
        if (!Art) Art = MakeUnique<FWowCharacterCreationArt>(Mpq);
        BuildStockLayout();
        return;
    }
    bOptions = false;
    ScreenBody->SetContent(SNullWidget::NullWidget);
    Backdrops.Reset();
    StatusLabel.Reset();
    StockControls.Remove(TEXT("Login"));
    StockControls.Remove(TEXT("Options"));
    StockControls.Remove(TEXT("Quit"));
    const FString Notice = CurrentExpansion == EWowExpansion::WrathOfTheLichKing
        ? TEXT("Unable to load the Wrath login scene.") : TEXT("Login is not available for this expansion yet.");
    ScreenBody->SetContent(SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(GetBackgroundColor())
        .HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,8)
            [SNew(STextBlock).Text(FText::FromString(GetExpansionTitle())).Font(FCoreStyle::GetDefaultFontStyle("Bold",26)).ColorAndOpacity(GetAccentColor())]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [SNew(STextBlock).Text(FText::FromString(Notice))]]);
}
void SWowLoginWidget::BuildStockLayout()
{
    ScreenBody->SetContent(SNullWidget::NullWidget);
    Backdrops.Reset();
    const FLinearColor Gold(1,.82f,0);auto Canvas=SNew(SConstraintCanvas);
    auto Put=[&](TSharedRef<SWidget> W,float X,float Y,float Width,float Height,float AX=0,float AY=0){Canvas->AddSlot().Anchors(FAnchors(AX,AY)).Alignment(FVector2D::ZeroVector).Offset(FMargin(X,Y,Width,Height))[W];};
    auto Font=[this](int32 Size){auto F=Art->Font(Size);F.OutlineSettings=FFontOutlineSettings(1);return F;};
    auto Label=[&](const FString& Text,int32 Size=16){return SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(Size)).ColorAndOpacity(Gold).Justification(ETextJustify::Center);};
    if(!BlueButton) BlueButton=MakeShared<FButtonStyle>();const FString Base=TEXT("Interface\\Glues\\Common\\Glue-Panel-Button");const FVector2f Max(.578125f,.75f);
    BlueButton->SetNormal(*Art->Brush(Base+TEXT("-Up-Blue"),FVector2f(0),Max));BlueButton->SetHovered(BlueButton->Normal);
    BlueButton->SetPressed(*Art->Brush(Base+TEXT("-Down-Blue"),FVector2f(0),Max));BlueButton->SetDisabled(*Art->Brush(Base+TEXT("-Disabled"),FVector2f(0),Max));
    BlueButton->SetNormalPadding(FMargin(0));BlueButton->SetPressedPadding(FMargin(1,1,0,0));
    auto Click=[&](FName Id,const FString& Text,FOnClicked Action,int32 Size=16)
    {
        // Align the visible glyphs to the blue face inside the textured frame.
        auto B=SNew(SButton).ButtonStyle(BlueButton.Get()).ContentPadding(FMargin(0,0,0,1))
            .HAlign(HAlign_Center).VAlign(VAlign_Center).OnClicked(Action)[Label(Text,Size)];
        StockControls.Add(Id,B);
        return B;
    };
    auto Backdrop=[&](FWowBackdrop D)
    {auto* B=NewObject<UWowBackdropWidget>();Backdrops.Add(TStrongObjectPtr<UWowBackdropWidget>(B));B->Configure(D,Cast<UTexture2D>(Art->Brush(D.BgFile)->GetResourceObject()),Cast<UTexture2D>(Art->Brush(D.EdgeFile)->GetResourceObject()),1);return B->TakeWidget();};
    Canvas->AddSlot().Anchors(FAnchors(0,0,1,1)).Offset(FMargin(0))[SNew(SLoginViewport).Scene(Scene.Get())];
    Put(SNew(SImage).Image(Art->Brush(TEXT("Interface\\Glues\\Common\\Glues-WoW-WotLKLogo"))),3,7,256,128);
    Put(SNew(SImage).Image(Art->Brush(TEXT("Interface\\Glues\\Mainmenu\\Glues-BlizzardLogo"))),-50,-108,100,100,.5f,1);
    Put(Label(Art->String(TEXT("BLIZZ_DISCLAIMER")),12),-360,-24,720,18,.5f,1);
    Put(Label(TEXT("Version 3.3.5 (12340)"),12),0,-24,230,18,0,1);
    StockEditStyle=FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");FSlateBrush Empty;Empty.DrawAs=ESlateBrushDrawType::NoDrawType;
    StockEditStyle.SetBackgroundImageNormal(Empty).SetBackgroundImageHovered(Empty).SetBackgroundImageFocused(Empty).SetBackgroundImageReadOnly(Empty);
    // Keep editable text centered vertically and clear of the beveled sides.
    StockEditStyle.SetPadding(FMargin(16, 0, 12, 0));
    auto Field=[&](const TSharedPtr<SEditableTextBox>& Box)
    {
        Box->SetStyle(&StockEditStyle);
        Box->SetFont(Font(16));
        FWowBackdrop D;D.BgFile=TEXT("Interface\\Tooltips\\UI-Tooltip-Background");D.EdgeFile=TEXT("Interface\\Glues\\Common\\Glue-Tooltip-Border");D.EdgeSize=16;D.TileSize=16;D.Tile=true;
        D.InsetLeft=10;D.InsetRight=5;D.InsetTop=4;D.InsetBottom=9;D.Color=FLinearColor(.09f,.09f,.09f,1);D.BorderColor=FLinearColor(.8f,.8f,.8f,1);
        return SNew(SOverlay)+SOverlay::Slot()[Backdrop(D)]+SOverlay::Slot()[Box.ToSharedRef()];
    };
    auto AddLoginField = [&](const FString& CaptionText, float LabelWidth, float FieldTop,
                             const TSharedPtr<SEditableTextBox>& Box)
    {
        // AccountLogin.xml centers the caption in a 64-unit region whose
        // bottom sits 23 units below the top of the edit box.
        constexpr float LabelHeight = 64.f;
        constexpr float LabelBottomOffset = 23.f;
        auto Caption = SNew(SBox).VAlign(VAlign_Center).Visibility(EVisibility::HitTestInvisible)
            [Label(CaptionText)];
        Put(Caption, -LabelWidth/2, FieldTop+LabelBottomOffset-LabelHeight, LabelWidth, LabelHeight, .5f, 1);
        Put(Field(Box), -100, FieldTop, 200, 37, .5f, 1);
    };
    AddLoginField(TEXT("Login server"), 256, -452, ServerBox);
    AddLoginField(Art->String(TEXT("ACCOUNT_NAME")), 600, -382, UsernameBox);
    AddLoginField(Art->String(TEXT("PASSWORD")), 256, -312, PasswordBox);
    Put(Click(TEXT("Login"),Art->String(TEXT("LOGIN")),FOnClicked::CreateSP(this,&SWowLoginWidget::OnLoginClicked)), -110,-215,220,45,.5f,1);
    Put(SAssignNew(StatusLabel,STextBlock).Font(Font(12)).ColorAndOpacity(FLinearColor(1,.25f,.15f)).Justification(ETextJustify::Center).AutoWrapText(true),-300,-159,600,42,.5f,1);
    Put(Click(TEXT("Options"),Art->String(TEXT("OPTIONS")),FOnClicked::CreateLambda([this]{bOptions=!bOptions;BuildStockLayout();return FReply::Handled();}),12),-155,-219,150,38,1,1);
    Put(Click(TEXT("Quit"),Art->String(TEXT("QUIT")),FOnClicked::CreateLambda([]{FPlatformMisc::RequestExit(false);return FReply::Handled();}),12),-155,-67,150,38,1,1);
    constexpr float PanelLeft=-385,PanelWidth=210;
    if(bOptions)
    {
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
    ScreenBody->SetContent(Canvas);
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
        return TEXT("World of Warcraft Classic");
    case EWowExpansion::BurningCrusade:
        return TEXT("The Burning Crusade");
    case EWowExpansion::WrathOfTheLichKing:
    default:
        return TEXT("Wrath of the Lich King");
    }
}

void SWowLoginWidget::Construct(const FArguments& InArgs)
{
    auto MakeInput = [this](bool IsPassword)
    {
        return SNew(SEditableTextBox).IsPassword(IsPassword)
            .OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit)
            {
                if (Commit == ETextCommit::OnEnter) OnLoginClicked();
            });
    };
    ServerBox = MakeInput(false);
    ServerBox->SetText(FText::FromString(TEXT("127.0.0.1:3724")));
    UsernameBox = MakeInput(false);
    PasswordBox = MakeInput(true);

    ChildSlot[SNew(SDPIScaler).DPIScale_Lambda([this]
    {
        const float Height = GetCachedGeometry().GetLocalSize().Y;
        return Height > 0 ? Height / 768.f : 1.f;
    })[SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1)[SAssignNew(ScreenBody,SBox)]
        + SVerticalBox::Slot().AutoHeight()[BuildExpansionTabs()]]];
}

TSharedRef<SWidget> SWowLoginWidget::BuildExpansionTabs()
{
    auto Tabs = SNew(SHorizontalBox);
    auto AddTab = [&](EWowExpansion Expansion, const TCHAR* Title)
    {
        auto Button = SNew(SButton)
            .ContentPadding(FMargin(16,4))
            .ButtonColorAndOpacity_Lambda([this,Expansion]
            {
                return CurrentExpansion == Expansion ? FLinearColor(.12f,.22f,.32f) : FLinearColor(.08f,.07f,.06f);
            })
            .OnClicked_Lambda([this,Expansion]
            {
                SetExpansion(Expansion);
                return FReply::Handled();
            })
            [SNew(STextBlock).Text(FText::FromString(Title)).Font(FCoreStyle::GetDefaultFontStyle("Bold",12))
                .ColorAndOpacity_Lambda([this,Expansion]() -> FSlateColor
                {
                    return CurrentExpansion == Expansion ? FLinearColor(1,.82f,.15f) : FLinearColor(.78f,.75f,.65f);
                })];
        StockControls.Add(FName(*FString::Printf(TEXT("Expansion_%u"),uint8(Expansion))),Button);
        Tabs->AddSlot().AutoWidth().Padding(2,0)[Button];
    };
    AddTab(EWowExpansion::Classic,TEXT("Classic"));
    AddTab(EWowExpansion::BurningCrusade,TEXT("The Burning Crusade"));
    AddTab(EWowExpansion::WrathOfTheLichKing,TEXT("Wrath of the Lich King"));
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
        .BorderBackgroundColor(FLinearColor(.023f,.025f,.03f)).Padding(0,3).HAlign(HAlign_Center)[Tabs];
}

void SWowLoginWidget::SetExpansion(EWowExpansion Expansion)
{
    if (Expansion == CurrentExpansion) return;
    CurrentExpansion = Expansion;
    OnExpansionChanged.ExecuteIfBound(static_cast<uint8>(Expansion));
}

// ─── Login logic ────────────────────────────────────────────────────────────

FReply SWowLoginWidget::OnLoginClicked()
{
    if (CurrentExpansion != EWowExpansion::WrathOfTheLichKing) return FReply::Handled();
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
