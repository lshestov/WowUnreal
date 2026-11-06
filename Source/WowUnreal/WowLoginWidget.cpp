#include "WowLoginWidget.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"

#include "WowLoginScene.h"
#include "WowLoginPreferences.h"
#include "WowAudioManager.h"
#include "WowCharacterCreationArt.h"
#include "WowBackdropWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Input/SSlider.h"
#include "Framework/Application/SlateApplication.h"

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
        InitializeStyles();
        ExpansionFooter->SetContent(BuildExpansionTabs());
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
void SWowLoginWidget::InitializeStyles()
{
    if(BlueButton) return;
    BlueButton=MakeShared<FButtonStyle>();
    const FString Base=TEXT("Interface\\Glues\\Common\\Glue-Panel-Button");
    const FVector2f Max(.578125f,.75f);
    BlueButton->SetNormal(*Art->Brush(Base+TEXT("-Up-Blue"),FVector2f(0),Max));
    BlueButton->SetHovered(BlueButton->Normal);
    BlueButton->SetPressed(*Art->Brush(Base+TEXT("-Down-Blue"),FVector2f(0),Max));
    BlueButton->SetDisabled(*Art->Brush(Base+TEXT("-Disabled"),FVector2f(0),Max));
    BlueButton->SetNormalPadding(FMargin(0));BlueButton->SetPressedPadding(FMargin(1,1,0,0));
    FSlateBrush Empty;Empty.DrawAs=ESlateBrushDrawType::NoDrawType;
    PlainButton.SetNormal(Empty).SetHovered(Empty).SetPressed(Empty).SetDisabled(Empty);
    PlainButton.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
}
TSharedRef<SButton> SWowLoginWidget::MakeButton(FName Id,const FString& Text,FOnClicked Action,int32 Size,TAttribute<bool> Selected)
{
    auto Button=SNew(SButton).ButtonStyle(BlueButton.Get()).ContentPadding(0)
        .HAlign(HAlign_Fill).VAlign(VAlign_Fill).OnClicked(Action);
    TWeakPtr<SButton> Weak=Button;
    auto Font=Art->Font(Size);Font.OutlineSettings=FFontOutlineSettings(1);
    Button->SetContent(SNew(SOverlay)
        +SOverlay::Slot()[SNew(SImage).Image(&BlueButton->Pressed).Visibility_Lambda([Selected]{return Selected.Get()?EVisibility::HitTestInvisible:EVisibility::Collapsed;})]
        +SOverlay::Slot()[SNew(SImage).Image(Art->Brush(TEXT("Interface\\Glues\\Common\\Glue-Panel-Button-Highlight-Blue"),FVector2f(0),FVector2f(.625f,.6875f),ESlateBrushMirrorType::NoMirror,true))
            .Visibility_Lambda([Weak]{auto B=Weak.Pin();return B && B->IsHovered()?EVisibility::HitTestInvisible:EVisibility::Collapsed;})]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0,0,0,1)
        [SNew(STextBlock).Text(FText::FromString(Text)).Font(Font).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)
            .ColorAndOpacity_Lambda([Weak,Selected]() -> FSlateColor {auto B=Weak.Pin();return (B && B->IsHovered()) || Selected.Get()?FLinearColor::White:FLinearColor(1,.78f,0);})]);
    StockControls.Add(Id,Button);return Button;
}
TSharedRef<SButton> SWowLoginWidget::MakeCheckBox(FName Id,const FString& Text,TAttribute<bool> Checked,FSimpleDelegate Toggle)
{
    auto Button=SNew(SButton).ButtonStyle(&PlainButton).ContentPadding(0)
        .OnClicked_Lambda([Toggle]{Toggle.ExecuteIfBound();return FReply::Handled();});
    TWeakPtr<SButton> Weak=Button;
    const auto* Up=Art->Brush(TEXT("Interface\\Buttons\\UI-CheckBox-Up"));
    const auto* Down=Art->Brush(TEXT("Interface\\Buttons\\UI-CheckBox-Down"));
    auto Font=Art->Font(12);Font.OutlineSettings=FFontOutlineSettings(1);
    Button->SetContent(SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [SNew(SBox).WidthOverride(20).HeightOverride(20)[SNew(SOverlay)
            +SOverlay::Slot()[SNew(SImage).Image_Lambda([Weak,Up,Down]{auto B=Weak.Pin();return B && B->IsPressed()?Down:Up;})]
            +SOverlay::Slot()[SNew(SImage).Image(Art->Brush(TEXT("Interface\\Buttons\\UI-CheckBox-Check")))
                .Visibility_Lambda([Checked]{return Checked.Get()?EVisibility::HitTestInvisible:EVisibility::Collapsed;})]
            +SOverlay::Slot()[SNew(SImage).Image(Art->Brush(TEXT("Interface\\Buttons\\UI-CheckBox-Highlight"),FVector2f(0),FVector2f(1),ESlateBrushMirrorType::NoMirror,true))
                .Visibility_Lambda([Weak]{auto B=Weak.Pin();return B && B->IsHovered()?EVisibility::HitTestInvisible:EVisibility::Collapsed;})]]]
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2,0,0,0)
        [SNew(STextBlock).Text(FText::FromString(Text)).Font(Font).ColorAndOpacity(FLinearColor(1,.78f,0))
            .ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)]);
    StockControls.Add(Id,Button);return Button;
}
void SWowLoginWidget::SavePreferences()
{
    if(!bPersistPreferences) return;
    FWowLoginPreferences Preferences;Preferences.bRememberAccount=bRememberAccount;
    Preferences.Server=ServerBox->GetText().ToString().TrimStartAndEnd();
    Preferences.AccountName=UsernameBox->GetText().ToString().TrimStartAndEnd();
    Preferences.Save();
}
void SWowLoginWidget::BuildStockLayout()
{
    ScreenBody->SetContent(SNullWidget::NullWidget);
    Backdrops.Reset();
    const FLinearColor Gold(1,.78f,0);auto Canvas=SNew(SConstraintCanvas);
    auto Put=[&](TSharedRef<SWidget> W,float X,float Y,float Width,float Height,float AX=0,float AY=0){Canvas->AddSlot().Anchors(FAnchors(AX,AY)).Alignment(FVector2D::ZeroVector).Offset(FMargin(X,Y,Width,Height))[W];};
    auto Font=[this](int32 Size){auto F=Art->Font(Size);F.OutlineSettings=FFontOutlineSettings(1);return F;};
    auto Label=[&](const FString& Text,int32 Size=15){return SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(Size)).ColorAndOpacity(Gold)
        .ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black).Justification(ETextJustify::Center);};
    auto Backdrop=[&](FWowBackdrop D)
    {auto* B=NewObject<UWowBackdropWidget>();Backdrops.Add(TStrongObjectPtr<UWowBackdropWidget>(B));B->Configure(D,Cast<UTexture2D>(Art->Brush(D.BgFile)->GetResourceObject()),Cast<UTexture2D>(Art->Brush(D.EdgeFile)->GetResourceObject()),1);return B->TakeWidget();};
    Canvas->AddSlot().Anchors(FAnchors(0,0,1,1)).Offset(FMargin(0))[SNew(SLoginViewport).Scene(Scene.Get())];
    Put(SNew(SImage).Image(Art->Brush(TEXT("Interface\\Glues\\Common\\Glues-WoW-WotLKLogo"))),3,7,256,128);
    Put(SNew(SImage).Image(Art->Brush(TEXT("Interface\\Glues\\Mainmenu\\Glues-BlizzardLogo"))),-50,-108,100,100,.5f,1);
    Put(Label(Art->String(TEXT("BLIZZ_DISCLAIMER")),12),-360,-24,720,18,.5f,1);
    auto Version=Label(TEXT("Version 3.3.5 (12340)"),12);Version->SetJustification(ETextJustify::Left);
    Put(Version,3,-24,230,18,0,1);
    StockEditStyle=FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");FSlateBrush Empty;Empty.DrawAs=ESlateBrushDrawType::NoDrawType;
    StockEditStyle.SetBackgroundImageNormal(Empty).SetBackgroundImageHovered(Empty).SetBackgroundImageFocused(Empty).SetBackgroundImageReadOnly(Empty);
    // Keep editable text centered vertically and clear of the beveled sides.
    StockEditStyle.SetPadding(FMargin(16, 0, 12, 0));
    auto Field=[&](const TSharedPtr<SEditableTextBox>& Box)
    {
        Box->SetStyle(&StockEditStyle);
        Box->SetFont(Art->EditFont(16));
        Box->SetForegroundColor(FLinearColor::White);
        FWowBackdrop D;D.BgFile=TEXT("Interface\\Tooltips\\UI-Tooltip-Background");D.EdgeFile=TEXT("Interface\\Glues\\Common\\Glue-Tooltip-Border");D.EdgeSize=16;D.TileSize=16;D.Tile=true;
        D.InsetLeft=10;D.InsetRight=5;D.InsetTop=4;D.InsetBottom=9;
        // AccountLogin.lua colors are display-space values; the backdrop painter encodes its tint to sRGB.
        D.Color=FLinearColor::FromSRGBColor(FColor(23,23,23));D.BorderColor=FLinearColor::FromSRGBColor(FColor(204,204,204));
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
    Put(MakeButton(TEXT("Login"),Art->String(TEXT("LOGIN")),FOnClicked::CreateSP(this,&SWowLoginWidget::OnLoginClicked)), -110,-215,220,45,.5f,1);
    Put(SNew(SBox).HAlign(HAlign_Center)[MakeCheckBox(TEXT("RememberAccount"),Art->String(TEXT("SAVE_ACCOUNT_NAME")),
        TAttribute<bool>::CreateLambda([this]{return bRememberAccount;}),FSimpleDelegate::CreateLambda([this]{bRememberAccount=!bRememberAccount;SavePreferences();}))],-150,-176,300,24,.5f,1);
    Put(SAssignNew(StatusLabel,STextBlock).Text(FText::FromString(StatusText)).Font(Font(12)).ColorAndOpacity(FLinearColor(1,.25f,.15f)).Justification(ETextJustify::Center).AutoWrapText(true),-270,-144,540,36,.5f,1);
    Put(MakeButton(TEXT("Options"),Art->String(TEXT("OPTIONS")),FOnClicked::CreateLambda([this]{bOptions=!bOptions;BuildStockLayout();return FReply::Handled();}),12),-155,-293,150,38,1,1);
    Put(MakeButton(TEXT("Quit"),Art->String(TEXT("QUIT")),FOnClicked::CreateLambda([]{FPlatformMisc::RequestExit(false);return FReply::Handled();}),12),-155,-67,150,38,1,1);
    auto Content=SNew(SOverlay)+SOverlay::Slot()[Canvas];
    if(bOptions)
    {
        Canvas->SetEnabled(false);
        Canvas=SNew(SConstraintCanvas);
        Content->AddSlot()[Canvas];
        // A modal uses the original dialog border, keeping controls readable over the scene.
        Canvas->AddSlot().Anchors(FAnchors(0,0,1,1)).Offset(FMargin(0))
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0,0,0,.55f))];
        FWowBackdrop Dialog;Dialog.BgFile=TEXT("Interface\\DialogFrame\\UI-DialogBox-Background");
        Dialog.EdgeFile=TEXT("Interface\\DialogFrame\\UI-DialogBox-Border");Dialog.Tile=true;Dialog.TileSize=32;Dialog.EdgeSize=32;
        Dialog.InsetLeft=Dialog.InsetRight=Dialog.InsetTop=Dialog.InsetBottom=11;
        Put(SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
            .BorderBackgroundColor(FLinearColor(.005f,.008f,.012f,1)), -200,-150,400,300,.5f,.5f);
        Put(Backdrop(Dialog),-210,-160,420,320,.5f,.5f);
        Put(Label(Art->String(TEXT("OPTIONS")),18),-180,-128,360,28,.5f,.5f);
        auto Check=[&](const TCHAR* Name,const FString& Text)
        {
            return MakeCheckBox(FName(Name),Text,TAttribute<bool>::CreateLambda([this,Name]
            {FString V;return Audio.IsValid() && Audio->GetUiOption(Name,V) && V==TEXT("1");}),FSimpleDelegate::CreateLambda([this,Name]
            {if(Audio.IsValid()){FString V;Audio->GetUiOption(Name,V);Audio->SetUiOption(Name,V==TEXT("1")?TEXT("0"):TEXT("1"));}}));
        };
        Put(Check(TEXT("sound_enableall"),Art->String(TEXT("ENABLE_SOUND"))),-155,-83,310,24,.5f,.5f);
        Put(Check(TEXT("sound_enablemusic"),Art->String(TEXT("ENABLE_MUSIC"))),-155,-47,310,24,.5f,.5f);
        Put(Label(Art->String(TEXT("MUSIC_VOLUME")),12),-155,-4,310,22,.5f,.5f);
        if(!GlueSlider) GlueSlider=MakeShared<FSliderStyle>();FSlateBrush NoBar;NoBar.DrawAs=ESlateBrushDrawType::NoDrawType;
        GlueSlider->SetNormalBarImage(NoBar).SetHoveredBarImage(NoBar).SetDisabledBarImage(NoBar);
        FSlateBrush Thumb=*Art->Brush(TEXT("Interface\\Buttons\\UI-SliderBar-Button-Horizontal"));Thumb.ImageSize=FVector2D(32,32);
        GlueSlider->SetNormalThumbImage(Thumb).SetHoveredThumbImage(Thumb).SetDisabledThumbImage(Thumb);
        FWowBackdrop SliderBack;SliderBack.BgFile=TEXT("Interface\\Buttons\\UI-SliderBar-Background");SliderBack.EdgeFile=TEXT("Interface\\Buttons\\UI-SliderBar-Border");SliderBack.Tile=true;SliderBack.TileSize=8;SliderBack.EdgeSize=8;SliderBack.InsetLeft=3;SliderBack.InsetRight=3;SliderBack.InsetTop=6;SliderBack.InsetBottom=6;
        Put(Backdrop(SliderBack),-145,33,290,17,.5f,.5f);
        Put(SNew(SSlider).Style(GlueSlider.Get()).Value_Lambda([this]{return Audio.IsValid()?Audio->MusicVolume:.5f;}).OnValueChanged_Lambda([this](float V){if(Audio.IsValid()) Audio->SetUiOption(TEXT("sound_musicvolume"),FString::SanitizeFloat(V));}),-145,26,290,32,.5f,.5f);
        Put(MakeButton(TEXT("OptionsDone"),Art->String(TEXT("DONE")),FOnClicked::CreateLambda([this]{bOptions=false;BuildStockLayout();return FReply::Handled();}),12),-75,94,150,38,.5f,.5f);
    }
    ExpansionFooter->SetEnabled(!bOptions);
    ScreenBody->SetContent(Content);
    if(bOptions) FSlateApplication::Get().SetKeyboardFocus(StockControls.FindChecked(TEXT("OptionsDone")).Pin(),EFocusCause::SetDirectly);
    else FSlateApplication::Get().SetKeyboardFocus(UsernameBox,EFocusCause::SetDirectly);
}
bool SWowLoginWidget::PressStockControl(FName Name)
{
    const auto Keep=AsShared();const auto* Weak=StockControls.Find(Name);const auto B=Weak?Weak->Pin():nullptr;if(!B || !B->IsEnabled()) return false;
    const FKeyEvent E(EKeys::SpaceBar,FModifierKeysState(),0,false,0,0);B->OnKeyDown(B->GetCachedGeometry(),E);B->OnKeyUp(B->GetCachedGeometry(),E);return true;
}
FReply SWowLoginWidget::OnPreviewKeyDown(const FGeometry&,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Escape && bOptions){bOptions=false;BuildStockLayout();return FReply::Handled();}
    return FReply::Unhandled();
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
    bPersistPreferences=InArgs._PersistPreferences;
    FWowLoginPreferences Preferences;
    if(bPersistPreferences && Preferences.Load()) bRememberAccount=Preferences.bRememberAccount;
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
        + SVerticalBox::Slot().AutoHeight()[SAssignNew(ExpansionFooter,SBox)[BuildExpansionTabs()]]]];
}

TSharedRef<SWidget> SWowLoginWidget::BuildExpansionTabs()
{
    auto Tabs = SNew(SHorizontalBox);
    auto AddTab = [&](EWowExpansion Expansion, const TCHAR* Title)
    {
        if(Art)
        {
            auto Button=MakeButton(FName(*FString::Printf(TEXT("Expansion_%u"),uint8(Expansion))),Title,
                FOnClicked::CreateLambda([this,Expansion]{SetExpansion(Expansion);return FReply::Handled();}),12,
                TAttribute<bool>::CreateLambda([this,Expansion]{return CurrentExpansion==Expansion;}));
            const float Width=Expansion==EWowExpansion::Classic?126.f:Expansion==EWowExpansion::BurningCrusade?196.f:220.f;
            Tabs->AddSlot().AutoWidth().Padding(0)[SNew(SBox).WidthOverride(Width).HeightOverride(38)[Button]];
            return;
        }
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
        .BorderBackgroundColor(FLinearColor(.012f,.018f,.023f)).Padding(0).HAlign(HAlign_Center)[Tabs];
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
    if (CurrentExpansion != EWowExpansion::WrathOfTheLichKing || bOptions) return FReply::Handled();
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
    SavePreferences();
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
    StatusText=Text;
    if (StatusLabel.IsValid())
    {
        StatusLabel->SetText(FText::FromString(Text));
    }
}
