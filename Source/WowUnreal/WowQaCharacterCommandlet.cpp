#include "WowQaCharacterCommandlet.h"
#include "WowConnectionManager.h"
#include "Async/TaskGraphInterfaces.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowQaCharacter,Log,All);

int32 UWowQaCharacterCommandlet::Main(const FString& Params)
{
    FString Path,Mode,Text,Account,Password,Name,Token;
    TSharedPtr<FJsonObject> Config;
    if(!FParse::Value(*Params,TEXT("fixture="),Path) || !FParse::Value(*Params,TEXT("mode="),Mode)
        || FPaths::GetCleanFilename(Path)!=TEXT("wowunreal-ui-primary-fixture.json") || (Mode!=TEXT("prepare") && Mode!=TEXT("delete"))
        || !FFileHelper::LoadFileToString(Text,*Path) || Text.Len()>16384
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Config) || !Config) return 1;
    if(!Config->TryGetStringField(TEXT("account"),Account) || !Config->TryGetStringField(TEXT("password"),Password)
        || !Config->TryGetStringField(TEXT("character"),Name) || !Config->TryGetStringField(TEXT("token"),Token)
        || Account.Len()!=14 || !Account.StartsWith(TEXT("QA_UI_")) || Password.IsEmpty() || Password.Len()>64
        || !Name.StartsWith(TEXT("Uiq")) || Name.Len()<8 || Name.Len()>12 || Token.Len()!=32) return 1;
    for(int32 I=6;I<Account.Len();++I) if(!FChar::IsHexDigit(Account[I])) return 1;
    for(TCHAR C:Name) if(!FChar::IsAlpha(C) || C>127) return 1;
    for(TCHAR C:Token) if(!FChar::IsHexDigit(C)) return 1;
    double ExpectedNumber=0; Config->TryGetNumberField(TEXT("guid"),ExpectedNumber);
    if(ExpectedNumber<0 || ExpectedNumber>MAX_uint32 || FMath::FloorToDouble(ExpectedNumber)!=ExpectedNumber) return 1;
    const uint64 Expected=uint64(ExpectedNumber); if(Mode==TEXT("delete") && !Expected) return 1;

    TStrongObjectPtr<UWowConnectionManager> Client(NewObject<UWowConnectionManager>());
    Client->Login(Account,Password,TEXT("127.0.0.1"),3724);
    const double End=FPlatformTime::Seconds()+90;
    bool Creating=false,Deleting=false; uint64 Guid=0; FString Result;
    while(FPlatformTime::Seconds()<End)
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        FTSTicker::GetCoreTicker().Tick(.01f);
        const auto State=Client->GetState();
        if(State==EWowSessionState::Error || State==EWowSessionState::Disconnected) break;
        if(State==EWowSessionState::AuthHaveRealmList)
        {
            const auto Realms=Client->GetCachedRealms();
            if(Realms.Num()!=1 || Realms[0].Address!=TEXT("127.0.0.1") || Realms[0].Port!=8085) break;
            Client->SelectRealm(0);
        }
        else if(State==EWowSessionState::WorldAuthenticated) Client->RequestCharacterList();
        else if(State==EWowSessionState::WorldHaveCharList)
        {
            const auto Characters=Client->GetCachedCharacters();
            if(Characters.Num()>1 || (Characters.Num()==1 && Characters[0].Name!=Name)) break;
            if(Characters.Num()==1)
            {
                Guid=uint64(Characters[0].Guid); if(Expected && Guid!=Expected) break;
                if(Mode==TEXT("prepare")) {Result=TEXT("prepared"); break;}
                if(!Deleting) {Deleting=true; Client->DeleteCharacter(int64(Guid));}
            }
            else if(Mode==TEXT("delete")) {Result=TEXT("deleted"); break;}
            else if(!Creating) {Creating=true; Client->CreateCharacter(Name,1,8,0);}
        }
        FPlatformProcess::Sleep(.01f);
    }
    Client->Disconnect();
    if(Result.IsEmpty()) {UE_LOG(LogWowQaCharacter,Error,TEXT("Owned QA character operation failed or timed out")); return 2;}
    const auto Status=MakeShared<FJsonObject>(); Status->SetStringField(TEXT("token"),Token);
    Status->SetStringField(TEXT("state"),Result); Status->SetNumberField(TEXT("guid"),double(Guid));
    FString Output; FJsonSerializer::Serialize(Status,TJsonWriterFactory<>::Create(&Output));
    if(!FFileHelper::SaveStringToFile(Output,*(Path+TEXT(".status.json")))) return 3;
    UE_LOG(LogWowQaCharacter,Display,TEXT("Owned QA character %s"),*Result); return 0;
}
