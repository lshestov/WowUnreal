#include "WowConnectionManager.h"
#include "Net/WowAuthSocket.h"
#include "Net/WowWorldSocket.h"
#include "WowOpcodes.h"
#include "Formats/Dbc/DbcStore.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowNet, Log, All);

namespace
{
    constexpr uint8 ACTION_BUTTON_TYPE_SPELL = 0x00;
    constexpr uint8 ACTION_BUTTON_TYPE_ITEM = 0x80;
    constexpr uint32 AUTO_ATTACK_SPELL_ID = 6603;
}

void UWowConnectionManager::SetState(EWowSessionState S)
{
    State = S;
    OnStateChanged.Broadcast(S);
}

void UWowConnectionManager::Login(const FString& U, const FString& P, const FString& S, int32 Port)
{
    UE_LOG(LogWowNet, Log, TEXT("Login: %s@%s:%d"), *U, *S, Port);

    // Disconnect any existing session
    Disconnect();

    CachedAccountName = U;
    SetState(EWowSessionState::AuthConnecting);

    AuthSocket = MakeShared<FWowAuthSocket>();
    AuthSocket->OnAuthResult.BindUObject(this, &UWowConnectionManager::OnAuthResultReceived);
    AuthSocket->OnRealmList.BindUObject(this, &UWowConnectionManager::OnRealmListReceived);

    if (!AuthSocket->Connect(S, Port, U, P))
    {
        UE_LOG(LogWowNet, Error, TEXT("Failed to connect to auth server"));
        SetState(EWowSessionState::Error);
        OnError.Broadcast(TEXT("Failed to connect to auth server"));
        AuthSocket.Reset();
        return;
    }

    SetState(EWowSessionState::AuthChallengeSent);
}

void UWowConnectionManager::OnAuthResultReceived(bool bSuccess)
{
    if (bSuccess)
    {
        UE_LOG(LogWowNet, Log, TEXT("Authentication successful"));
        SessionKey = AuthSocket->GetSessionKey();
        SetState(EWowSessionState::AuthProofSent);

        // Automatically request realm list
        SetState(EWowSessionState::AuthRequestingRealmList);
        AuthSocket->RequestRealmList();
    }
    else
    {
        UE_LOG(LogWowNet, Error, TEXT("Authentication failed"));
        SetState(EWowSessionState::Error);
        OnError.Broadcast(TEXT("Authentication failed"));
        AuthSocket.Reset();
    }
}

void UWowConnectionManager::OnRealmListReceived(const TArray<FWowRealmInfo>& Realms)
{
    UE_LOG(LogWowNet, Log, TEXT("Received %d realms"), Realms.Num());
    CachedRealms = Realms;
    SetState(EWowSessionState::AuthHaveRealmList);
    OnRealmList.Broadcast(Realms);
}

void UWowConnectionManager::SelectRealm(int32 I)
{
    UE_LOG(LogWowNet, Log, TEXT("Realm: %d"), I);

    if (!CachedRealms.IsValidIndex(I))
    {
        UE_LOG(LogWowNet, Error, TEXT("Invalid realm index: %d"), I);
        OnError.Broadcast(TEXT("Invalid realm index"));
        return;
    }

    const FWowRealmInfo& Realm = CachedRealms[I];
    CurrentRealmName=Realm.Name;
    UE_LOG(LogWowNet, Log, TEXT("Selected realm: %s id=%u (%s:%d)"), *Realm.Name, Realm.RealmId, *Realm.Address, Realm.Port);

    if (SessionKey.Num() == 0)
    {
        UE_LOG(LogWowNet, Error, TEXT("Cannot connect to world server: no session key (auth not completed)"));
        OnError.Broadcast(TEXT("No session key — authenticate first"));
        return;
    }

    SetState(EWowSessionState::WorldConnecting);

    // Disconnect auth socket -- no longer needed
    if (AuthSocket.IsValid())
    {
        AuthSocket->Disconnect();
        AuthSocket.Reset();
    }

    // Create and connect world socket
    WorldSocket = MakeShared<FWowWorldSocket, ESPMode::ThreadSafe>();
    WorldSocket->OnAuthResult.BindUObject(this, &UWowConnectionManager::OnWorldAuthResult);
    WorldSocket->OnCharacterList.BindUObject(this, &UWowConnectionManager::OnWorldCharacterList);

    // Route unhandled server packets through the packet handler (fires on game thread)
    WorldSocket->OnPacket.BindWeakLambda(this, [this](uint16 Opcode, const TArray<uint8>& Data)
    {
        // Intercept char create/delete results
        if(Opcode==0x004C && Data.Num()>=5)
        {
            FPacketReader Reader(Data); const uint32 Reason=Reader.ReadU32(); const bool Instant=Reader.ReadU8()!=0;
            OnLogoutResponse.Broadcast(Reason,Instant); return;
        }
        if(Opcode==0x004D)
        {
            OnLogoutComplete.Broadcast();
            TArray<uint64> Guids; PacketHandler.EntityManager.GetAll().GetKeys(Guids);
            for(uint64 Guid:Guids) PacketHandler.EntityManager.Remove(Guid);
            PacketHandler.EntityManager.LocalPlayerGuid=0; TargetGuid=0; CursorPayload=FCursorPayloadState();
            PacketHandler.KnownSpells.Reset(); PacketHandler.ActionButtons.Reset(); PacketHandler.QuestLog.Reset(); PacketHandler.ResetTalentState(); PacketHandler.Reputations.Reset();
            PacketHandler.SpellCooldowns.Reset(); PacketHandler.SpellCooldownInfo.Reset(); PacketHandler.CombatHistory.Reset();
            PacketHandler.GroupInfo=FWowGroupInfo(); PacketHandler.PetActionBar=FWowPetActionBarState();
            PacketHandler.Merchant.Clear();
            RepairCursor=false; PacketHandler.GuildCanRepair=false; PacketHandler.GuildRepairRequested=false; PacketHandler.GuildRepairId=0; PacketHandler.GuildWithdrawMoney=0; PacketHandler.GuildRoster.Reset(); PacketHandler.GuildRankAccess.Reset(); PacketHandler.GuildInfoText.Empty(); PacketHandler.ItemRefunds.Empty(); PacketHandler.RequestedItemRefunds.Empty(); PacketHandler.TotalPlayedTime=0; PacketHandler.PlayedTimeReceived=0; PacketHandler.PlayedTimeRequested=false;
            PacketHandler.WeaponProficiency=PacketHandler.ArmorProficiency=0; PacketHandler.HasWeaponProficiency=PacketHandler.HasArmorProficiency=false;
            RequestCharacterList(); return;
        }
        if (Opcode == WowOpcode::SMSG_CHAR_CREATE || Opcode == WowOpcode::SMSG_CHAR_DELETE)
        {
            uint8 Result = Data.Num() > 0 ? Data[0] : 0xFF;
            OnCharCreateResult.Broadcast(Result);
            // Auto-refresh character list on success (0x2F = CHAR_CREATE_SUCCESS, 0x3A = CHAR_DELETE_SUCCESS)
            if (Result == 0x2F || Result == 0x3A)
            {
                RequestCharacterList();
            }
            return;
        }
        PacketHandler.HandlePacket(Opcode, Data);
    });

    // Allow packet handler to send responses back to the server (e.g. TIME_SYNC_RESP)
    PacketHandler.OnSendPacket.BindLambda([this](uint32 Opcode, const TArray<uint8>& Data)
    {
        if (WorldSocket.IsValid())
        {
            WorldSocket->SendPacket(Opcode, Data);
        }
    });

    if (!WorldSocket->Connect(Realm.Address, Realm.Port, CachedAccountName, SessionKey, Realm.RealmId))
    {
        UE_LOG(LogWowNet, Error, TEXT("Failed to connect to world server %s:%d"), *Realm.Address, Realm.Port);
        SetState(EWowSessionState::Error);
        OnError.Broadcast(TEXT("Failed to connect to world server"));
        WorldSocket.Reset();
        return;
    }

    UE_LOG(LogWowNet, Log, TEXT("Connecting to world server %s:%d ..."), *Realm.Address, Realm.Port);
}

void UWowConnectionManager::OnWorldAuthResult(bool bSuccess)
{
    if (bSuccess)
    {
        UE_LOG(LogWowNet, Log, TEXT("World server authenticated"));
        SetState(EWowSessionState::WorldAuthenticated);

        // Initialize Warden anti-cheat system with session key
        PacketHandler.InitializeWarden(SessionKey);
    }
    else
    {
        UE_LOG(LogWowNet, Error, TEXT("World server authentication failed"));
        SetState(EWowSessionState::Error);
        OnError.Broadcast(TEXT("World server authentication failed"));
        WorldSocket.Reset();
    }
}

void UWowConnectionManager::OnWorldCharacterList(const TArray<FWowCharacterInfo>& Characters)
{
    UE_LOG(LogWowNet, Log, TEXT("Received %d characters from world server"), Characters.Num());
    CachedCharacters = Characters;
    SetState(EWowSessionState::WorldHaveCharList);
    OnCharacterList.Broadcast(Characters);
}

void UWowConnectionManager::RequestCharacterList()
{
    UE_LOG(LogWowNet, Log, TEXT("Requesting character list"));

    if (!WorldSocket.IsValid())
    {
        UE_LOG(LogWowNet, Error, TEXT("No world socket connection"));
        OnError.Broadcast(TEXT("Not connected to world server"));
        return;
    }

    SetState(EWowSessionState::WorldRequestingCharList);
    WorldSocket->SendCharEnum();
}

void UWowConnectionManager::EnterWorld(int64 G)
{
    UE_LOG(LogWowNet, Log, TEXT("Enter world: GUID %llu"), G);

    if (!WorldSocket.IsValid())
    {
        UE_LOG(LogWowNet, Error, TEXT("No world socket connection"));
        OnError.Broadcast(TEXT("Not connected to world server"));
        return;
    }

    PacketHandler.EntityManager.LocalPlayerGuid = static_cast<uint64>(G);
    for(const auto& Character:CachedCharacters) if(Character.Guid==G) PacketHandler.PlayerNameCache.Add(uint64(G),Character.Name);

    // Listen for LOGIN_VERIFY_WORLD to transition to InGame state
    if(EnterWorldHandle.IsValid()) PacketHandler.OnLoginVerifyWorld.Remove(EnterWorldHandle);
    EnterWorldHandle=PacketHandler.OnLoginVerifyWorld.AddLambda([this](uint32 MapId, float X, float Y, float Z, float Orientation)
    {
        UE_LOG(LogWowNet, Log, TEXT("Entered world: map=%d pos=(%.1f, %.1f, %.1f) orient=%.2f"), MapId, X, Y, Z, Orientation);
        SetState(EWowSessionState::WorldInGame);
    });

    SetState(EWowSessionState::WorldEnteringWorld);
    WorldSocket->SendPlayerLogin(G);
}

void UWowConnectionManager::CreateCharacter(const FString& Name, uint8 Race, uint8 Class, uint8 Gender,
    uint8 Skin, uint8 Face, uint8 HairStyle, uint8 HairColor, uint8 FacialHair)
{
    if (!WorldSocket.IsValid())
    {
        OnError.Broadcast(TEXT("Not connected to world server"));
        return;
    }

    WorldSocket->SendCharCreate(Name, Race, Class, Gender, Skin, Face, HairStyle, HairColor, FacialHair);
}

void UWowConnectionManager::DeleteCharacter(int64 Guid)
{
    if (!WorldSocket.IsValid())
    {
        OnError.Broadcast(TEXT("Not connected to world server"));
        return;
    }

    WorldSocket->SendCharDelete(static_cast<uint64>(Guid));
}

void UWowConnectionManager::SendMovement(int32 Opcode, const FVector& Position, float Orientation, int32 MoveFlags)
{
    FWowMovementInfo Move;
    Move.Position=Position; Move.Orientation=Orientation; Move.MoveFlags=uint32(MoveFlags);
    SendMovementInfo(Opcode,Move);
}

void UWowConnectionManager::RequestLogout()
{
    if(WorldSocket && State==EWowSessionState::WorldInGame) WorldSocket->SendPacket(0x004B,{});
}

void UWowConnectionManager::CancelLogout()
{
    if(WorldSocket && State==EWowSessionState::WorldInGame) WorldSocket->SendPacket(0x004E,{});
}

TArray<uint8> UWowConnectionManager::BuildChatPacket(const FString& Message,int32 Type,const FString& Target,int32 Language)
{
    TArray<uint8> Data;
    const uint32 ChatType=Type,Lang=Language;
    Data.Append(reinterpret_cast<const uint8*>(&ChatType),4); Data.Append(reinterpret_cast<const uint8*>(&Lang),4);
    const auto AppendString=[&Data](const FString& Text,int32 Limit)
    {
        FTCHARToUTF8 Utf8(*Text); int32 Length=FMath::Min(Utf8.Length(),Limit);
        // Do not split a UTF-8 code point at the protocol byte limit.
        while(Length<Utf8.Length() && Length>0 && (uint8(Utf8.Get()[Length])&0xc0)==0x80) --Length;
        Data.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Length); Data.Add(0);
    };
    if(Type==7 || Type==17) AppendString(Target,255);
    AppendString(Message,255); return Data;
}

void UWowConnectionManager::SendChatMessage(const FString& Message,int32 Type,const FString& Target,int32 Language)
{
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || Message.IsEmpty() || ((Type==7 || Type==17) && Target.IsEmpty())) return;
    WorldSocket->SendPacket(WowOpcode::CMSG_MESSAGECHAT,BuildChatPacket(Message,Type,Target,Language));
    UE_LOG(LogWowNet, Log, TEXT("Sent chat (type=%d language=%d target=%s): %s"), Type,Language,*Target,*Message);
}

void UWowConnectionManager::SendSetSelection(int64 InTargetGuid)
{
    if (!WorldSocket.IsValid()) return;

    TargetGuid = InTargetGuid;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 G = static_cast<uint64>(InTargetGuid);
    FMemory::Memcpy(Data.GetData(), &G, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_SET_SELECTION, Data);
    UE_LOG(LogWowNet, Log, TEXT("Set target: GUID %lld"), InTargetGuid);
}

void UWowConnectionManager::SendCastSpell(int32 SpellId, int64 InTargetGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    // Most UI callers pass 0 to mean "use the current selection".
    const int64 ResolvedTargetGuid = (InTargetGuid != 0) ? InTargetGuid : TargetGuid;

    // Auto Attack (spell 6603) is not cast via CMSG_CAST_SPELL in 3.3.5.
    // Route it through the melee swing opcode so Blizzard action buttons can start combat.
    if (SpellId == 6603)
    {
        if (ResolvedTargetGuid == 0)
        {
            UE_LOG(LogWowNet, Warning, TEXT("SendCastSpell: ignoring auto-attack without a valid target"));
            return;
        }

        SendAttackSwing(ResolvedTargetGuid);
        UE_LOG(LogWowNet, Log, TEXT("SendCastSpell: routed auto-attack spell %d to AttackSwing on target %lld"),
            SpellId, ResolvedTargetGuid);
        return;
    }

    TArray<uint8> Data;
    Data.Reserve(32);

    // castCount (uint8) — increments per cast, wraps at 255
    static uint8 CastCounter = 0;
    Data.Add(CastCounter++);

    UE_LOG(LogWowNet, Log, TEXT("SendCastSpell: spell=%d target=%lld requestedTarget=%lld castCount=%d"),
        SpellId, ResolvedTargetGuid, InTargetGuid, CastCounter - 1);

    // spellId (uint32)
    uint32 Spell = static_cast<uint32>(SpellId);
    Data.Append(reinterpret_cast<const uint8*>(&Spell), 4);

    // castFlags (uint8) — 0 for normal cast
    uint8 CastFlags = 0;
    Data.Add(CastFlags);

    // targetMask (uint32) — TARGET_FLAG_UNIT (0x02) if we have a target, else TARGET_FLAG_NONE (0x00)
    uint64 TGuid = static_cast<uint64>(ResolvedTargetGuid);
    uint32 TargetMask = (TGuid != 0) ? 0x0002 : 0x0000;
    Data.Append(reinterpret_cast<const uint8*>(&TargetMask), 4);

    // If targeting a unit, write packed GUID
    if (TGuid != 0)
    {
        uint8 GuidMask = 0;
        uint8 GuidBytes[8];
        int32 GuidByteCount = 0;
        for (int32 i = 0; i < 8; ++i)
        {
            uint8 Byte = static_cast<uint8>((TGuid >> (i * 8)) & 0xFF);
            if (Byte != 0)
            {
                GuidMask |= (1 << i);
                GuidBytes[GuidByteCount++] = Byte;
            }
        }
        Data.Add(GuidMask);
        Data.Append(GuidBytes, GuidByteCount);
    }

    WorldSocket->SendPacket(WowOpcode::CMSG_CAST_SPELL, Data);
    UE_LOG(LogWowNet, Log, TEXT("Cast spell %d on target %lld"), SpellId, ResolvedTargetGuid);
}

void UWowConnectionManager::SendSetActionButton(int32 SlotIndex, uint32 ActionId, uint8 ActionType)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    if (SlotIndex < 0 || SlotIndex >= 144) return;

    TArray<uint8> Data;
    Data.Reserve(5);

    const uint8 Button = static_cast<uint8>(SlotIndex);
    const uint32 PackedAction = (ActionId & 0x00FFFFFFu) | (uint32(ActionType) << 24);

    Data.Add(Button);
    Data.Append(reinterpret_cast<const uint8*>(&PackedAction), sizeof(PackedAction));

    WorldSocket->SendPacket(WowOpcode::CMSG_SET_ACTION_BUTTON, Data);
    UE_LOG(LogWowNet, Log, TEXT("Set action button %d -> action=%u type=%u"), SlotIndex, ActionId, ActionType);
}

void UWowConnectionManager::SendClearActionButton(int32 SlotIndex)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    if (SlotIndex < 0 || SlotIndex >= 144) return;

    TArray<uint8> Data;
    Data.Reserve(5);

    const uint8 Button = static_cast<uint8>(SlotIndex);
    const uint32 PackedAction = 0;

    Data.Add(Button);
    Data.Append(reinterpret_cast<const uint8*>(&PackedAction), sizeof(PackedAction));

    WorldSocket->SendPacket(WowOpcode::CMSG_SET_ACTION_BUTTON, Data);
    UE_LOG(LogWowNet, Log, TEXT("Cleared action button %d"), SlotIndex);
}

void UWowConnectionManager::PickupSpellCursor(int32 SpellId, const FString& BookType)
{
    RepairCursor=false; CursorPayload=FCursorPayloadState();
    CursorPayload.Type = ECursorPayloadType::Spell;
    CursorPayload.PrimaryId = SpellId;
    CursorPayload.Detail = BookType;
    CursorPayload.ActionType = ACTION_BUTTON_TYPE_SPELL;
    CursorPayload.SourceActionSlot = -1;
    OnCursorChanged.Broadcast();
}

void UWowConnectionManager::PickupMacroCursor(uint32 Id)
{
    if(!Id || Id>=0x1000000u) return;
    RepairCursor=false;
    CursorPayload=FCursorPayloadState(); CursorPayload.Type=ECursorPayloadType::Action;
    CursorPayload.PrimaryId=Id; CursorPayload.ActionType=0x40; CursorPayload.SourceActionSlot=-1;
    OnCursorChanged.Broadcast();
}

void UWowConnectionManager::PickupActionCursor(int32 SlotIndex)
{
    if (SlotIndex < 0 || SlotIndex >= 144 || !PacketHandler.ActionButtons.IsValidIndex(SlotIndex))
    {
        return;
    }

    const uint32 PackedAction = PacketHandler.ActionButtons[SlotIndex];
    if (PackedAction == 0)
    {
        return;
    }

    RepairCursor=false;

    CursorPayload.Type = ECursorPayloadType::Action;
    CursorPayload.PrimaryId = static_cast<int32>(PackedAction & 0x00FFFFFFu);
    CursorPayload.Detail.Empty();
    CursorPayload.ActionType = static_cast<uint8>((PackedAction >> 24) & 0xFF);
    CursorPayload.SourceActionSlot = SlotIndex;
    OnCursorChanged.Broadcast();
}

void UWowConnectionManager::PickupBagItemCursor(uint8 SourceBag, uint8 SourceSlot, int32 ItemId, const FString& ItemLink, uint32 SplitCount)
{
    const auto* Item=ResolveCursorSource(SourceBag,SourceSlot);
    if(!Item || Item->Entry!=uint32(ItemId) || Item->GetStackCount()<=0 || SplitCount>=uint32(Item->GetStackCount())) return;
    CancelGlyphUse(); CursorPayload=FCursorPayloadState(); CursorPayload.SourceItemGuid=Item->Guid;
    CursorPayload.Type = ECursorPayloadType::Item;
    CursorPayload.PrimaryId = ItemId;
    CursorPayload.Detail = ItemLink;
    CursorPayload.SourceBag = SourceBag;
    CursorPayload.SourceSlot = SourceSlot;
    CursorPayload.SourceInventorySlot = 0;
    CursorPayload.bFromInventorySlot = false;
    CursorPayload.ActionType = 0;
    CursorPayload.SourceActionSlot = -1;
    CursorPayload.SplitCount = SplitCount;
    OnCursorChanged.Broadcast();
}

void UWowConnectionManager::PickupInventoryItemCursor(uint8 InventorySlot, int32 ItemId, const FString& ItemLink)
{
    const auto* Item=InventorySlot<23?ResolveCursorSource(255,InventorySlot):nullptr;
    if(!Item || Item->Entry!=uint32(ItemId) || Item->GetStackCount()<=0) return;
    CancelGlyphUse(); CursorPayload=FCursorPayloadState(); CursorPayload.SourceItemGuid=Item->Guid;
    CursorPayload.Type = ECursorPayloadType::Item;
    CursorPayload.PrimaryId = ItemId;
    CursorPayload.Detail = ItemLink;
    CursorPayload.SourceBag = 255;
    CursorPayload.SourceSlot = InventorySlot;
    CursorPayload.SourceInventorySlot = InventorySlot;
    CursorPayload.bFromInventorySlot = true;
    CursorPayload.ActionType = 0;
    CursorPayload.SourceActionSlot = -1;
    CursorPayload.SplitCount = 0;
    OnCursorChanged.Broadcast();
}

void UWowConnectionManager::ClearCursorPayload()
{
    CursorPayload = FCursorPayloadState();
    OnCursorChanged.Broadcast();
}

bool UWowConnectionManager::DropActionCursor()
{
    if(CursorPayload.Type!=ECursorPayloadType::Action) return false;
    const int32 Slot=CursorPayload.SourceActionSlot;
    if(PacketHandler.ActionButtons.IsValidIndex(Slot)) { SendClearActionButton(Slot); PacketHandler.ActionButtons[Slot]=0; }
    ClearCursorPayload(); BroadcastActionButtonsChanged(); return true;
}

bool UWowConnectionManager::HasCursorPayload() const
{
    return CursorPayload.Type==ECursorPayloadType::Item?HasCursorItemPayload():CursorPayload.Type==ECursorPayloadType::Merchant?HasCursorMerchantPayload():CursorPayload.Type!=ECursorPayloadType::None;
}

bool UWowConnectionManager::HasCursorSpellPayload() const
{
    return CursorPayload.Type == ECursorPayloadType::Spell;
}

bool UWowConnectionManager::HasCursorItemPayload() const
{
    return GetCursorItem()!=nullptr;
}

bool UWowConnectionManager::GetCursorInfo(FString& OutType, int32& OutId, FString& OutDetail) const
{
    if(HasCursorMerchantPayload()) { OutType=TEXT("merchant"); OutId=CursorPayload.SourceActionSlot+1; OutDetail.Empty(); return true; }
    if (CursorPayload.Type == ECursorPayloadType::Spell)
    {
        OutType = TEXT("spell");
        OutId = CursorPayload.PrimaryId;
        OutDetail = CursorPayload.Detail;
        return true;
    }

    if (CursorPayload.Type == ECursorPayloadType::Action)
    {
        if(CursorPayload.SourceActionSlot<0)
        {
            OutType=CursorPayload.ActionType==0x80?TEXT("item"):CursorPayload.ActionType==0?TEXT("spell"):TEXT("macro");
            OutId=CursorPayload.PrimaryId; OutDetail=CursorPayload.ActionType==0?TEXT("spell"):FString(); return true;
        }
        OutType = TEXT("action");
        OutId = CursorPayload.SourceActionSlot + 1;
        OutDetail.Empty();
        return true;
    }

    if (CursorPayload.Type == ECursorPayloadType::Item && HasCursorItemPayload())
    {
        OutType = TEXT("item");
        OutId = CursorPayload.PrimaryId;
        OutDetail = CursorPayload.Detail;
        return true;
    }

    OutType.Empty();
    OutId = 0;
    OutDetail.Empty();
    return false;
}

bool UWowConnectionManager::PlaceCursorIntoActionSlot(int32 SlotIndex)
{
    // Vendor selections are not owned inventory and cannot replace an action.
    if(CursorPayload.Type==ECursorPayloadType::Merchant) return false;
    if(CursorPayload.Type==ECursorPayloadType::Item && !HasCursorItemPayload()) { RefreshItemCursor(); return false; }
    if (SlotIndex < 0 || SlotIndex >= 144 || CursorPayload.Type == ECursorPayloadType::None)
    {
        return false;
    }

    EnsureActionButtonCapacity(SlotIndex);
    const uint32 ReplacedAction=PacketHandler.ActionButtons[SlotIndex];

    if (CursorPayload.Type == ECursorPayloadType::Spell)
    {
        SendSetActionButton(SlotIndex, static_cast<uint32>(CursorPayload.PrimaryId), ACTION_BUTTON_TYPE_SPELL);
        PacketHandler.ActionButtons[SlotIndex] = (static_cast<uint32>(CursorPayload.PrimaryId) & 0x00FFFFFFu);
    }
    else if (CursorPayload.Type == ECursorPayloadType::Action)
    {
        if (CursorPayload.SourceActionSlot == SlotIndex)
        {
            ClearCursorPayload();
            return true;
        }

        const uint32 PackedAction = (static_cast<uint32>(CursorPayload.PrimaryId) & 0x00FFFFFFu)
            | (uint32(CursorPayload.ActionType) << 24);
        SendSetActionButton(SlotIndex, static_cast<uint32>(CursorPayload.PrimaryId), CursorPayload.ActionType);
        PacketHandler.ActionButtons[SlotIndex] = PackedAction;

        if (CursorPayload.SourceActionSlot >= 0 && CursorPayload.SourceActionSlot < 144)
        {
            EnsureActionButtonCapacity(CursorPayload.SourceActionSlot);
            SendClearActionButton(CursorPayload.SourceActionSlot);
            PacketHandler.ActionButtons[CursorPayload.SourceActionSlot] = 0;
        }
    }
    else if(CursorPayload.Type==ECursorPayloadType::Item)
    {
        SendSetActionButton(SlotIndex,uint32(CursorPayload.PrimaryId),ACTION_BUTTON_TYPE_ITEM);
        PacketHandler.ActionButtons[SlotIndex]=(uint32(CursorPayload.PrimaryId)&0x00ffffffu)|(uint32(ACTION_BUTTON_TYPE_ITEM)<<24);
    }

    CursorPayload = FCursorPayloadState();
    if(ReplacedAction)
    {
        CursorPayload.Type=ECursorPayloadType::Action; CursorPayload.PrimaryId=ReplacedAction&0x00ffffffu;
        CursorPayload.ActionType=ReplacedAction>>24; CursorPayload.SourceActionSlot=-1;
    }
    OnCursorChanged.Broadcast();
    BroadcastActionButtonsChanged();
    return true;
}

bool UWowConnectionManager::PlaceCursorIntoContainerSlot(uint8 DestinationBag, uint8 DestinationSlot)
{
    if(HasCursorMerchantPayload())
    {
        if(!SendBuyVendorSlotIntoBag(CursorPayload.SourceVendorSlot,1,DestinationBag,DestinationSlot)) return false;
        ClearCursorPayload(); return true;
    }
    if (!HasCursorItemPayload() && !HasCursorMerchantPayload())
    {
        RefreshItemCursor();
        return false;
    }

    if (CursorPayload.SplitCount > 0 && !CursorPayload.bFromInventorySlot)
    {
        SendSplitItem(CursorPayload.SourceBag, CursorPayload.SourceSlot, DestinationBag, DestinationSlot, CursorPayload.SplitCount);
        ClearCursorPayload();
        return true;
    }

    SendSwapItem(CursorPayload.SourceBag,CursorPayload.SourceSlot,DestinationBag,DestinationSlot);
    ClearCursorPayload();
    return true;
}

bool UWowConnectionManager::PlaceCursorIntoFirstFreeContainerSlot(uint8 DestinationBag)
{
    if (!HasCursorItemPayload() && !HasCursorMerchantPayload())
    {
        RefreshItemCursor();
        return false;
    }

    const FWowPlayerEntity* Player = PacketHandler.EntityManager.GetLocalPlayer();
    if (!Player)
    {
        return false;
    }

    if (DestinationBag == 255)
    {
        for (uint8 SlotIndex = 0; SlotIndex < 16; ++SlotIndex)
        {
            if (Player->GetBackpackItemGuid(SlotIndex) == 0)
            {
                return PlaceCursorIntoContainerSlot(255, SlotIndex+23);
            }
        }
        return false;
    }

    const uint64 BagGuid = DestinationBag>=19 && DestinationBag<=22?Player->GetBagGuid(DestinationBag-18):0;
    if (BagGuid == 0)
    {
        return false;
    }

    const FWowContainerEntity* Container = PacketHandler.EntityManager.FindContainer(BagGuid);
    if (!Container)
    {
        return false;
    }

    for (int32 SlotIndex = 0; SlotIndex < Container->GetNumSlots(); ++SlotIndex)
    {
        if (Container->GetItemGuidAtSlot(SlotIndex) == 0)
        {
            return PlaceCursorIntoContainerSlot(DestinationBag, static_cast<uint8>(SlotIndex));
        }
    }

    return false;
}

bool UWowConnectionManager::AutoEquipCursorItem()
{
    if(HasCursorMerchantPayload())
    {
        const auto* Item=PacketHandler.ItemTemplates.Find(GetCursorMerchantItemId());
        if(!Item) return false;
        static const int32 Slots[]={-1,0,1,2,3,4,5,6,7,8,9,10,12,15,16,17,14,15,19,18,4,15,16,16,18,17,17,19,17};
        int32 Slot=Item->InventoryType<UE_ARRAY_COUNT(Slots)?Slots[Item->InventoryType]:-1; if(Slot<0) return false;
        if(const auto* P=PacketHandler.EntityManager.GetLocalPlayer())
        {
            const int32 Last=Item->InventoryType==11?11:Item->InventoryType==12?13:Item->InventoryType==18 || Item->InventoryType==27?22:Slot;
            for(int32 I=Slot;I<=Last;++I) if(!P->GetField64(PlayerField::INV_SLOT_HEAD+I*2)) {Slot=I; break;}
        }
        return PlaceCursorIntoContainerSlot(255,uint8(Slot));
    }
    if (!HasCursorItemPayload() || CursorPayload.bFromInventorySlot || CursorPayload.SplitCount)
    {
        RefreshItemCursor();
        return false;
    }

    SendAutoEquipItem(CursorPayload.SourceBag, CursorPayload.SourceSlot);
    ClearCursorPayload();
    return true;
}

bool UWowConnectionManager::AutoStoreCursorItem(uint8 DestinationBag)
{
    if(HasCursorMerchantPayload()) return PlaceCursorIntoFirstFreeContainerSlot(DestinationBag);
    if (!HasCursorItemPayload())
    {
        RefreshItemCursor();
        return false;
    }
    if(CursorPayload.SplitCount) return PlaceCursorIntoFirstFreeContainerSlot(DestinationBag);

    if (CursorPayload.bFromInventorySlot)
    {
        SendAutoStoreBagItem(255, CursorPayload.SourceInventorySlot, DestinationBag);
        ClearCursorPayload();
        return true;
    }

    SendAutoStoreBagItem(CursorPayload.SourceBag, CursorPayload.SourceSlot, DestinationBag);
    ClearCursorPayload();
    return true;
}

bool UWowConnectionManager::DeleteCursorItem()
{
    if (!HasCursorItemPayload())
    {
        RefreshItemCursor();
        return false;
    }

    const uint8 SourceBag = CursorPayload.bFromInventorySlot ? 255 : CursorPayload.SourceBag;
    const uint8 SourceSlot = CursorPayload.bFromInventorySlot ? CursorPayload.SourceInventorySlot : CursorPayload.SourceSlot;
    // DestroyItem has a byte-sized count. Split stacks can be larger on a
    // server; keep the selected quantity exact without truncating or deleting
    // the full physical stack. Every request leaves its unselected remainder.
    uint32 Remaining=CursorPayload.SplitCount;
    if(!Remaining) SendDestroyItem(SourceBag,SourceSlot,0);
    while(Remaining)
    {
        const uint8 Count=uint8(FMath::Min(Remaining,255u));
        SendDestroyItem(SourceBag,SourceSlot,Count); Remaining-=Count;
    }
    ClearCursorPayload();
    return true;
}

FWowActionInvocation UWowConnectionManager::ResolveActionInvocation(uint32 PackedAction, int64 InTargetGuid)
{
    FWowActionInvocation Invocation;
    Invocation.ActionId = PackedAction & 0x00FFFFFFu;
    Invocation.TargetGuid = InTargetGuid;

    if (Invocation.ActionId == 0)
    {
        return Invocation;
    }

    const uint8 ActionType = static_cast<uint8>((PackedAction >> 24) & 0xFF);
    switch (ActionType)
    {
    case ACTION_BUTTON_TYPE_SPELL:
        Invocation.Kind = (Invocation.ActionId == AUTO_ATTACK_SPELL_ID)
            ? EWowActionInvocationKind::AutoAttack
            : EWowActionInvocationKind::SpellCast;
        break;
    case 64:
        Invocation.Kind = EWowActionInvocationKind::Macro;
        break;
    case 128:
        Invocation.Kind = EWowActionInvocationKind::ItemUse;
        break;
    default:
        Invocation.Kind = EWowActionInvocationKind::None;
        break;
    }

    return Invocation;
}

bool UWowConnectionManager::UseActionSlot(int32 SlotIndex)
{
    if (SlotIndex < 0 || SlotIndex >= 144 || !PacketHandler.ActionButtons.IsValidIndex(SlotIndex))
    {
        return false;
    }

    const FWowActionInvocation Invocation =
        ResolveActionInvocation(PacketHandler.ActionButtons[SlotIndex], GetTargetGuid());
    if (!Invocation.IsValid())
    {
        return false;
    }

    switch (Invocation.Kind)
    {
    case EWowActionInvocationKind::SpellCast:
        SendCastSpell(static_cast<int32>(Invocation.ActionId), Invocation.TargetGuid);
        return true;

    case EWowActionInvocationKind::AutoAttack:
        if (Invocation.TargetGuid == 0)
        {
            UE_LOG(LogWowNet, Log, TEXT("UseActionSlot: slot %d auto-attack ignored with no target"), SlotIndex);
            return false;
        }

        SendAttackSwing(Invocation.TargetGuid);
        return true;

    case EWowActionInvocationKind::ItemUse:
    {
        RequestItemTemplate(Invocation.ActionId);
        const auto* T=PacketHandler.ItemTemplates.Find(Invocation.ActionId);
        const auto* Player=PacketHandler.EntityManager.GetLocalPlayer();
        if(!T || !Player) return false;
        const auto Use=[&](uint64 Guid,uint8 Bag,uint8 Slot)->bool
        {
            const auto* Item=PacketHandler.EntityManager.FindItem(Guid);
            if(!Item || Item->Entry!=Invocation.ActionId) return false;
            if(T->UseSpellId) { SendUseItem(Bag,Slot,Guid,T->UseSpellId); return true; }
            if(T->InventoryType) { SendAutoEquipItem(Bag,Slot); return true; }
            return false;
        };
        for(uint8 Slot=0; Slot<19; ++Slot) if(Use(Player->GetEquipmentItemGuid(Slot),255,Slot)) return true;
        for(uint8 Slot=0; Slot<16; ++Slot) if(Use(Player->GetBackpackItemGuid(Slot),255,Slot+23)) return true;
        for(uint8 Bag=1; Bag<=4; ++Bag) if(const auto* Container=PacketHandler.EntityManager.FindContainer(Player->GetBagGuid(Bag)))
            for(int32 Slot=0; Slot<Container->GetNumSlots(); ++Slot) if(Use(Container->GetItemGuidAtSlot(Slot),Bag+18,Slot)) return true;
        return false;
    }

    case EWowActionInvocationKind::Macro:
        return OnMacroUse.IsBound() && OnMacroUse.Execute(Invocation.ActionId);

    case EWowActionInvocationKind::None:
    default:
        return false;
    }
}

void UWowConnectionManager::BroadcastActionButtonsChanged()
{
    PacketHandler.OnActionButtonsUpdated.Broadcast();
}

void UWowConnectionManager::EnsureActionButtonCapacity(int32 SlotIndex)
{
    if (SlotIndex < 0)
    {
        return;
    }

    if (PacketHandler.ActionButtons.Num() <= SlotIndex)
    {
        PacketHandler.ActionButtons.SetNumZeroed(SlotIndex + 1);
    }
}

void UWowConnectionManager::SendAttackSwing(int64 InTargetGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 G = static_cast<uint64>(InTargetGuid);
    FMemory::Memcpy(Data.GetData(), &G, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_ATTACKSWING, Data);
    UE_LOG(LogWowNet, Log, TEXT("Attack swing on target %lld"), InTargetGuid);
}

void UWowConnectionManager::SendAttackStop()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::CMSG_ATTACKSTOP);
    UE_LOG(LogWowNet, Log, TEXT("Attack stop"));
}

void UWowConnectionManager::SendKeepAlive()
{
    if (!WorldSocket.IsValid()) return;
    WorldSocket->SendPacket(WowOpcode::CMSG_KEEP_ALIVE);
}

void UWowConnectionManager::SendLoot(int64 CorpseGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 Guid = static_cast<uint64>(CorpseGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_LOOT, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_LOOT for corpse %llu"), Guid);
}

void UWowConnectionManager::SendLootRelease(int64 CorpseGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 Guid = static_cast<uint64>(CorpseGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_LOOT_RELEASE, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_LOOT_RELEASE for corpse %llu"), Guid);
}

void UWowConnectionManager::SendLootItem(int32 LootSlot)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Add(static_cast<uint8>(LootSlot));

    WorldSocket->SendPacket(WowOpcode::CMSG_AUTOSTORE_LOOT_ITEM, Data);
    UE_LOG(LogWowNet, Log, TEXT("Loot item slot %d"), LootSlot);
}

void UWowConnectionManager::SendBuyItem(int64 VendorGuid, int32 ItemId, int32 Count)
{
    if(uint64(VendorGuid)!=PacketHandler.Merchant.Guid || Count<1) return;
    for(const auto& Item:PacketHandler.Merchant.Items) if(Item.ItemId==uint32(ItemId)) { SendBuyVendorSlot(Item.Slot,Count); return; }
}

void UWowConnectionManager::SendSellItem(int64 VendorGuid, int64 ItemGuid, int32 Count)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || Count<0) return;

    TArray<uint8> Data;
    Data.Reserve(20);

    // 12340: uint64 vendor, uint64 item, uint32 count (zero sells the stack).
    uint64 VGuid = static_cast<uint64>(VendorGuid);
    Data.SetNumUninitialized(8);
    FMemory::Memcpy(Data.GetData(), &VGuid, 8);

    uint64 IGuid = static_cast<uint64>(ItemGuid);
    Data.SetNumUninitialized(Data.Num() + 8);
    FMemory::Memcpy(Data.GetData() + 8, &IGuid, 8);

    const uint32 WireCount=uint32(Count); Data.Append(reinterpret_cast<const uint8*>(&WireCount),sizeof(WireCount));

    WorldSocket->SendPacket(WowOpcode::CMSG_SELL_ITEM, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sell item %lld (count=%d) to vendor %lld"), ItemGuid, Count, VendorGuid);
}

void UWowConnectionManager::SendAutoEquipItem(uint8 SourceBag, uint8 SourceSlot)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame)
    {
        return;
    }

    TArray<uint8> Data;
    Data.Reserve(2);
    Data.Add(SourceBag);
    Data.Add(SourceSlot);

    WorldSocket->SendPacket(WowOpcode::CMSG_AUTOEQUIP_ITEM, Data);
    UE_LOG(LogWowNet, Log, TEXT("Auto equip item from bag=%u slot=%u"), SourceBag, SourceSlot);
}

void UWowConnectionManager::SendSwapItem(uint8 SourceBag,uint8 SourceSlot,uint8 DestinationBag,uint8 DestinationSlot)
{
    if(!WorldSocket.IsValid() || State!=EWowSessionState::WorldInGame) return;
    WorldSocket->SendPacket(WowOpcode::CMSG_SWAP_ITEM,{DestinationBag,DestinationSlot,SourceBag,SourceSlot});
}

TArray<uint8> UWowConnectionManager::BuildUseItemPacket(uint8 Bag,uint8 Slot,uint64 ItemGuid,uint32 SpellId,uint64 ExplicitTargetGuid,uint32 GlyphSocket)
{
    if(GlyphSocket>=6) return {};
    TArray<uint8> Data={Bag,Slot,0}; // bag, slot, cast count
    Data.Append(reinterpret_cast<const uint8*>(&SpellId),4);
    Data.Append(reinterpret_cast<const uint8*>(&ItemGuid),8);
    const uint32 Glyph=GlyphSocket,TargetMask=ExplicitTargetGuid?2:0; // no inferred hostile target for consumables
    Data.Append(reinterpret_cast<const uint8*>(&Glyph),4); Data.Add(0);
    Data.Append(reinterpret_cast<const uint8*>(&TargetMask),4);
    if(ExplicitTargetGuid)
    {
        uint8 Mask=0; TArray<uint8> Bytes;
        for(int32 I=0;I<8;++I) { const uint8 Byte=ExplicitTargetGuid>>(I*8); if(Byte) { Mask|=1<<I; Bytes.Add(Byte); } }
        Data.Add(Mask); Data.Append(Bytes);
    }
    return Data;
}

void UWowConnectionManager::SendUseItem(uint8 Bag,uint8 Slot,uint64 ItemGuid,uint32 SpellId,uint64 ExplicitTargetGuid,int32 GlyphSocket)
{
    if(!WorldSocket.IsValid() || State!=EWowSessionState::WorldInGame || !ItemGuid || !SpellId) return;
    if(GlyphSocket<0) if(const auto* Spell=FDbcStore::Get().Spells().GetById(SpellId)) for(const uint32 Effect:Spell->Effect) if(Effect==74) { BeginGlyphUse(Bag,Slot,ItemGuid,SpellId); return; }
    if(GlyphSocket>=6) return;
    WorldSocket->SendPacket(WowOpcode::CMSG_USE_ITEM,BuildUseItemPacket(Bag,Slot,ItemGuid,SpellId,ExplicitTargetGuid,GlyphSocket<0?0:GlyphSocket));
}

void UWowConnectionManager::SendSetTitle(int32 TitleIndex)
{
    if(!WorldSocket.IsValid() || State!=EWowSessionState::WorldInGame) return;
    TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&TitleIndex),4);
    WorldSocket->SendPacket(WowOpcode::CMSG_SET_TITLE,Data);
}

void UWowConnectionManager::SendAutoStoreBagItem(uint8 SourceBag, uint8 SourceSlot, uint8 DestinationBag)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame)
    {
        return;
    }

    TArray<uint8> Data;
    Data.Reserve(3);
    Data.Add(SourceBag);
    Data.Add(SourceSlot);
    Data.Add(DestinationBag);

    WorldSocket->SendPacket(WowOpcode::CMSG_AUTOSTORE_BAG_ITEM, Data);
    UE_LOG(LogWowNet, Log, TEXT("Auto store item from bag=%u slot=%u into bag=%u"),
        SourceBag, SourceSlot, DestinationBag);
}

void UWowConnectionManager::SendSplitItem(uint8 SourceBag, uint8 SourceSlot, uint8 DestinationBag, uint8 DestinationSlot, uint32 Count)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame)
    {
        return;
    }

    TArray<uint8> Data;
    Data.Reserve(8);
    Data.Add(SourceBag);
    Data.Add(SourceSlot);
    Data.Add(DestinationBag);
    Data.Add(DestinationSlot);
    Data.Append(reinterpret_cast<const uint8*>(&Count), sizeof(Count));

    WorldSocket->SendPacket(WowOpcode::CMSG_SPLIT_ITEM, Data);
    UE_LOG(LogWowNet, Log, TEXT("Split item from bag=%u slot=%u into bag=%u slot=%u count=%u"),
        SourceBag, SourceSlot, DestinationBag, DestinationSlot, Count);
}

void UWowConnectionManager::SendDestroyItem(uint8 Bag, uint8 Slot, uint8 Count)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame)
    {
        return;
    }

    TArray<uint8> Data;
    Data.Reserve(6);
    Data.Add(Bag);
    Data.Add(Slot);
    Data.Add(Count);
    Data.Add(0);
    Data.Add(0);
    Data.Add(0);

    WorldSocket->SendPacket(WowOpcode::CMSG_DESTROYITEM, Data);
    UE_LOG(LogWowNet, Log, TEXT("Destroy item bag=%u slot=%u count=%u"), Bag, Slot, Count);
}

void UWowConnectionManager::SendInitiateTrade(int64 TargetGuidToTrade)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    const uint64 Guid = static_cast<uint64>(TargetGuidToTrade);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_INITIATE_TRADE, Data);
    PacketHandler.CurrentTrade = FWowTradeState{};
    PacketHandler.CurrentTrade.TraderGuid = Guid;
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_INITIATE_TRADE for player %llu"), Guid);
}

void UWowConnectionManager::SendBeginTrade()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::CMSG_BEGIN_TRADE, {});
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_BEGIN_TRADE"));
}

void UWowConnectionManager::SendAcceptTrade()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::CMSG_ACCEPT_TRADE, {});
    PacketHandler.CurrentTrade.bLocalAccepted = true;
    PacketHandler.OnTradeUpdated.Broadcast(PacketHandler.CurrentTrade);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_ACCEPT_TRADE"));
}

void UWowConnectionManager::SendUnacceptTrade()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::CMSG_UNACCEPT_TRADE, {});
    PacketHandler.CurrentTrade.bLocalAccepted = false;
    PacketHandler.OnTradeUpdated.Broadcast(PacketHandler.CurrentTrade);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_UNACCEPT_TRADE"));
}

void UWowConnectionManager::SendCancelTrade()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::CMSG_CANCEL_TRADE, {});
    PacketHandler.CurrentTrade.Status = WowTradeStatus::TRADE_CANCELED;
    PacketHandler.CurrentTrade.bTradeOpen = false;
    PacketHandler.CurrentTrade.bLocalAccepted = false;
    PacketHandler.CurrentTrade.bTargetAccepted = false;
    PacketHandler.CurrentTrade.PlayerMoney = 0;
    PacketHandler.CurrentTrade.TargetMoney = 0;
    PacketHandler.CurrentTrade.PlayerSpell = 0;
    PacketHandler.CurrentTrade.TargetSpell = 0;
    PacketHandler.CurrentTrade.PlayerItems.Reset();
    PacketHandler.CurrentTrade.TargetItems.Reset();
    PacketHandler.OnTradeUpdated.Broadcast(PacketHandler.CurrentTrade);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_CANCEL_TRADE"));
}

void UWowConnectionManager::SendSetTradeGold(int32 CopperAmount)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(4);
    const uint32 Gold = static_cast<uint32>(FMath::Max(0, CopperAmount));
    FMemory::Memcpy(Data.GetData(), &Gold, 4);

    WorldSocket->SendPacket(WowOpcode::CMSG_SET_TRADE_GOLD, Data);
    PacketHandler.CurrentTrade.PlayerMoney = Gold;
    PacketHandler.CurrentTrade.bLocalAccepted = false;
    PacketHandler.CurrentTrade.bTargetAccepted = false;
    PacketHandler.OnTradeUpdated.Broadcast(PacketHandler.CurrentTrade);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_SET_TRADE_GOLD amount=%u"), Gold);
}

void UWowConnectionManager::SendAcceptDuel(int64 ArbiterGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || ArbiterGuid == 0) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(sizeof(uint64));
    const uint64 Guid = static_cast<uint64>(ArbiterGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, sizeof(Guid));

    WorldSocket->SendPacket(WowOpcode::CMSG_DUEL_ACCEPTED, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_DUEL_ACCEPTED arbiter=%llu"), Guid);
}

void UWowConnectionManager::SendCancelDuel(int64 ArbiterGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame || ArbiterGuid == 0) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(sizeof(uint64));
    const uint64 Guid = static_cast<uint64>(ArbiterGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, sizeof(Guid));

    WorldSocket->SendPacket(WowOpcode::CMSG_DUEL_CANCELLED, Data);

    if (PacketHandler.CurrentDuel.IsRequestPending())
    {
        PacketHandler.CurrentDuel.MarkInterrupted();
        PacketHandler.OnDuelUpdated.Broadcast(PacketHandler.CurrentDuel);
    }

    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_DUEL_CANCELLED arbiter=%llu"), Guid);
}

void UWowConnectionManager::SendRequestPetInfo()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::CMSG_REQUEST_PET_INFO, {});
    UE_LOG(LogWowNet, Verbose, TEXT("Sent CMSG_REQUEST_PET_INFO"));
}

void UWowConnectionManager::SendPetActionBarSlot(int32 SlotIndex, int64 InTargetGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    const FWowPetActionSlot* Slot = PacketHandler.PetActionBar.GetSlot(SlotIndex);
    if (!Slot || !Slot->IsUsable() || PacketHandler.PetActionBar.PetGuid == 0)
    {
        return;
    }

    uint64 Target = static_cast<uint64>(InTargetGuid != 0 ? InTargetGuid : TargetGuid);
    if (!Slot->IsSpell() && (!Slot->IsCommand() || Slot->ActionId != WowPetCommand::ATTACK))
    {
        Target = 0;
    }

    TArray<uint8> Data;
    Data.Reserve(sizeof(uint64) + sizeof(uint32) + sizeof(uint64));

    const uint64 PetGuid = PacketHandler.PetActionBar.PetGuid;
    const uint32 PackedData = Slot->PackedData;
    Data.Append(reinterpret_cast<const uint8*>(&PetGuid), sizeof(PetGuid));
    Data.Append(reinterpret_cast<const uint8*>(&PackedData), sizeof(PackedData));
    Data.Append(reinterpret_cast<const uint8*>(&Target), sizeof(Target));

    WorldSocket->SendPacket(WowOpcode::CMSG_PET_ACTION, Data);

    if (Slot->IsReaction())
    {
        PacketHandler.PetActionBar.ReactState = static_cast<uint8>(Slot->ActionId);
        PacketHandler.OnPetBarUpdated.Broadcast(PacketHandler.PetActionBar);
    }
    else if (Slot->IsCommand())
    {
        if (Slot->ActionId == WowPetCommand::FOLLOW || Slot->ActionId == WowPetCommand::STAY)
        {
            PacketHandler.PetActionBar.CommandState = static_cast<uint8>(Slot->ActionId);
            PacketHandler.OnPetBarUpdated.Broadcast(PacketHandler.PetActionBar);
        }
    }

    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_PET_ACTION slot=%d pet=%llu packed=0x%08X target=%llu"),
        SlotIndex, PetGuid, PackedData, Target);
}

void UWowConnectionManager::SendPetSpellAutocast(int32 SlotIndex, bool bEnabled)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    const FWowPetActionSlot* Slot = PacketHandler.PetActionBar.GetSlot(SlotIndex);
    if (!Slot || !Slot->IsAutocastCapable() || PacketHandler.PetActionBar.PetGuid == 0 || Slot->ActionId == 0)
    {
        return;
    }

    TArray<uint8> Data;
    Data.Reserve(sizeof(uint64) + sizeof(uint32) + sizeof(uint8));

    const uint64 PetGuid = PacketHandler.PetActionBar.PetGuid;
    Data.Append(reinterpret_cast<const uint8*>(&PetGuid), sizeof(PetGuid));
    Data.Append(reinterpret_cast<const uint8*>(&Slot->ActionId), sizeof(Slot->ActionId));
    Data.Add(bEnabled ? 1u : 0u);

    WorldSocket->SendPacket(WowOpcode::CMSG_PET_SPELL_AUTOCAST, Data);

    if (FWowPetActionSlot* MutableSlot = PacketHandler.PetActionBar.GetMutableSlot(SlotIndex))
    {
        MutableSlot->ActionType = bEnabled ? WowPetActionType::ENABLED : WowPetActionType::DISABLED;
        MutableSlot->PackedData = (MutableSlot->PackedData & 0x00FFFFFF) | (static_cast<uint32>(MutableSlot->ActionType) << 24);
        PacketHandler.OnPetBarUpdated.Broadcast(PacketHandler.PetActionBar);
    }

    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_PET_SPELL_AUTOCAST slot=%d spell=%u enabled=%d"),
        SlotIndex, Slot->ActionId, bEnabled ? 1 : 0);
}

void UWowConnectionManager::SendRequestRaidTargetIcons()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Add(0xFF);
    WorldSocket->SendPacket(WowOpcode::MSG_RAID_TARGET_UPDATE, Data);
    UE_LOG(LogWowNet, Verbose, TEXT("Requested current raid target icon list"));
}

void UWowConnectionManager::SendSetRaidTargetIcon(int32 IconIndex, int64 InTargetGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;
    if (IconIndex < 0 || IconIndex > 7) return;

    TArray<uint8> Data;
    Data.Reserve(9);

    const uint8 Icon = static_cast<uint8>(IconIndex);
    const uint64 Guid = static_cast<uint64>(InTargetGuid);
    Data.Add(Icon);
    Data.Append(reinterpret_cast<const uint8*>(&Guid), sizeof(Guid));

    WorldSocket->SendPacket(WowOpcode::MSG_RAID_TARGET_UPDATE, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent MSG_RAID_TARGET_UPDATE icon=%d guid=%llu"), IconIndex, Guid);
}

void UWowConnectionManager::SendStartReadyCheck()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::MSG_RAID_READY_CHECK, {});
    UE_LOG(LogWowNet, Log, TEXT("Sent MSG_RAID_READY_CHECK request"));
}

void UWowConnectionManager::SendReadyCheckConfirm(bool bReady)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Add(bReady ? WowReadyCheckResponse::READY : WowReadyCheckResponse::NOT_READY);

    WorldSocket->SendPacket(WowOpcode::MSG_RAID_READY_CHECK, Data);

    const uint64 LocalPlayerGuid = PacketHandler.EntityManager.LocalPlayerGuid;
    if (PacketHandler.ReadyCheck.bActive && LocalPlayerGuid != 0)
    {
        PacketHandler.ReadyCheck.SetResponse(LocalPlayerGuid, Data[0]);
        PacketHandler.OnReadyCheckUpdated.Broadcast(PacketHandler.ReadyCheck);
    }

    UE_LOG(LogWowNet, Log, TEXT("Sent MSG_RAID_READY_CHECK response ready=%d"), bReady ? 1 : 0);
}

void UWowConnectionManager::SendFinishReadyCheck()
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    WorldSocket->SendPacket(WowOpcode::MSG_RAID_READY_CHECK_FINISHED, {});
    PacketHandler.ReadyCheck.Clear();
    PacketHandler.OnReadyCheckUpdated.Broadcast(PacketHandler.ReadyCheck);
    UE_LOG(LogWowNet, Log, TEXT("Sent MSG_RAID_READY_CHECK_FINISHED"));
}

void UWowConnectionManager::SendGossipHello(int64 NpcGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 Guid = static_cast<uint64>(NpcGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    // Use CMSG_GOSSIP_HELLO — the server routes to questgiver/vendor/trainer
    // based on the NPC's flags automatically
    WorldSocket->SendPacket(WowOpcode::CMSG_GOSSIP_HELLO, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_GOSSIP_HELLO for NPC %llu"), Guid);
}

void UWowConnectionManager::SendGameObjectUse(int64 GameObjectGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    const uint64 Guid = static_cast<uint64>(GameObjectGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_GAMEOBJ_USE, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_GAMEOBJ_USE for gameobject %llu"), Guid);
}

void UWowConnectionManager::SendBankerActivate(int64 BankerGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 Guid = static_cast<uint64>(BankerGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_BANKER_ACTIVATE, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_BANKER_ACTIVATE for NPC %llu"), Guid);
}

void UWowConnectionManager::SendGetMailList(int64 MailboxGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    const uint64 Guid = static_cast<uint64>(MailboxGuid);
    FMemory::Memcpy(Data.GetData(), &Guid, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_GET_MAIL_LIST, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent CMSG_GET_MAIL_LIST for mailbox %llu"), Guid);
}

void UWowConnectionManager::SendQuestAccept(int64 QuestGiverGuid, int32 QuestId)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    const auto Data=BuildQuestAcceptPacket(static_cast<uint64>(QuestGiverGuid),static_cast<uint32>(QuestId));

    WorldSocket->SendPacket(WowOpcode::CMSG_QUESTGIVER_ACCEPT_QUEST, Data);
    UE_LOG(LogWowNet, Log, TEXT("Accept quest %d from questgiver %lld"), QuestId, QuestGiverGuid);
}

void UWowConnectionManager::SendQuestChooseReward(int64 QuestGiverGuid, int32 QuestId, int32 RewardChoice)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Reserve(16);

    // uint64 questgiver_guid, uint32 quest_id, uint32 reward_choice
    uint64 QGuid = static_cast<uint64>(QuestGiverGuid);
    Data.SetNumUninitialized(8);
    FMemory::Memcpy(Data.GetData(), &QGuid, 8);

    uint32 QId = static_cast<uint32>(QuestId);
    Data.SetNumUninitialized(Data.Num() + 4);
    FMemory::Memcpy(Data.GetData() + 8, &QId, 4);

    uint32 RChoice = static_cast<uint32>(RewardChoice);
    Data.SetNumUninitialized(Data.Num() + 4);
    FMemory::Memcpy(Data.GetData() + 12, &RChoice, 4);

    WorldSocket->SendPacket(WowOpcode::CMSG_QUESTGIVER_CHOOSE_REWARD, Data);
    UE_LOG(LogWowNet, Log, TEXT("Choose quest reward %d for quest %d from questgiver %lld"), RewardChoice, QuestId, QuestGiverGuid);
}

void UWowConnectionManager::SendRawPacket(uint32 Opcode, const TArray<uint8>& Data)
{
    if (!WorldSocket.IsValid()) return;
    WorldSocket->SendPacket(Opcode, Data);
}

void UWowConnectionManager::SendNameQuery(int64 Guid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.SetNumUninitialized(8);
    uint64 G = static_cast<uint64>(Guid);
    FMemory::Memcpy(Data.GetData(), &G, 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_NAME_QUERY, Data);
    UE_LOG(LogWowNet, Verbose, TEXT("Sent name query for GUID %lld"), Guid);
}

void UWowConnectionManager::SendCreatureQuery(int32 Entry, int64 Guid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Reserve(12);

    // Entry (uint32)
    uint32 E = static_cast<uint32>(Entry);
    Data.Append(reinterpret_cast<const uint8*>(&E), 4);

    // GUID (uint64)
    uint64 G = static_cast<uint64>(Guid);
    Data.Append(reinterpret_cast<const uint8*>(&G), 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_CREATURE_QUERY, Data);
    UE_LOG(LogWowNet, Verbose, TEXT("Sent creature query for Entry %d, GUID %lld"), Entry, Guid);
}

void UWowConnectionManager::SendTextEmote(int32 EmoteTextId, int64 InTargetGuid)
{
    if (!WorldSocket.IsValid() || State != EWowSessionState::WorldInGame) return;

    TArray<uint8> Data;
    Data.Reserve(16);

    // EmoteTextId (uint32) — from EmotesText.dbc
    uint32 TextEmoteId = static_cast<uint32>(EmoteTextId);
    Data.Append(reinterpret_cast<const uint8*>(&TextEmoteId), 4);

    // EmoteNum (uint32) — always 0 for client-sent emotes
    uint32 EmoteNum = 0;
    Data.Append(reinterpret_cast<const uint8*>(&EmoteNum), 4);

    // TargetGuid (uint64) — 0 for untargeted emotes
    uint64 EmoteTargetGuid = static_cast<uint64>(InTargetGuid);
    Data.Append(reinterpret_cast<const uint8*>(&EmoteTargetGuid), 8);

    WorldSocket->SendPacket(WowOpcode::CMSG_TEXT_EMOTE, Data);
    UE_LOG(LogWowNet, Log, TEXT("Sent text emote: EmoteTextId=%d, Target=%lld"), EmoteTextId, InTargetGuid);
}

TArray<uint8> UWowConnectionManager::BuildLearnTalentPacket(uint32 TalentId,uint32 Rank,uint64 PetGuid)
{
    TArray<uint8> Data; if(!TalentId || Rank>4) return Data;
    if(PetGuid) Data.Append(reinterpret_cast<const uint8*>(&PetGuid),8);
    Data.Append(reinterpret_cast<const uint8*>(&TalentId),4); Data.Append(reinterpret_cast<const uint8*>(&Rank),4); return Data;
}
TArray<uint8> UWowConnectionManager::BuildPreviewTalentsPacket(const TArray<FWowTalentInfo>& Talents,uint64 PetGuid)
{
    TArray<uint8> Data; if(Talents.IsEmpty() || Talents.Num()>128) return Data;
    for(const auto& Talent:Talents) if(!Talent.TalentId || Talent.Rank>4) return Data;
    if(PetGuid) Data.Append(reinterpret_cast<const uint8*>(&PetGuid),8);
    const uint32 Count=Talents.Num(); Data.Append(reinterpret_cast<const uint8*>(&Count),4);
    for(const auto& Talent:Talents) { const uint32 Rank=Talent.Rank; Data.Append(reinterpret_cast<const uint8*>(&Talent.TalentId),4); Data.Append(reinterpret_cast<const uint8*>(&Rank),4); }
    return Data;
}
void UWowConnectionManager::SendLearnTalent(int32 TalentId,int32 RequestedRank)
{
    const auto Data=BuildLearnTalentPacket(TalentId,RequestedRank); if(Data.IsEmpty()) return;
    SendRawPacket(WowOpcode::CMSG_LEARN_TALENT,Data);
}
void UWowConnectionManager::SendLearnPetTalent(uint32 TalentId,uint32 Rank)
{
    const uint64 Guid=PacketHandler.PetActionBar.PetGuid; if(!Guid) return;
    const auto Data=BuildLearnTalentPacket(TalentId,Rank,Guid); if(!Data.IsEmpty()) SendRawPacket(0x47A,Data);
}
void UWowConnectionManager::SendLearnPreviewTalents(const TArray<FWowTalentInfo>& Talents,bool Pet)
{
    const uint64 Guid=Pet?PacketHandler.PetActionBar.PetGuid:0; if(Pet && !Guid) return;
    const auto Data=BuildPreviewTalentsPacket(Talents,Guid); if(!Data.IsEmpty()) SendRawPacket(Pet?0x4C2:0x4C1,Data);
}

void UWowConnectionManager::Disconnect()
{
    CancelGlyphUse();
    if (WorldSocket.IsValid())
    {
        WorldSocket->Disconnect();
        WorldSocket.Reset();
    }

    if (AuthSocket.IsValid())
    {
        AuthSocket->Disconnect();
        AuthSocket.Reset();
    }

    PacketHandler.EntityManager.Clear();
    PacketHandler.Merchant.Clear();
    RepairCursor=false; PacketHandler.GuildCanRepair=false; PacketHandler.GuildRepairRequested=false; PacketHandler.GuildRepairId=0; PacketHandler.GuildWithdrawMoney=0; PacketHandler.GuildRoster.Reset(); PacketHandler.GuildRankAccess.Reset(); PacketHandler.GuildInfoText.Empty(); PacketHandler.ItemRefunds.Empty(); PacketHandler.RequestedItemRefunds.Empty(); PacketHandler.TotalPlayedTime=0; PacketHandler.PlayedTimeReceived=0; PacketHandler.PlayedTimeRequested=false;
    PacketHandler.WeaponProficiency=PacketHandler.ArmorProficiency=0; PacketHandler.HasWeaponProficiency=PacketHandler.HasArmorProficiency=false;
    PacketHandler.CombatHistory.Reset();
    PacketHandler.OnLoginVerifyWorld.Clear();
    PacketHandler.OnSendPacket.Unbind();

    SessionKey.Empty();
    CachedRealms.Empty();
    CachedCharacters.Empty();
    CachedAccountName.Empty();

    SetState(EWowSessionState::Disconnected);
}
