#include "WowCharacterCreateWidget.h"
#include "WowCharacterPreview.h"
#include "WowCharacterCreationArt.h"
#include "WowWorldManager.h"
#include "WowBackdropWidget.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Formats/Dbc/DbcStore.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Input/Reply.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
class SWowCreationViewport : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWowCreationViewport) {} SLATE_ARGUMENT(AWowCharacterPreview*,Preview) SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Preview=Args._Preview;
        Brush.SetResourceObject(Args._Preview->GetRenderTarget()); Brush.ImageSize=FVector2D(Args._Preview->GetRenderTarget()->SizeX,Args._Preview->GetRenderTarget()->SizeY);
        Brush.DrawAs=ESlateBrushDrawType::Image;
        ChildSlot [SNew(SImage).Image(&Brush)];
    }
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& E) override
    {return E.GetEffectingButton()==EKeys::LeftMouseButton?FReply::Handled().CaptureMouse(AsShared()):FReply::Unhandled();}
    virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent& E) override
    {return E.GetEffectingButton()==EKeys::LeftMouseButton?FReply::Handled().ReleaseMouseCapture():FReply::Unhandled();}
    virtual FReply OnMouseMove(const FGeometry&,const FPointerEvent& E) override
    {if (HasMouseCapture() && Preview.IsValid()) {Preview->Rotate(E.GetCursorDelta().X*.5f); return FReply::Handled();} return FReply::Unhandled();}
    virtual FReply OnMouseWheel(const FGeometry&,const FPointerEvent& E) override
    {if (Preview.IsValid()) Preview->Zoom(E.GetWheelDelta()); return FReply::Handled();}
    virtual void Tick(const FGeometry& G,double Time,float DT) override
    {
        SCompoundWidget::Tick(G,Time,DT);
        if(Preview.IsValid() && G.GetLocalSize().Y>0) Preview->SetViewAspect(float(G.GetLocalSize().X/G.GetLocalSize().Y));
    }
private:
    FSlateBrush Brush;
    TWeakObjectPtr<AWowCharacterPreview> Preview;
};

}

void SWowCharacterCreateWidget::Construct(const FArguments& Args)
{
    // The real layout is assembled after MPQ ownership has been supplied.
    SAssignNew(NameBox,SEditableTextBox);
    ChildSlot [SAssignNew(StatusLabel,STextBlock).Text(FText::FromString(TEXT("Loading character data...")))];
}

void SWowCharacterCreateWidget::BuildStockLayout()
{
    const FString Root=TEXT("Interface\\Glues\\CharacterCreate\\");
    const FString Common=TEXT("Interface\\Glues\\Common\\");
    const FLinearColor Gold(1,.82f,0);
    auto Canvas=SNew(SConstraintCanvas);
    auto Put=[&](TSharedRef<SWidget> W,float X,float Y,float Width,float Height,float AX=0,float AY=0,float AlignX=0,float AlignY=0)
    {Canvas->AddSlot().Anchors(FAnchors(AX,AY)).Alignment(FVector2D(AlignX,AlignY)).Offset(FMargin(X,Y,Width,Height))[W];};
    auto Image=[&](const FString& Path,FVector2f Min=FVector2f(0,0),FVector2f Max=FVector2f(1,1),ESlateBrushMirrorType::Type Mirror=ESlateBrushMirrorType::NoMirror)
    {return SNew(SImage).Image(Art->Brush(Path,Min,Max,Mirror)).Visibility(EVisibility::HitTestInvisible);};
    auto Text=[&](TAttribute<FText> Value,int32 Size=12,bool Title=false,FLinearColor Color=FLinearColor::White)
    {return SNew(STextBlock).Text(Value).Font(Art->Font(Size,Title)).ColorAndOpacity(Color).ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black).Justification(ETextJustify::Center);};
    auto Label=[&](const FString& Key,int32 Size=12,bool Title=false,FLinearColor Color=FLinearColor::White)
    {return Text(FText::FromString(Art->String(Key)),Size,Title,Color);};
    auto Click=[&](const FString& Key,FOnClicked OnClick)
    {
        auto Button=SNew(SButton).ButtonStyle(Art->Button(Common+TEXT("Glue-Panel-Button"),true)).ContentPadding(0).OnClicked(OnClick)[Label(Key,13,false,Gold)];
        StockControls.Add(FName(*Key),Button);return Button;
    };
    auto Enabled=[&](TSharedRef<SWidget> W){return SNew(SBox).IsEnabled_Lambda([this]{return bDataReady && !bSubmitting;})[W];};
    auto RaceToken=[this](uint8 Race)
    {const auto* R=FDbcStore::Get().ChrRaces().GetById(Race);return R?R->ClientFileString.ToUpper():FString();};
    auto Tint=[this]
    {const auto* R=FDbcStore::Get().ChrRaces().GetById(Appearance.Race);return R && R->FactionGroup==1?FLinearColor(.19f,.05f,.05f,1):FLinearColor(.09f,.09f,.19f,1);};
    Canvas->AddSlot().Anchors(FAnchors(0,0,1,1)).Offset(FMargin(0))
        [SAssignNew(PreviewBox,SBox)[SNew(SWowCreationViewport).Preview(Preview.Get())]];
    Put(SNew(SImage).Image(Art->Brush(TEXT("Interface\\Tooltips\\UI-Tooltip-Background"))).ColorAndOpacity_Lambda(Tint),19,13,236,742);
    Put(Image(Root+TEXT("UI-CharacterCreate-OuterBorder"),FVector2f(0,0),FVector2f(1,1),ESlateBrushMirrorType::Vertical),9,5,256,256);
    Put(Image(Root+TEXT("UI-CharacterCreate-OuterBorder"),FVector2f(0,0),FVector2f(1,.9609375f)),9,261,256,246);
    Put(Image(Root+TEXT("UI-CharacterCreate-OuterBorder")),9,507,256,256);
    Put(Image(Root+TEXT("UI-CharacterCreate-Banners")),9,10,256,512);
    Put(Label(TEXT("ALLIANCE"),18,true),40,25,94,24);Put(Label(TEXT("HORDE"),18,true),140,25,94,24);
    auto Icon=[&](TAttribute<const FSlateBrush*> IconBrush,TAttribute<bool> Selected,FOnClicked OnClick,TAttribute<FText> Tooltip,FName Key=NAME_None)
    {
        auto Button=SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0).OnClicked(OnClick).ToolTipText(Tooltip);
        if(Key!=NAME_None) StockControls.Add(Key,Button);
        TWeakPtr<SButton> Weak=Button;
        Button->SetContent(SNew(SOverlay)
            +SOverlay::Slot().Padding(-10)[Image(Root+TEXT("UI-CharacterCreate-IconShadow"))]
            +SOverlay::Slot()[SNew(SImage).Image(IconBrush)]
            +SOverlay::Slot()[Image(Root+TEXT("UI-CharacterCreate-Highlights"),FVector2f(0,.5f),FVector2f(.5f,1))]
            +SOverlay::Slot()[SNew(SImage).Image(Art->Brush(Root+TEXT("UI-CharacterCreate-Highlights"),FVector2f(.5f,0),FVector2f(1,.5f),ESlateBrushMirrorType::NoMirror,true))
                .Visibility_Lambda([Selected]{return Selected.Get()?EVisibility::HitTestInvisible:EVisibility::Collapsed;})]
            +SOverlay::Slot()[SNew(SImage).Image(Art->Brush(Root+TEXT("UI-CharacterCreate-Highlights"),FVector2f(0,0),FVector2f(.5f,.5f),ESlateBrushMirrorType::NoMirror,true))
                .ColorAndOpacity(FLinearColor(1,1,1,.5f)).Visibility_Lambda([Weak]{auto B=Weak.Pin();return B && B->IsHovered()?EVisibility::HitTestInvisible:EVisibility::Collapsed;})]);
        return Enabled(Button);
    };
    // Original faction columns; IDs come from the original playable race table.
    int32 Alliance=0,Horde=0;
    for(uint8 Race:FDbcStore::Get().CharacterCreation().Races())
    {
        const auto* Entry=FDbcStore::Get().ChrRaces().GetById(Race);const bool IsHorde=Entry && Entry->FactionGroup==1;
        const int32 Row=IsHorde?Horde++:Alliance++;
        const float X=IsHorde?168:68,Y=66+59*Row;
        Put(Icon(TAttribute<const FSlateBrush*>::CreateLambda([this,Race,RaceToken,Root]{return Art->Icon(Root+TEXT("UI-CharacterCreate-Races"),TEXT("RACE_ICON_TCOORDS"),RaceToken(Race)+(Appearance.Gender?TEXT("_FEMALE"):TEXT("_MALE")));}),
            TAttribute<bool>::CreateLambda([this,Race]{return Appearance.Race==Race;}),FOnClicked::CreateLambda([this,Race]{SelectIdentity(0,Race);return FReply::Handled();}),
            FText::FromString(Entry?Entry->Name:TEXT("")),FName(*FString::Printf(TEXT("Race_%u"),Race))),X,Y,38,38);
        Put(Text(TAttribute<FText>::CreateLambda([this,Race]{const auto* R=FDbcStore::Get().ChrRaces().GetById(Race);return FText::FromString(Appearance.Race==Race && R?R->Name:TEXT(""));}),10,false,Gold),X-26,Y-17,90,16);
    }
    Put(Text(TAttribute<FText>::CreateLambda([this]{return FText::FromString(Art->String(Appearance.Gender?TEXT("FEMALE"):TEXT("MALE")));}),12,false,Gold),36,375,202,18);
    for(uint8 Gender:{uint8(0),uint8(1)})
        Put(Icon(Art->Brush(Root+TEXT("UI-CharacterCreate-Gender"),FVector2f(Gender*.5f,0),FVector2f((Gender+1)*.5f,1)),
            TAttribute<bool>::CreateLambda([this,Gender]{return Appearance.Gender==Gender;}),FOnClicked::CreateLambda([this,Gender]{SelectIdentity(2,Gender);return FReply::Handled();}),
            FText::FromString(Art->String(Gender?TEXT("FEMALE"):TEXT("MALE"))),FName(*FString::Printf(TEXT("Gender_%u"),Gender))),98+40*Gender,397,38,38);
    Put(Text(TAttribute<FText>::CreateLambda([this]{return FText::FromString(FDbcStore::Get().CharacterCreation().ClassName(Appearance.Class));}),12,false,Gold),24,443,226,20);
    const uint8 Classes[]={1,2,3,4,5,6,7,8,9,11};
    for(int32 I=0;I<10;++I)
    {
        const uint8 Class=Classes[I];
        Put(SNew(SBox).IsEnabled_Lambda([this,Class]{return bDataReady && !bSubmitting && FDbcStore::Get().CharacterCreation().Allows(Appearance.Race,Class);})
            [Icon(TAttribute<const FSlateBrush*>::CreateLambda([this,Class,Root]{return Art->Icon(Root+TEXT("UI-CharacterCreate-Classes"),TEXT("CLASS_ICON_TCOORDS"),FDbcStore::Get().CharacterCreation().ClassToken(Class));}),
                TAttribute<bool>::CreateLambda([this,Class]{return Appearance.Class==Class;}),FOnClicked::CreateLambda([this,Class]{SelectIdentity(1,Class);return FReply::Handled();}),
                TAttribute<FText>::CreateLambda([Class]{return FText::FromString(FDbcStore::Get().CharacterCreation().ClassName(Class));}),FName(*FString::Printf(TEXT("Class_%u"),Class)))],28+44*(I%5),468+44*(I/5),38,38);
    }
    for(int32 I=0;I<5;++I)
    {
        const auto O=EWowAppearanceOption(I);const float Y=560+32*I;
        Put(Image(Root+TEXT("CharacterCreate-LabelFrame"),FVector2f(0,0),FVector2f(.1953125f,1)),11,Y-16,25,64);
        Put(Image(Root+TEXT("CharacterCreate-LabelFrame"),FVector2f(.1953125f,0),FVector2f(.8046875f,1)),36,Y-16,141,64);
        Put(Image(Root+TEXT("CharacterCreate-LabelFrame"),FVector2f(.8046875f,0),FVector2f(1,1)),177,Y-16,25,64);
        Put(Text(TAttribute<FText>::CreateLambda([this,I]
        {
            const auto* R=FDbcStore::Get().ChrRaces().GetById(Appearance.Race);
            FString Key=FString::Printf(TEXT("CHAR_CUSTOMIZATION%d_DESC"),I+1);
            if(R && I==2) Key=TEXT("HAIR_")+R->HairCustomization.ToUpper()+TEXT("_STYLE");
            if(R && I==3) Key=TEXT("HAIR_")+R->HairCustomization.ToUpper()+TEXT("_COLOR");
            if(R && I==4) Key=TEXT("FACIAL_HAIR_")+(Appearance.Gender?R->FacialHairCustomization2:R->FacialHairCustomization1).ToUpper();
            FString Value=Art->String(Key);if(Value.IsEmpty()) Value=Art->String(FString::Printf(TEXT("CHAR_CUSTOMIZATION%d_DESC"),I+1));return FText::FromString(Value);
        }),12),29,Y+4,144,24);
        for(int32 Direction:{-1,1})
        {
            auto Arrow=SNew(SButton).ButtonStyle(Art->Button(Common+(Direction<0?TEXT("Glue-LeftArrow-Button"):TEXT("Glue-RightArrow-Button")))).ContentPadding(0)
                .IsEnabled_Lambda([this,O]{return !bSubmitting && GetOptions(O).Num()>1;})
                .ToolTipText_Lambda([this,O]{auto V=GetOptions(O);return FText::FromString(FString::Printf(TEXT("%d / %d"),V.IndexOfByKey(Value(O))+1,V.Num()));})
                .OnClicked_Lambda([this,O,Direction]{ChangeOption(O,Direction);return FReply::Handled();});
            StockControls.Add(FName(*FString::Printf(TEXT("Appearance_%d_%d"),I,Direction)),Arrow);
            Put(Enabled(Arrow),Direction<0?185:214,Y,32,32);
        }
    }
    Put(Enabled(Click(TEXT("RANDOMIZE"),FOnClicked::CreateLambda([this]
    {
        for(int32 I=0;I<5;++I) {auto O=EWowAppearanceOption(I);auto V=GetOptions(O);if(V.Num()) Value(O)=V[FMath::RandHelper(V.Num())];Refresh();}
        return FReply::Handled();
    }))),64,723,146,30);
    Put(Image(Common+TEXT("Glues-WoW-WotLKLogo")),-9,0,256,128,1,0,1);
    auto Info=[&](bool Class,float X,float Y)
    {
        Put(SNew(SImage).Image(Art->Brush(TEXT("Interface\\Tooltips\\UI-Tooltip-Background"))).ColorAndOpacity_Lambda(Tint),X+12,Y+20,252,261,1);
        Put(Image(Root+TEXT("UI-CharacterCreate-InfoBox"),FVector2f(.005859375f,.005859375f),FVector2f(.52734375f,.56640625f)),X,Y,267,287,1);
        Put(SNew(SImage).Image_Lambda([this,Class,RaceToken,Root]
        {return Class?Art->Icon(TEXT("Interface\\TargetingFrame\\UI-Classes-Circles"),TEXT("CLASS_ICON_TCOORDS"),FDbcStore::Get().CharacterCreation().ClassToken(Appearance.Class)):
            Art->Icon(Root+TEXT("UI-CharacterCreate-RacesRound"),TEXT("RACE_ICON_TCOORDS"),RaceToken(Appearance.Race)+(Appearance.Gender?TEXT("_FEMALE"):TEXT("_MALE")));}),X+4,Y+4,48,48,1);
        Put(Text(TAttribute<FText>::CreateLambda([this,Class]{const auto* R=FDbcStore::Get().ChrRaces().GetById(Appearance.Race);return FText::FromString(Class?FDbcStore::Get().CharacterCreation().ClassName(Appearance.Class):R?R->Name:TEXT(""));}),18,true),X+55,Y+10,206,30,1);
        auto Ability=TAttribute<FText>::CreateLambda([this,Class,RaceToken]
        {
            const FString Token=Class?FDbcStore::Get().CharacterCreation().ClassToken(Appearance.Class):RaceToken(Appearance.Race);
            FString Value;
            for(int32 I=Class?0:1;I<12;++I){const FString Line=Art->String((Class?TEXT("CLASS_INFO_"):TEXT("ABILITY_INFO_"))+Token+FString::FromInt(I));if(Line.IsEmpty()) break;Value+=Line+TEXT("\n\n");}
            return FText::FromString(Value);
        });
        auto Flavor=TAttribute<FText>::CreateLambda([this,Class,RaceToken]
        {
            const FString Token=Class?FDbcStore::Get().CharacterCreation().ClassToken(Appearance.Class):RaceToken(Appearance.Race);
            const FString Key=(Class?TEXT("CLASS_"):TEXT("RACE_INFO_"))+Token;
            FString Value=Art->String(Key+(Appearance.Gender?TEXT("_FEMALE"):TEXT("")));if(Value.IsEmpty()) Value=Art->String(Key);
            return FText::FromString(Value+TEXT("\n\n"));
        });
        auto Bar=SNew(SScrollBar).Style(Art->ScrollBar()).Thickness(FVector2D(18,18)).Padding(0).AlwaysShowScrollbar(true);
        auto Scroll=SNew(SScrollBox).ExternalScrollbar(Bar)+SScrollBox::Slot()[SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Art->Font(12)).ColorAndOpacity(Gold).AutoWrapText(true).Text(Ability)]
            +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Art->Font(12)).ColorAndOpacity(FLinearColor::White).AutoWrapText(true).Text(Flavor)]];
        Put(Scroll,X+20,Y+55,220,220,1);Put(Bar,X+245,Y+73,18,184,1);
        for(bool Up:{true,false})
            Put(SNew(SButton).ButtonStyle(Art->ScrollArrow(Up)).ContentPadding(0)
                .IsEnabled_Lambda([Scroll,Up]{return Up?Scroll->GetScrollOffset()>0:Scroll->GetScrollOffset()<Scroll->GetScrollOffsetOfEnd();})
                .OnClicked_Lambda([Scroll,Up]{Scroll->SetScrollOffset(FMath::Clamp(Scroll->GetScrollOffset()+(Up?-110.f:110.f),0.f,Scroll->GetScrollOffsetOfEnd()));return FReply::Handled();}),X+245,Y+(Up?55:257),18,18,1);
    };
    Info(false,-276,123);Info(true,-281,415);
    NameStyle=MakeShared<FEditableTextBoxStyle>(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"));
    FSlateBrush Empty;Empty.DrawAs=ESlateBrushDrawType::NoDrawType;NameStyle->SetBackgroundImageNormal(Empty).SetBackgroundImageHovered(Empty).SetBackgroundImageFocused(Empty).SetBackgroundImageReadOnly(Empty).SetPadding(FMargin(15,4,5,9));
    auto Backdrop=[&](float Width,float Height)
    {
        auto* W=NewObject<UWowBackdropWidget>();Backdrops.Add(TStrongObjectPtr<UWowBackdropWidget>(W));
        FWowBackdrop D;D.BgFile=TEXT("Interface\\Tooltips\\UI-Tooltip-Background");D.EdgeFile=Common+TEXT("Glue-Tooltip-Border");
        D.EdgeSize=16;D.TileSize=16;D.Tile=true;D.InsetLeft=10;D.InsetRight=5;D.InsetTop=4;D.InsetBottom=9;D.Color=Tint();D.BorderColor=FLinearColor(.5f,.5f,.5f,1);
        W->Configure(D,Cast<UTexture2D>(Art->Brush(D.BgFile)->GetResourceObject()),Cast<UTexture2D>(Art->Brush(D.EdgeFile)->GetResourceObject()),1);
        return W->TakeWidget();
    };
    Put(Backdrop(156,40),-78,-55,156,40,.5f,1,0,1);
    Put(SAssignNew(NameBox,SEditableTextBox).Style(NameStyle.Get()).Font(Art->Font(16)).ForegroundColor(FLinearColor::White)
        .Justification(ETextJustify::Center).IsEnabled_Lambda([this]{return !bSubmitting;})
        .OnKeyDownHandler_Lambda([this](const FGeometry& G,const FKeyEvent& E){return E.GetKey()==EKeys::Escape?OnKeyDown(G,E):FReply::Unhandled();})
        .OnTextCommitted_Lambda([this](const FText&,ETextCommit::Type Type){if(Type==ETextCommit::OnEnter) Submit();}),-78,-55,156,40,.5f,1,0,1);
    Put(Label(TEXT("NAME"),18,false,Gold),-128,-94,256,32,.5f,1,0,1);
    for(int32 Direction:{-1,1})
        Put(SNew(SButton).ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0).OnClicked_Lambda([this,Direction]{if(Preview.IsValid()) Preview->Rotate(Direction*20);return FReply::Handled();})
            [Image(Root+TEXT("UI-RotationRight-Big-Up"),FVector2f(0,0),FVector2f(1,1),Direction<0?ESlateBrushMirrorType::Horizontal:ESlateBrushMirrorType::NoMirror)],Direction<0?-41:-9,5,50,50,.5f,1,0,1);
    Put(SNew(SBox).IsEnabled_Lambda([this]{return bValid && !bSubmitting && (!Preview.IsValid() || Preview->IsReady());})[Click(TEXT("CHARACTER_CREATE_ACCEPT"),FOnClicked::CreateLambda([this]{Submit();return FReply::Handled();}))],-223,705,160,35,1);
    Put(SNew(SBox).IsEnabled_Lambda([this]{return !bSubmitting;})[Click(TEXT("BACK"),FOnClicked::CreateLambda([this]{if(!bSubmitting) OnBackToCharSelect.ExecuteIfBound();return FReply::Handled();}))],-199,737,120,30,1);
    // Stock GlueDialog styling for pending requests and server errors.
    auto Dialog=SNew(SOverlay)+SOverlay::Slot()[Backdrop(420,140)]
        +SOverlay::Slot().Padding(24,18,24,50)[SAssignNew(StatusLabel,STextBlock).Font(Art->Font(14)).AutoWrapText(true).Justification(ETextJustify::Center).ColorAndOpacity(Gold)]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0,0,0,12)
            [SNew(SBox).WidthOverride(120).HeightOverride(30).IsEnabled_Lambda([this]{return !bSubmitting;})
                [Click(TEXT("OKAY"),FOnClicked::CreateLambda([this]{SetStatusText(TEXT(""));return FReply::Handled();}))]];
    auto Modal=SNew(SOverlay)
        +SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0,0,0,.25f))
            .OnMouseButtonDown_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(SBox).WidthOverride(420).HeightOverride(140)[Dialog]];
    Modal->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]{return StatusLabel && !StatusLabel->GetText().IsEmpty()?EVisibility::Visible:EVisibility::Collapsed;}));
    Canvas->AddSlot().Anchors(FAnchors(0,0,1,1)).Offset(FMargin(0))[Modal];
    ChildSlot [SNew(SDPIScaler).DPIScale_Lambda([this]{return GetCachedGeometry().GetLocalSize().Y>0?float(GetCachedGeometry().GetLocalSize().Y/768):1.f;})[Canvas]];
    if(FSlateApplication::IsInitialized()) FSlateApplication::Get().SetKeyboardFocus(NameBox,EFocusCause::SetDirectly);
}
bool SWowCharacterCreateWidget::PressStockControl(FName Name)
{
    const auto KeepAlive=AsShared();
    const auto* Weak=StockControls.Find(Name);const auto Button=Weak?Weak->Pin():nullptr;
    if(!Button || !Button->IsEnabled()) return false;
    const FKeyEvent Event(EKeys::SpaceBar,FModifierKeysState(),0,false,0,0);
    Button->OnKeyDown(Button->GetCachedGeometry(),Event);Button->OnKeyUp(Button->GetCachedGeometry(),Event);return true;
}
void SWowCharacterCreateWidget::Tick(const FGeometry& G,double Time,float DT)
{
    SCompoundWidget::Tick(G,Time,DT);
    if (!bDataReady && FDbcStore::Get().IsDeferredLoaded()) {bDataReady=true; if(Art) BuildStockLayout(); Refresh();}
}
void SWowCharacterCreateWidget::SetPreview(AWowCharacterPreview* P)
{
    Preview=P;
    if(P && P->GetWorldManager()) {Art=MakeShared<FWowCharacterCreationArt>(P->GetWorldManager()->GetMpqManager()); if(bDataReady) BuildStockLayout();}
    if (bDataReady) Refresh();
}
uint32 SWowCharacterCreateWidget::MemoryKey() const { return (Appearance.Race<<2)|(Appearance.Gender<<1)|(Appearance.Class==6); }
uint8& SWowCharacterCreateWidget::Value(EWowAppearanceOption Option)
{
    switch(Option) {case EWowAppearanceOption::Skin:return Appearance.Skin; case EWowAppearanceOption::Face:return Appearance.Face;
        case EWowAppearanceOption::HairStyle:return Appearance.HairStyle; case EWowAppearanceOption::HairColor:return Appearance.HairColor;
        default:return Appearance.FacialHair;}
}
TArray<uint8> SWowCharacterCreateWidget::GetOptions(EWowAppearanceOption Option) const
{
    const auto& D=FDbcStore::Get();
    return bDataReady?FCharacterCreationDbc::Options(Option,Appearance,D.CharSections(),D.CharacterFacialHairStyles()):TArray<uint8>();
}
void SWowCharacterCreateWidget::SelectIdentity(int32 Field,uint8 Selected)
{
    if(bSubmitting || !bDataReady) return;
    const auto& D=FDbcStore::Get().CharacterCreation();
    if((Field==0 && !D.Races().Contains(Selected)) || (Field==1 && !D.Allows(Appearance.Race,Selected)) || (Field==2 && Selected>1)) return;
    Remembered.Add(MemoryKey(),Appearance);
    if(Field==0) Appearance.Race=Selected;else if(Field==1) Appearance.Class=Selected;else Appearance.Gender=Selected;
    if(!D.Allows(Appearance.Race,Appearance.Class)) {const auto Classes=D.Classes(Appearance.Race);if(Classes.Num()) Appearance.Class=Classes[0];}
    if(const auto* Saved=Remembered.Find(MemoryKey())) {const uint8 Class=Appearance.Class;Appearance=*Saved;Appearance.Class=Class;}
    Refresh();
}
void SWowCharacterCreateWidget::ChangeIdentity(int32 Field,int32 Delta)
{
    if(bSubmitting || !bDataReady) return;
    const auto& D=FDbcStore::Get().CharacterCreation();
    const auto Values=Field==0?D.Races():Field==1?D.Classes(Appearance.Race):TArray<uint8>({0,1});
    const uint8 Current=Field==0?Appearance.Race:Field==1?Appearance.Class:Appearance.Gender;
    if(Values.Num()) SelectIdentity(Field,Values[(FMath::Max(0,Values.IndexOfByKey(Current))+Values.Num()+Delta)%Values.Num()]);
}
FReply SWowCharacterCreateWidget::OnKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(E.GetKey()==EKeys::Escape)
    {
        if(!bSubmitting)
        {
            if(StatusLabel && !StatusLabel->GetText().IsEmpty()) SetStatusText(TEXT(""));
            else OnBackToCharSelect.ExecuteIfBound();
        }
        return FReply::Handled();
    }
    return SCompoundWidget::OnKeyDown(G,E);
}
void SWowCharacterCreateWidget::ChangeOption(EWowAppearanceOption Option,int32 Delta)
{
    if (bSubmitting) return;
    const auto Values=GetOptions(Option);
    if(Values.Num()) Value(Option)=Values[(FMath::Max(0,Values.IndexOfByKey(Value(Option)))+Values.Num()+Delta)%Values.Num()];
    Refresh();
}
void SWowCharacterCreateWidget::SetAppearance(FWowCharacterAppearance A) {if(!bSubmitting) {Appearance=A; Refresh();}}
void SWowCharacterCreateWidget::Refresh()
{
    if(!bDataReady) return;
    const auto& D=FDbcStore::Get();
    bValid=D.CharacterCreation().Allows(Appearance.Race,Appearance.Class) && Appearance.Gender<2 &&
        FCharacterCreationDbc::Normalize(Appearance,D.CharSections(),D.CharacterFacialHairStyles()) &&
        D.CharStartOutfit().GetByRaceClassGender(Appearance.Race,Appearance.Class,Appearance.Gender);
    SetStatusText(bValid?TEXT(""):TEXT("Character data unavailable for this selection."));
    if(Preview.IsValid())
    {
        uint8 SceneRace=Appearance.Race==7?3:Appearance.Race==8?2:Appearance.Race;
        const auto* R=D.ChrRaces().GetById(SceneRace);
        const FString Token=Appearance.Class==6?D.CharacterCreation().ClassToken(6):R?R->ClientFileString:FString();
        if(Art) Preview->SetOriginalLighting(Art->Lights(Token.ToUpper()));
        if(bValid) Preview->ShowCreation(Appearance);else Preview->ClearCharacter();
    }
}
void SWowCharacterCreateWidget::SetName(const FString& Name) {if(NameBox && !bSubmitting) NameBox->SetText(FText::FromString(Name));}
void SWowCharacterCreateWidget::Submit()
{
    if(bSubmitting || !bValid || !OnCharacterCreated.IsBound() || (Preview.IsValid() && !Preview->IsReady())) return;
    const FString Name=NameBox->GetText().ToString().TrimStartAndEnd();
    if(Name.IsEmpty()) {SetStatusText(TEXT("Please enter a character name.")); return;}
    bSubmitting=true; SetStatusText(TEXT("Creating character..."));
    const auto A=Appearance;
    OnCharacterCreated.Execute(Name,A.Race,A.Class,A.Gender,A.Skin,A.Face,A.HairStyle,A.HairColor,A.FacialHair);
}
void SWowCharacterCreateWidget::CompleteRequest(const FString& Text,bool Success) {bSubmitting=Success; SetStatusText(Text);}
void SWowCharacterCreateWidget::SetStatusText(const FString& Text) {if(StatusLabel) StatusLabel->SetText(FText::FromString(Text));}
