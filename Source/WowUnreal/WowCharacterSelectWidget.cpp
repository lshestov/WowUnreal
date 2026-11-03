#include "WowCharacterSelectWidget.h"
#include "WowCharacterPreview.h"
#include "WowCharacterCreationArt.h"
#include "WowWorldManager.h"
#include "WowBackdropWidget.h"
#include "WowTextBlock.h"
#include "Formats/Dbc/DbcStore.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"

namespace
{
class SSelectionRow : public SButton
{
public:
    SLATE_BEGIN_ARGS(SSelectionRow) {} SLATE_EVENT(FOnClicked,OnClicked) SLATE_EVENT(FOnClicked,OnActivate) SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
    void Construct(const FArguments& A) {Activate=A._OnActivate;SButton::Construct(SButton::FArguments().ButtonStyle(FCoreStyle::Get(),"NoBorder").ContentPadding(0).OnClicked(A._OnClicked)[A._Content.Widget]);}
    virtual FReply OnMouseButtonDoubleClick(const FGeometry&,const FPointerEvent& E) override {return E.GetEffectingButton()==EKeys::LeftMouseButton && Activate.IsBound()?Activate.Execute():FReply::Unhandled();}
private:
    FOnClicked Activate;
};
class SSelectionViewport : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SSelectionViewport) {} SLATE_ARGUMENT(AWowCharacterPreview*,Preview) SLATE_END_ARGS()
    void Construct(const FArguments& A) {Preview=A._Preview;Brush.SetResourceObject(A._Preview->GetRenderTarget());Brush.DrawAs=ESlateBrushDrawType::Image;ChildSlot[SNew(SImage).Image(&Brush)];}
    virtual void Tick(const FGeometry& G,double T,float DT) override {SCompoundWidget::Tick(G,T,DT);if(Preview.IsValid() && G.GetLocalSize().Y>0) Preview->SetViewAspect(G.GetLocalSize().X/G.GetLocalSize().Y);}
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& E) override {return E.GetEffectingButton()==EKeys::LeftMouseButton?FReply::Handled().CaptureMouse(AsShared()):FReply::Unhandled();}
    virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent& E) override {return E.GetEffectingButton()==EKeys::LeftMouseButton?FReply::Handled().ReleaseMouseCapture():FReply::Unhandled();}
    virtual FReply OnMouseMove(const FGeometry&,const FPointerEvent& E) override {if(HasMouseCapture() && Preview.IsValid()){Preview->Rotate(E.GetCursorDelta().X*.5f);return FReply::Handled();}return FReply::Unhandled();}
    virtual FReply OnMouseWheel(const FGeometry&,const FPointerEvent& E) override {if(Preview.IsValid()) Preview->Zoom(E.GetWheelDelta());return FReply::Handled();}
private:
    TWeakObjectPtr<AWowCharacterPreview> Preview;
    FSlateBrush Brush;
};
}

void SWowCharacterSelectWidget::Construct(const FArguments&) {ChildSlot[SNew(STextBlock).Text(FText::FromString(TEXT("Loading character data...")))];}
SWowCharacterSelectWidget::~SWowCharacterSelectWidget()=default;
const FWowCharacterInfo* SWowCharacterSelectWidget::Selected() const {return CachedCharacters.FindByPredicate([this](const auto& C){return C.Guid==SelectedGuid;});}
void SWowCharacterSelectWidget::SetPreview(AWowCharacterPreview* P,const FString& Realm)
{
    Preview=P;RealmName=Realm;
    if(!P || !P->GetWorldManager()) return;
    P->EnableCreationScene();Art=MakeUnique<FWowCharacterCreationArt>(P->GetWorldManager()->GetMpqManager());
    bDataReady=FDbcStore::Get().IsDeferredLoaded();BuildStockLayout();RefreshPreview();
}
void SWowCharacterSelectWidget::PopulateCharacters(const TArray<FWowCharacterInfo>& Characters,int64 PreferredGuid)
{
    CachedCharacters=Characters;if(CachedCharacters.Num()>10) CachedCharacters.SetNum(10);
    if(PreferredGuid) SelectedGuid=PreferredGuid;
    if(!Selected()) SelectedGuid=CachedCharacters.IsEmpty()?0:CachedCharacters[0].Guid;
    bBusy=false;DeleteGuid=0;Status.Reset();if(Art){BuildStockLayout();RefreshPreview();}
}
void SWowCharacterSelectWidget::Tick(const FGeometry& G,double T,float DT)
{
    SCompoundWidget::Tick(G,T,DT);
    if(!bDataReady && FDbcStore::Get().IsDeferredLoaded()){bDataReady=true;if(Art){BuildStockLayout();RefreshPreview();}}
}
void SWowCharacterSelectWidget::RefreshPreview()
{
    if(!Preview.IsValid() || !Art || !bDataReady) return;
    const auto* C=Selected();uint8 Race=C?C->Race:1;Race=Race==7?3:Race==8?2:Race;
    const auto& D=FDbcStore::Get();const auto* R=D.ChrRaces().GetById(Race);
    const FString Token=C && C->Class==6?D.CharacterCreation().ClassToken(6):R?R->ClientFileString:TEXT("Human");
    Preview->SetOriginalLighting(Art->Lights(Token.ToUpper()));Preview->ResetCamera();
    if(C) Preview->ShowCharacter(*C);else Preview->ShowEmptyScene();
}
bool SWowCharacterSelectWidget::HighlightCharacter(int64 Guid)
{
    if(bBusy || DeleteGuid || !CachedCharacters.ContainsByPredicate([Guid](const auto& C){return C.Guid==Guid;})) return false;
    SelectedGuid=Guid;Status.Reset();RefreshPreview();OnCharacterHighlighted.ExecuteIfBound(Guid);return true;
}
bool SWowCharacterSelectWidget::EnterSelected()
{
    if(bBusy || DeleteGuid || !Selected() || !OnCharacterSelected.IsBound()) return false;
    bBusy=true;OnCharacterSelected.Execute(SelectedGuid);return true;
}
TSharedRef<SWidget> SWowCharacterSelectWidget::Backdrop(const FWowBackdrop& D)
{
    auto* W=NewObject<UWowBackdropWidget>();Backdrops.Add(TStrongObjectPtr<UWowBackdropWidget>(W));
    W->Configure(D,Cast<UTexture2D>(Art->Brush(D.BgFile)->GetResourceObject()),Cast<UTexture2D>(Art->Brush(D.EdgeFile)->GetResourceObject()),1);
    return W->TakeWidget();
}
void SWowCharacterSelectWidget::BuildStockLayout()
{
    StockControls.Reset();Backdrops.Reset();const FLinearColor Gold(1,.82f,0);auto Canvas=SNew(SConstraintCanvas);
    auto Put=[&](TSharedRef<SWidget> W,float X,float Y,float Width,float Height,float AX=0,float AY=0)
    {Canvas->AddSlot().Anchors(FAnchors(AX,AY)).Alignment(FVector2D::ZeroVector).Offset(FMargin(X,Y,Width,Height))[W];};
    auto Font=[this](int32 Size){auto F=Art->Font(Size);F.Size=Size;F.OutlineSettings=FFontOutlineSettings(1);return F;};
    auto Label=[&](const FString& Key,int32 Size=12){return SNew(STextBlock).Text(FText::FromString(Art->String(Key))).Font(Font(Size)).ColorAndOpacity(Gold).ShadowOffset(FVector2D(1,1)).Justification(ETextJustify::Center);};
    auto Click=[&](FName Name,const FString& Key,FOnClicked Action,int32 FontSize=12)
    {auto B=SNew(SButton).ButtonStyle(Art->Button(TEXT("Interface\\Glues\\Common\\Glue-Panel-Button"),true)).ContentPadding(0).HAlign(HAlign_Center).VAlign(VAlign_Center).OnClicked(Action)[Label(Key,FontSize)];StockControls.Add(Name,B);return B;};
    auto Enabled=[&](TSharedRef<SWidget> W){return SNew(SBox).IsEnabled_Lambda([this]{return !bBusy && !DeleteGuid;})[W];};
    // The modal intercepts input. Disabling the image would also gray out the
    // original animated scene through Slate's disabled paint effect.
    Canvas->AddSlot().Anchors(FAnchors(0,0,1,1)).Offset(FMargin(0))[SNew(SSelectionViewport).Preview(Preview.Get())];
    Put(SNew(SImage).Image(Art->Brush(TEXT("Interface\\Glues\\Common\\Glues-WoW-WotLKLogo"))).Visibility(EVisibility::HitTestInvisible),3,7,256,128);
    FWowBackdrop ListBackdrop;ListBackdrop.BgFile=TEXT("Interface\\Glues\\Common\\Glue-Tooltip-Background");ListBackdrop.EdgeFile=TEXT("Interface\\Glues\\Common\\Glue-Tooltip-Border");
    ListBackdrop.EdgeSize=16;ListBackdrop.TileSize=16;ListBackdrop.Tile=true;ListBackdrop.InsetLeft=10;ListBackdrop.InsetRight=5;ListBackdrop.InsetTop=4;ListBackdrop.InsetBottom=9;
    ListBackdrop.Color=FLinearColor(.09f,.09f,.19f,.85f);ListBackdrop.BorderColor=FLinearColor(.5f,.5f,.5f,1);
    auto CharacterPanel=Backdrop(ListBackdrop);CharacterPanel->SetVisibility(EVisibility::HitTestInvisible);Put(CharacterPanel,-265,15,260,654,1);
    const auto RealmFont=Font(18);FString DisplayRealm=RealmName;const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    if(Measure->Measure(RealmName,RealmFont).X>240)
    {
        const FString Ellipsis=TEXT("\u2026");const int32 Width=FMath::FloorToInt(240-Measure->Measure(Ellipsis,RealmFont).X);
        DisplayRealm=RealmName.Left(Measure->FindLastWholeCharacterIndexBeforeOffset(RealmName,RealmFont,Width)+1)+Ellipsis;
    }
    Put(SNew(STextBlock).Text(FText::FromString(DisplayRealm)).ToolTipText(FText::FromString(RealmName)).Font(RealmFont).ColorAndOpacity(FLinearColor(.5f,.5f,.5f)).Justification(ETextJustify::Center)
        .Clipping(EWidgetClipping::ClipToBounds),-257,22,244,24,1);
    Put(Enabled(Click(TEXT("ChangeRealm"),TEXT("CHANGE_REALM"),FOnClicked::CreateLambda([this]{OnChangeRealmRequest.ExecuteIfBound();return FReply::Handled();}))),-217,52,164,26,1);
    for(int32 I=0;I<CachedCharacters.Num();++I)
    {
        const auto C=CachedCharacters[I];const FName Key(*FString::Printf(TEXT("Character_%lld"),C.Guid));
        auto Highlight=SNew(SImage).Image(Art->Brush(TEXT("Interface\\Glues\\CharacterSelect\\Glue-CharacterSelect-Highlight"),FVector2f(0),FVector2f(1),ESlateBrushMirrorType::NoMirror,true))
            .Visibility_Lambda([this,Guid=C.Guid,Key]{const auto* Weak=StockControls.Find(Key);const auto B=Weak?Weak->Pin():nullptr;return SelectedGuid==Guid || (B && B->IsHovered())?EVisibility::HitTestInvisible:EVisibility::Collapsed;});
        Put(Highlight,-261,80+I*57,256,74,1);
        const auto& D=FDbcStore::Get();const FString Class=D.CharacterCreation().ClassName(C.Class);
        const FString Info=Art->String(TEXT("CHARACTER_SELECT_INFO")).Replace(TEXT("%d"),*FString::FromInt(C.Level)).Replace(TEXT("%s"),*Class);const auto* Area=D.AreaTable().GetById(C.ZoneId);
        auto Row=SNew(SSelectionRow).OnClicked_Lambda([this,Guid=C.Guid]{HighlightCharacter(Guid);return FReply::Handled();})
            .OnActivate_Lambda([this,Guid=C.Guid]{HighlightCharacter(Guid);EnterSelected();return FReply::Handled();})
            [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(20).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(C.Name)).Font(Font(15)).ColorAndOpacity(Gold).ShadowOffset(FVector2D(1,1))]]
            +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(16).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Info)).Font(Font(12)).ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(1,1))]]
            +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(16).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Area?Area->Name:FString())).Font(Font(12)).ColorAndOpacity(FLinearColor(.5f,.5f,.5f)).ShadowOffset(FVector2D(1,1))]]];
        StockControls.Add(Key,Row);Put(Enabled(Row),-241,93+I*57,217,52,1);
    }
    auto Create=Click(TEXT("Create"),TEXT("CREATE_NEW_CHARACTER"),FOnClicked::CreateLambda([this]{OnCreateCharacterRequest.ExecuteIfBound();return FReply::Handled();}));
    Create->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]{return CachedCharacters.Num()<10?EVisibility::Visible:EVisibility::Collapsed;}));Put(Enabled(Create),-235,616,200,38,1);
    Put(SNew(STextBlock).Text_Lambda([this]{const auto* C=Selected();return FText::FromString(C?C->Name:FString());}).Font(Font(17)).ColorAndOpacity(Gold).ShadowOffset(FVector2D(1,1)).Justification(ETextJustify::Center),-260,-147,520,31,.5f,1);
    auto Enter=Click(TEXT("EnterWorld"),TEXT("ENTER_WORLD"),FOnClicked::CreateLambda([this]{EnterSelected();return FReply::Handled();}),15);
    Enter->SetEnabled(TAttribute<bool>::CreateLambda([this]{return !bBusy && !DeleteGuid && Selected();}));Put(Enter,-100,-110,200,60,.5f,1);
    const FString RotationBase=TEXT("Interface\\Glues\\CharacterCreate\\UI-RotationRight-Big");
    RotationStyle=MakeShared<FButtonStyle>(*Art->Button(RotationBase));
    // Remove transparent texture padding from the button's layout/hit area.
    const FVector2f RotationMin(9.f/64,10.f/64),RotationMax(55.f/64,55.f/64);
    RotationStyle->SetNormal(*Art->Brush(RotationBase+TEXT("-Up"),RotationMin,RotationMax));
    RotationStyle->SetHovered(RotationStyle->Normal).SetDisabled(RotationStyle->Normal);
    RotationStyle->SetPressed(*Art->Brush(RotationBase+TEXT("-Down"),RotationMin,RotationMax));
    for(int32 Direction:{-1,1})
    {
        auto B=SNew(SButton).ButtonStyle(RotationStyle.Get()).ContentPadding(0)
            .OnClicked_Lambda([this,Direction]{if(Preview.IsValid()) Preview->Rotate(Direction*20);return FReply::Handled();});
        if(Direction<0){B->SetRenderTransformPivot(FVector2D(.5,.5));B->SetRenderTransform(FSlateRenderTransform(FScale2D(-1,1)));}
        Put(Enabled(B),Direction<0?-39:3,-42,36,35,.5f,1);
    }
    Put(Enabled(Click(TEXT("Back"),TEXT("BACK"),FOnClicked::CreateLambda([this]{OnBackRequest.ExecuteIfBound();return FReply::Handled();}))),-130,-60,100,35,1,1);
    auto Delete=Click(TEXT("Delete"),TEXT("DELETE_CHARACTER"),FOnClicked::CreateLambda([this]{OpenDeleteDialog();return FReply::Handled();}));
    Delete->SetEnabled(TAttribute<bool>::CreateLambda([this]{return !bBusy && !DeleteGuid && Selected() && OnDeleteCharacterRequest.IsBound();}));Put(Delete,-295,-60,165,35,1,1);
    DeleteStyle=MakeShared<FEditableTextBoxStyle>(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"));FSlateBrush Empty;Empty.DrawAs=ESlateBrushDrawType::NoDrawType;
    DeleteStyle->SetBackgroundImageNormal(Empty).SetBackgroundImageHovered(Empty).SetBackgroundImageFocused(Empty).SetPadding(FMargin(0));
    const FString PanelBase=TEXT("Interface\\Glues\\Common\\Glue-Panel-Button");
    DialogButtonStyle=MakeShared<FButtonStyle>(*Art->Button(PanelBase,true));
    DialogButtonStyle->SetDisabled(*Art->Brush(PanelBase+TEXT("-Disabled"),FVector2f(0),FVector2f(.578125f,.75f)));
    auto Confirm=Click(TEXT("ConfirmDelete"),TEXT("OKAY"),FOnClicked::CreateLambda([this]{ConfirmDelete();return FReply::Handled();}),15);
    Confirm->SetButtonStyle(DialogButtonStyle.Get());Confirm->SetContentPadding(FMargin(0,0,0,4));
    Confirm->SetContent(SNew(STextBlock).Text(FText::FromString(Art->String(TEXT("OKAY")))).Font(Font(15)).ShadowOffset(FVector2D(1,1))
        .ColorAndOpacity_Lambda([this,Gold]{return CanConfirmDelete()?Gold:FLinearColor(.5f,.5f,.5f); }));
    Confirm->SetEnabled(TAttribute<bool>::CreateLambda([this]{return CanConfirmDelete();}));
    Confirm->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]{return DeleteGuid?EVisibility::Visible:EVisibility::Collapsed;}));
    auto Dismiss=Click(TEXT("Dismiss"),TEXT("CANCEL"),FOnClicked::CreateLambda([this]{DeleteGuid=0;Status.Reset();return FReply::Handled();}),15);
    Dismiss->SetButtonStyle(DialogButtonStyle.Get());Dismiss->SetContentPadding(FMargin(0,0,0,4));
    Dismiss->SetContent(SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Art->String(DeleteGuid?TEXT("CANCEL"):TEXT("OKAY")));})
        .Font(Font(15)).ColorAndOpacity(Gold).ShadowOffset(FVector2D(1,1)).Justification(ETextJustify::Center));
    Dismiss->SetEnabled(TAttribute<bool>::CreateLambda([this]{return !bBusy;}));
    // CharacterDeleteBackground / CharacterDeleteDialog_OnShow from GlueXML.
    FWowBackdrop DialogBackdrop;DialogBackdrop.BgFile=TEXT("Interface\\DialogFrame\\UI-DialogBox-Background");DialogBackdrop.EdgeFile=TEXT("Interface\\DialogFrame\\UI-DialogBox-Border");
    DialogBackdrop.Tile=true;DialogBackdrop.TileSize=32;DialogBackdrop.EdgeSize=32;DialogBackdrop.InsetLeft=11;DialogBackdrop.InsetRight=12;DialogBackdrop.InsetTop=12;DialogBackdrop.InsetBottom=11;
    auto Dialog=SNew(SConstraintCanvas);
    Dialog->AddSlot().Anchors(FAnchors(0,0,1,1)).Offset(FMargin(0))[Backdrop(DialogBackdrop)];
    DialogTitle.Reset(NewObject<UWowTextBlock>());DialogTitle->InitializeLabel(nullptr,1);DialogTitle->SetFont(Font(18));DialogTitle->SetColorAndOpacity(Gold);
    DialogTitle->SetJustification(ETextJustify::Center);DialogTitle->SetVerticalJustification(TEXT("TOP"));DialogTitle->SetWrapTextAt(400);DialogTitle->SetShadowOffset(FVector2D(2,2));DialogTitle->SetPixelSpacing(2);
    DialogTitleSlate=DialogTitle->TakeWidget();
    Dialog->AddSlot().Anchors(FAnchors(.5f,0)).Alignment(FVector2D(.5f,0)).AutoSize(true).Offset(FMargin(0,16,0,0))
        [SNew(SBox).WidthOverride_Lambda([this]{return DeleteGuid?400.f:450.f;})[DialogTitleSlate.ToSharedRef()]];
    Dialog->AddSlot().Anchors(FAnchors(.5f,0)).Alignment(FVector2D(.5f,0)).AutoSize(true)
        .Offset_Lambda([this]{return FMargin(0,36+DialogTitleSlate->GetDesiredSize().Y,0,0);})
        [SNew(SBox).WidthOverride(400).Visibility_Lambda([this]{return DeleteGuid?EVisibility::Visible:EVisibility::Collapsed;})
            [SAssignNew(DialogInstructions,STextBlock).Text(FText::FromString(Art->String(TEXT("CONFIRM_CHAR_DELETE_INSTRUCTIONS")))).Font(Font(12)).WrapTextAt(400).Justification(ETextJustify::Center).ColorAndOpacity(Gold).ShadowOffset(FVector2D(1,1))]];
    Dialog->AddSlot().Anchors(FAnchors(0,.5f)).Alignment(FVector2D(0,.5f)).Offset(FMargin(24,-10,48,48))
        [SNew(SImage).Image(Art->Brush(TEXT("Interface\\DialogFrame\\UI-Dialog-Icon-AlertNew"))).Visibility_Lambda([this]{return DeleteGuid?EVisibility::HitTestInvisible:EVisibility::Collapsed;})];
    auto Edit=SNew(SConstraintCanvas);
    Edit->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(-10,0,75,32))[SNew(SImage).Image(Art->Brush(TEXT("Interface\\ChatFrame\\UI-ChatInputBorder-Left"),FVector2f(0),FVector2f(.29296875f,1)))];
    Edit->AddSlot().Alignment(FVector2D::ZeroVector).Offset(FMargin(65,0,75,32))[SNew(SImage).Image(Art->Brush(TEXT("Interface\\ChatFrame\\UI-ChatInputBorder-Right"),FVector2f(.70703125f,0),FVector2f(1)))];
    Edit->AddSlot().Anchors(FAnchors(0,0,1,1)).Offset(FMargin(0))
        [SAssignNew(DeleteBox,SEditableTextBox).Style(DeleteStyle.Get()).Font(Font(15)).ForegroundColor(FLinearColor::White)
            .OnVerifyTextChanged_Lambda([](const FText& Text,FText&){return Text.ToString().Len()<=32;})];
    Dialog->AddSlot().Anchors(FAnchors(.5f,0)).Alignment(FVector2D(.5f,0))
        .Offset_Lambda([this]{return FMargin(0,41+DialogTitleSlate->GetDesiredSize().Y+DialogInstructions->GetDesiredSize().Y,130,32);})
        [SNew(SBox).Visibility_Lambda([this]{return DeleteGuid?EVisibility::Visible:EVisibility::Collapsed;})[Edit]];
    Dialog->AddSlot().Anchors(FAnchors(.5f,1)).Alignment(FVector2D::ZeroVector).Offset(FMargin(-226,-56,220,40))[Confirm];
    Dialog->AddSlot().Anchors(FAnchors(.5f,1)).Alignment(FVector2D::ZeroVector).Offset_Lambda([this]{return FMargin(DeleteGuid?7:-110,-56,220,40);})[Dismiss];
    UpdateDialogText();DialogInstructions->SlatePrepass(1);
    auto Modal=SNew(SOverlay)+SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0,0,0,0)).OnMouseButtonDown_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(SBox).WidthOverride(512).HeightOverride_Lambda([this]{return DialogHeight();})[Dialog]];
    Modal->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]{return Status.IsEmpty()?EVisibility::Collapsed:EVisibility::Visible;}));
    Canvas->AddSlot().Anchors(FAnchors(0,0,1,1)).Offset(FMargin(0))[Modal];
    ChildSlot[SNew(SDPIScaler).DPIScale_Lambda([this]{return GetCachedGeometry().GetLocalSize().Y>0?float(GetCachedGeometry().GetLocalSize().Y/768):1.f;})[Canvas]];
}
void SWowCharacterSelectWidget::OpenDeleteDialog()
{
    if(bBusy || DeleteGuid || !Selected() || !OnDeleteCharacterRequest.IsBound()) return;
    const auto C=*Selected();DeleteGuid=C.Guid;DeleteBox->SetText(FText::GetEmpty());
    FString Format=Art->String(TEXT("CONFIRM_CHAR_DELETE"));
    const int32 NameAt=Format.Find(TEXT("%s"));if(NameAt!=INDEX_NONE) Format=Format.Left(NameAt)+C.Name+Format.Mid(NameAt+2);
    Status=Format.Replace(TEXT("%d"),*FString::FromInt(C.Level)).Replace(TEXT("%s"),*FDbcStore::Get().CharacterCreation().ClassName(C.Class));UpdateDialogText();
    FSlateApplication::Get().SetKeyboardFocus(DeleteBox,EFocusCause::SetDirectly);
}
bool SWowCharacterSelectWidget::CanConfirmDelete() const {return !bBusy && DeleteGuid && SelectedGuid==DeleteGuid && Selected() && OnDeleteCharacterRequest.IsBound() && DeleteBox && Art && !Art->String(TEXT("DELETE_CONFIRM_STRING")).IsEmpty() && DeleteBox->GetText().ToString().Equals(Art->String(TEXT("DELETE_CONFIRM_STRING")),ESearchCase::IgnoreCase);}
void SWowCharacterSelectWidget::ConfirmDelete()
{
    if(!CanConfirmDelete()) return;const int64 Guid=DeleteGuid;DeleteGuid=0;bBusy=true;Status=Art->String(TEXT("CHAR_DELETE_IN_PROGRESS"));UpdateDialogText();OnDeleteCharacterRequest.ExecuteIfBound(Guid);
}
void SWowCharacterSelectWidget::SetDeleteConfirmationText(const FString& Text) {if(DeleteBox && DeleteGuid) DeleteBox->SetText(FText::FromString(Text));}
void SWowCharacterSelectWidget::CompleteDeletion(bool Success,const FString& Message) {if(!Success) bBusy=false;DeleteGuid=0;Status=Message;UpdateDialogText();}
void SWowCharacterSelectWidget::SetStatusText(const FString& Text) {Status=Text;UpdateDialogText();}
void SWowCharacterSelectWidget::UpdateDialogText()
{
    if(!DialogTitle.IsValid() || !DialogTitleSlate) return;
    DialogTitle->SetWrapTextAt(DeleteGuid?400:450);DialogTitle->SetText(FText::FromString(Status));DialogTitleSlate->SlatePrepass(1);
}
float SWowCharacterSelectWidget::DialogHeight() const
{
    const float Title=DialogTitleSlate?DialogTitleSlate->GetDesiredSize().Y:0;
    return DeleteGuid?135+Title+(DialogInstructions?DialogInstructions->GetDesiredSize().Y:0):80+Title;
}
bool SWowCharacterSelectWidget::PressStockControl(FName Name,bool DoubleClick)
{
    const auto KeepAlive=AsShared();if(bBusy || (DeleteGuid && Name!=TEXT("ConfirmDelete") && Name!=TEXT("Dismiss")) || (!DeleteGuid && !Status.IsEmpty() && Name!=TEXT("Dismiss"))) return false;
    const auto* Weak=StockControls.Find(Name);const auto B=Weak?Weak->Pin():nullptr;if(!B || !B->IsEnabled() || B->GetVisibility()!=EVisibility::Visible) return false;
    if(DoubleClick){const FPointerEvent E(0,FVector2D::ZeroVector,FVector2D::ZeroVector,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());B->OnMouseButtonDoubleClick(B->GetCachedGeometry(),E);return true;}
    const FKeyEvent E(EKeys::SpaceBar,FModifierKeysState(),0,false,0,0);B->OnKeyDown(B->GetCachedGeometry(),E);B->OnKeyUp(B->GetCachedGeometry(),E);return true;
}
bool SWowCharacterSelectWidget::FocusStockControl(FName Name)
{
    const auto* Weak=StockControls.Find(Name);const auto B=Weak?Weak->Pin():nullptr;
    if(!B || !B->IsEnabled() || B->GetVisibility()!=EVisibility::Visible) return false;
    FSlateApplication::Get().SetKeyboardFocus(B,EFocusCause::Navigation);return B->HasKeyboardFocus();
}
FReply SWowCharacterSelectWidget::OnPreviewKeyDown(const FGeometry&,const FKeyEvent& E)
{
    if(bBusy) return FReply::Handled();
    if(E.GetKey()==EKeys::Escape){if(DeleteGuid || !Status.IsEmpty()){DeleteGuid=0;Status.Reset();}else OnBackRequest.ExecuteIfBound();return FReply::Handled();}
    if(E.GetKey()==EKeys::Enter)
    {
        const auto* Weak=StockControls.Find(TEXT("Dismiss"));const auto Dismiss=Weak?Weak->Pin():nullptr;
        if(DeleteGuid && Dismiss && Dismiss->HasKeyboardFocus()){DeleteGuid=0;Status.Reset();}
        else if(DeleteGuid) ConfirmDelete();else if(Status.IsEmpty()) EnterSelected();else Status.Reset();
        return FReply::Handled();
    }
    if(!DeleteGuid && Status.IsEmpty() && (E.GetKey()==EKeys::Up || E.GetKey()==EKeys::Down) && CachedCharacters.Num())
    {const int32 Index=CachedCharacters.IndexOfByPredicate([this](const auto& C){return C.Guid==SelectedGuid;});HighlightCharacter(CachedCharacters[FMath::Clamp(Index+(E.GetKey()==EKeys::Down?1:-1),0,CachedCharacters.Num()-1)].Guid);return FReply::Handled();}
    return FReply::Unhandled();
}
