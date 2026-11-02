#include "WowLoginController.h"
#include "WowConnectionManager.h"
#include "WowCharacterCreateWidget.h"
#include "WowCharacterPreview.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

/** Opt-in rendered protocol verification. Never logs in to or deletes a character. */
struct FWowCharacterCreationProbe
{
    TSharedPtr<FJsonObject> Spec,Report=MakeShared<FJsonObject>();
    FString Output,Name,Account,Failure;
    int32 Phase=0;
    uint32 Serial=0,Sent=0;
    double Started=FPlatformTime::Seconds(),Changed=Started;
    bool Finished=false;
    FWowCharacterAppearance Expected;
    explicit FWowCharacterCreationProbe(const FString& Path)
    {
        FString Text;
        if(!FFileHelper::LoadFileToString(Text,*Path) || Text.Len()>16384 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Spec) || !Spec)
        {Failure=TEXT("Invalid owned creation probe");return;}
        Output=Spec->GetStringField(TEXT("output"));Name=Spec->GetStringField(TEXT("character"));Account=Spec->GetStringField(TEXT("account"));
        if(!Name.StartsWith(TEXT("Uiq")) || Name.Len()<8 || Name.Len()>12 || !Account.StartsWith(TEXT("QA_UI_")) || Account.Len()!=14)
            Failure=TEXT("Creation probe requires an owned disposable identity");
        Expected.Race=Spec->GetIntegerField(TEXT("race"));Expected.Class=Spec->GetIntegerField(TEXT("class"));Expected.Gender=Spec->GetIntegerField(TEXT("gender"));
    }
    void Finish(AWowLoginController& C,bool Passed,const FString& Error=FString())
    {
        if(Finished) return;Finished=true;
        Report->SetBoolField(TEXT("passed"),Passed);Report->SetStringField(TEXT("failure"),Error);
        Report->SetStringField(TEXT("character"),Name);Report->SetNumberField(TEXT("phase"),Phase);Report->SetNumberField(TEXT("completedSteps"),Phase);
        Report->SetNumberField(TEXT("requestsSent"),C.ConnectionManager?C.ConnectionManager->GetCharacterCreateSendCount():0);
        Report->SetBoolField(TEXT("neverEnteredWorld"),C.ConnectionManager && C.ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid==0);
        FString Text;FJsonSerializer::Serialize(Report.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Output/TEXT("probe.json")));
        if(C.ConnectionManager) C.ConnectionManager->Disconnect(); C.ClearCurrentScreen();
        FPlatformMisc::RequestExitWithStatus(false,Passed?0:3);
    }
    void Next(int32 Value) {Phase=Value;Changed=FPlatformTime::Seconds();}
    void Capture(const TCHAR* Label) {FScreenshotRequest::RequestScreenshot(Output/(FString(Label)+TEXT(".png")),true,false);}
    bool Choose(SWowCharacterCreateWidget& W,uint8 Race,uint8 Class,uint8 Gender)
    {
        if(W.Appearance.Race!=Race && !W.PressStockControl(FName(*FString::Printf(TEXT("Race_%u"),Race)))) return false;
        if(W.Appearance.Class!=Class && !W.PressStockControl(FName(*FString::Printf(TEXT("Class_%u"),Class)))) return false;
        if(W.Appearance.Gender!=Gender && !W.PressStockControl(FName(*FString::Printf(TEXT("Gender_%u"),Gender)))) return false;
        return W.Appearance.Race==Race && W.Appearance.Class==Class && W.Appearance.Gender==Gender && W.bValid;
    }
    void Send(AWowLoginController& C,const FString& CharacterName)
    {
        Serial=C.CreationResultSerial;Sent=C.ConnectionManager->GetCharacterCreateSendCount();
        C.CharCreateWidget->SetName(CharacterName);C.CharCreateWidget->PressStockControl(TEXT("CHARACTER_CREATE_ACCEPT"));C.CharCreateWidget->PressStockControl(TEXT("CHARACTER_CREATE_ACCEPT"));
        if(C.ConnectionManager->GetCharacterCreateSendCount()!=Sent+1 || !C.CharCreateWidget->IsSubmitting()) Failure=TEXT("Repeated UI submit sent more than one request");
    }
    void Tick(AWowLoginController& C)
    {
        if(Finished) return;
        if(!Failure.IsEmpty()) {Finish(C,false,Failure);return;}
        const double Now=FPlatformTime::Seconds();
        if(Now-Started>180 || Now-Changed>40) {Finish(C,false,TEXT("Native creation phase timed out"));return;}
        auto* Net=C.ConnectionManager.Get();if(!Net) return;
        if(!Net->GetAccountName().IsEmpty() && Net->GetAccountName()!=Account) {Finish(C,false,TEXT("Owned account selector mismatch"));return;}
        if(Net->PacketHandler.EntityManager.LocalPlayerGuid) {Finish(C,false,TEXT("Creation probe must never enter the world"));return;}
        if(Phase==0)
        {
            if(Net->GetState()!=EWowSessionState::WorldHaveCharList) return;
            if(!Net->GetCachedCharacters().IsEmpty()) {Finish(C,false,TEXT("Owned account must start empty"));return;}
            C.HandleCreateCharacterRequest();Next(1);return;
        }
        if(Phase==1)
        {
            if(!C.CharCreateWidget || !C.CharCreateWidget->bDataReady || !C.CharacterPreview->IsReady()) return;
            C.CharCreateWidget->SetName(TEXT(""));C.CharCreateWidget->PressStockControl(TEXT("CHARACTER_CREATE_ACCEPT"));
            if(Net->GetCharacterCreateSendCount()!=0) {Finish(C,false,TEXT("Empty name reached network"));return;}
            Send(C,TEXT("A"));Next(2);return;
        }
        if(Phase==2)
        {
            if(C.CreationResultSerial==Serial) return;
            if(C.LastCreationResult!=0x5a || C.CharCreateWidget->IsSubmitting()) {Finish(C,false,TEXT("Expected original short-name rejection"));return;}
            Report->SetNumberField(TEXT("shortNameResult"),C.LastCreationResult);Capture(TEXT("01-short-name-error"));Next(3);return;
        }
        if(Phase==3 && Now-Changed>.5)
        {
            C.CharCreateWidget->PressStockControl(TEXT("OKAY"));
            if(!Choose(*C.CharCreateWidget,1,6,0)) {Finish(C,false,TEXT("Cannot select death knight"));return;}
            if(!C.CharacterPreview->IsReady()) return;
            Send(C,Name);Next(4);return;
        }
        if(Phase==4)
        {
            if(C.CreationResultSerial==Serial) return;
            if(C.LastCreationResult!=0x3b || C.CharCreateWidget->IsSubmitting()) {Finish(C,false,TEXT("Fresh owned account must retain server DK level requirement"));return;}
            Report->SetNumberField(TEXT("deathKnightResult"),C.LastCreationResult);Capture(TEXT("02-level-requirement"));Next(5);return;
        }
        if(Phase==5 && Now-Changed>.5)
        {
            C.CharCreateWidget->PressStockControl(TEXT("OKAY"));
            if(!Choose(*C.CharCreateWidget,Expected.Race,Expected.Class,Expected.Gender)) {Finish(C,false,TEXT("Cannot select original race/class/gender"));return;}
            for(int32 I=0;I<5;++I) for(int32 N=0;N<2;++N) C.CharCreateWidget->PressStockControl(FName(*FString::Printf(TEXT("Appearance_%d_1"),I)));
            Expected=C.CharCreateWidget->GetAppearance();C.CharCreateWidget->SetName(Name);Next(6);return;
        }
        if(Phase==6)
        {
            if(!C.CharacterPreview->IsReady() || Now-Changed<1.5) return;
            Capture(TEXT("03-customized-preview"));Next(7);return;
        }
        if(Phase==7 && Now-Changed>.5) {Send(C,Name);Next(8);return;}
        if(Phase==8)
        {
            if(C.CreationResultSerial==Serial || Net->GetState()!=EWowSessionState::WorldHaveCharList || !C.CharacterPreview || !C.CharacterPreview->IsReady()) return;
            const auto& Chars=Net->GetCachedCharacters();
            if(C.LastCreationResult!=0x2f || Chars.Num()!=1 || Chars[0].Name!=Name) {Finish(C,false,TEXT("Creation/list identity mismatch"));return;}
            const auto& A=Chars[0];
            if(A.Race!=Expected.Race || A.Class!=Expected.Class || A.Gender!=Expected.Gender || A.Skin!=Expected.Skin || A.Face!=Expected.Face || A.HairStyle!=Expected.HairStyle || A.HairColor!=Expected.HairColor || A.FacialHair!=Expected.FacialHair)
            {Finish(C,false,TEXT("Server appearance differs from the preview submission"));return;}
            const auto& Shown=C.CharacterPreview->GetShownCharacter();
            if(Shown.Guid!=A.Guid || Shown.Skin!=A.Skin || Shown.Face!=A.Face || Shown.HairStyle!=A.HairStyle || Shown.HairColor!=A.HairColor || Shown.FacialHair!=A.FacialHair)
            {Finish(C,false,TEXT("List preview lost saved appearance"));return;}
            Report->SetNumberField(TEXT("guid"),A.Guid);Report->SetNumberField(TEXT("race"),A.Race);Report->SetNumberField(TEXT("class"),A.Class);Report->SetNumberField(TEXT("gender"),A.Gender);
            Report->SetNumberField(TEXT("skin"),A.Skin);Report->SetNumberField(TEXT("face"),A.Face);Report->SetNumberField(TEXT("hairStyle"),A.HairStyle);Report->SetNumberField(TEXT("hairColor"),A.HairColor);Report->SetNumberField(TEXT("facialHair"),A.FacialHair);
            Report->SetBoolField(TEXT("enumMatchesPreview"),true);Report->SetBoolField(TEXT("stockSlateControlsInvoked"),true); Capture(TEXT("04-created-list"));Next(9);return;
        }
        if(Phase==9 && Now-Changed>.6)
        {C.HandleCreateCharacterRequest();Next(10);return;}
        if(Phase==10)
        {
            if(!C.CharCreateWidget || !C.CharCreateWidget->IsDataReady()) return;
            if(!Choose(*C.CharCreateWidget,Expected.Race,Expected.Class,Expected.Gender)) {Finish(C,false,TEXT("Cannot restore creation screen"));return;}
            if(!C.CharacterPreview->IsReady()) return;
            Send(C,Name);Next(11);return;
        }
        if(Phase==11)
        {
            if(C.CreationResultSerial==Serial) return;
            if(C.LastCreationResult!=0x32 || C.CharCreateWidget->IsSubmitting()) {Finish(C,false,TEXT("Expected original duplicate-name error"));return;}
            Report->SetNumberField(TEXT("duplicateNameResult"),C.LastCreationResult);Capture(TEXT("05-name-in-use"));Next(12);return;
        }
        if(Phase==12 && Now-Changed>.6) {C.CharCreateWidget->PressStockControl(TEXT("OKAY"));C.CharCreateWidget->PressStockControl(TEXT("BACK"));Next(13);return;}
        if(Phase==13)
        {
            if(!C.CharacterPreview || !C.CharacterPreview->IsReady() || Now-Changed<1) return;
            Capture(TEXT("06-returned-list"));Next(14);return;
        }
        if(Phase==14 && Now-Changed>.6) Finish(C,true);
    }
};
void AWowLoginController::TickCreationProbe()
{
    if(!CreationProbe)
    {
        FString Path;if(!FParse::Value(FCommandLine::Get(),TEXT("charactercreationprobe="),Path)) return;
        CreationProbe=MakeShared<FWowCharacterCreationProbe>(Path);
    }
    CreationProbe->Tick(*this);
}
