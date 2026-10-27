#include "WowUiTestPeer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Formats/Dbc/DbcStore.h"

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
    Client.Reset(NewObject<UWowConnectionManager>());
    Client->PacketHandler.OnTeleportRequest.AddLambda([this](uint64 Guid,uint32 Order,uint32 Time,FVector Position,float Orientation)
    {
        if(!Client.IsValid() || Guid!=Client->PacketHandler.EntityManager.LocalPlayerGuid) return;
        if(auto* P=Client->PacketHandler.EntityManager.GetLocalPlayer()) {P->Movement.Position=Position; P->Movement.Orientation=Orientation;}
        Client->SendTeleportAck(Guid,Order,Time);
    });
    Client->PacketHandler.OnMapTransfer.AddLambda([this](uint32,float,float,float,float)
    {
        // NEW_WORLD has already updated the peer's authoritative map/position.
        if(Client.IsValid()) Client->SendRawPacket(WowOpcode::MSG_MOVE_WORLDPORT_ACK,{});
    });
    Client->Login(Account,Password,TEXT("127.0.0.1"),3724); Status(TEXT("connecting")); return true;
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
    if(Action.StartsWith(TEXT("lfg_")))
    {
        const auto& H=Client->PacketHandler; const auto* P=H.EntityManager.GetLocalPlayer();
        const bool Solo=H.GroupInfo.IsEmpty() && (Action==TEXT("lfg_join") || Action==TEXT("lfg_proposal") || Action==TEXT("lfg_leave") || Action==TEXT("lfg_comment"));
        if(!P || (!Solo && !H.GroupInfo.FindMember(MasterGuid))) return false;
        const uint8 Class=P->GetClassId(); const uint32 Available=9u|((Class==1 || Class==2 || Class==6 || Class==11)?2u:0u)|((Class==2 || Class==5 || Class==7 || Class==11)?4u:0u);
        const auto Packet=[&](uint32 Op,const TArray<uint8>& Bytes){Client->SendRawPacket(Op,Bytes); return true;};
        const auto Append=[](TArray<uint8>& Bytes,auto V){Bytes.Append(reinterpret_cast<const uint8*>(&V),sizeof(V));}; TArray<uint8> Bytes;
        if(Action==TEXT("lfg_join"))
        {
            const auto* D=FDbcStore::Get().LfgUi().Dungeon(Value); if(!D || (!Solo && H.GroupInfo.LeaderGuid!=CharacterGuid) || Slot<0 || (uint32(Slot)&~Available) || !(Slot&14)) return false;
            Append(Bytes,uint32(Slot)); Bytes.Add(0); Bytes.Add(0); Bytes.Add(1); Append(Bytes,D->Entry()); Bytes.Add(3); Bytes.AddZeroed(4); return Packet(0x35C,Bytes);
        }
        if(Action==TEXT("lfg_role"))
        {
            if(H.Lfg.RoleCheck.State!=2 || Slot<0 || (uint32(Slot)&~Available)) return false; Bytes.Add(uint8(Slot)); return Packet(0x36A,Bytes);
        }
        if(Action==TEXT("lfg_proposal"))
        {
            const auto& Proposal=H.Lfg.Proposal; if(!Proposal.Valid || Proposal.State!=0 || Slot<0 || Slot>1) return false;
            for(const auto& M:Proposal.Members) if(M.Self && M.Answered) return true;
            Append(Bytes,Proposal.Id); Bytes.Add(uint8(Slot)); return Packet(0x362,Bytes);
        }
        if(Action==TEXT("lfg_teleport")) {if(!H.GroupInfo.IsLfgGroup() || Slot<0 || Slot>1) return false; Bytes.Add(uint8(Slot)); return Packet(0x370,Bytes);}
        if(Action==TEXT("lfg_vote")) {if(!H.Lfg.Boot.Progress || Slot<0 || Slot>1) return false; if(H.Lfg.Boot.Voted) return true; Bytes.Add(uint8(Slot)); return Packet(0x36C,Bytes);}
        if(Action==TEXT("lfg_boot")) {if(!H.GroupInfo.IsLfgGroup() || !Value || uint64(Value)==CharacterGuid || !H.GroupInfo.FindMember(Value)) return false; Append(Bytes,uint64(Value)); const char* Reason="Owned QA vote verification"; Bytes.Append(reinterpret_cast<const uint8*>(Reason),FCStringAnsi::Strlen(Reason)+1); return Packet(0x76,Bytes);}
        if(Action==TEXT("lfg_leave")) return Packet(0x35D,Bytes);
        if(Action==TEXT("lfg_comment")) {const char* Comment="Owned QA raid listing"; Bytes.Append(reinterpret_cast<const uint8*>(Comment),FCStringAnsi::Strlen(Comment)+1); return Packet(0x366,Bytes);}
        return false;
    }
    if(Action==TEXT("arena_queue"))
    {
        if(Slot<0 || Slot>2 || Value!=19911) return false;
        const auto& H=Client->PacketHandler;
        const auto* P=H.EntityManager.GetLocalPlayer(); uint64 Vendor=0;
        for(const auto& Pair:H.EntityManager.GetAll()) if(Pair.Value->IsUnit() && Pair.Value->Entry==Value && P
            && FVector::DistSquared(P->Movement.Position,Pair.Value->Movement.Position)<400) {Vendor=Pair.Key; break;}
        if(!Vendor) return false;
        TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(&Vendor),8); Bytes.Add(uint8(Slot)); Bytes.Add(0); Bytes.Add(0);
        Client->SendRawPacket(0x358,Bytes); return true;
    }
    if(Action==TEXT("arena_enter") || Action==TEXT("arena_leave"))
    {
        const auto& H=Client->PacketHandler;
        for(const auto& Q:H.Pvp.Queues) if(Q.Status && Q.Arena)
        {
            if(Action==TEXT("arena_enter") && Q.Status!=2) return Q.Status==3;
            TArray<uint8> Bytes; Bytes.Add(Q.Arena); Bytes.Add(0); Bytes.Append(reinterpret_cast<const uint8*>(&Q.Type),4);
            const uint16 Marker=0x1F90; Bytes.Append(reinterpret_cast<const uint8*>(&Marker),2);
            if(Action==TEXT("arena_enter") || Q.Status!=3) Bytes.Add(Action==TEXT("arena_enter")?1:0);
            Client->SendRawPacket(Q.Status==3 && Action==TEXT("arena_leave")?0x2E1:0x2D5,Bytes); return true;
        }
        return Action==TEXT("arena_leave");
    }
    if(Action==TEXT("arena_accept"))
    {
        auto& H=Client->PacketHandler; const FString MasterName=H.PlayerNameCache.FindRef(MasterGuid);
        if(MasterName.IsEmpty() || H.Pvp.Inviter!=MasterName || H.Pvp.Invitation.IsEmpty()) return false;
        Client->SendRawPacket(0x351,{}); H.Pvp.Inviter.Empty(); H.Pvp.Invitation.Empty(); return true;
    }
    if(Action==TEXT("guild_accept"))
    {
        auto& H=Client->PacketHandler; const FString MasterName=H.PlayerNameCache.FindRef(MasterGuid);
        if(MasterName.IsEmpty() || H.GuildInviter!=MasterName || H.GuildInvitation.IsEmpty()) return false;
        Client->SendRawPacket(0x84,{}); H.GuildInviter.Empty(); H.GuildInvitation.Empty(); return true;
    }
    if(Action==TEXT("guild_leave"))
    {
        const auto* P=Client->PacketHandler.EntityManager.GetLocalPlayer();
        if(!P || !P->GetField(PlayerField::GUILD_ID) || P->GetField(PlayerField::GUILD_RANK)==0) return false;
        Client->SendRawPacket(0x8D,{}); return true;
    }
    if(Action==TEXT("loot_roll"))
    {
        if(Slot<0 || Slot>3 || !Value || !Client->PacketHandler.GroupInfo.FindMember(MasterGuid)) return false;
        FWowLootRoll* Selected=nullptr;
        for(auto& Pair:Client->PacketHandler.LootRolls) if(Pair.Value.ItemId==Value && !Pair.Value.Finished && Pair.Value.Choice<0 && Pair.Value.PendingChoice<0 && Pair.Value.MillisecondsLeft()>0)
        {if(Selected) return false; Selected=&Pair.Value;}
        if(!Selected || !(Selected->VoteMask&(1<<Slot))) return false;
        TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(&Selected->Guid),8); Bytes.Append(reinterpret_cast<const uint8*>(&Selected->Slot),4); Bytes.Add(uint8(Slot));
        Client->SendRawPacket(0x02A0,Bytes); Selected->PendingChoice=Slot; return true;
    }
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
