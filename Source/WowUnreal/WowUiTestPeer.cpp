#include "WowUiTestPeer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
TSharedPtr<FJsonObject> ReadPeerJson(const FString& Path)
{
    FString Text; TSharedPtr<FJsonObject> Json;
    if(FFileHelper::LoadFileToString(Text,*Path)) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json);
    return Json;
}
}
bool FWowUiTestPeer::Start(const FString& Fixture,uint64 MainGuid,bool CleanupOnly)
{
    if(Client.IsValid() || FPaths::GetCleanFilename(Fixture)!=TEXT("wowunreal-ui-peer-fixture.json")) return false;
    const auto Config=ReadPeerJson(Fixture); FString Account,Password;
    if(!Config || !Config->TryGetStringField(TEXT("account"),Account) || !Config->TryGetStringField(TEXT("password"),Password) || !Config->TryGetStringField(TEXT("character"),Character) || !Config->TryGetStringField(TEXT("token"),Token)) return false;
    if(!Account.StartsWith(TEXT("QA_UI_")) || !Character.StartsWith(TEXT("Uiq")) || Character.Len()>12 || Token.Len()!=32 || uint64(Config->GetNumberField(TEXT("masterGuid")))!=MainGuid) return false;
    for(TCHAR C:Token) if(!FChar::IsHexDigit(C)) return false;
    for(TCHAR C:Character) if(!FChar::IsAlpha(C)) return false;
    Path=Fixture; MasterGuid=MainGuid; Cleanup=CleanupOnly;
    Client.Reset(NewObject<UWowConnectionManager>()); Client->Login(Account,Password,TEXT("127.0.0.1"),3724); Status(TEXT("connecting")); return true;
}
void FWowUiTestPeer::Status(const FString& Phase)
{
    if(Path.IsEmpty() || LastPhase==Phase) return;
    LastPhase=Phase; const auto Json=MakeShared<FJsonObject>(); Json->SetStringField(TEXT("token"),Token); Json->SetStringField(TEXT("phase"),Phase); Json->SetNumberField(TEXT("guid"),double(CharacterGuid));
    FString Text; FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text)); FFileHelper::SaveStringToFile(Text,*(Path+TEXT(".status.json")));
}
void FWowUiTestPeer::Fail(const TCHAR* Message)
{
    if(Failure.IsEmpty()) Failure=Message; Status(TEXT("error")); Stop();
}
bool FWowUiTestPeer::Ready() const
{
    return Client.IsValid() && Client->GetState()==EWowSessionState::WorldInGame && Client->PacketHandler.EntityManager.GetLocalPlayer() && !Stopping && Failure.IsEmpty();
}
void FWowUiTestPeer::Stop()
{
    if(Stopping || Finished) return;
    Stopping=true; StopAt=FPlatformTime::Seconds();
    if(!Client.IsValid()) { Finished=true; return; }
    if(auto* Player=Client->PacketHandler.EntityManager.GetLocalPlayer()) Client->SendMovement(WowOpcode::MSG_MOVE_STOP,Player->Movement.Position,Player->Movement.Orientation,0);
    Walking=false;
    Client->SendCancelTrade();
    if(Client->PacketHandler.GroupInfo.FindMember(MasterGuid)) Client->SendRawPacket(0x007B,{});
    if(Client->GetState()==EWowSessionState::WorldInGame) Client->RequestLogout();
}
void FWowUiTestPeer::Tick()
{
    if(!Client.IsValid() || Finished) return;
    const double Now=FPlatformTime::Seconds(); const auto State=Client->GetState();
    if(Stopping && Now-StopAt>65) { Client->Disconnect(); Finished=true; Failure=TEXT("QA peer cleanup timed out"); Status(TEXT("cleanup_failed")); return; }
    if(State==EWowSessionState::Error || State==EWowSessionState::Disconnected)
    {
        if(Stopping && !CharacterGuid) {Finished=true; Status(TEXT("deleted")); return;}
        if(!Stopping) Fail(TEXT("QA peer connection failed")); return;
    }
    if(State==EWowSessionState::AuthHaveRealmList)
    {
        const auto Realms=Client->GetCachedRealms();
        if(Realms.Num()!=1 || Realms[0].Address!=TEXT("127.0.0.1") || Realms[0].Port!=8085) { Fail(TEXT("QA peer requires the project loopback realm")); return; }
        Client->SelectRealm(0); return;
    }
    if(State==EWowSessionState::WorldAuthenticated) {Client->RequestCharacterList(); return;}
    if(State==EWowSessionState::WorldHaveCharList)
    {
        const auto Characters=Client->GetCachedCharacters();
        if(Characters.Num()>1 || (Characters.Num()==1 && Characters[0].Name!=Character)) {Fail(TEXT("QA peer account contains an unexpected character")); return;}
        if(Characters.Num()==1) CharacterGuid=uint64(Characters[0].Guid);
        if(Stopping || Cleanup)
        {
            if(Characters.IsEmpty()) {Client->Disconnect(); Finished=true; Status(TEXT("deleted")); return;}
            if(!Deleting) {Deleting=true; Client->DeleteCharacter(int64(CharacterGuid));} return;
        }
        if(Characters.IsEmpty())
        {
            if(!Creating) {Creating=true; Client->CreateCharacter(Character,1,1,0);} return;
        }
        Status(TEXT("character_ready"));
        const auto Gate=ReadPeerJson(Path+TEXT(".ready.json"));
        if(!Entering && Gate && Gate->GetStringField(TEXT("token"))==Token && uint64(Gate->GetNumberField(TEXT("guid")))==CharacterGuid && Gate->GetBoolField(TEXT("login"))) {Entering=true; Client->EnterWorld(int64(CharacterGuid));}
        return;
    }
    if(State==EWowSessionState::WorldInGame)
    {
        if(Stopping) return;
        if(!WorldReady)
        {
            WorldReady=true; TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&CharacterGuid),8);
            Client->SendRawPacket(0x026A,Data); Client->SendRawPacket(0x00FC,{}); Client->SendNameQuery(int64(MasterGuid)); Status(TEXT("in_world"));
        }
        if(Now-LastPing>10) {Client->SendKeepAlive(); LastPing=Now;}
        if(Walking && Now-WalkAt>=.1)
        {
            const double Elapsed=FMath::Min(Now-WalkAt,.25); WalkAt=Now;
            WalkDistance=FMath::Min(WalkDistance+Elapsed*6.,double(FMath::Abs(WalkRequest)));
            const FVector Position=WalkOrigin+FVector(FMath::Sign(WalkRequest)*WalkDistance,0,0);
            const bool Done=WalkDistance>=FMath::Abs(WalkRequest);
            if(auto* Player=Client->PacketHandler.EntityManager.GetLocalPlayer()) {Player->Movement.Position=Position; Player->Movement.Orientation=WalkRequest>=0?0:PI;}
            Client->SendMovement(Done?WowOpcode::MSG_MOVE_STOP:WowOpcode::MSG_MOVE_HEARTBEAT,Position,WalkRequest>=0?0:PI,Done?0:1);
            if(Done) {Walking=false; WalkComplete=true;}
        }
        auto& T=Client->PacketHandler.CurrentTrade;
        if(T.TraderGuid && T.TraderGuid!=MasterGuid) {Client->SendCancelTrade(); return;}
        if(T.Status==WowTradeStatus::BEGIN_TRADE) Client->SendBeginTrade();
        Client->TickTradeState();
    }
}
bool FWowUiTestPeer::Command(const FString& Action,int32 Slot,uint32 Value)
{
    if(!Ready()) return false;
    if(Action.StartsWith(TEXT("group_")))
    {
        auto& H=Client->PacketHandler; const auto& G=H.GroupInfo;
        const FString MasterName=H.PlayerNameCache.FindRef(MasterGuid);
        if(Action==TEXT("group_invite"))
        {
            if(MasterName.IsEmpty()) return false;
            FTCHARToUTF8 Name(*MasterName); TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Name.Get()),Name.Length()); Bytes.Add(0); Bytes.AddZeroed(4);
            Client->SendRawPacket(0x006E,Bytes); return true;
        }
        if(Action==TEXT("group_accept"))
        {
            if(MasterName.IsEmpty() || H.PendingGroupInviter!=MasterName) return false;
            TArray<uint8> Bytes; Bytes.AddZeroed(4); Client->SendRawPacket(0x0072,Bytes); H.PendingGroupInviter.Empty(); return true;
        }
        if(!G.FindMember(MasterGuid)) return false;
        if(Action==TEXT("group_promote"))
        {
            if(G.LeaderGuid!=CharacterGuid) return false;
            TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(&MasterGuid),8); Client->SendRawPacket(0x0078,Bytes); return true;
        }
        if(Action==TEXT("group_ready")) {if(!H.ReadyCheck.bActive) return false; Client->SendReadyCheckConfirm(true); return true;}
        if(Action==TEXT("group_leave")) {Client->SendRawPacket(0x007B,{}); return true;}
        if(Action==TEXT("group_walk"))
        {
            if(Slot==0 || FMath::Abs(Slot)>150 || H.CurrentMapId!=0) return false;
            if(WalkComplete && WalkRequest==Slot) {WalkComplete=false; return true;}
            if(!Walking)
            {
                const auto* Player=H.EntityManager.GetLocalPlayer(); if(!Player) return false;
                WalkOrigin=Player->Movement.Position; WalkRequest=Slot; WalkAt=FPlatformTime::Seconds(); WalkDistance=0; Walking=true;
                Client->SendMovement(WowOpcode::MSG_MOVE_START_FORWARD,WalkOrigin,Slot>=0?0:PI,1);
            }
            return false;
        }
        return false;
    }
    auto& T=Client->PacketHandler.CurrentTrade;
    if(T.TraderGuid && T.TraderGuid!=MasterGuid) return false;
    if(Action==TEXT("initiate")) {Client->SendInitiateTrade(int64(MasterGuid)); return true;}
    if(Action==TEXT("cancel")) {Client->SendCancelTrade(); return true;}
    if(Action==TEXT("unaccept")) {Client->SendUnacceptTrade(); return true;}
    if(Action==TEXT("money") && Value<=100) {Client->SendSetTradeGold(int32(Value)); return true;}
    if(Action==TEXT("accept"))
    {
        // A typo in a probe must never complete a trade of the user's real items.
        if(T.PendingKind || !T.bTradeOpen || T.PlayerSpell || T.TargetSpell || T.PlayerMoney>100 || T.TargetMoney>100) return false;
        for(const auto& Item:T.TargetItems) if(Item.Slot<6 && Item.ItemId && Item.ItemId!=17184) return false;
        Client->SendAcceptTrade(); return true;
    }
    if(Action==TEXT("clear") && Slot>=1 && Slot<=7) return Client->ClickTradeItem(Slot-1,true);
    if(Action==TEXT("item") && Value==17184 && Slot>=1 && Slot<=6)
    {
        for(const auto* Item:Client->PacketHandler.EntityManager.GetInventoryItems()) if(Item->Entry==Value)
        {
            uint8 Bag=0,Position=0; if(!Client->FindCarriedItemPosition(Item->Guid,Bag,Position)) continue;
            Client->RequestItemTemplate(Item->Entry); if(!Client->PacketHandler.ItemTemplates.Contains(Item->Entry)) return false;
            Client->PickupBagItemCursor(Bag,Position,Item->Entry); return Client->ClickTradeItem(Slot-1);
        }
    }
    return false;
}
