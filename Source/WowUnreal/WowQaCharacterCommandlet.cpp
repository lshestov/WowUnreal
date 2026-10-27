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
        || FPaths::GetCleanFilename(Path)!=TEXT("wowunreal-ui-primary-fixture.json") || (Mode!=TEXT("prepare") && Mode!=TEXT("delete") && Mode!=TEXT("disband") && Mode!=TEXT("disband_arena"))
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
    const uint64 Expected=uint64(ExpectedNumber); if(Mode!=TEXT("prepare") && !Expected) return 1;
    double ClassNumber=8; Config->TryGetNumberField(TEXT("class"),ClassNumber); if(ClassNumber!=1 && ClassNumber!=8) return 1;
    FString GuildName; double GuildNumber=0; TSet<uint64> Owned;
    const bool ArenaDisband=Mode==TEXT("disband_arena");
    if(Mode==TEXT("disband") || ArenaDisband)
    {
        if(!Config->TryGetStringField(ArenaDisband?TEXT("qaArenaName"):TEXT("qaGuildName"),GuildName) || !Config->TryGetNumberField(ArenaDisband?TEXT("qaArenaId"):TEXT("qaGuildId"),GuildNumber)
            || !GuildName.StartsWith(ArenaDisband?TEXT("QA Arena "):TEXT("QA UI ")) || GuildName.Len()!=(ArenaDisband?17:14) || GuildNumber<1 || GuildNumber>MAX_uint32 || FMath::FloorToDouble(GuildNumber)!=GuildNumber) return 1;
        Owned.Add(Expected); const TSharedPtr<FJsonObject>* Secondary=nullptr;
        if(Config->TryGetObjectField(TEXT("secondary"),Secondary)) {double Peer=0; (*Secondary)->TryGetNumberField(TEXT("guid"),Peer); if(Peer>=1 && Peer<=MAX_uint32) Owned.Add(uint64(Peer));}
    }

    TStrongObjectPtr<UWowConnectionManager> Client(NewObject<UWowConnectionManager>());
    Client->Login(Account,Password,TEXT("127.0.0.1"),3724);
    const double End=FPlatformTime::Seconds()+90;
    bool Creating=false,Deleting=false,Entering=false,Queried=false,Disbanding=false; uint64 Guid=0; FString Result;
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
                if(Mode==TEXT("disband") || ArenaDisband) {if(!Entering) {Entering=true; Client->EnterWorld(int64(Guid));}}
                else if(!Deleting) {Deleting=true; Client->DeleteCharacter(int64(Guid));}
            }
            else if(Mode==TEXT("delete")) {Result=TEXT("deleted"); break;}
            else if(!Creating) {Creating=true; Client->CreateCharacter(Name,1,uint8(ClassNumber),0);}
        }
        else if(State==EWowSessionState::WorldInGame && ArenaDisband)
        {
            auto& H=Client->PacketHandler; const auto* P=H.EntityManager.GetLocalPlayer();
            if(P)
            {
                int32 Slot=-1; for(int32 I=0;I<3;++I) if(P->GetField(UnitField::END+0x454+I*7)==uint32(GuildNumber)) Slot=I;
                if(Slot<0) {if(Disbanding) Result=TEXT("arena_disbanded"); if(Disbanding || Queried) break;}
                else
                {
                    if(P->GetField(UnitField::END+0x454+Slot*7+2)!=0) break;
                    if(!Queried) {Queried=true; const uint32 Id=uint32(GuildNumber); TArray<uint8> B; B.Append(reinterpret_cast<const uint8*>(&Id),4); Client->SendRawPacket(0x34B,B); Client->SendRawPacket(0x34D,B);}
                    const auto* M=H.ArenaIdentities.Find(uint32(GuildNumber)); const auto* T=H.Pvp.ArenaTeams.Find(uint32(GuildNumber));
                    if(M && T && !T->Members.IsEmpty() && !Disbanding)
                    {
                        bool Safe=M->Name==GuildName; for(const auto& Member:T->Members) Safe&=Owned.Contains(Member.Guid);
                        if(!Safe) break; Disbanding=true; const uint32 Id=uint32(GuildNumber); TArray<uint8> B; B.Append(reinterpret_cast<const uint8*>(&Id),4); Client->SendRawPacket(0x355,B);
                    }
                }
            }
        }
        else if(State==EWowSessionState::WorldInGame && Mode==TEXT("disband"))
        {
            auto& H=Client->PacketHandler; const auto* P=H.EntityManager.GetLocalPlayer();
            if(P)
            {
                const uint32 Guild=P->GetField(PlayerField::GUILD_ID);
                if(!Guild && Disbanding) {Result=TEXT("disbanded"); break;}
                if(Guild!=uint32(GuildNumber) || P->GetField(PlayerField::GUILD_RANK)!=0) break;
                if(!Queried) {Queried=true; TArray<uint8> B; B.Append(reinterpret_cast<const uint8*>(&Guild),4); Client->SendRawPacket(0x54,B); Client->SendRawPacket(0x89,{});}
                const auto* G=H.Guilds.Find(Guild);
                if(G && !H.GuildRoster.IsEmpty() && !Disbanding)
                {
                    bool Safe=G->Name==GuildName; for(const auto& Member:H.GuildRoster) Safe&=Owned.Contains(Member.Guid);
                    if(!Safe) break; Disbanding=true; Client->SendRawPacket(0x8F,{});
                }
            }
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
