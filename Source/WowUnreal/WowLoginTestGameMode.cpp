#include "WowLoginTestGameMode.h"
#include "WowLoginController.h"
#include "WowLoginWidget.h"
#include "WowLoginScene.h"
#include "WowCharacterPreview.h"
#include "WowAudioManager.h"
#include "WowM2EmitterComponent.h"
#include "WowConnectionManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/M2Types.h"
#include "AudioMixerBlueprintLibrary.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "GameFramework/HUD.h"
#include "Misc/App.h"

AWowLoginTestGameMode::AWowLoginTestGameMode(){PrimaryActorTick.bCanEverTick=true;HUDClass=AHUD::StaticClass();}
void AWowLoginTestGameMode::SetupTestScene(UWorld* World,AWowWorldManager* Manager)
{
    Started=Changed=FPlatformTime::Seconds();FString Spec,Text;TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    // Hidden replay windows are unfocused. Record the real hardware mix with
    // focus attenuation disabled for this opt-in QA game mode only.
    FApp::SetUnfocusedVolumeMultiplier(1);FApp::SetVolumeMultiplier(1);
    FParse::Value(FCommandLine::Get(),TEXT("loginsteps="),Spec);FParse::Value(FCommandLine::Get(),TEXT("loginoutput="),Output);
    if(Output.IsEmpty() || !FFileHelper::LoadFileToString(Text,*Spec) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root) || !Root->TryGetArrayField(TEXT("steps"),Rows))
    {Finish(false,TEXT("Missing login specification"));return;}Steps=*Rows;
    Controller=World->SpawnActorDeferred<AWowLoginController>(AWowLoginController::StaticClass(),FTransform::Identity);Controller->WorldManager=Manager;
    Controller->FinishSpawning(FTransform::Identity);
    for(const auto* Name:{TEXT("sound_enableall"),TEXT("sound_enablemusic"),TEXT("sound_enableambience"),TEXT("sound_enablesfx")}) Controller->MenuAudio->SetUiOption(Name,TEXT("1"));
    Controller->MenuAudio->SetUiOption(TEXT("sound_mastervolume"),TEXT("1"));Controller->MenuAudio->SetUiOption(TEXT("sound_musicvolume"),TEXT(".5"));
    Controller->LoginWidget->SetCredentials(TEXT("127.0.0.1:3724"),TEXT(""),TEXT(""));
    Controller->LoginWidget->OnLoginSubmit.BindLambda([this](const FString&,int32,const FString&,const FString&){++Submits;});
    if(!Controller->LoginScene){Finish(false,TEXT("Original scene not created"));return;}
    UAudioMixerBlueprintLibrary::StartRecordingOutput(this,180);Recording=true;
}
void AWowLoginTestGameMode::Tick(float Delta)
{
    Super::Tick(Delta);if(Finished || !Controller) return;const double Now=FPlatformTime::Seconds();
    if(Now-Started>240){Finish(false,TEXT("Login replay timeout"));return;}
    if(!FDbcStore::Get().IsDeferredLoaded()) return;
    if(Phase==2){if(Now-Changed>2) Finish(true);return;}
    if(Phase==3){if(Now-Changed>1.5){FScreenshotRequest::RequestScreenshot(Output/(PendingCapture+TEXT(".png")),true,false);Phase=1;}return;}
    if(Phase==1){if(!FScreenshotRequest::IsScreenshotRequested()){++Index;Phase=0;}return;}
    if(Index>=Steps.Num())
    {
        if(Recording){UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("mixer"),Output);Recording=false;}
        Changed=Now;Phase=2;return;
    }
    const auto S=Steps[Index]->AsObject();if(Now-Started<S->GetNumberField(TEXT("at"))) return;
    const FString Action=S->GetStringField(TEXT("action"));auto* Audio=Controller->MenuAudio.Get();
    if(Action==TEXT("capture")){}
    else if(Action==TEXT("expansion"))
    {
        const int32 Version=S->GetIntegerField(TEXT("version"));
        auto Widget=Controller->LoginWidget;
        if (!Widget || Version<0 || Version>=int32(EWowExpansion::Count))
        {Finish(false,TEXT("Invalid expansion step"));return;}
        if (S->HasField(TEXT("server")))
            Widget->SetCredentials(S->GetStringField(TEXT("server")),TEXT("offline"),TEXT("draft"));
        if (!Widget->PressStockControl(FName(*FString::Printf(TEXT("Expansion_%d"),Version))) ||
            int32(Widget->GetCurrentExpansion())!=Version)
        {Finish(false,TEXT("Footer did not select the requested expansion"));return;}
        const bool Wrath=Version==int32(EWowExpansion::WrathOfTheLichKing);
        if (IsValid(Controller->LoginScene)!=Wrath || (!Wrath && Audio->IsGlueMusicPlaying()))
        {Finish(false,TEXT("Expansion scene or audio lifetime mismatch"));return;}
        if (!Wrath)
        {
            const int32 Previous=Submits;
            Widget->AutoSubmit();
            if(Submits!=Previous){Finish(false,TEXT("Unavailable expansion dispatched login"));return;}
        }
        else if(S->HasField(TEXT("expectedPort")))
        {
            bool Matched=false;
            Widget->OnLoginSubmit.BindLambda([&](const FString& Host,int32 Port,const FString& User,const FString& Password)
            {Matched=Host==TEXT("localhost") && Port==S->GetIntegerField(TEXT("expectedPort")) && User==TEXT("offline") && Password==TEXT("draft");});
            Widget->PressStockControl(TEXT("Login"));
            Widget->OnLoginSubmit.BindLambda([this](const FString&,int32,const FString&,const FString&){++Submits;});
            if(!Matched){Finish(false,TEXT("Login draft changed across expansion tabs"));return;}
            Widget->SetStatusText(FString());
        }
    }
    else if(Action==TEXT("musicOff")){Audio->SetUiOption(TEXT("sound_enablesfx"),TEXT("0"));Audio->SetUiOption(TEXT("sound_enableambience"),TEXT("0"));Audio->SetUiOption(TEXT("sound_enablemusic"),TEXT("0"));}
    else if(Action==TEXT("musicOn")) Audio->SetUiOption(TEXT("sound_enablemusic"),TEXT("1"));
    else if(Action==TEXT("masterOff")) Audio->SetUiOption(TEXT("sound_mastervolume"),TEXT("0"));
    else if(Action==TEXT("masterOn")) Audio->SetUiOption(TEXT("sound_mastervolume"),TEXT("1"));
    else if(Action==TEXT("quiet")) Audio->SetUiOption(TEXT("sound_musicvolume"),TEXT(".1"));
    else if(Action==TEXT("normal")) Audio->SetUiOption(TEXT("sound_musicvolume"),TEXT(".5"));
    else if(Action==TEXT("options")){if(!Controller->LoginWidget || !Controller->LoginWidget->PressStockControl(TEXT("Options"))){Finish(false,TEXT("Options input failed"));return;}}
    else if(Action==TEXT("invalid")){Controller->LoginWidget->PressStockControl(TEXT("Login"));if(Submits){Finish(false,TEXT("Empty credentials dispatched"));return;}}
    else if(Action==TEXT("error")) Controller->SetStatusText(TEXT("Unable to connect. Please try again later."));
    else if(Action==TEXT("realm"))
    {
        Retired.Add(Controller->LoginScene);TArray<FWowRealmInfo> Realms;FWowRealmInfo R;R.Name=TEXT("Offline QA");Realms.Add(R);Controller->ShowRealmSelectScreen(Realms);++Cycles;
    }
    else if(Action==TEXT("select")){Retired.Add(Controller->LoginScene);Retired.Add(Controller->CharacterPreview);Controller->ShowCharacterSelectScreen({});++Cycles;}
    else if(Action==TEXT("create")){Retired.Add(Controller->CharacterPreview);Controller->ShowCharacterCreateScreen();++Cycles;}
    else if(Action==TEXT("loading")){Retired.Add(Controller->CharacterPreview);Controller->ClearCurrentScreen();Audio->SetGlueMode(false);++Cycles;}
    else if(Action==TEXT("login"))
    {
        Retired.Add(Controller->CharacterPreview);Retired.Add(Controller->LoginScene);Controller->ShowLoginScreen();Controller->LoginWidget->SetCredentials(TEXT("127.0.0.1:3724"),TEXT(""),TEXT(""));
        Controller->LoginWidget->OnLoginSubmit.BindLambda([this](const FString&,int32,const FString&,const FString&){++Submits;});++Cycles;
    }
    else if(Action==TEXT("gc"))
    {
        CollectGarbage(RF_NoFlags,true);for(const auto& A:Retired) if(A.IsValid()){Finish(false,TEXT("Retired menu actor survived cleanup"));return;}Retired.Reset();
    }
    else {Finish(false,TEXT("Unknown login replay action"));return;}
    auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("action"),Action);Row->SetNumberField(TEXT("elapsed"),Now-Started);
    Row->SetBoolField(TEXT("playing"),Audio->IsGlueMusicPlaying());Row->SetNumberField(TEXT("pcmBytes"),Audio->GetGlueGeneratedBytes());Row->SetNumberField(TEXT("gain"),Audio->GetMusicGain());Row->SetNumberField(TEXT("effects"),Audio->GetGlueEffectCount());
    int32 Scenes=0,Previews=0,Audios=0;for(TActorIterator<AWowLoginScene> It(GetWorld());It;++It) ++Scenes;for(TActorIterator<AWowCharacterPreview> It(GetWorld());It;++It) ++Previews;for(TActorIterator<AWowAudioManager> It(GetWorld());It;++It) ++Audios;
    Row->SetNumberField(TEXT("scenes"),Scenes);Row->SetNumberField(TEXT("previews"),Previews);
    if(Scenes>1 || Previews>1 || Audios!=1 || (Scenes && Previews)){Finish(false,TEXT("Duplicate menu resource owner"));return;}
    if(Controller->LoginScene)
    {
        auto* Scene=Controller->LoginScene.Get();const auto* Renderer=Scene->GetRenderer();Row->SetNumberField(TEXT("ageMs"),Scene->GetAgeMs());Row->SetNumberField(TEXT("particles"),Renderer->GetParticleCount());
        Row->SetStringField(TEXT("camera"),Scene->GetCameraPosition().ToString());
        if(Scene->GetModel()->Cameras[0].PositionTrack.Sequences[0].Keys.Num()<2){Finish(false,TEXT("Camera animation missing"));return;}
    }
    Results.Add(MakeShared<FJsonValueObject>(Row));FString CaptureName;
    if(S->TryGetStringField(TEXT("capture"),CaptureName))
    {
        if(Action==TEXT("realm") || Action==TEXT("select") || Action==TEXT("create") || Action==TEXT("login") || Action==TEXT("expansion")){PendingCapture=CaptureName;Changed=Now;Phase=3;}
        else {FScreenshotRequest::RequestScreenshot(Output/(CaptureName+TEXT(".png")),true,false);Phase=1;}
    }
    else ++Index;
}
void AWowLoginTestGameMode::Finish(bool Passed,const FString& Failure)
{
    if(Finished) return;Finished=true;
    if(Recording){UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("mixer"),Output);Recording=false;}
    auto Root=MakeShared<FJsonObject>();Root->SetBoolField(TEXT("passed"),Passed);Root->SetStringField(TEXT("failure"),Failure);Root->SetNumberField(TEXT("cycles"),Cycles);
    Root->SetBoolField(TEXT("networkLogin"),false);Root->SetBoolField(TEXT("actionsAreSpies"),true);Root->SetArrayField(TEXT("steps"),Results);
    FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Output/TEXT("runtime.json")));
    // Tick tasks for the later tick groups may already be queued. Let the
    // engine reclaim destroyed actors after the frame, never GC them here.
    if(Controller){Controller->ClearCurrentScreen();Controller->Destroy();Controller=nullptr;}
    UE_LOG(LogTemp,Log,TEXT("LOGIN_REPLAY_%s: %s"),Passed?TEXT("PASS"):TEXT("FAIL"),*Failure);FPlatformMisc::RequestExitWithStatus(false,Passed?0:3);
}
void AWowLoginTestGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
    if(Controller){Controller->Destroy();Controller=nullptr;}Super::EndPlay(Reason);
}
