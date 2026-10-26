#pragma once
#include "CoreMinimal.h"
#include "WowSessionState.h"
#include "WowPacketHandler.h"
#include "WowConnectionManager.generated.h"

class FWowAuthSocket;
class FWowWorldSocket;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSessionStateChanged, EWowSessionState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRealmList, const TArray<FWowRealmInfo>&, Realms);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharacterList, const TArray<FWowCharacterInfo>&, Characters);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWowError, const FString&, Msg);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharCreateResult, uint8, ResultCode);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLogoutResponse, uint32, bool);
DECLARE_MULTICAST_DELEGATE(FOnLogoutComplete);
DECLARE_MULTICAST_DELEGATE(FOnWowCursorChanged);
DECLARE_DELEGATE_RetVal_OneParam(bool,FOnWowMacroUse,uint32);

enum class EWowActionInvocationKind : uint8
{
    None,
    SpellCast,
    AutoAttack,
    ItemUse,
    Macro,
};

struct FWowActionInvocation
{
    EWowActionInvocationKind Kind = EWowActionInvocationKind::None;
    uint32 ActionId = 0;
    int64 TargetGuid = 0;

    bool IsValid() const
    {
        return Kind != EWowActionInvocationKind::None && ActionId != 0;
    }
};

UCLASS(BlueprintType)
class WOWNETWORK_API UWowConnectionManager : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable) void Login(const FString& User, const FString& Pass, const FString& Server, int32 Port = 3724);
    UFUNCTION(BlueprintCallable) void SelectRealm(int32 Index);
    UFUNCTION(BlueprintCallable) void RequestCharacterList();
    UFUNCTION(BlueprintCallable) void EnterWorld(int64 Guid);
    UFUNCTION(BlueprintCallable) void CreateCharacter(const FString& Name, uint8 Race, uint8 Class, uint8 Gender, uint8 Skin = 0, uint8 Face = 0, uint8 HairStyle = 0, uint8 HairColor = 0, uint8 FacialHair = 0);
    UFUNCTION(BlueprintCallable) void DeleteCharacter(int64 Guid);
    UFUNCTION(BlueprintCallable) void Disconnect();
    void RequestLogout();
    void CancelLogout();
    FOnLogoutResponse OnLogoutResponse;
    FOnLogoutComplete OnLogoutComplete;
    static TArray<uint8> BuildChatPacket(const FString& Message,int32 Type,const FString& Target,int32 Language);
    UFUNCTION(BlueprintCallable) EWowSessionState GetState() const { return State; }

    /** Send a movement packet to the server */
    UFUNCTION(BlueprintCallable) void SendMovement(int32 Opcode, const FVector& Position, float Orientation, int32 MoveFlags);
    void SendMovementInfo(int32 Opcode,const FWowMovementInfo& Movement);
    static TArray<uint8> BuildMovementPacket(uint64 Guid,const FWowMovementInfo& Movement,uint32 TimeMilliseconds);

    /** Send a chat message */
    UFUNCTION(BlueprintCallable) void SendChatMessage(const FString& Message, int32 Type = 1 /*SAY*/, const FString& Target = TEXT(""), int32 Language = 7 /*Common*/);

    /** Send keep-alive heartbeat */
    UFUNCTION(BlueprintCallable) void SendKeepAlive();

    /** Set target selection */
    UFUNCTION(BlueprintCallable) void SendSetSelection(int64 TargetGuid);

    /** Cast a spell on the current target (or self if no target) */
    UFUNCTION(BlueprintCallable) void SendCastSpell(int32 SpellId, int64 TargetGuid = 0);

    /** Assign or clear an action bar slot */
    void SendSetActionButton(int32 SlotIndex, uint32 ActionId, uint8 ActionType = 0);
    void SendClearActionButton(int32 SlotIndex);

    /** Cursor payload helpers for spell/action pickup flows */
    void PickupSpellCursor(int32 SpellId, const FString& BookType = TEXT("spell"));
    void PickupMacroCursor(uint32 Id);
    FOnWowMacroUse OnMacroUse;
    void PickupActionCursor(int32 SlotIndex);
    void PickupBagItemCursor(uint8 SourceBag, uint8 SourceSlot, int32 ItemId, const FString& ItemLink = TEXT(""), uint32 SplitCount = 0);
    void PickupInventoryItemCursor(uint8 InventorySlot, int32 ItemId, const FString& ItemLink = TEXT(""));
    void ClearCursorPayload();
    FOnWowCursorChanged OnCursorChanged;
    bool HasCursorPayload() const;
    bool HasCursorSpellPayload() const;
    bool HasCursorItemPayload() const;
    bool HasCursorMerchantPayload() const;
    uint32 GetCursorMerchantItemId() const;
    void PickupMerchantCursor(int32 Index);
    bool SellCursorToMerchant();
    void CloseMerchant();
    void SetRepairMode(bool Active);
    bool IsRepairMode() const { return RepairCursor && PacketHandler.Merchant.Open; }
    const FWowItemEntity* GetCursorItem() const;
    uint32 GetCursorItemCount() const;
    void RequestItemRefundInfo(uint64 Guid);
    bool SendBankItem(uint8 Bag,uint8 Slot,bool Deposit);
    bool SendBuyBankSlot();
    void CloseBank();
    void OpenMail(uint64 Guid);
    void CloseMail();
    void ClearOutgoingMail(bool Force=false);
    bool ClickOutgoingMailAttachment(int32 Index,bool Remove=false);
    bool IsItemInOutgoingMail(uint64 Guid) const;
    const FWowItemEntity* GetOutgoingMailItem(int32 Index) const;
    bool SendMail(const FString& Receiver,const FString& Subject,const FString& Body);
    bool SendMailAction(uint32 Id,uint32 Action,uint32 Attachment=0);
    bool MarkMailRead(uint32 Id);
    void TickMailState();
    void StartMailAutoLoot(uint32 Id);
    void QueryNextMailTime();
    void ResetMailState();
    bool FindCarriedItemPosition(uint64 Guid,uint8& Bag,uint8& Slot) const;
    void CheckOutgoingMailRefund(int32 Index);
    void RespondOutgoingMailLock(int32 Index,bool Keep);
    bool RejectOutgoingMailItem(const TCHAR* Key);
    bool SendItemRefund(uint64 Guid);
    void RefreshItemCursor();
    bool DropActionCursor();
    bool GetCursorInfo(FString& OutType, int32& OutId, FString& OutDetail) const;
    bool PlaceCursorIntoActionSlot(int32 SlotIndex);
    bool PlaceCursorIntoContainerSlot(uint8 DestinationBag, uint8 DestinationSlot);
    bool PlaceCursorIntoFirstFreeContainerSlot(uint8 DestinationBag);
    bool AutoEquipCursorItem();
    bool AutoStoreCursorItem(uint8 DestinationBag = 255);
    bool DeleteCursorItem();

    /** Resolve the action assigned to a slot into a concrete gameplay invocation. */
    static FWowActionInvocation ResolveActionInvocation(uint32 PackedAction, int64 TargetGuid);

    /** Use the action assigned to a slot, matching Blizzard action-button behavior. */
    bool UseActionSlot(int32 SlotIndex);

    /** Start melee attack on target */
    UFUNCTION(BlueprintCallable) void SendAttackSwing(int64 TargetGuid);

    /** Stop melee attack */
    UFUNCTION(BlueprintCallable) void SendAttackStop();

    /** Send loot request (right-click corpse to open loot window) */
    UFUNCTION(BlueprintCallable) void SendLoot(int64 CorpseGuid);

    /** Send loot release (close loot window) */
    UFUNCTION(BlueprintCallable) void SendLootRelease(int64 CorpseGuid);

    /** Send loot item command */
    UFUNCTION(BlueprintCallable) void SendLootItem(int32 LootSlot);

    /** Send buy item from vendor command */
    UFUNCTION(BlueprintCallable) void SendBuyItem(int64 VendorGuid, int32 ItemId, int32 Count = 1);
    void RequestVendorInventory(uint64 VendorGuid);
    /** Counts are vendor bundles, not individual items in a bundle. */
    bool SendBuyVendorSlot(uint32 VendorSlot,uint32 Count);
    bool SendBuyVendorSlotIntoBag(uint32 VendorSlot,uint32 Count,uint8 Bag,uint8 Slot);
    bool SendBuybackItem(uint32 InventorySlot);
    bool SendRepairItems(uint64 ItemGuid=0,bool GuildFunds=false);

    /** Send sell item to vendor command */
    UFUNCTION(BlueprintCallable) void SendSellItem(int64 VendorGuid, int64 ItemGuid, int32 Count = 1);

    /** Auto-equip an inventory item from the specified bag/slot. */
    void SendAutoEquipItem(uint8 SourceBag, uint8 SourceSlot);
    void SendSwapItem(uint8 SourceBag,uint8 SourceSlot,uint8 DestinationBag,uint8 DestinationSlot);
    void SendUseItem(uint8 Bag,uint8 Slot,uint64 ItemGuid,uint32 SpellId,uint64 ExplicitTargetGuid=0,int32 GlyphSocket=-1);
    struct FPendingGlyph { uint64 ItemGuid=0; uint32 Spell=0,Property=0; uint8 Bag=255,Slot=0,Group=0; };
    FPendingGlyph PendingGlyph;
    FOnWowCursorChanged OnGlyphTargetingChanged;
    bool BeginGlyphUse(uint8 Bag,uint8 Slot,uint64 ItemGuid,uint32 SpellId);
    bool HasPendingGlyph() const;
    bool GlyphMatchesSocket(uint32 Socket) const;
    bool PlaceGlyph(uint32 Socket);
    void CancelGlyphUse();
    void SendRemoveGlyph(uint32 Socket);
    static TArray<uint8> BuildUseItemPacket(uint8 Bag,uint8 Slot,uint64 ItemGuid,uint32 SpellId,uint64 ExplicitTargetGuid=0,uint32 GlyphSocket=0);
    void SendSetTitle(int32 TitleIndex);

    /** Auto-store an equipped or bag item into the requested destination bag. */
    void SendAutoStoreBagItem(uint8 SourceBag, uint8 SourceSlot, uint8 DestinationBag = 255);

    /** Split an item stack into a specific destination slot. */
    void SendSplitItem(uint8 SourceBag, uint8 SourceSlot, uint8 DestinationBag, uint8 DestinationSlot, uint32 Count);

    /** Destroy an item or reduce a stack count. */
    void SendDestroyItem(uint8 Bag, uint8 Slot, uint8 Count = 0);

    /** Start a player trade with the target player */
    UFUNCTION(BlueprintCallable) void SendInitiateTrade(int64 TargetGuid);

    /** Accept an incoming trade request and open the trade window */
    UFUNCTION(BlueprintCallable) void SendBeginTrade();

    /** Accept the current trade contents */
    UFUNCTION(BlueprintCallable) void SendAcceptTrade();

    /** Revoke local trade acceptance */
    UFUNCTION(BlueprintCallable) void SendUnacceptTrade();

    /** Cancel the current trade */
    UFUNCTION(BlueprintCallable) void SendCancelTrade();

    /** Set the amount of gold offered in the current trade */
    UFUNCTION(BlueprintCallable) void SendSetTradeGold(int32 CopperAmount);

    /** Accept the currently pending duel request */
    void SendAcceptDuel(int64 ArbiterGuid);

    /** Decline a pending duel request or forfeit an active duel */
    void SendCancelDuel(int64 ArbiterGuid);

    /** Request the current pet action bar state from the server */
    void SendRequestPetInfo();

    /** Trigger the pet action currently assigned to a pet action bar slot */
    void SendPetActionBarSlot(int32 SlotIndex, int64 TargetGuid = 0);

    /** Toggle pet spell autocast for the action currently assigned to a slot */
    void SendPetSpellAutocast(int32 SlotIndex, bool bEnabled);

    /** Request the current raid target icon assignments */
    void SendRequestRaidTargetIcons();

    /** Assign a raid target icon to the specified guid (or clear by passing 0) */
    void SendSetRaidTargetIcon(int32 IconIndex, int64 InTargetGuid);

    /** Start a ready check if the local player can issue one */
    void SendStartReadyCheck();

    /** Send a ready-check response */
    void SendReadyCheckConfirm(bool bReady);

    /** Finish the active ready check */
    void SendFinishReadyCheck();

    /** Send gossip/questgiver hello to initiate NPC interaction */
    UFUNCTION(BlueprintCallable) void SendGossipHello(int64 NpcGuid);
    void SendGossipSelect(uint64 NpcGuid, uint32 MenuId, uint32 OptionId);
    void SendQuestQuery(uint64 NpcGuid, uint32 QuestId);
    void SendQuestComplete(uint64 NpcGuid, uint32 QuestId);
    void SendQuestAbandon(uint32 QuestId);
    void SendLootMoney();
    void RequestItemTemplate(uint32 ItemId);
    void RequestQuestTemplate(uint32 QuestId);
    int32 GetBonusActionOffset() const;
    int32 GetVisibleActionSlot(int32 ButtonIndex) const;

    /** Use an interactable gameobject such as a mailbox */
    UFUNCTION(BlueprintCallable) void SendGameObjectUse(int64 GameObjectGuid);

    /** Send banker activate to open the bank window */
    UFUNCTION(BlueprintCallable) void SendBankerActivate(int64 BankerGuid);

    /** Request mailbox contents from a mailbox gameobject or mailbox NPC */
    UFUNCTION(BlueprintCallable) void SendGetMailList(int64 MailboxGuid);

    /** Send quest accept command */
    UFUNCTION(BlueprintCallable) void SendQuestAccept(int64 QuestGiverGuid, int32 QuestId);
    static TArray<uint8> BuildQuestAcceptPacket(uint64 Guid,uint32 QuestId);

    /** Send quest complete/choose reward command */
    UFUNCTION(BlueprintCallable) void SendQuestChooseReward(int64 QuestGiverGuid, int32 QuestId, int32 RewardChoice = 0);

    /** Get current target GUID */
    UFUNCTION(BlueprintCallable) int64 GetTargetGuid() const { return TargetGuid; }

    /** Get the character name of the local player (from cached character list) */
    UFUNCTION(BlueprintCallable) FString GetCharacterName() const
    {
        uint64 Guid = PacketHandler.EntityManager.LocalPlayerGuid;
        for (const FWowCharacterInfo& C : CachedCharacters)
        {
            if (static_cast<uint64>(C.Guid) == Guid) return C.Name;
        }
        return TEXT("");
    }
    FString GetAccountName() const { return CachedAccountName; }
    FString GetRealmName() const { return CurrentRealmName; }

    /** Send a raw packet (for advanced use) */
    void SendRawPacket(uint32 Opcode, const TArray<uint8>& Data = {});

    /** Send name query for a player */
    UFUNCTION(BlueprintCallable) void SendNameQuery(int64 Guid);

    /** Send creature query for an NPC */
    UFUNCTION(BlueprintCallable) void SendCreatureQuery(int32 Entry, int64 Guid);

    /** Send text emote command */
    UFUNCTION(BlueprintCallable) void SendTextEmote(int32 EmoteTextId, int64 TargetGuid = 0);

    /** Send learn talent command */
    UFUNCTION(BlueprintCallable) void SendLearnTalent(int32 TalentId, int32 RequestedRank);
    static TArray<uint8> BuildLearnTalentPacket(uint32 TalentId,uint32 Rank,uint64 PetGuid=0);
    static TArray<uint8> BuildPreviewTalentsPacket(const TArray<FWowTalentInfo>& Talents,uint64 PetGuid=0);
    void SendLearnPetTalent(uint32 TalentId,uint32 Rank);
    void SendLearnPreviewTalents(const TArray<FWowTalentInfo>& Talents,bool Pet=false);

    /** Get the cached character list (valid after WorldHaveCharList state) */
    UFUNCTION(BlueprintCallable) TArray<FWowCharacterInfo> GetCachedCharacters() const { return CachedCharacters; }

    /** Get the cached realm list (valid after AuthHaveRealmList state) */
    UFUNCTION(BlueprintCallable) TArray<FWowRealmInfo> GetCachedRealms() const { return CachedRealms; }

    /** Packet handler — dispatches SMSG opcodes and tracks entities */
    FWowPacketHandler PacketHandler;

    UPROPERTY(BlueprintAssignable) FOnSessionStateChanged OnStateChanged;
    UPROPERTY(BlueprintAssignable) FOnRealmList OnRealmList;
    UPROPERTY(BlueprintAssignable) FOnCharacterList OnCharacterList;
    UPROPERTY(BlueprintAssignable) FOnWowError OnError;
    UPROPERTY(BlueprintAssignable) FOnCharCreateResult OnCharCreateResult;
private:
    TSet<uint32> RequestedItems;
    enum class ECursorPayloadType : uint8
    {
        None,
        Spell,
        Action,
        Item,
        Merchant,
    };

    struct FCursorPayloadState
    {
        ECursorPayloadType Type = ECursorPayloadType::None;
        int32 PrimaryId = 0;
        FString Detail;
        uint8 ActionType = 0;
        int32 SourceActionSlot = -1;
        uint8 SourceBag = 255;
        uint8 SourceSlot = 0;
        uint8 SourceInventorySlot = 0;
        bool bFromInventorySlot = false;
        uint32 SplitCount = 0;
        uint64 SourceItemGuid=0;
        uint64 SourceVendorGuid=0;
        uint32 SourceVendorSlot=0;
    };

    EWowSessionState State = EWowSessionState::Disconnected;
    void SetState(EWowSessionState S);
    void BroadcastActionButtonsChanged();
    void EnsureActionButtonCapacity(int32 SlotIndex);

    void OnAuthResultReceived(bool bSuccess);
    void OnRealmListReceived(const TArray<FWowRealmInfo>& Realms);
    void OnWorldAuthResult(bool bSuccess);
    void OnWorldCharacterList(const TArray<FWowCharacterInfo>& Characters);

    TSharedPtr<FWowAuthSocket> AuthSocket;
    TSharedPtr<FWowWorldSocket, ESPMode::ThreadSafe> WorldSocket;
    FString CachedAccountName;
    FString CurrentRealmName;
    TArray<uint8> SessionKey;
    TArray<FWowRealmInfo> CachedRealms;
    TArray<FWowCharacterInfo> CachedCharacters;
    int64 TargetGuid = 0;
    FCursorPayloadState CursorPayload;
    bool RepairCursor=false;
    const FWowItemEntity* ResolveCursorSource(uint8 Bag,uint8 Slot) const;
    FDelegateHandle EnterWorldHandle;
};
