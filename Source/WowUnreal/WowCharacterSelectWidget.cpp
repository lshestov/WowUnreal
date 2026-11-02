#include "WowCharacterSelectWidget.h"
#include "WowCharacterPreview.h"
#include "WowCharacterCreationArt.h"
#include "WowWorldManager.h"
#include "WowBackdropWidget.h"
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
TSharedRef<SWidget> SWowCharacterSelectWidget::Backdrop(float Width,float Height)
{
    auto* W=NewObject<UWowBackdropWidget>();Backdrops.Add(TStrongObjectPtr<UWowBackdropWidget>(W));
    FWowBackdrop D;D.BgFile=TEXT("Interface\\Tooltips\\UI-Tooltip-Background");D.EdgeFile=TEXT("Interface\\Glues\\Common\\Glue-Tooltip-Border");
    D.EdgeSize=16;D.TileSize=16;D.Tile=true;D.InsetLeft=10;D.InsetRight=5;D.InsetTop=4;D.InsetBottom=9;
    W->Configure(D,Cast<UTexture2D>(Art->Brush(D.BgFile)->GetResourceObject()),Cast<UTexture2D>(Art->Brush(D.EdgeFile)->GetResourceObject()),1);
    return SNew(SBox).WidthOverride(Width).HeightOverride(Height)[W->TakeWidget()];
}
void SWowCharacterSelectWidget::BuildStockLayout()
{
    StockControls.Reset();Backdrops.Reset();const FLinearColor Gold(1,.82f,0);auto Canvas=SNew(SConstraintCanvas);
    auto Put=[&](TSharedRef<SWidget> W,float X,float Y,float Width,float Height,float AX=0,float AY=0)
    {Canvas->AddSlot().Anchors(FAnchors(AX,AY)).Alignment(FVector2D::ZeroVector).Offset(FMargin(X,Y,Width,Height))[W];};
    auto Label=[&](const FString& Key,int32 Size=13){return SNew(STextBlock).Text(FText::FromString(Art->String(Key))).Font(Art->Font(Size)).ColorAndOpacity(Gold).ShadowOffset(FVector2D(1,1)).Justification(ETextJustify::Center);};
    auto Click=[&](FName Name,const FString& Key,FOnClicked Action)
    {auto B=SNew(SButton).ButtonStyle(Art->Button(TEXT("Interface\\Glues\\Common\\Glue-Panel-Button"),true)).ContentPadding(0).HAlign(HAlign_Center).VAlign(VAlign_Center).OnClicked(Action)[Label(Key)];StockControls.Add(Name,B);return B;};
    auto Enabled=[&](TSharedRef<SWidget> W){return SNew(SBox).IsEnabled_Lambda([this]{return !bBusy && !DeleteGuid;})[W];};
    Canvas->AddSlot().Anchors(FAnchors(0,0,1,1)).Offset(FMargin(0))[SNew(SSelectionViewport).Preview(Preview.Get()).IsEnabled_Lambda([this]{return !bBusy && !DeleteGuid;})];
    Put(SNew(SImage).Image(Art->Brush(TEXT("Interface\\Glues\\Common\\Glues-WoW-WotLKLogo"))).Visibility(EVisibility::HitTestInvisible),3,7,256,128);
    Put(SNew(STextBlock).Text(FText::FromString(RealmName)).Font(Art->Font(18)).ColorAndOpacity(FLinearColor(.5f,.5f,.5f)).Justification(ETextJustify::Center),-257,25,244,25,1);
    Put(Enabled(Click(TEXT("ChangeRealm"),TEXT("CHANGE_REALM"),FOnClicked::CreateLambda([this]{OnChangeRealmRequest.ExecuteIfBound();return FReply::Handled();}))),-217,40,164,33,1);
    for(int32 I=0;I<CachedCharacters.Num();++I)
    {
        const auto C=CachedCharacters[I];const FName Key(*FString::Printf(TEXT("Character_%lld"),C.Guid));
        auto Highlight=SNew(SImage).Image(Art->Brush(TEXT("Interface\\Glues\\CharacterSelect\\Glue-CharacterSelect-Highlight"),FVector2f(0),FVector2f(1),ESlateBrushMirrorType::NoMirror,true))
            .Visibility_Lambda([this,Guid=C.Guid,Key]{const auto* Weak=StockControls.Find(Key);const auto B=Weak?Weak->Pin():nullptr;return SelectedGuid==Guid || (B && B->IsHovered())?EVisibility::HitTestInvisible:EVisibility::Collapsed;});
        Put(Highlight,-261,72+I*57,256,74,1);
        const auto& D=FDbcStore::Get();const FString Class=D.CharacterCreation().ClassName(C.Class);
        const FString Info=Art->String(TEXT("CHARACTER_SELECT_INFO")).Replace(TEXT("%d"),*FString::FromInt(C.Level)).Replace(TEXT("%s"),*Class);const auto* Area=D.AreaTable().GetById(C.ZoneId);
        auto Row=SNew(SSelectionRow).OnClicked_Lambda([this,Guid=C.Guid]{HighlightCharacter(Guid);return FReply::Handled();})
            .OnActivate_Lambda([this,Guid=C.Guid]{HighlightCharacter(Guid);EnterSelected();return FReply::Handled();})
            [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(C.Name)).Font(Art->Font(13)).ColorAndOpacity(Gold).ShadowOffset(FVector2D(1,1))]
            +SVerticalBox::Slot().AutoHeight().Padding(0,1)[SNew(STextBlock).Text(FText::FromString(Info)).Font(Art->Font(12)).ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(1,1))]
            +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Area?Area->Name:FString())).Font(Art->Font(12)).ColorAndOpacity(FLinearColor(.5f,.5f,.5f)).ShadowOffset(FVector2D(1,1))]];
        StockControls.Add(Key,Row);Put(Enabled(Row),-241,85+I*57,217,50,1);
    }
    auto Create=Click(TEXT("Create"),TEXT("CREATE_NEW_CHARACTER"),FOnClicked::CreateLambda([this]{OnCreateCharacterRequest.ExecuteIfBound();return FReply::Handled();}));
    Create->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]{return CachedCharacters.Num()<10?EVisibility::Visible:EVisibility::Collapsed;}));Put(Enabled(Create),-235,597,200,45,1);
    Put(SNew(STextBlock).Text_Lambda([this]{const auto* C=Selected();return FText::FromString(C?C->Name:FString());}).Font(Art->Font(22)).ColorAndOpacity(Gold).ShadowOffset(FVector2D(1,1)).Justification(ETextJustify::Center),-260,-131,520,31,.5f,1);
    auto Enter=Click(TEXT("EnterWorld"),TEXT("ENTER_WORLD"),FOnClicked::CreateLambda([this]{EnterSelected();return FReply::Handled();}));
    Enter->SetEnabled(TAttribute<bool>::CreateLambda([this]{return !bBusy && !DeleteGuid && Selected();}));Put(Enter,-100,-90,200,60,.5f,1);
    for(int32 Direction:{-1,1})
    {
        auto B=SNew(SButton).ButtonStyle(Art->Button(TEXT("Interface\\Glues\\CharacterCreate\\UI-RotationRight-Big"))).ContentPadding(0)
            .OnClicked_Lambda([this,Direction]{if(Preview.IsValid()) Preview->Rotate(Direction*20);return FReply::Handled();});
        if(Direction<0){B->SetRenderTransformPivot(FVector2D(.5,.5));B->SetRenderTransform(FSlateRenderTransform(FScale2D(-1,1)));}
        Put(Enabled(B),Direction<0?-40:-9,-59,50,50,.5f,1);
    }
    Put(Enabled(Click(TEXT("Back"),TEXT("BACK"),FOnClicked::CreateLambda([this]{OnBackRequest.ExecuteIfBound();return FReply::Handled();}))),-130,-60,100,35,1,1);
    auto Delete=Click(TEXT("Delete"),TEXT("DELETE_CHARACTER"),FOnClicked::CreateLambda([this]{OpenDeleteDialog();return FReply::Handled();}));
    Delete->SetEnabled(TAttribute<bool>::CreateLambda([this]{return !bBusy && !DeleteGuid && Selected() && OnDeleteCharacterRequest.IsBound();}));Put(Delete,-295,-60,165,35,1,1);
    DeleteStyle=MakeShared<FEditableTextBoxStyle>(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"));FSlateBrush Empty;Empty.DrawAs=ESlateBrushDrawType::NoDrawType;
    DeleteStyle->SetBackgroundImageNormal(Empty).SetBackgroundImageHovered(Empty).SetBackgroundImageFocused(Empty).SetPadding(FMargin(8,3));
    auto Confirm=Click(TEXT("ConfirmDelete"),TEXT("DELETE_CHARACTER"),FOnClicked::CreateLambda([this]{ConfirmDelete();return FReply::Handled();}));
    Confirm->SetEnabled(TAttribute<bool>::CreateLambda([this]{return CanConfirmDelete();}));
    Confirm->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]{return DeleteGuid?EVisibility::Visible:EVisibility::Collapsed;}));
    auto Dismiss=Click(TEXT("Dismiss"),TEXT("CANCEL"),FOnClicked::CreateLambda([this]{DeleteGuid=0;Status.Reset();return FReply::Handled();}));
    Dismiss->SetContent(SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Art->String(DeleteGuid?TEXT("CANCEL"):TEXT("OKAY")));})
        .Font(Art->Font(13)).ColorAndOpacity(Gold).ShadowOffset(FVector2D(1,1)).Justification(ETextJustify::Center));
    Dismiss->SetEnabled(TAttribute<bool>::CreateLambda([this]{return !bBusy;}));
    auto Dialog=SNew(SOverlay)+SOverlay::Slot()[Backdrop(430,210)]
        +SOverlay::Slot().Padding(24,18,24,65)[SNew(STextBlock).Font(Art->Font(14)).AutoWrapText(true).Justification(ETextJustify::Center).ColorAndOpacity(Gold).Text_Lambda([this]{return FText::FromString(Status);})]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0,0,0,53)
            [SNew(SBox).WidthOverride(220).HeightOverride(34).Visibility_Lambda([this]{return DeleteGuid?EVisibility::Visible:EVisibility::Collapsed;})
                [SNew(SOverlay)+SOverlay::Slot()[Backdrop(220,34)]+SOverlay::Slot()[SAssignNew(DeleteBox,SEditableTextBox).Style(DeleteStyle.Get()).Font(Art->Font(14)).ForegroundColor(FLinearColor::White)]]]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0,0,0,12)
            [SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(130).HeightOverride(30)[Confirm]]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(120).HeightOverride(30)[Dismiss]]];
    auto Modal=SNew(SOverlay)+SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0,0,0,.25f)).OnMouseButtonDown_Lambda([](const FGeometry&,const FPointerEvent&){return FReply::Handled();})]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(SBox).WidthOverride(430).HeightOverride(210)[Dialog]];
    Modal->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]{return Status.IsEmpty()?EVisibility::Collapsed:EVisibility::Visible;}));
    Canvas->AddSlot().Anchors(FAnchors(0,0,1,1)).Offset(FMargin(0))[Modal];
    ChildSlot[SNew(SDPIScaler).DPIScale_Lambda([this]{return GetCachedGeometry().GetLocalSize().Y>0?float(GetCachedGeometry().GetLocalSize().Y/768):1.f;})[Canvas]];
}
void SWowCharacterSelectWidget::OpenDeleteDialog()
{
    if(bBusy || DeleteGuid || !Selected() || !OnDeleteCharacterRequest.IsBound()) return;
    const auto C=*Selected();DeleteGuid=C.Guid;DeleteBox->SetText(FText::GetEmpty());
    FString Format=Art->String(TEXT("CONFIRM_CHAR_DELETE")).Replace(TEXT("|cffffffff"),TEXT("")).Replace(TEXT("|r"),TEXT(""));
    const int32 NameAt=Format.Find(TEXT("%s"));if(NameAt!=INDEX_NONE) Format=Format.Left(NameAt)+C.Name+Format.Mid(NameAt+2);
    Status=Format.Replace(TEXT("%d"),*FString::FromInt(C.Level)).Replace(TEXT("%s"),*FDbcStore::Get().CharacterCreation().ClassName(C.Class))+TEXT("\n\n")+Art->String(TEXT("CONFIRM_CHAR_DELETE_INSTRUCTIONS"));
    FSlateApplication::Get().SetKeyboardFocus(DeleteBox,EFocusCause::SetDirectly);
}
bool SWowCharacterSelectWidget::CanConfirmDelete() const {return !bBusy && DeleteGuid && SelectedGuid==DeleteGuid && Selected() && OnDeleteCharacterRequest.IsBound() && DeleteBox && Art && !Art->String(TEXT("DELETE_CONFIRM_STRING")).IsEmpty() && DeleteBox->GetText().ToString().Equals(Art->String(TEXT("DELETE_CONFIRM_STRING")),ESearchCase::IgnoreCase);}
void SWowCharacterSelectWidget::ConfirmDelete()
{
    if(!CanConfirmDelete()) return;const int64 Guid=DeleteGuid;DeleteGuid=0;bBusy=true;Status=Art->String(TEXT("CHAR_DELETE_IN_PROGRESS"));OnDeleteCharacterRequest.ExecuteIfBound(Guid);
}
void SWowCharacterSelectWidget::SetDeleteConfirmationText(const FString& Text) {if(DeleteBox && DeleteGuid) DeleteBox->SetText(FText::FromString(Text));}
void SWowCharacterSelectWidget::CompleteDeletion(bool Success,const FString& Message) {if(!Success) bBusy=false;DeleteGuid=0;Status=Message;}
void SWowCharacterSelectWidget::SetStatusText(const FString& Text) {Status=Text;}
bool SWowCharacterSelectWidget::PressStockControl(FName Name,bool DoubleClick)
{
    const auto KeepAlive=AsShared();if(bBusy || (DeleteGuid && Name!=TEXT("ConfirmDelete") && Name!=TEXT("Dismiss")) || (!DeleteGuid && !Status.IsEmpty() && Name!=TEXT("Dismiss"))) return false;
    const auto* Weak=StockControls.Find(Name);const auto B=Weak?Weak->Pin():nullptr;if(!B || !B->IsEnabled() || B->GetVisibility()!=EVisibility::Visible) return false;
    if(DoubleClick){const FPointerEvent E(0,FVector2D::ZeroVector,FVector2D::ZeroVector,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());B->OnMouseButtonDoubleClick(B->GetCachedGeometry(),E);return true;}
    const FKeyEvent E(EKeys::SpaceBar,FModifierKeysState(),0,false,0,0);B->OnKeyDown(B->GetCachedGeometry(),E);B->OnKeyUp(B->GetCachedGeometry(),E);return true;
}
FReply SWowCharacterSelectWidget::OnPreviewKeyDown(const FGeometry&,const FKeyEvent& E)
{
    if(bBusy) return FReply::Handled();
    if(E.GetKey()==EKeys::Escape){if(DeleteGuid || !Status.IsEmpty()){DeleteGuid=0;Status.Reset();}else OnBackRequest.ExecuteIfBound();return FReply::Handled();}
    if(E.GetKey()==EKeys::Enter){if(DeleteGuid) ConfirmDelete();else if(Status.IsEmpty()) EnterSelected();return FReply::Handled();}
    if(!DeleteGuid && Status.IsEmpty() && (E.GetKey()==EKeys::Up || E.GetKey()==EKeys::Down) && CachedCharacters.Num())
    {const int32 Index=CachedCharacters.IndexOfByPredicate([this](const auto& C){return C.Guid==SelectedGuid;});HighlightCharacter(CachedCharacters[FMath::Clamp(Index+(E.GetKey()==EKeys::Down?1:-1),0,CachedCharacters.Num()-1)].Guid);return FReply::Handled();}
    return FReply::Unhandled();
}
