#include "WowCharacterSelectionTestGameMode.h"
#include "WowCharacterSelectWidget.h"
#include "WowCharacterPreview.h"
#include "WowCharacterBuilder.h"
#include "Formats/Dbc/DbcStore.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

AWowCharacterSelectionTestGameMode::AWowCharacterSelectionTestGameMode(){PrimaryActorTick.bCanEverTick=true;HUDClass=nullptr;}
void AWowCharacterSelectionTestGameMode::SetupTestScene(UWorld*,AWowWorldManager*)
{
    Started=FPlatformTime::Seconds();FString Spec,Text;TSharedPtr<FJsonObject> Root;
    FParse::Value(FCommandLine::Get(),TEXT("selectionsteps="),Spec);FParse::Value(FCommandLine::Get(),TEXT("selectionoutput="),Output);
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(Output.IsEmpty() || !FFileHelper::LoadFileToString(Text,*Spec) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root) || !Root->TryGetArrayField(TEXT("cases"),Rows) || Rows->IsEmpty())
    {Finish(false,TEXT("Missing selection capture specification"));return;}Cases=*Rows;
}
void AWowCharacterSelectionTestGameMode::CloseScreen()
{
    if(Widget && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
    Widget.Reset();if(Preview) Preview->Destroy();Preview=nullptr;
}
void AWowCharacterSelectionTestGameMode::StartCase()
{
    CloseScreen();Phase=4;Changed=FPlatformTime::Seconds();
}
void AWowCharacterSelectionTestGameMode::OpenNextCase()
{
    CollectGarbage(RF_NoFlags,true);++Cycles;
    for(TActorIterator<AWowCharacterPreview> It(GetWorld());It;++It) {Finish(false,TEXT("Previous selection rig retained"));return;}
    if(++Index>=Cases.Num()){Finish(true);return;}
    const auto Case=Cases[Index]->AsObject();const auto& D=FDbcStore::Get();Roster.Reset();
    const uint8 Races[]={1,2,3,4,5,6,7,8,10,11},Classes[]={1,9,2,11,4,7,8,3,2,7};
    const uint32 Zones[]={1519,14,1,1657,1497,215,1,14,3430,3524};
    const TCHAR* Names[]={TEXT("Aldren"),TEXT("Tharok"),TEXT("Brundin"),TEXT("Lunara"),TEXT("Morven"),TEXT("Tashira"),TEXT("Fizzwick"),TEXT("Zulani"),TEXT("Kaelora"),TEXT("Velenna")};
    bool SameAppearance=false;Case->TryGetBoolField(TEXT("sameAppearance"),SameAppearance);
    for(int32 I=0;I<10;++I)
    {
        FWowCharacterAppearance A;A.Race=SameAppearance?Case->GetIntegerField(TEXT("race")):Races[I];A.Class=SameAppearance?1:Classes[I];A.Gender=SameAppearance?Case->GetIntegerField(TEXT("gender")):I%2;
        if(Case->GetIntegerField(TEXT("race"))==A.Race)
        {
            A.Gender=Case->GetIntegerField(TEXT("gender"));double Class=0;if(Case->TryGetNumberField(TEXT("class"),Class)) A.Class=Class;
            auto Last=[&](EWowAppearanceOption Option,uint8& Value){const auto V=FCharacterCreationDbc::Options(Option,A,D.CharSections(),D.CharacterFacialHairStyles());if(V.Num()) Value=V.Last();FCharacterCreationDbc::Normalize(A,D.CharSections(),D.CharacterFacialHairStyles());};
            Last(EWowAppearanceOption::Skin,A.Skin);Last(EWowAppearanceOption::Face,A.Face);Last(EWowAppearanceOption::HairStyle,A.HairStyle);Last(EWowAppearanceOption::HairColor,A.HairColor);Last(EWowAppearanceOption::FacialHair,A.FacialHair);
        }
        FCharacterCreationDbc::Normalize(A,D.CharSections(),D.CharacterFacialHairStyles());FWowCharacterInfo C;
        C.Guid=1000+I;C.Name=Names[I];C.Race=A.Race;C.Class=A.Class;C.Gender=A.Gender;C.Level=80-I*3;C.ZoneId=Zones[I];C.Skin=A.Skin;C.Face=A.Face;C.HairStyle=A.HairStyle;C.HairColor=A.HairColor;C.FacialHair=A.FacialHair;
        if(const auto* O=D.CharStartOutfit().GetByRaceClassGender(C.Race,C.Class,C.Gender)) for(int32 Item=0;Item<24;++Item) if(O->ItemDisplayIds[Item]>0)
        {
            const uint8 Type=O->ItemInventorySlots[Item];int32 Slot=INDEX_NONE;
            switch(Type){case 1:Slot=0;break;case 3:Slot=2;break;case 4:Slot=3;break;case 5:case 20:Slot=4;break;case 6:Slot=5;break;case 7:Slot=6;break;case 8:Slot=7;break;case 9:Slot=8;break;case 10:Slot=9;break;case 16:Slot=14;break;case 13:case 17:case 21:Slot=15;break;case 14:case 22:case 23:Slot=16;break;case 15:case 25:case 26:Slot=17;break;case 19:Slot=18;break;}
            if(Slot!=INDEX_NONE){FWowCharacterEquipment E;E.DisplayId=O->ItemDisplayIds[Item];E.InventoryType=Type;E.Slot=Slot;C.Equipment.Add(E);}
        }
        Roster.Add(C);
    }
    const auto* Found=Roster.FindByPredicate([&](const auto& C){return C.Race==Case->GetIntegerField(TEXT("race"));});
    int64 Guid=Found?Found->Guid:0;bool Empty=false,Single=false;Case->TryGetBoolField(TEXT("empty"),Empty);Case->TryGetBoolField(TEXT("single"),Single);
    if(Single && Found){const auto Copy=*Found;Roster={Copy};}if(Empty) Roster.Reset();
    double Count=0;if(Case->TryGetNumberField(TEXT("count"),Count)) Roster.SetNum(FMath::Clamp(int32(Count),0,Roster.Num()));
    double Selection=0;if(Case->TryGetNumberField(TEXT("selection"),Selection) && Roster.IsValidIndex(int32(Selection))) Guid=Roster[int32(Selection)].Guid;
    Preview=GetWorld()->SpawnActor<AWowCharacterPreview>();Preview->Setup(CachedWorldManager,1024,768);
    Widget=SNew(SWowCharacterSelectWidget);Widget->OnCharacterSelected.BindLambda([this](int64 G){++EnterCount;EnterGuid=G;});
    Widget->OnDeleteCharacterRequest.BindLambda([this](int64 G){++DeleteCount;DeletedGuid=G;});Widget->OnCreateCharacterRequest.BindLambda([this]{++CreateCount;});
    FString Realm=TEXT("Idle WoW");Case->TryGetStringField(TEXT("realm"),Realm);
    Widget->PopulateCharacters(Roster,Guid);Widget->SetPreview(Preview,Realm);GEngine->GameViewport->AddViewportWidgetContent(Widget.ToSharedRef(),100);
    Phase=1;Changed=FPlatformTime::Seconds();
}
bool AWowCharacterSelectionTestGameMode::VerifyActions()
{
    // Advance on separate frames: Slate attributes must update between typing,
    // visibility changes and the next physical button event.
    const int64 Guid=Roster[0].Guid;const FName Row(*FString::Printf(TEXT("Character_%lld"),Guid));
    switch(ActionStep)
    {
    case 0: ActionInitialGuid=Widget->GetSelectedGuid();Widget->PopulateCharacters(Roster,Guid);break;
    case 1:
        if(Widget->HighlightCharacter(-1) || (Roster.Num()==10 && Widget->PressStockControl(TEXT("Create")))) return false;
        Widget->OnPreviewKeyDown(FGeometry(),FKeyEvent(EKeys::Down,FModifierKeysState(),0,false,0,0));break;
    case 2:
        if(Widget->GetSelectedGuid()!=Roster[1].Guid || EnterCount) return false;
        Widget->OnPreviewKeyDown(FGeometry(),FKeyEvent(EKeys::Up,FModifierKeysState(),0,false,0,0));break;
    case 3:
        if(Widget->GetSelectedGuid()!=Guid || !Widget->PressStockControl(Row) || EnterCount || !Widget->PressStockControl(Row,true) || EnterCount!=1 || EnterGuid!=Guid || Widget->EnterSelected()) return false;
        break;
    case 4: Widget->PopulateCharacters(Roster,Guid);EnterCount=0;break;
    case 5:
        if(!Widget->PressStockControl(TEXT("EnterWorld")) || EnterCount!=1 || EnterGuid!=Guid || Widget->PressStockControl(TEXT("EnterWorld"))) return false;
        break;
    case 6: Widget->PopulateCharacters(Roster);EnterCount=0;break;
    case 7:
        if(Widget->GetSelectedGuid()!=Guid) return false;
        Widget->OnPreviewKeyDown(FGeometry(),FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));if(EnterCount!=1 || EnterGuid!=Guid) return false;
        break;
    case 8: Widget->PopulateCharacters(Roster,Guid);EnterCount=0;break;
    case 9: if(!Widget->PressStockControl(TEXT("Delete"))) return false;break;
    case 10:
        if(!Widget->IsDeleteDialogOpen() || Widget->PressStockControl(TEXT("ConfirmDelete"))) return false;
        Widget->SetDeleteConfirmationText(TEXT("wrong"));break;
    case 11:
        if(Widget->PressStockControl(TEXT("ConfirmDelete")) || DeleteCount || Widget->HighlightCharacter(Roster.Last().Guid) || !Widget->PressStockControl(TEXT("Dismiss"))) return false;
        break;
    case 12: if(DeleteCount || Widget->IsDeleteDialogOpen() || !Widget->PressStockControl(TEXT("Delete"))) return false;Widget->SetDeleteConfirmationText(TEXT("DELETE"));break;
    case 13:
        if(!Widget->FocusStockControl(TEXT("Dismiss"))) return false;
        Widget->OnPreviewKeyDown(FGeometry(),FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
        if(DeleteCount || Widget->IsDeleteDialogOpen()) return false;
        break;
    case 14: if(!Widget->PressStockControl(TEXT("Delete"))) return false;Widget->SetDeleteConfirmationText(TEXT("DELETE"));break;
    case 15:
        if(!Widget->PressStockControl(TEXT("ConfirmDelete")) || DeleteCount!=1 || DeletedGuid!=Guid || Widget->PressStockControl(TEXT("ConfirmDelete")) || Widget->EnterSelected()) return false;
        break;
    case 16: Widget->CompleteDeletion(false,TEXT("Deletion failed"));break;
    case 17: if(!Widget->PressStockControl(TEXT("Dismiss"))) return false;Widget->PopulateCharacters({});DeleteCount=0;break;
    case 18:
        if(Widget->EnterSelected() || Widget->PressStockControl(TEXT("Delete")) || !Widget->PressStockControl(TEXT("Create")) || CreateCount!=1) return false;
        Widget->PopulateCharacters(Roster,ActionInitialGuid);CreateCount=0;ActionsVerified=true;break;
    }
    ++ActionStep;return true;
}
void AWowCharacterSelectionTestGameMode::Tick(float DT)
{
    Super::Tick(DT);if(Finished) return;const double Now=FPlatformTime::Seconds();if(Now-Started>240){Finish(false,TEXT("Selection capture timeout"));return;}
    if(!FDbcStore::Get().IsDeferredLoaded()) return;
    if(Phase==4){if(Now-Changed>.25) OpenNextCase();return;}
    if(Index<0){StartCase();return;}
    if(Phase==1)
    {
        if((ActionStep==0 || ActionsVerified) && (Roster.IsEmpty()? !Preview->HasScene():!Preview->IsReady())) return;
        if(Now-Changed<(!ActionsVerified && ActionStep>0?.1:1.)) return;
        if(!ActionsVerified && Roster.Num()>1){if(!VerifyActions()){Finish(false,FString::Printf(TEXT("Selection input/confirmation guards failed at step %d (enter=%d delete=%d create=%d)"),ActionStep,EnterCount,DeleteCount,CreateCount));return;}Changed=Now;return;}
        FString Modal;Cases[Index]->AsObject()->TryGetStringField(TEXT("modal"),Modal);
        double Hover=0;if(Cases[Index]->AsObject()->TryGetNumberField(TEXT("hover"),Hover) && Roster.IsValidIndex(int32(Hover)))
            if(!Widget->HoverStockControl(FName(*FString::Printf(TEXT("Character_%lld"),Roster[int32(Hover)].Guid)),true)){Finish(false,TEXT("Native row hover failed"));return;}
        if(Modal==TEXT("delete")){FString Confirmation=TEXT("DELE");Cases[Index]->AsObject()->TryGetStringField(TEXT("confirmation"),Confirmation);Widget->PressStockControl(TEXT("Delete"));Widget->SetDeleteConfirmationText(Confirmation);}
        else if(Modal==TEXT("error")) Widget->CompleteDeletion(false,TEXT("This character is a Guild Master and cannot be deleted until the rank is transferred to another character."));
        CollectGarbage(RF_NoFlags,true);Phase=2;Changed=Now;return;
    }
    if(Phase==2 && Now-Changed>.75)
    {
        const int64 Guid=Widget->GetSelectedGuid();const auto* C=Roster.FindByPredicate([Guid](const auto& X){return X.Guid==Guid;});
        if(C)
        {
            const auto& P=Preview->GetShownCharacter();
            if(!Preview->IsReady() || !FWowCharacterBuilder::GetAnimationController(Preview->GetCharacterActor()) || P.Guid!=C->Guid || P.Race!=C->Race || P.Class!=C->Class || P.Gender!=C->Gender || P.Skin!=C->Skin || P.Face!=C->Face || P.HairStyle!=C->HairStyle || P.HairColor!=C->HairColor || P.FacialHair!=C->FacialHair || P.Equipment.Num()!=C->Equipment.Num()) {Finish(false,TEXT("Selected enum appearance/equipment differs"));return;}
            for(int32 I=0;I<C->Equipment.Num();++I) if(P.Equipment[I].DisplayId!=C->Equipment[I].DisplayId || P.Equipment[I].Slot!=C->Equipment[I].Slot) {Finish(false,TEXT("Selected equipment changed"));return;}
        }
        int32 Rigs=0,Models=0;for(TActorIterator<AWowCharacterPreview> It(GetWorld());It;++It) if(!It->IsActorBeingDestroyed()) ++Rigs;
        for(TActorIterator<AActor> It(GetWorld());It;++It) if(!It->IsActorBeingDestroyed() && It->FindComponentByClass<USkeletalMeshComponent>()) ++Models;
        if(Rigs!=1 || Models!=(C?1:0) || EnterCount || DeleteCount){Finish(false,TEXT("Unexpected model or account action"));return;}
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Cases[Index]->AsObject()->GetStringField(TEXT("name")));Row->SetNumberField(TEXT("characters"),Roster.Num());Row->SetNumberField(TEXT("selectedGuid"),Guid);
        Row->SetBoolField(TEXT("enumAppearanceMatches"),true);Row->SetBoolField(TEXT("actionsAreSpies"),true);Row->SetNumberField(TEXT("liveModels"),Models);Row->SetNumberField(TEXT("liveRigs"),Rigs);Results.Add(MakeShared<FJsonValueObject>(Row));
        FScreenshotRequest::RequestScreenshot(Output/(Row->GetStringField(TEXT("name"))+TEXT(".png")),true,false);Phase=3;Changed=Now;
    }
    if(Phase==3 && Now-Changed>.4 && !FScreenshotRequest::IsScreenshotRequested()) StartCase();
}
void AWowCharacterSelectionTestGameMode::Finish(bool Passed,const FString& Failure)
{
    if(Finished) return;Finished=true;CloseScreen();auto Root=MakeShared<FJsonObject>();Root->SetBoolField(TEXT("passed"),Passed);Root->SetStringField(TEXT("failure"),Failure);Root->SetBoolField(TEXT("inputGuardsVerified"),ActionsVerified);Root->SetNumberField(TEXT("screenCycles"),Cycles);Root->SetArrayField(TEXT("cases"),Results);
    FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Output/TEXT("runtime.json")));FPlatformMisc::RequestExitWithStatus(false,Passed?0:3);
}
void AWowCharacterSelectionTestGameMode::EndPlay(const EEndPlayReason::Type R){CloseScreen();Super::EndPlay(R);}
