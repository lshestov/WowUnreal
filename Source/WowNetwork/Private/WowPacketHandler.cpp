#include "WowPacketHandler.h"
#include "WowOpcodes.h"
#include "WowUpdateFields.h"
#include "WowWardenHandler.h"
#include "Misc/Compression.h"
#include "Formats/Dbc/DbcStore.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowPacket, Log, All);

FWowPacketHandler::FWowPacketHandler()
{
    // Register handlers
    Handlers.Add(WowOpcode::SMSG_GOSSIP_MESSAGE, &FWowPacketHandler::HandleGossipMessage);
    Handlers.Add(WowOpcode::SMSG_ITEM_QUERY_SINGLE_RESPONSE, &FWowPacketHandler::HandleItemQueryResponse);
    Handlers.Add(WowOpcode::SMSG_QUESTGIVER_QUEST_LIST, &FWowPacketHandler::HandleQuestgiverQuestList);
    Handlers.Add(WowOpcode::SMSG_LOOT_REMOVED, &FWowPacketHandler::HandleLootRemoved);
    Handlers.Add(WowOpcode::SMSG_LOOT_CLEAR_MONEY, &FWowPacketHandler::HandleLootClearMoney);
    Handlers.Add(WowOpcode::SMSG_LOGIN_VERIFY_WORLD,      &FWowPacketHandler::HandleLoginVerifyWorld);
    Handlers.Add(WowOpcode::SMSG_UPDATE_OBJECT,            &FWowPacketHandler::HandleUpdateObject);
    Handlers.Add(WowOpcode::SMSG_COMPRESSED_UPDATE_OBJECT, &FWowPacketHandler::HandleCompressedUpdateObject);
    Handlers.Add(WowOpcode::SMSG_DESTROY_OBJECT,           &FWowPacketHandler::HandleDestroyObject);
    Handlers.Add(WowOpcode::SMSG_MESSAGECHAT,              &FWowPacketHandler::HandleMessageChat);
    Handlers.Add(0x03B3, &FWowPacketHandler::HandleGmMessageChat);
    Handlers.Add(WowOpcode::SMSG_INITIAL_SPELLS,           &FWowPacketHandler::HandleInitialSpells);
    Handlers.Add(WowOpcode::SMSG_ACTION_BUTTONS,           &FWowPacketHandler::HandleActionButtons);
    Handlers.Add(WowOpcode::SMSG_TIME_SYNC_REQ,            &FWowPacketHandler::HandleTimeSyncReq);
    Handlers.Add(WowOpcode::SMSG_SPELL_START,              &FWowPacketHandler::HandleSpellStart);
    Handlers.Add(WowOpcode::SMSG_SPELL_GO,                 &FWowPacketHandler::HandleSpellGo);
    Handlers.Add(WowOpcode::SMSG_SPELL_FAILURE,            &FWowPacketHandler::HandleSpellFailure);
    Handlers.Add(WowOpcode::SMSG_SPELL_COOLDOWN,           &FWowPacketHandler::HandleSpellCooldown);
    Handlers.Add(WowOpcode::SMSG_ATTACKERSTATEUPDATE,      &FWowPacketHandler::HandleAttackerStateUpdate);
    Handlers.Add(0x0250,&FWowPacketHandler::HandleSpellDamageLog);
    Handlers.Add(0x0150,&FWowPacketHandler::HandleSpellHealLog);
    Handlers.Add(WowOpcode::SMSG_ATTACKSTART,              &FWowPacketHandler::HandleAttackStart);
    Handlers.Add(WowOpcode::SMSG_ATTACKSTOP,               &FWowPacketHandler::HandleAttackStop);
    Handlers.Add(WowOpcode::SMSG_AURA_UPDATE,              &FWowPacketHandler::HandleAuraUpdate);
    Handlers.Add(WowOpcode::SMSG_AURA_UPDATE_ALL,          &FWowPacketHandler::HandleAuraUpdateAll);
    Handlers.Add(WowOpcode::SMSG_POWER_UPDATE,             &FWowPacketHandler::HandlePowerUpdate);
    Handlers.Add(WowOpcode::SMSG_MONSTER_MOVE,             &FWowPacketHandler::HandleMonsterMove);

    // Inventory system handlers
    Handlers.Add(WowOpcode::SMSG_INVENTORY_CHANGE_FAILURE, &FWowPacketHandler::HandleInventoryChangeFailure);
    Handlers.Add(WowOpcode::SMSG_LOOT_RESPONSE,            &FWowPacketHandler::HandleLootResponse);
    Handlers.Add(WowOpcode::SMSG_LOOT_RELEASE_RESPONSE,    &FWowPacketHandler::HandleLootReleaseResponse);
    Handlers.Add(WowOpcode::SMSG_ITEM_PUSH_RESULT,         &FWowPacketHandler::HandleItemPushResult);

    // Vendor system handlers
    Handlers.Add(WowOpcode::SMSG_LIST_INVENTORY,           &FWowPacketHandler::HandleListInventory);
    Handlers.Add(WowOpcode::SMSG_BUY_ITEM,&FWowPacketHandler::HandleBuyItemResult);
    Handlers.Add(WowOpcode::SMSG_BUY_FAILED,&FWowPacketHandler::HandleBuyItemFailure);
    Handlers.Add(WowOpcode::SMSG_SELL_ITEM,&FWowPacketHandler::HandleSellItemResult);

    // Quest system handlers
    Handlers.Add(WowOpcode::SMSG_QUESTGIVER_STATUS,        &FWowPacketHandler::HandleQuestgiverStatus);
    Handlers.Add(WowOpcode::SMSG_QUEST_QUERY_RESPONSE,      &FWowPacketHandler::HandleQuestQueryResponse);
    Handlers.Add(WowOpcode::SMSG_QUESTGIVER_QUEST_DETAILS, &FWowPacketHandler::HandleQuestgiverQuestDetails);
    Handlers.Add(WowOpcode::SMSG_QUESTGIVER_OFFER_REWARD,  &FWowPacketHandler::HandleQuestgiverOfferReward);
    Handlers.Add(WowOpcode::SMSG_QUEST_UPDATE_ADD_KILL,    &FWowPacketHandler::HandleQuestUpdateAddKill);
    Handlers.Add(WowOpcode::SMSG_QUEST_UPDATE_COMPLETE,    &FWowPacketHandler::HandleQuestUpdateComplete);

    // Talent system handlers
    Handlers.Add(WowOpcode::SMSG_TALENTS_INFO,             &FWowPacketHandler::HandleTalentsInfo);
    Handlers.Add(WowOpcode::SMSG_LEARNED_SPELL,            &FWowPacketHandler::HandleLearnedSpell);
    Handlers.Add(WowOpcode::SMSG_SUPERCEDED_SPELL,         &FWowPacketHandler::HandleSupersededSpell);
    Handlers.Add(WowOpcode::SMSG_REMOVED_SPELL,            &FWowPacketHandler::HandleRemovedSpell);

    // ── Social / Guild / Friends handlers ──────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_FRIEND_LIST,              &FWowPacketHandler::HandleFriendList);
    Handlers.Add(WowOpcode::SMSG_FRIEND_STATUS,            &FWowPacketHandler::HandleFriendStatus);
    Handlers.Add(WowOpcode::SMSG_GUILD_ROSTER,             &FWowPacketHandler::HandleGuildRoster);
    Handlers.Add(0x04B2,&FWowPacketHandler::HandleItemRefundInfo);
    Handlers.Add(0x04B5,&FWowPacketHandler::HandleItemRefundResult);
    Handlers.Add(WowOpcode::SMSG_GUILD_EVENT,              &FWowPacketHandler::HandleGuildEvent);
    Handlers.Add(WowOpcode::SMSG_CHANNEL_NOTIFY,           &FWowPacketHandler::HandleChannelNotify);
    Handlers.Add(0x009B,&FWowPacketHandler::HandleChannelList);
    Handlers.Add(WowOpcode::SMSG_GROUP_LIST,               &FWowPacketHandler::HandleGroupList);
    Handlers.Add(WowOpcode::MSG_RAID_TARGET_UPDATE,        &FWowPacketHandler::HandleRaidTargetUpdate);
    Handlers.Add(WowOpcode::MSG_RAID_READY_CHECK,          &FWowPacketHandler::HandleRaidReadyCheck);
    Handlers.Add(WowOpcode::MSG_RAID_READY_CHECK_CONFIRM,  &FWowPacketHandler::HandleRaidReadyCheckConfirm);
    Handlers.Add(WowOpcode::MSG_RAID_READY_CHECK_FINISHED, &FWowPacketHandler::HandleRaidReadyCheckFinished);
    Handlers.Add(WowOpcode::SMSG_PARTY_COMMAND_RESULT,     &FWowPacketHandler::HandlePartyCommandResult);
    Handlers.Add(WowOpcode::SMSG_GROUP_INVITE,             &FWowPacketHandler::HandleGroupInvite);
    Handlers.Add(WowOpcode::SMSG_WHO,                      &FWowPacketHandler::HandleWho);

    // ── Taxi / Flight Path handlers ─────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_SHOWTAXINODES,            &FWowPacketHandler::HandleShowTaxiNodes);
    Handlers.Add(WowOpcode::SMSG_ACTIVATETAXIREPLY,        &FWowPacketHandler::HandleActivateTaxiReply);
    Handlers.Add(WowOpcode::SMSG_NEW_TAXI_PATH,            &FWowPacketHandler::HandleNewTaxiPath);

    // ── Death / Corpse / Resurrection handlers ──────────────────────────────
    Handlers.Add(WowOpcode::SMSG_CORPSE_RECLAIM_DELAY,     &FWowPacketHandler::HandleCorpseReclaimDelay);
    Handlers.Add(WowOpcode::SMSG_RESURRECT_REQUEST,        &FWowPacketHandler::HandleResurrectRequest);

    // ── Name query handlers ─────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_NAME_QUERY_RESPONSE,      &FWowPacketHandler::HandleNameQueryResponse);
    Handlers.Add(WowOpcode::SMSG_CREATURE_QUERY_RESPONSE,  &FWowPacketHandler::HandleCreatureQueryResponse);

    // ── Emote handlers ──────────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_EMOTE,                    &FWowPacketHandler::HandleEmote);
    Handlers.Add(WowOpcode::SMSG_TEXT_EMOTE,               &FWowPacketHandler::HandleTextEmote);

    // Movement handlers — all use the same parser
    for (uint16 Op = WowOpcode::MSG_MOVE_START_FORWARD; Op <= WowOpcode::MSG_MOVE_SET_PITCH; ++Op)
    {
        Handlers.Add(Op, &FWowPacketHandler::HandleMovement);
    }
    Handlers.Add(WowOpcode::MSG_MOVE_ROOT, &FWowPacketHandler::HandleMovement);
    Handlers.Add(WowOpcode::MSG_MOVE_UNROOT, &FWowPacketHandler::HandleMovement);
    Handlers.Add(WowOpcode::MSG_MOVE_HEARTBEAT, &FWowPacketHandler::HandleMovement);

    // Warden and teleport handlers
    Handlers.Add(WowOpcode::SMSG_WARDEN_DATA,       &FWowPacketHandler::HandleWardenData);
    Handlers.Add(WowOpcode::MSG_MOVE_TELEPORT,      &FWowPacketHandler::HandleMovement);
    Handlers.Add(WowOpcode::MSG_MOVE_TELEPORT_ACK,  &FWowPacketHandler::HandleMoveTeleport);
    Handlers.Add(WowOpcode::SMSG_TRANSFER_PENDING,  &FWowPacketHandler::HandleTransferPending);
    Handlers.Add(WowOpcode::SMSG_NEW_WORLD,         &FWowPacketHandler::HandleNewWorld);

    // ── Player progression handlers ─────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_LEVELUP_INFO,            &FWowPacketHandler::HandleLevelUpInfo);
    Handlers.Add(WowOpcode::SMSG_LOG_XPGAIN,              &FWowPacketHandler::HandleLogXPGain);
    Handlers.Add(WowOpcode::SMSG_EXPLORATION_EXPERIENCE,  &FWowPacketHandler::HandleExplorationExperience);
    Handlers.Add(WowOpcode::SMSG_ENVIRONMENTAL_DAMAGE_LOG, &FWowPacketHandler::HandleEnvironmentalDamageLog);
    Handlers.Add(WowOpcode::SMSG_BINDPOINTUPDATE,         &FWowPacketHandler::HandleBindPointUpdate);
    Handlers.Add(WowOpcode::SMSG_PLAYED_TIME,             &FWowPacketHandler::HandlePlayedTime);

    // ── World state handlers ────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_WEATHER,                 &FWowPacketHandler::HandleWeather);
    Handlers.Add(WowOpcode::SMSG_INIT_WORLD_STATES,       &FWowPacketHandler::HandleInitWorldStates);
    Handlers.Add(WowOpcode::SMSG_UPDATE_WORLD_STATE,      &FWowPacketHandler::HandleUpdateWorldState);

    // ── Account/Party handlers ──────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_SET_PROFICIENCY,         &FWowPacketHandler::HandleSetProficiency);
    Handlers.Add(WowOpcode::SMSG_ACCOUNT_DATA_TIMES,      &FWowPacketHandler::HandleAccountDataTimes);
    Handlers.Add(WowOpcode::SMSG_PARTY_MEMBER_STATS,      &FWowPacketHandler::HandlePartyMemberStats);

    // ── Mail system handlers ────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_SHOW_MAILBOX,            &FWowPacketHandler::HandleShowMailbox);
    Handlers.Add(WowOpcode::SMSG_MAIL_LIST_RESULT,        &FWowPacketHandler::HandleMailListResult);
    Handlers.Add(0x0239,&FWowPacketHandler::HandleMailResult);
    Handlers.Add(0x0284,&FWowPacketHandler::HandleNextMailTime);
    Handlers.Add(0x0285,&FWowPacketHandler::HandleReceivedMail);

    // ── Pet system handlers ─────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_PET_SPELLS,              &FWowPacketHandler::HandlePetSpells);

    // ── Duel system handlers ────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_DUEL_REQUESTED,          &FWowPacketHandler::HandleDuelRequested);
    Handlers.Add(WowOpcode::SMSG_DUEL_OUTOFBOUNDS,        &FWowPacketHandler::HandleDuelOutOfBounds);
    Handlers.Add(WowOpcode::SMSG_DUEL_INBOUNDS,           &FWowPacketHandler::HandleDuelInBounds);
    Handlers.Add(WowOpcode::SMSG_DUEL_COMPLETE,           &FWowPacketHandler::HandleDuelComplete);
    Handlers.Add(WowOpcode::SMSG_DUEL_WINNER,             &FWowPacketHandler::HandleDuelWinner);
    Handlers.Add(WowOpcode::SMSG_DUEL_COUNTDOWN,          &FWowPacketHandler::HandleDuelCountdown);

    // ── Trade system handlers ───────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_TRADE_STATUS,            &FWowPacketHandler::HandleTradeStatus);
    Handlers.Add(WowOpcode::SMSG_TRADE_STATUS_EXTENDED,   &FWowPacketHandler::HandleTradeStatusExtended);

    // ── Bank system handlers ────────────────────────────────────────────────
    Handlers.Add(WowOpcode::SMSG_SHOW_BANK,               &FWowPacketHandler::HandleShowBank);

    // Bind Warden response delegate to send packets
    WardenHandler.OnSendResponse.BindLambda([this](uint32 Opcode, const TArray<uint8>& Data)
    {
        SendWardenResponse(Opcode, Data);
    });
}

void FWowPacketHandler::HandlePacket(uint16 Opcode, const TArray<uint8>& Data)
{
    // Validate opcode is in valid WoW 3.3.5a range (max opcode ~0x04F6)
    if (Opcode > 0x0FFF)
    {
        UE_LOG(LogWowPacket, Warning, TEXT("Ignoring out-of-range opcode 0x%04X (%d bytes)"), Opcode, Data.Num());
        return;
    }

    if(Opcode==0x0329 || Opcode==0x04EB)
    {
        FPacketReader R(Data); if(R.CanRead(4)) { auto& Difficulty=Opcode==0x0329?DungeonDifficulty:RaidDifficulty; Difficulty=R.ReadU32(); }
    }
    else if(Opcode==0x02CC)
    {
        FPacketReader R(Data); const uint32 Count=R.ReadU32();
        if(Count<1000 && R.CanRead(Count*22)) { SavedInstances.Reset(); for(uint32 I=0;I<Count;++I) { FSavedInstance V; V.Map=R.ReadU32(); V.Difficulty=R.ReadU32(); V.Id=R.ReadU64(); V.Locked=R.ReadU8()!=0; V.Extended=R.ReadU8()!=0; V.Reset=R.ReadU32(); SavedInstances.Add(V); } }
    }
    else if(Opcode==0x02A9 || Opcode==0x0219 || Opcode==0x01CB || Opcode==0x02FD)
    {
        FPacketReader R(Data); FString Text;
        if(Opcode==0x02A9) Text=FString::Printf(TEXT("No player named '%s' is currently playing."),*R.ReadCString());
        else if(Opcode==0x0219) Text=TEXT("You can only whisper to members of your faction.");
        else if(Opcode==0x01CB) Text=R.ReadCString();
        else Text=TEXT("The server has restricted this chat message.");
        OnChatMessage.Broadcast(0,0,0,TEXT(""),Text,TEXT(""));
    }
    else if(Opcode==0x0122)
    {
        FPacketReader R(Data); const uint32 Count=R.ReadU32();
        if(Count<=128 && R.CanRead(Count*5)) { Reputations.SetNum(128); for(uint32 I=0;I<Count;++I) { Reputations[I].Flags=R.ReadU8(); Reputations[I].Standing=int32(R.ReadU32()); } }
    }
    else if(Opcode==0x0123 || Opcode==0x0313)
    {
        FPacketReader R(Data); if(R.CanRead(4)) { const uint32 I=R.ReadU32(); if(Reputations.IsValidIndex(I)) { if(Opcode==0x0123) Reputations[I].Flags|=1; else if(R.CanRead(1)) Reputations[I].Flags=(Reputations[I].Flags&~2)|(R.ReadU8()?2:0); } }
    }
    else if(Opcode==0x0124)
    {
        FPacketReader R(Data); if(R.CanRead(9)) { R.ReadFloat(); R.ReadU8(); const uint32 Count=R.ReadU32(); if(Count<=128 && R.CanRead(Count*8)) for(uint32 N=0;N<Count;++N) { const uint32 I=R.ReadU32(); const int32 Standing=int32(R.ReadU32()); if(Reputations.IsValidIndex(I)) Reputations[I].Standing=Standing; } }
    }
    if(Opcode==0x03F0 || Opcode==0x03F1 || Opcode==0x03F2) {FPacketReader Users(Data); HandleChannelUserList(Users,Opcode==0x03F1);}
    if(Opcode==0x0032 || Opcode==0x0130 || Opcode==0x0139 || Opcode==0x013A || Opcode==0x014F || Opcode==0x0151 || Opcode==0x01D7 || Opcode==0x01F5 || Opcode==0x024B || Opcode==0x024C || Opcode==0x024E || Opcode==0x024F || Opcode==0x0260 || Opcode==0x0262 || Opcode==0x0263 || Opcode==0x027B || Opcode==0x032F || Opcode==0x0333)
    {FPacketReader Combat(Data); HandleCombatPacket(Opcode,Combat); return;}
    HandlerFunc* Found = Handlers.Find(Opcode);
    if(Opcode==0x01BA && Data.Num()>=4) { FPacketReader BankResult(Data); OnBankSlotResult.Broadcast(BankResult.ReadU32()); }
    if(Opcode==0x03FE && Data.Num()>=4)
    {
        FPacketReader Withdraw(Data); const uint32 Money=Withdraw.ReadU32(); GuildWithdrawMoney=Money==MAX_uint32?-1:int64(Money); OnVendorUpdated.Broadcast();
    }
    if(Opcode==0x0127 && Data.Num()>=5)
    {
        FPacketReader Proficiency(Data); const uint8 Class=Proficiency.ReadU8(); const uint32 Mask=Proficiency.ReadU32();
        if(Class==2) { WeaponProficiency=Mask; HasWeaponProficiency=true; }
        else if(Class==4) { ArmorProficiency=Mask; HasArmorProficiency=true; }
        OnPlayerInventoryUpdate.Broadcast();
    }
    if (Found)
    {
        FPacketReader R(Data);
        (this->**Found)(R);
    }
    else
    {
        UE_LOG(LogWowPacket, Verbose, TEXT("Unhandled opcode 0x%04X (%s) — %d bytes"),
            Opcode, WowOpcode::GetName(Opcode), Data.Num());
    }

    // Notify subscribers (EventSystem wires this to fire WoW UI events)
    OnOpcodeReceived.Broadcast(Opcode);
}

// ── SMSG_LOGIN_VERIFY_WORLD ──────────────────────────────────────────────────
// uint32 mapId, float x, float y, float z, float orientation

void FWowPacketHandler::HandleLoginVerifyWorld(FPacketReader& R)
{
    uint32 MapId = R.ReadU32();
    float X = R.ReadFloat();
    float Y = R.ReadFloat();
    float Z = R.ReadFloat();
    float O = R.ReadFloat();

    UE_LOG(LogWowPacket, Log, TEXT("LOGIN_VERIFY_WORLD: map=%d pos=(%.1f, %.1f, %.1f) orient=%.2f"),
        MapId, X, Y, Z, O);

    // Clear quest log on zone change
    QuestLog.Empty();

    CurrentMapId=MapId;
    OnLoginVerifyWorld.Broadcast(MapId, X, Y, Z, O);
}

void FWowPacketHandler::HandleQuestQueryResponse(FPacketReader& R)
{
    if(!R.CanRead(4)) return;
    FWowQuestTemplate Q; Q.Id=R.ReadU32(); if(Q.Id&0x80000000) return;
    if(!R.CanRead(100+80+60+16)) return;
    R.ReadU32(); Q.Level=int32(R.ReadU32()); Q.MinLevel=R.ReadU32(); Q.Zone=int32(R.ReadU32()); Q.Type=R.ReadU32(); Q.Group=R.ReadU32();
    R.Skip(20); Q.XPDifficulty=R.ReadU32(); Q.Money=int32(R.ReadU32());
    Q.MaxLevelMoney=R.ReadU32(); Q.RewardSpell=R.ReadU32(); Q.RewardSpellCast=R.ReadU32(); Q.Honor=R.ReadU32(); Q.HonorMultiplier=R.ReadFloat();
    Q.SourceItemId=R.ReadU32(); Q.Flags=R.ReadU32(); Q.TitleId=R.ReadU32(); R.ReadU32(); Q.Talents=R.ReadU32(); Q.ArenaPoints=R.ReadU32(); R.ReadU32();
    for(int32 I=0;I<4;++I) { FWowQuestRewardItem Item; Item.ItemId=R.ReadU32(); Item.Count=R.ReadU32(); if(Item.ItemId) Q.Rewards.Add(Item); }
    for(int32 I=0;I<6;++I) { FWowQuestRewardItem Item; Item.ItemId=R.ReadU32(); Item.Count=R.ReadU32(); if(Item.ItemId) Q.Choices.Add(Item); }
    for(auto& Id:Q.RewardFactionId) Id=R.ReadU32();
    for(auto& Value:Q.RewardFactionValue) Value=int32(R.ReadU32());
    for(auto& Value:Q.RewardFactionOverride) Value=int32(R.ReadU32());
    R.Skip(16);
    Q.Title=R.ReadCString(); Q.Objectives=R.ReadCString(); Q.Details=R.ReadCString(); Q.AreaDescription=R.ReadCString(); Q.CompletedText=R.ReadCString();
    if(!R.CanRead(64+48+4)) return;
    for(int32 I=0; I<4; ++I) { Q.CreatureId[I]=R.ReadU32(); Q.CreatureCount[I]=R.ReadU32(); R.Skip(8); }
    for(int32 I=0; I<6; ++I) { Q.ItemId[I]=R.ReadU32(); Q.ItemCount[I]=R.ReadU32(); }
    for(int32 I=0; I<4; ++I) Q.ObjectiveText[I]=R.ReadCString();
    QuestTemplates.Add(Q.Id,MoveTemp(Q)); OnQuestLogChanged.Broadcast();
}

// ── SMSG_UPDATE_OBJECT ───────────────────────────────────────────────────────
// uint32 blockCount, then blockCount update blocks

void FWowPacketHandler::HandleUpdateObject(FPacketReader& R)
{
    uint32 BlockCount = R.ReadU32();

    for (uint32 i = 0; i < BlockCount && R.Remaining() > 0; ++i)
    {
        ParseUpdateBlock(R);
    }

    UE_LOG(LogWowPacket, Log, TEXT("UPDATE_OBJECT: %d blocks (total entities: %d created, %d updated, %d destroyed, %d tracked)"),
        BlockCount, EntitiesCreated, EntitiesUpdated, EntitiesDestroyed, EntityManager.Num());
}

// ── SMSG_COMPRESSED_UPDATE_OBJECT ────────────────────────────────────────────
// uint32 decompressedSize, then zlib-compressed UPDATE_OBJECT payload

void FWowPacketHandler::HandleCompressedUpdateObject(FPacketReader& R)
{
    uint32 DecompSize = R.ReadU32();
    int32 CompSize = R.Remaining();

    if (CompSize <= 0 || DecompSize == 0 || DecompSize > 10 * 1024 * 1024)
    {
        UE_LOG(LogWowPacket, Warning, TEXT("COMPRESSED_UPDATE_OBJECT: invalid sizes (comp=%d, decomp=%u)"), CompSize, DecompSize);
        return;
    }

    TArray<uint8> Decompressed;
    Decompressed.SetNumUninitialized(DecompSize);

    // zlib decompress
    int32 UncompSize = static_cast<int32>(DecompSize);
    if (!FCompression::UncompressMemory(NAME_Zlib, Decompressed.GetData(), UncompSize,
            R.Data + R.Pos, CompSize))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("COMPRESSED_UPDATE_OBJECT: zlib decompress failed"));
        return;
    }

    UE_LOG(LogWowPacket, Verbose, TEXT("COMPRESSED_UPDATE_OBJECT: decompressed %d → %d bytes"), CompSize, UncompSize);

    // Parse as regular UPDATE_OBJECT
    FPacketReader DecompReader(Decompressed);
    uint32 BlockCount = DecompReader.ReadU32();
    for (uint32 i = 0; i < BlockCount && DecompReader.Remaining() > 0; ++i)
    {
        ParseUpdateBlock(DecompReader);
    }

    UE_LOG(LogWowPacket, Log, TEXT("COMPRESSED_UPDATE_OBJECT: %d blocks (total: %d entities tracked)"),
        BlockCount, EntityManager.Num());
}

// ── Parse a single update block ──────────────────────────────────────────────

void FWowPacketHandler::ParseUpdateBlock(FPacketReader& R)
{
    uint8 Type = R.ReadU8();

    switch (Type)
    {
    case UpdateType::VALUES:
    {
        uint64 Guid = R.ReadPackedGuid();
        FWowEntity* Entity = EntityManager.Find(Guid);
        if (Entity)
        {
            const int32 PreviousHealth=Entity->IsUnit()?Entity->GetHealth():0;
            ParseUpdateFields(R, *Entity);
            if(PreviousHealth>0 && Entity->IsUnit() && Entity->GetHealth()==0)
            {RecordUnitDeath(Guid); OnEntityDeath.Broadcast(Guid);}
            if (const uint32 UpdatedTypeMask = Entity->GetField(ObjectField::TYPE); UpdatedTypeMask != 0)
            {
                Entity = &EntityManager.PromoteToTyped(Guid, UpdatedTypeMask);
                Entity->Entry = Entity->GetField(ObjectField::ENTRY);
                Entity->Scale = Entity->GetFieldFloat(ObjectField::SCALE_X);
                if (Entity->Scale == 0.0f)
                {
                    Entity->Scale = 1.0f;
                }
            }

            // Check for player death
            if (Entity->Guid == EntityManager.LocalPlayerGuid && Entity->IsUnit())
            {
                int32 Health = Entity->GetHealth();
                if (Health == 0)
                {
                    UE_LOG(LogWowPacket, Warning, TEXT("Player has died! Health = 0"));
                    OnPlayerDeath.Broadcast();
                }
            }

            EntitiesUpdated++;
            EntityManager.OnEntityUpdated.Broadcast(*Entity);
        }
        else
        {
            UE_LOG(LogWowPacket, Warning, TEXT("VALUES update for unknown GUID %llu"), Guid);
            // Must consume the field block to keep packet aligned
            uint8 SkipBlockCount = R.ReadU8();
            TArray<uint32> SkipMask;
            SkipMask.SetNum(SkipBlockCount);
            for (int32 m = 0; m < SkipBlockCount; ++m)
            {
                SkipMask[m] = R.ReadU32();
            }
            for (int32 Block = 0; Block < SkipBlockCount; ++Block)
            {
                for (int32 Bit = 0; Bit < 32; ++Bit)
                {
                    if (SkipMask[Block] & (1u << Bit))
                    {
                        R.Skip(4);
                    }
                }
            }
            break;
        }
        break;
    }

    case UpdateType::MOVEMENT:
    {
        uint64 Guid = R.ReadPackedGuid();
        FWowEntity& Entity = EntityManager.GetOrCreate(Guid);
        // Movement block
        {
            uint16 Flags = R.ReadU16();
            if (Flags & UpdateFlag::LIVING)
            {
                ParseMovementInfo(R, Entity.Movement);
            }
            else if (Flags & UpdateFlag::STATIONARY_POSITION)
            {
                Entity.Movement.Position.X = R.ReadFloat();
                Entity.Movement.Position.Y = R.ReadFloat();
                Entity.Movement.Position.Z = R.ReadFloat();
                Entity.Movement.Orientation = R.ReadFloat();
            }
        }
        EntityManager.OnEntityUpdated.Broadcast(Entity);
        break;
    }

    case UpdateType::CREATE_OBJECT:
    case UpdateType::CREATE_OBJECT2:
    {
        uint64 Guid = R.ReadPackedGuid();
        uint8 ObjTypeId = R.ReadU8();

        FWowEntity& BaseEntity = EntityManager.GetOrCreate(Guid);
        BaseEntity.ObjectTypeId = ObjTypeId;

        // Movement/position block
        uint16 Flags = R.ReadU16();

        if (Flags & UpdateFlag::LIVING)
        {
            ParseMovementInfo(R, BaseEntity.Movement);
        }
        else if (Flags & UpdateFlag::STATIONARY_POSITION)
        {
            BaseEntity.Movement.Position.X = R.ReadFloat();
            BaseEntity.Movement.Position.Y = R.ReadFloat();
            BaseEntity.Movement.Position.Z = R.ReadFloat();
            BaseEntity.Movement.Orientation = R.ReadFloat();
        }

        if (Flags & UpdateFlag::LOWGUID)
        {
            R.ReadU32(); // lowGuid extra
        }

        if (Flags & UpdateFlag::HAS_TARGET)
        {
            R.ReadPackedGuid(); // target GUID
        }

        if (Flags & UpdateFlag::TRANSPORT)
        {
            R.ReadU32(); // transport timer
        }

        if (Flags & UpdateFlag::VEHICLE)
        {
            R.ReadU32(); // vehicle ID
            R.ReadFloat(); // vehicle aim adjustment
        }

        if (Flags & UpdateFlag::ROTATION)
        {
            R.ReadU64(); // packed rotation (gameobjects)
        }

        // Update fields
        ParseUpdateFields(R, BaseEntity);

        // Extract common fields
        BaseEntity.TypeMask = BaseEntity.GetField(ObjectField::TYPE);
        BaseEntity.Entry = BaseEntity.GetField(ObjectField::ENTRY);
        BaseEntity.Scale = BaseEntity.GetFieldFloat(ObjectField::SCALE_X);
        if (BaseEntity.Scale == 0.0f)
        {
            BaseEntity.Scale = 1.0f;
        }

        FWowEntity& Entity = EntityManager.PromoteToTyped(Guid, BaseEntity.TypeMask);
        Entity.ObjectTypeId = ObjTypeId;
        Entity.Entry = Entity.GetField(ObjectField::ENTRY);
        Entity.Scale = Entity.GetFieldFloat(ObjectField::SCALE_X);
        if (Entity.Scale == 0.0f)
        {
            Entity.Scale = 1.0f;
        }

        EntitiesCreated++;
        if (Guid==EntityManager.LocalPlayerGuid) RefreshQuestLog();
        EntityManager.OnEntityCreated.Broadcast(Entity);
        if (Entity.IsItem()) OnPlayerInventoryUpdate.Broadcast();

        UE_LOG(LogWowPacket, Verbose, TEXT("Created %s GUID=%llu type=%d entry=%u pos=(%.1f,%.1f,%.1f)"),
            Entity.GetEntityKindName(), Guid, ObjTypeId, Entity.Entry,
            Entity.Movement.Position.X, Entity.Movement.Position.Y, Entity.Movement.Position.Z);
        break;
    }

    case UpdateType::OUT_OF_RANGE_OBJECTS:
    {
        uint32 Count = R.ReadU32();
        for (uint32 i = 0; i < Count && R.Remaining() > 0; ++i)
        {
            uint64 Guid = R.ReadPackedGuid();
            EntityManager.Remove(Guid);
            EntitiesDestroyed++;
        }
        UE_LOG(LogWowPacket, Verbose, TEXT("OUT_OF_RANGE: %d objects removed"), Count);
        break;
    }

    case UpdateType::NEAR_OBJECTS:
    {
        uint32 Count = R.ReadU32();
        for (uint32 i = 0; i < Count && R.Remaining() > 0; ++i)
        {
            R.ReadPackedGuid(); // just acknowledge
        }
        break;
    }

    default:
        UE_LOG(LogWowPacket, Warning, TEXT("Unknown update block type: %d (remaining: %d bytes) - skipping rest of packet to avoid corruption"),
            Type, R.Remaining());

        // Unknown block type - we can't safely parse this packet any further
        // Skip to end of packet to avoid reading corrupted data
        R.Skip(R.Remaining());
        return;
    }
}

// ── Parse movement info for LIVING entities ──────────────────────────────────

void FWowPacketHandler::ParseMovementInfo(FPacketReader& R, FWowMovementInfo& Out)
{
    Out.MoveFlags = R.ReadU32();
    Out.MoveFlags2 = R.ReadU16();
    R.ReadU32(); // timestamp

    Out.Position.X = R.ReadFloat();
    Out.Position.Y = R.ReadFloat();
    Out.Position.Z = R.ReadFloat();
    Out.Orientation = R.ReadFloat();

    // Transport
    if (Out.MoveFlags & 0x00000200) // MOVEMENTFLAG_ONTRANSPORT
    {
        Out.TransportGuid = R.ReadPackedGuid();
        Out.TransportOffset.X = R.ReadFloat();
        Out.TransportOffset.Y = R.ReadFloat();
        Out.TransportOffset.Z = R.ReadFloat();
        Out.TransportOrientation = R.ReadFloat();
        R.ReadU32(); // transport time
        R.ReadU8();  // transport seat

        if (Out.MoveFlags2 & 0x0200) // MOVEMENTFLAG2_INTERP_MOVEMENT
        {
            R.ReadU32(); // transport time 2
        }
    }

    // Swimming / Flying
    if (Out.MoveFlags & (0x00200000 | 0x02000000)) // SWIMMING | FLYING
    {
        R.ReadFloat(); // pitch
    }

    Out.FallTime = R.ReadU32();

    // Falling
    if (Out.MoveFlags & 0x00001000) // FALLING
    {
        R.ReadFloat(); // jump velocity
        R.ReadFloat(); // jump sin
        R.ReadFloat(); // jump cos
        R.ReadFloat(); // jump xy speed
    }

    // Spline elevation
    if (Out.MoveFlags & 0x04000000) // SPLINE_ELEVATION
    {
        R.ReadFloat();
    }

    // Speeds (9 speeds for living entities)
    Out.WalkSpeed = R.ReadFloat();
    Out.RunSpeed = R.ReadFloat();
    Out.RunBackSpeed = R.ReadFloat();
    Out.SwimSpeed = R.ReadFloat();
    Out.SwimBackSpeed = R.ReadFloat();
    Out.FlightSpeed = R.ReadFloat();
    Out.FlightBackSpeed = R.ReadFloat();
    Out.TurnRate = R.ReadFloat();
    Out.PitchRate = R.ReadFloat();

    // Spline data
    if (Out.MoveFlags & 0x08000000) // SPLINE_ENABLED
    {
        // Skip spline data — variable length, complex
        uint32 SplineFlags = R.ReadU32();
        if (SplineFlags & 0x00020000) // SPLINEFLAG_FINAL_ANGLE
        {
            R.ReadFloat();
        }
        else if (SplineFlags & 0x00040000) // SPLINEFLAG_FINAL_TARGET
        {
            R.ReadU64();
        }
        else if (SplineFlags & 0x00080000) // SPLINEFLAG_FINAL_POINT
        {
            R.ReadFloat();
            R.ReadFloat();
            R.ReadFloat();
        }

        R.ReadU32(); // spline time passed
        R.ReadU32(); // spline duration
        R.ReadU32(); // spline id

        R.ReadFloat(); // spline duration multiplier
        R.ReadFloat(); // spline duration multiplier next

        R.ReadU32(); // spline vertical acceleration

        R.ReadU32(); // spline time started

        uint32 SplineCount = R.ReadU32();
        for (uint32 i = 0; i < SplineCount; ++i)
        {
            R.ReadFloat(); R.ReadFloat(); R.ReadFloat(); // point
        }

        R.ReadU8(); // spline mode

        R.ReadFloat(); R.ReadFloat(); R.ReadFloat(); // endpoint
    }
}

// ── Parse bitmask-based update fields ────────────────────────────────────────

void FWowPacketHandler::ParseUpdateFields(FPacketReader& R, FWowEntity& Entity)
{
    // The update fields block starts with a count of uint32 bitmask blocks
    uint8 BlockCount = R.ReadU8();

    if (BlockCount == 0) return;

    // Read the bitmask
    TArray<uint32> Mask;
    Mask.SetNum(BlockCount);
    for (int32 i = 0; i < BlockCount; ++i)
    {
        Mask[i] = R.ReadU32();
    }

    bool bInventoryFieldUpdated = false;
    bool bHealthFieldUpdated = false;
    bool bPowerFieldUpdated = false;

    // Each set bit in the mask means a uint32 value follows
    for (int32 Block = 0; Block < BlockCount; ++Block)
    {
        for (int32 Bit = 0; Bit < 32; ++Bit)
        {
            if (Mask[Block] & (1u << Bit))
            {
                uint16 FieldIndex = static_cast<uint16>(Block * 32 + Bit);
                uint32 Value = R.ReadU32();
                Entity.SetField(FieldIndex, Value);

                // Check if this is a player inventory field update
                if (Entity.IsPlayer() &&
                    ((FieldIndex >= PlayerField::INV_SLOT_HEAD && FieldIndex <= PlayerField::INV_SLOT_LAST + 1) ||
                     (FieldIndex >= PlayerField::PACK_SLOT_START && FieldIndex <= PlayerField::PACK_SLOT_END) ||
                     (FieldIndex >= PlayerField::BANK_SLOT_START && FieldIndex <= PlayerField::BANK_SLOT_END) ||
                     (FieldIndex >= PlayerField::BANKBAG_SLOT_1 && FieldIndex <= PlayerField::BANKBAG_SLOT_END) ||
                     FieldIndex == PlayerField::BYTES_2))
                {
                    bInventoryFieldUpdated = true;
                }

                // Check if this is a unit health field update
                if (Entity.IsUnit() && (FieldIndex == UnitField::HEALTH || FieldIndex == UnitField::MAXHEALTH))
                {
                    bHealthFieldUpdated = true;
                }

                // Check if this is a unit power field update (covers all 7 power types: mana, rage, focus, energy, happiness, runes, runic power)
                if (Entity.IsUnit() && ((FieldIndex >= UnitField::POWER1 && FieldIndex <= UnitField::POWER7) ||
                                        (FieldIndex >= UnitField::MAXPOWER1 && FieldIndex <= UnitField::MAXPOWER7)))
                {
                    bPowerFieldUpdated = true;
                }
            }
        }
    }

    if (Entity.Guid == EntityManager.LocalPlayerGuid) RefreshQuestLog();

    // Fire inventory update event if player inventory was updated
    if (bInventoryFieldUpdated)
    {
        OnPlayerInventoryUpdate.Broadcast();
    }

    // Fire health update event if unit health fields were updated
    if (bHealthFieldUpdated)
    {
        OnUnitHealthUpdate.Broadcast(Entity.Guid);
    }

    // Fire power update event if unit power fields were updated
    if (bPowerFieldUpdated)
    {
        OnUnitPowerUpdate.Broadcast(Entity.Guid);
    }
}

// ── SMSG_DESTROY_OBJECT ──────────────────────────────────────────────────────

void FWowPacketHandler::HandleDestroyObject(FPacketReader& R)
{
    uint64 Guid = R.ReadU64();
    uint8 OnDeath = R.ReadU8();

    UE_LOG(LogWowPacket, Log, TEXT("DESTROY_OBJECT: GUID=%llu onDeath=%d"), Guid, OnDeath);

    // onDeath=1 means the CORPSE is being removed (~60s after death), not the death itself.
    // Death animation is triggered by health→0 in OnEntityUpdated.
    // Always remove from entity manager — the gameplay controller's OnEntityDestroyed
    // will clean up the actor.
    Casts.Remove(Guid); EntityManager.Remove(Guid);
    EntitiesDestroyed++;
}

// ── Movement packets ─────────────────────────────────────────────────────────

void FWowPacketHandler::HandleMovement(FPacketReader& R)
{
    if(!R.CanRead(1)) return;
    const uint8 Mask=R.ReadU8(); int32 Count=0; for(int32 I=0;I<8;++I) if(Mask&(1<<I)) ++Count;
    if(!R.CanRead(Count)) return; uint64 Guid=0; for(int32 I=0;I<8;++I) if(Mask&(1<<I)) Guid|=uint64(R.ReadU8())<<(8*I);
    FWowEntity* Entity = EntityManager.Find(Guid);
    if (!Entity) return;
    FWowMovementInfo Info=Entity->Movement; uint32 Time=0;
    if(!ParseMovementPayload(R,Info,Time)) return;
    Entity->Movement = Info;
    EntityManager.OnEntityUpdated.Broadcast(*Entity);
}

// ── SMSG_MESSAGECHAT ─────────────────────────────────────────────────────────

void FWowPacketHandler::HandleMessageChat(FPacketReader& R)
{
    ParseMessageChat(R,false);
}
void FWowPacketHandler::HandleGmMessageChat(FPacketReader& R)
{
    ParseMessageChat(R,true);
}
void FWowPacketHandler::ParseMessageChat(FPacketReader& R,bool Gm)
{
    if(!R.CanRead(17)) return;
    uint8 Type = R.ReadU8();
    uint32 Language = R.ReadU32();
    uint64 SenderGuid = R.ReadU64();
    R.ReadU32(); // flags

    FString SenderName;
    const bool Monster=(Type>=12 && Type<=16) || Type==41 || Type==42 || Type==47;
    const bool Foreign=Type==8;
    const bool Battleground=Type==36 || Type==37 || Type==38;
    if(Monster || Foreign || (Gm && !Battleground && Type!=48 && Type!=49))
    {
        if(!R.CanRead(4)) return; const uint32 Length=R.ReadU32(); if(!Length || Length>uint32(R.Remaining())) return;
        SenderName=R.ReadCString();
    }
    // Channel name for channel messages
    FString Channel;
    if (Type == 17) // CHAT_MSG_CHANNEL
    {
        Channel = R.ReadCString();
    }

    uint64 TargetGuid = R.ReadU64();
    if((Monster && TargetGuid && (TargetGuid>>48)!=0 && (TargetGuid>>48)!=0xF140) || (Battleground && TargetGuid && (TargetGuid>>48)!=0))
    {
        if(!R.CanRead(4)) return; const uint32 Length=R.ReadU32(); if(!Length || Length>uint32(R.Remaining())) return; R.ReadCString();
    }
    if(!R.CanRead(4)) return;
    uint32 MsgLen = R.ReadU32();
    if(!MsgLen || MsgLen>uint32(R.Remaining()) || !R.CanRead(MsgLen+1) || R.Data[R.Pos+MsgLen-1]!=0) return;
    FString Message = R.ReadCString();
    uint8 ChatTag = R.ReadU8();

    // Get sender name from cache
    if (SenderName.IsEmpty() && SenderGuid != 0)
    {
        FString* CachedName = PlayerNameCache.Find(SenderGuid);
        if (CachedName)
        {
            SenderName = *CachedName;
        }
        else
        {
            SenderName = FString::Printf(TEXT("Player-%llu"), SenderGuid);
        }
    }

    UE_LOG(LogWowPacket, Log, TEXT("CHAT type=%d lang=%d sender=%s channel=%s: %s"),
           Type, Language, *SenderName, *Channel, *Message);
    OnChatMessage.Broadcast(Type, Language, SenderGuid, SenderName, Message, Channel);
}

// ── SMSG_INITIAL_SPELLS ──────────────────────────────────────────────────────

void FWowPacketHandler::HandleInitialSpells(FPacketReader& R)
{
    R.ReadU8(); // talent spec
    uint16 SpellCount = R.ReadU16();

    KnownSpells.Empty(SpellCount);
    SpellCooldowns.Reset(); SpellCooldownInfo.Reset();
    for (int32 i = 0; i < SpellCount; ++i)
    {
        uint32 SpellId = R.ReadU32();
        R.ReadU16(); // unknown (slot?)
        KnownSpells.Add(SpellId);
    }

    uint16 CooldownCount = R.ReadU16();
    for (int32 i = 0; i < CooldownCount; ++i)
    {
        uint32 SpellId = R.ReadU32();
        R.ReadU16(); // item ID
        R.ReadU16(); // spell category
        uint32 CooldownMs = R.ReadU32();
        CooldownMs=FMath::Max(CooldownMs,R.ReadU32()); // category cooldown

        if (CooldownMs > 0)
        {
            double ExpiryTime = FPlatformTime::Seconds() + CooldownMs / 1000.0;
            SpellCooldowns.Add(SpellId, ExpiryTime);
            SpellCooldownInfo.Add(SpellId,{ExpiryTime-CooldownMs/1000.0,CooldownMs/1000.f});
            OnSpellCooldown.Broadcast(SpellId, CooldownMs / 1000.0f);
        }
    }

    UE_LOG(LogWowPacket, Log, TEXT("INITIAL_SPELLS: %d spells stored, %d cooldowns"), SpellCount, CooldownCount);

    // Fire delegate with loaded spells
    OnInitialSpells.Broadcast(KnownSpells.Array());
}

// ── SMSG_ACTION_BUTTONS ──────────────────────────────────────────────────────

void FWowPacketHandler::HandleActionButtons(FPacketReader& R)
{
    // 3.3.5: first byte is packet type (0=initial, 1=update, 2=clear)
    if (R.Remaining() < 1) return;
    uint8 PacketType = R.ReadU8();
    UE_LOG(LogWowPacket, Log, TEXT("ACTION_BUTTONS packet type: %d"), PacketType);

    // 144 action buttons × 4 bytes each = 576 bytes
    int32 ButtonCount = FMath::Min(R.Remaining() / 4, 144);

    ActionButtons.Empty(ButtonCount);
    ActionButtons.AddZeroed(ButtonCount);

    int32 NonEmpty = 0;
    for (int32 i = 0; i < ButtonCount; ++i)
    {
        uint32 PackedAction = R.ReadU32();
        ActionButtons[i] = PackedAction;
        if (PackedAction != 0) NonEmpty++;
    }

    UE_LOG(LogWowPacket, Log, TEXT("ACTION_BUTTONS: %d/%d slots assigned"), NonEmpty, ButtonCount);

    // Debug: log first 12 slots (main action bar)
    for (int32 i = 0; i < FMath::Min(12, ButtonCount); ++i)
    {
        uint32 Data = ActionButtons[i];
        if (Data != 0)
        {
            uint8 Type = (Data >> 24) & 0xFF;
            uint32 Id = Data & 0x00FFFFFF;
            UE_LOG(LogWowPacket, Log, TEXT("  Slot %d: type=%d id=%d (raw=0x%08X)"), i+1, Type, Id, Data);
        }
    }

    // Fire delegate for action bar updates
    OnActionButtonsUpdated.Broadcast();
}

// ── SMSG_TIME_SYNC_REQ ──────────────────────────────────────────────────────

void FWowPacketHandler::HandleTimeSyncReq(FPacketReader& R)
{
    uint32 Counter = R.ReadU32();
    UE_LOG(LogWowPacket, Verbose, TEXT("TIME_SYNC_REQ: counter=%u"), Counter);

    // Respond with CMSG_TIME_SYNC_RESP: uint32 counter + uint32 clientTicks
    TArray<uint8> Resp;
    Resp.SetNumUninitialized(8);
    FMemory::Memcpy(Resp.GetData(), &Counter, 4);
    uint32 ClientTicks = uint32(uint64(FPlatformTime::Seconds()*1000.0)); // same millisecond clock as movement
    FMemory::Memcpy(Resp.GetData() + 4, &ClientTicks, 4);

    OnSendPacket.ExecuteIfBound(WowOpcode::CMSG_TIME_SYNC_RESP, Resp);
}

// ── SMSG_SPELL_COOLDOWN ──────────────────────────────────────────────────
// SMSG_SPELL_COOLDOWN format (3.3.5a):
// - uint64 CasterGuid
// - uint8 Flags (1 = ON_LOGIN)
// - Then repeated until end of packet:
//   - uint32 SpellId
//   - uint32 CooldownMs

void FWowPacketHandler::HandleSpellCooldown(FPacketReader& R)
{
    if (!R.CanRead(9)) // minimum: guid (8) + flags (1)
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SPELL_COOLDOWN: packet too short"));
        return;
    }

    uint64 CasterGuid = R.ReadU64();
    uint8 Flags = R.ReadU8();
    if(EntityManager.LocalPlayerGuid && CasterGuid!=EntityManager.LocalPlayerGuid) return;

    int32 CooldownsProcessed = 0;
    while (R.CanRead(8)) // spell id (4) + cooldown ms (4)
    {
        uint32 SpellId = R.ReadU32();
        uint32 CooldownMs = R.ReadU32();

        if (CooldownMs > 0)
        {
            double ExpiryTime = FPlatformTime::Seconds() + CooldownMs / 1000.0;
            SpellCooldowns.Add(SpellId, ExpiryTime);
            SpellCooldownInfo.Add(SpellId,{ExpiryTime-CooldownMs/1000.0,CooldownMs/1000.f});
            OnSpellCooldown.Broadcast(SpellId, CooldownMs / 1000.0f);
            CooldownsProcessed++;
        }
        else { SpellCooldowns.Remove(SpellId); SpellCooldownInfo.Remove(SpellId); }
    }

    UE_LOG(LogWowPacket, Log, TEXT("SPELL_COOLDOWN: caster=%llu flags=%u processed=%d cooldowns"),
        CasterGuid, Flags, CooldownsProcessed);
}

// ── SMSG_ATTACKERSTATEUPDATE ────────────────────────────────────────────
// uint32 hit_info, packed guid attacker, packed guid target, uint32 damage, uint8 damage_school_count
// for each school: uint32 damage, uint32 absorbed


void FWowPacketHandler::HandleAttackStart(FPacketReader& R)
{
    if(!R.CanRead(16) || R.Remaining()!=16) return;
    uint64 AttackerGuid = R.ReadU64();
    uint64 VictimGuid = R.ReadU64();
    UE_LOG(LogWowPacket, Log, TEXT("SMSG_ATTACKSTART: attacker=%llu victim=%llu"), AttackerGuid, VictimGuid);
    if(AttackerGuid==EntityManager.LocalPlayerGuid && HasPendingAttackCast) {HasPendingAttackCast=false; OnCastResult.ExecuteIfBound(AttackerGuid,6603,PendingAttackCounter,true);}
    OnAttackStart.Broadcast(AttackerGuid, VictimGuid);
}

void FWowPacketHandler::HandleAttackStop(FPacketReader& R)
{
    if(!R.CanRead(2)) return;
    uint64 AttackerGuid = R.ReadPackedGuid();
    uint64 VictimGuid = R.ReadPackedGuid();
    if(!R.CanRead(4)) return; R.ReadU32(); if(R.Remaining()) return;
    UE_LOG(LogWowPacket, Log, TEXT("SMSG_ATTACKSTOP: attacker=%llu victim=%llu"), AttackerGuid, VictimGuid);
    if(AttackerGuid==EntityManager.LocalPlayerGuid && HasPendingAttackCast) {HasPendingAttackCast=false; OnCastResult.ExecuteIfBound(AttackerGuid,6603,PendingAttackCounter,false);}
    OnAttackStop.Broadcast(AttackerGuid, VictimGuid);
}

// ── SMSG_AURA_UPDATE ────────────────────────────────────────────────────
// packed guid target, uint8 slot, [if slot != 0xFF: uint32 spell id, uint8 flags, uint8 level, uint8 charges, possible packed guid caster]

// ── SMSG_POWER_UPDATE ───────────────────────────────────────────────────
// packed guid target, uint8 power type, uint32 power value

void FWowPacketHandler::HandlePowerUpdate(FPacketReader& R)
{
    if (!R.CanRead(6)) // minimum: packed guid + power type + power value
    {
        UE_LOG(LogWowPacket, Warning, TEXT("POWER_UPDATE: packet too short"));
        return;
    }

    uint64 TargetGuid = R.ReadPackedGuid();
    uint8 PowerType = R.ReadU8();
    uint32 PowerValue = R.ReadU32();

    FWowEntity* Entity = EntityManager.Find(TargetGuid);
    if (Entity)
    {
        // Update power field in entity based on power type
        if (PowerType < 7) // Valid power types: 0=mana, 1=rage, 2=focus, 3=energy, 4=happiness, 5=runes, 6=runic power
        {
            // WoW power fields start at UNIT_FIELD_POWER1 + power_type
            uint16 PowerFieldIndex = UnitField::POWER1 + PowerType;
            Entity->SetField(PowerFieldIndex, PowerValue);
        }
    }

    UE_LOG(LogWowPacket, Log, TEXT("POWER_UPDATE: target=%llu type=%d value=%u"),
        TargetGuid, PowerType, PowerValue);
}

// ── SMSG_MONSTER_MOVE ───────────────────────────────────────────────────
// packed guid target, uint8 new_in_31, 3 floats position, uint32 timestamp, uint8 move_type,
// [if move_type == 0: uint32 spline_flags, uint32 duration, uint32 point_count, points...]

void FWowPacketHandler::HandleMonsterMove(FPacketReader& R)
{
    if (!R.CanRead(1)) return;
    const uint64 Guid = R.ReadPackedGuid();
    if (!R.CanRead(18)) return;
    R.ReadU8(); // movement flag extension, not a spline flag
    const float X = R.ReadFloat(), Y = R.ReadFloat(), Z = R.ReadFloat();
    const uint32 SplineId = R.ReadU32();
    const uint8 Type = R.ReadU8();
    FWowEntity* Entity = EntityManager.Find(Guid);
    if (!Entity || Type > 4) return;
    // Parse into a copy so a truncated update cannot destroy a valid route.
    FWowMovementInfo Move = Entity->Movement;
    Move.Position = Move.SplineStartPosition = FVector(X, Y, Z);
    Move.SplineId = SplineId;
    Move.SplineMoveType = Type;
    Move.SplineElapsed = 0;
    Move.bHasActiveSpline = false;
    Move.SplineWaypoints.Reset();
    Move.SplineFacingTarget = 0;
    if (Type != 1)
    {
        if (Type == 2)
        {
            if (!R.CanRead(12)) return;
            const float FX = R.ReadFloat(), FY = R.ReadFloat(), FZ = R.ReadFloat();
            Move.SplineFacingPoint = FVector(FX, FY, FZ);
        }
        else if (Type == 3)
        {
            if (!R.CanRead(8)) return;
            Move.SplineFacingTarget = R.ReadU64();
        }
        else if (Type == 4)
        {
            if (!R.CanRead(4)) return;
            Move.SplineFacingAngle = R.ReadFloat();
        }
        if (!R.CanRead(4)) return;
        Move.SplineFlags = R.ReadU32();
        if (Move.SplineFlags & 0x00200000) // animation transition before duration
        {
            if (!R.CanRead(5)) return;
            R.ReadU8(); R.ReadU32();
        }
        if (!R.CanRead(4)) return;
        Move.SplineDuration = R.ReadU32();
        if (Move.SplineFlags & 0x00000800) // parabolic parameters after duration
        {
            if (!R.CanRead(8)) return;
            R.ReadFloat(); R.ReadU32();
        }
        if (!R.CanRead(4)) return;
        const uint32 Count = R.ReadU32();
        if (Count == 0 || Count > 500 || Move.SplineDuration == 0) return;
        if (Move.SplineFlags & (0x00002000 | 0x00040000)) // flying / Catmull-Rom: full points
        {
            if (!R.CanRead(Count * 12)) return;
            for (uint32 I = 0; I < Count; ++I)
            {
                const float PX = R.ReadFloat(), PY = R.ReadFloat(), PZ = R.ReadFloat();
                Move.SplineWaypoints.Add(FWowSplineWaypoint(FVector(PX, PY, PZ)));
            }
            if ((Move.SplineFlags & 0x00080000) && Move.SplineWaypoints.Last().Position.IsNearlyZero())
                Move.SplineWaypoints.Pop(); // server's ground-cycle marker
        }
        else
        {
            if (!R.CanRead(12 + (Count - 1) * 4)) return;
            const float DX = R.ReadFloat(), DY = R.ReadFloat(), DZ = R.ReadFloat();
            const FVector Destination(DX, DY, DZ);
            const FVector Midpoint = (Move.SplineStartPosition + Destination) * 0.5;
            for (uint32 I = 1; I < Count; ++I)
            {
                const uint32 Packed = R.ReadU32();
                int32 PX = Packed & 0x7FF, PY = (Packed >> 11) & 0x7FF, PZ = (Packed >> 22) & 0x3FF;
                if (PX & 0x400) PX |= ~0x7FF;
                if (PY & 0x400) PY |= ~0x7FF;
                if (PZ & 0x200) PZ |= ~0x3FF;
                // Server encodes midpoint minus point, not point minus midpoint.
                Move.SplineWaypoints.Add(FWowSplineWaypoint(Midpoint - FVector(PX, PY, PZ) * 0.25));
            }
            Move.SplineWaypoints.Add(FWowSplineWaypoint(Destination));
        }
        if ((Move.SplineFlags & 0x00080000) && !Move.SplineWaypoints.IsEmpty()
            && !Move.SplineWaypoints.Last().Position.Equals(Move.SplineStartPosition))
            Move.SplineWaypoints.Add(FWowSplineWaypoint(Move.SplineStartPosition));
        Move.bHasActiveSpline = !Move.SplineWaypoints.IsEmpty();
    }
    Entity->Movement = Move;
    UE_LOG(LogWowPacket, Verbose, TEXT("MONSTER_MOVE guid=%llu type=%u flags=0x%X duration=%u points=%d speed=%.2f"),
        Guid, Type, Move.SplineFlags, Move.SplineDuration, Move.SplineWaypoints.Num(), Move.GetSplineSpeed());
    EntityManager.OnEntityUpdated.Broadcast(*Entity);
}
// ── SMSG_INVENTORY_CHANGE_FAILURE ─────────────────────────────────────────
// uint8 error code

void FWowPacketHandler::HandleInventoryChangeFailure(FPacketReader& R)
{
    if (!R.CanRead(1)) return;

    uint8 ErrorCode = R.ReadU8();

    const TCHAR* ErrorMsg = TEXT("Unknown error");
    switch (ErrorCode)
    {
    case 0:  ErrorMsg = TEXT("CANT_EQUIP_LEVEL_I"); break;
    case 1:  ErrorMsg = TEXT("CANT_EQUIP_SKILL"); break;
    case 2:  ErrorMsg = TEXT("ITEM_DOESNT_GO_TO_SLOT"); break;
    case 3:  ErrorMsg = TEXT("BAG_FULL"); break;
    case 4:  ErrorMsg = TEXT("NONEMPTY_BAG_OVER_OTHER_BAG"); break;
    case 5:  ErrorMsg = TEXT("CANT_TRADE_EQUIP_BAGS"); break;
    case 6:  ErrorMsg = TEXT("ONLY_AMMO_CAN_GO_HERE"); break;
    case 7:  ErrorMsg = TEXT("NO_REQUIRED_PROFICIENCY"); break;
    case 8:  ErrorMsg = TEXT("NO_EQUIPMENT_SLOT_AVAILABLE"); break;
    case 9:  ErrorMsg = TEXT("YOU_CAN_NEVER_USE_THAT_ITEM"); break;
    case 10: ErrorMsg = TEXT("YOU_CAN_NEVER_USE_THAT_ITEM2"); break;
    case 11: ErrorMsg = TEXT("NO_EQUIPMENT_SLOT_AVAILABLE2"); break;
    case 12: ErrorMsg = TEXT("CANT_EQUIP_WITH_TWOHANDED"); break;
    case 13: ErrorMsg = TEXT("CANT_DUAL_WIELD"); break;
    case 14: ErrorMsg = TEXT("ITEM_DOESNT_GO_INTO_BAG"); break;
    case 15: ErrorMsg = TEXT("ITEM_DOESNT_GO_INTO_BAG2"); break;
    case 16: ErrorMsg = TEXT("CANT_CARRY_MORE_OF_THIS"); break;
    case 17: ErrorMsg = TEXT("NO_EQUIPMENT_SLOT_AVAILABLE3"); break;
    case 18: ErrorMsg = TEXT("ITEM_CANT_STACK"); break;
    case 19: ErrorMsg = TEXT("ITEM_CANT_BE_EQUIPPED"); break;
    case 20: ErrorMsg = TEXT("ITEMS_CANT_BE_SWAPPED"); break;
    case 21: ErrorMsg = TEXT("SLOT_IS_EMPTY"); break;
    case 22: ErrorMsg = TEXT("ITEM_NOT_FOUND"); break;
    case 23: ErrorMsg = TEXT("CANT_DROP_SOULBOUND"); break;
    case 24: ErrorMsg = TEXT("OUT_OF_RANGE"); break;
    case 25: ErrorMsg = TEXT("TRIED_TO_SPLIT_MORE_THAN_COUNT"); break;
    case 26: ErrorMsg = TEXT("COULDNT_SPLIT_ITEMS"); break;
    case 27: ErrorMsg = TEXT("MISSING_REAGENT"); break;
    case 28: ErrorMsg = TEXT("NOT_ENOUGH_MONEY"); break;
    case 29: ErrorMsg = TEXT("NOT_A_BAG"); break;
    case 30: ErrorMsg = TEXT("CAN_ONLY_DO_WITH_EMPTY_BAGS"); break;
    case 31: ErrorMsg = TEXT("DONT_OWN_THAT_ITEM"); break;
    case 32: ErrorMsg = TEXT("CAN_EQUIP_ONLY1_QUIVER"); break;
    case 33: ErrorMsg = TEXT("MUST_PURCHASE_THAT_BAG_SLOT"); break;
    case 34: ErrorMsg = TEXT("TOO_FAR_AWAY_FROM_BANK"); break;
    case 35: ErrorMsg = TEXT("ITEM_LOCKED"); break;
    case 36: ErrorMsg = TEXT("YOU_ARE_STUNNED"); break;
    case 37: ErrorMsg = TEXT("YOU_ARE_DEAD"); break;
    case 38: ErrorMsg = TEXT("CANT_DO_RIGHT_NOW"); break;
    case 39: ErrorMsg = TEXT("INT_BAG_ERROR"); break;
    case 40: ErrorMsg = TEXT("CAN_EQUIP_ONLY1_BOLT"); break;
    case 41: ErrorMsg = TEXT("CAN_EQUIP_ONLY1_AMMOPOUCH"); break;
    case 42: ErrorMsg = TEXT("STACKABLE_CANT_BE_WRAPPED"); break;
    case 43: ErrorMsg = TEXT("EQUIPPED_CANT_BE_WRAPPED"); break;
    case 44: ErrorMsg = TEXT("WRAPPED_CANT_BE_WRAPPED"); break;
    case 45: ErrorMsg = TEXT("BOUND_CANT_BE_WRAPPED"); break;
    case 46: ErrorMsg = TEXT("UNIQUE_CANT_BE_WRAPPED"); break;
    case 47: ErrorMsg = TEXT("BAGS_CANT_BE_WRAPPED"); break;
    case 48: ErrorMsg = TEXT("ALREADY_LOOTED"); break;
    case 49: ErrorMsg = TEXT("INVENTORY_FULL"); break;
    case 50: ErrorMsg = TEXT("BANK_FULL"); break;
    case 51: ErrorMsg = TEXT("ITEM_IS_CURRENTLY_SOLD_OUT"); break;
    case 52: ErrorMsg = TEXT("BAG_FULL3"); break;
    case 53: ErrorMsg = TEXT("ITEM_NOT_FOUND2"); break;
    case 54: ErrorMsg = TEXT("ITEM_CANT_STACK2"); break;
    case 55: ErrorMsg = TEXT("BAG_FULL4"); break;
    case 56: ErrorMsg = TEXT("ITEM_SOLD_OUT"); break;
    case 57: ErrorMsg = TEXT("OBJECT_IS_BUSY"); break;
    case 58: ErrorMsg = TEXT("NONE"); break;
    case 59: ErrorMsg = TEXT("NOT_IN_COMBAT"); break;
    case 60: ErrorMsg = TEXT("NOT_WHILE_DISARMED"); break;
    case 61: ErrorMsg = TEXT("BAG_FULL6"); break;
    case 62: ErrorMsg = TEXT("CANT_EQUIP_RANK"); break;
    case 63: ErrorMsg = TEXT("CANT_EQUIP_REPUTATION"); break;
    case 64: ErrorMsg = TEXT("TOO_MANY_SPECIAL_BAGS"); break;
    case 65: ErrorMsg = TEXT("LOOT_CANT_LOOT_THAT_NOW"); break;
    }

    UE_LOG(LogWowPacket, Warning, TEXT("INVENTORY_CHANGE_FAILURE: error=%d (%s)"), ErrorCode, ErrorMsg);
}

// ── SMSG_LIST_INVENTORY ──────────────────────────────────────────────────────
// full uint64 vendor_guid, uint8 item_count, then eight uint32 fields per item.

void FWowPacketHandler::HandleListInventory(FPacketReader& R)
{
    if (!R.CanRead(9)) return;
    uint64 VendorGuid = R.ReadU64();
    uint8 ItemCount = R.ReadU8();
    if(!VendorGuid || !R.CanRead(ItemCount*32)) return;

    TArray<FWowVendorItem> Items;
    Items.Reserve(ItemCount);

    for (int32 i = 0; i < ItemCount; ++i)
    {
        if (!R.CanRead(32)) break; // slot + itemid + displayid + maxcount + price + maxdurability + buycount + extendedcost

        FWowVendorItem Item;
        Item.Slot = R.ReadU32();
        Item.ItemId = R.ReadU32();
        Item.DisplayId = R.ReadU32();
        Item.MaxCount = R.ReadU32();
        Item.Price = R.ReadU32();
        Item.MaxDurability = R.ReadU32();
        Item.BuyCount = R.ReadU32();
        Item.ExtendedCost = R.ReadU32();

        Items.Add(Item);
    }

    UE_LOG(LogWowPacket, Log, TEXT("LIST_INVENTORY: vendor=%llu items=%d"), VendorGuid, ItemCount);
    Merchant.Guid=VendorGuid; Merchant.Items=MoveTemp(Items); Merchant.Open=true; ++Merchant.Revision;
    OnVendorOpened.Broadcast(VendorGuid, Merchant.Items);
}

// ── ──────────────────────────────────────────────────────────────────────────
// ── Social / Guild / Friends System Handlers ────────────────────────────────
// ── ──────────────────────────────────────────────────────────────────────────

// ── SMSG_FRIEND_LIST ─────────────────────────────────────────────────────────
// uint8 count, for each friend: uint64 guid, uint8 status, if online: uint32 areaId, uint8 level, uint8 class

void FWowPacketHandler::HandleFriendList(FPacketReader& R)
{
    if (!R.CanRead(1)) return;

    uint8 Count = R.ReadU8();
    FriendsList.Empty(Count);

    UE_LOG(LogWowPacket, Log, TEXT("FRIEND_LIST: %d friends"), Count);

    for (int32 i = 0; i < Count; ++i)
    {
        if (!R.CanRead(9)) break; // minimum: guid + status

        FWowFriendInfo Friend;
        Friend.Guid = R.ReadU64();
        Friend.Status = R.ReadU8();

        // If friend is online, read additional data
        if (Friend.Status > 0) // online, AFK, DND
        {
            if (R.CanRead(6)) // areaId + level + class
            {
                Friend.AreaId = R.ReadU32();
                Friend.Level = R.ReadU8();
                Friend.Class = R.ReadU8();
            }
        }

        FriendsList.Add(Friend);

        UE_LOG(LogWowPacket, Verbose, TEXT("  Friend GUID=%llu status=%d level=%d"),
               Friend.Guid, Friend.Status, Friend.Level);
    }

    OnFriendListUpdated.Broadcast();
}

// ── SMSG_FRIEND_STATUS ──────────────────────────────────────────────────────
// uint8 resultType, uint64 guid, FString name (null-terminated)

void FWowPacketHandler::HandleFriendStatus(FPacketReader& R)
{
    if (!R.CanRead(9)) return; // result type + guid

    uint8 ResultType = R.ReadU8();
    uint64 Guid = R.ReadU64();
    FString Name = R.ReadCString();

    const TCHAR* StatusMsg = TEXT("Unknown");
    switch (ResultType)
    {
    case 0: StatusMsg = TEXT("offline"); break;
    case 1: StatusMsg = TEXT("online"); break;
    case 2: StatusMsg = TEXT("AFK"); break;
    case 3: StatusMsg = TEXT("DND"); break;
    }

    UE_LOG(LogWowPacket, Log, TEXT("FRIEND_STATUS: %s is now %s"), *Name, StatusMsg);
}

// ── SMSG_GUILD_ROSTER ───────────────────────────────────────────────────────
// uint32 memberCount, FString motd, FString guildInfo, uint32 rankCount,
// for each rank: uint32 rights + uint32 goldLimit
// for each member: uint64 guid, uint8 status, FString name, uint32 rankId, uint8 level, uint8 class, uint8 gender, uint32 zoneId, FString publicNote, FString officerNote

void FWowPacketHandler::HandleGuildRoster(FPacketReader& R)
{
    if(!R.CanRead(4)) return;
    const uint32 Members=R.ReadU32(); if(Members>5000) return;
    const auto ReadText=[&](FString& Out) { const int32 Start=R.Pos; Out=R.ReadCString(); return R.Pos>Start && R.Data[R.Pos-1]==0; };
    FString Motd,Info; if(!ReadText(Motd) || !ReadText(Info) || !R.CanRead(4)) return;
    const uint32 RankCount=R.ReadU32(); if(RankCount>10 || !R.CanRead(RankCount*56)) return;
    TArray<FWowGuildRankAccess> Ranks;
    for(uint32 I=0;I<RankCount;++I)
    {
        FWowGuildRankAccess Rank; Rank.Rights=R.ReadU32(); Rank.GoldPerDay=R.ReadU32();
        for(int32 Tab=0;Tab<6;++Tab) { Rank.TabRights[Tab]=R.ReadU32(); Rank.TabSlots[Tab]=R.ReadU32(); }
        Ranks.Add(Rank);
    }
    TArray<FWowGuildMember> Rows;
    for(uint32 I=0;I<Members;++I)
    {
        if(!R.CanRead(9)) return;
        FWowGuildMember Member; Member.Guid=R.ReadU64(); Member.Status=R.ReadU8();
        if(!ReadText(Member.Name) || !R.CanRead(11)) return;
        const uint32 Rank=R.ReadU32(); if(Rank>=RankCount) return;
        Member.RankId=uint8(Rank); Member.Level=R.ReadU8(); Member.Class=R.ReadU8(); Member.Gender=R.ReadU8(); Member.ZoneId=R.ReadU32();
        if(!Member.Status) { if(!R.CanRead(4)) return; Member.DaysOffline=R.ReadFloat(); }
        if(!ReadText(Member.PublicNote) || !ReadText(Member.OfficerNote)) return;
        Rows.Add(MoveTemp(Member));
    }
    GuildRoster=MoveTemp(Rows); GuildRankAccess=MoveTemp(Ranks); GuildMotd=MoveTemp(Motd); GuildInfoText=MoveTemp(Info);
    GuildCanRepair=false;
    for(const auto& Member:GuildRoster) if(Member.Guid==EntityManager.LocalPlayerGuid && GuildRankAccess.IsValidIndex(Member.RankId)) GuildCanRepair=Member.RankId==0 || (GuildRankAccess[Member.RankId].Rights&0x00040000)!=0;
    OnGuildRosterUpdated.Broadcast(); if(Merchant.Open) OnVendorUpdated.Broadcast();
}
// ── SMSG_GUILD_EVENT ────────────────────────────────────────────────────────
// uint8 eventType, uint8 stringCount, then read each string

void FWowPacketHandler::HandleGuildEvent(FPacketReader& R)
{
    if (!R.CanRead(2)) return;

    uint8 EventType = R.ReadU8();
    uint8 StringCount = R.ReadU8();

    TArray<FString> Strings;
    Strings.Reserve(StringCount);

    for (int32 i = 0; i < StringCount; ++i)
    {
        FString Str = R.ReadCString();
        Strings.Add(Str);
    }

    UE_LOG(LogWowPacket, Log, TEXT("GUILD_EVENT: type=%d strings=%d"), EventType, StringCount);
    for (int32 i = 0; i < Strings.Num(); ++i)
    {
        UE_LOG(LogWowPacket, Verbose, TEXT("  String[%d]: %s"), i, *Strings[i]);
    }
}

// ── SMSG_CHANNEL_NOTIFY ─────────────────────────────────────────────────────
// uint8 type, FString channelName

// ── SMSG_GROUP_LIST ─────────────────────────────────────────────────────────
// uint8 groupFlags, uint8 selfSubgroup, uint8 selfFlags, uint8 selfRoles,
// if LFG: uint8 completedFlag, uint32 dungeonId,
// uint64 groupGuid, uint32 counter, uint32 otherMemberCount,
// repeated otherMemberCount: CString name, uint64 guid, uint8 online, uint8 subgroup, uint8 flags, uint8 roles,
// uint64 leaderGuid, optional loot data

void FWowPacketHandler::HandleGroupList(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    const uint8 RawGroupType = R.ReadU8();
    const uint8 SelfSubgroup = R.ReadU8();
    const uint8 SelfFlags = R.ReadU8();
    const uint8 SelfRoles = R.ReadU8();

    if ((RawGroupType & WowGroupType::LFG) != 0)
    {
        if (!R.CanRead(5)) return;
        R.Skip(1); // completed dungeon flag
        R.Skip(4); // dungeon id
    }

    if (!R.CanRead(16)) return;
    const uint64 GroupGuid = R.ReadU64();
    const uint32 Counter = R.ReadU32();
    const uint32 OtherMemberCount = R.ReadU32();

    const uint64 PreviousGroupGuid = GroupInfo.GroupGuid;
    const bool bHadRaidTargets = RaidTargets.HasAnyIcons();
    const bool bHadReadyCheck = ReadyCheck.bActive;

    GroupInfo.Clear();
    GroupInfo.RawGroupType = RawGroupType;
    GroupInfo.GroupType = (RawGroupType & WowGroupType::RAID) != 0 ? 1 : 0;
    GroupInfo.GroupGuid = GroupGuid;
    GroupInfo.SelfSubgroup = SelfSubgroup;
    GroupInfo.SelfFlags = SelfFlags;
    GroupInfo.SelfRoles = SelfRoles;
    GroupInfo.Members.Reserve(OtherMemberCount);

    for (uint32 i = 0; i < OtherMemberCount; ++i)
    {
        const FString MemberName = R.ReadCString();

        if (!R.CanRead(12)) break; // guid + online + subgroup + flags + roles
        const uint64 MemberGuid = R.ReadU64();
        const uint8 Online = R.ReadU8();
        const uint8 MemberSubgroup = R.ReadU8();
        const uint8 MemberFlags = R.ReadU8();
        const uint8 MemberRoles = R.ReadU8();

        FWowGroupMember Member;
        Member.Guid = MemberGuid;
        Member.Name = MemberName;
        Member.Group = MemberSubgroup;
        Member.Flags = MemberFlags;
        Member.Roles = MemberRoles;
        Member.Status = Online;

        // Try to get level and class from entity data
        if (auto* Entity = EntityManager.Find(MemberGuid))
        {
            if (Entity->IsUnit())
            {
                Member.Level = static_cast<uint8>(Entity->GetLevel());
                Member.Class = static_cast<uint8>(Entity->GetFieldByte(UnitField::BYTES_0, 1));
            }
        }

        GroupInfo.Members.Add(Member);

        UE_LOG(LogWowPacket, Verbose, TEXT("  Member: %s (GUID=%llu online=%d level=%d class=%d)"),
               *MemberName, MemberGuid, Online, Member.Level, Member.Class);
    }

    if (!R.CanRead(8)) return;
    GroupInfo.LeaderGuid = R.ReadU64();

    if (OtherMemberCount > 0 && R.CanRead(12))
    {
        R.Skip(1); // loot method
        R.Skip(8); // master looter guid
        R.Skip(1); // loot threshold
        R.Skip(1); // dungeon difficulty
        R.Skip(1); // raid difficulty
        if (R.CanRead(1))
        {
            R.Skip(1); // heroic raid difficulty toggle
        }
    }

    const bool bHasGroup = (GroupInfo.LeaderGuid != 0) || (OtherMemberCount > 0);
    GroupInfo.MemberCount = bHasGroup ? static_cast<uint8>(FMath::Min<uint32>(OtherMemberCount + 1, MAX_uint8)) : 0;

    if (!bHasGroup)
    {
        GroupInfo.Clear();
    }

    const bool bGroupChanged = PreviousGroupGuid != 0 && PreviousGroupGuid != GroupInfo.GroupGuid;

    if (!GroupInfo.IsRaidGroup() || bGroupChanged)
    {
        RaidTargets.Clear();
        ReadyCheck.Clear();
    }

    UE_LOG(LogWowPacket, Log, TEXT("GROUP_LIST: rawType=0x%02X normalizedType=%d members=%d leader=%llu groupGuid=%llu counter=%u"),
        RawGroupType,
        GroupInfo.GroupType,
        GroupInfo.MemberCount,
        GroupInfo.LeaderGuid,
        GroupGuid,
        Counter);

    OnGroupUpdated.Broadcast();

    if ((bHadRaidTargets && !RaidTargets.HasAnyIcons()) || (bHadReadyCheck && !ReadyCheck.bActive))
    {
        if (bHadRaidTargets)
        {
            OnRaidTargetsUpdated.Broadcast(RaidTargets);
        }
        if (bHadReadyCheck)
        {
            OnReadyCheckUpdated.Broadcast(ReadyCheck);
        }
    }
}

// ── MSG_RAID_TARGET_UPDATE ─────────────────────────────────────────────────
// uint8 mode
// mode 1: repeated uint8 icon, uint64 targetGuid
// mode 0: uint64 setterGuid, uint8 icon, uint64 targetGuid

void FWowPacketHandler::HandleRaidTargetUpdate(FPacketReader& R)
{
    if (!R.CanRead(1)) return;

    const uint8 Mode = R.ReadU8();
    if (Mode == 1)
    {
        RaidTargets.Clear();
        while (R.CanRead(9))
        {
            const uint8 IconIndex = R.ReadU8();
            const uint64 TargetGuid = R.ReadU64();
            RaidTargets.SetTarget(IconIndex, TargetGuid);
        }

        UE_LOG(LogWowPacket, Log, TEXT("RAID_TARGET_UPDATE: received icon list"));
    }
    else
    {
        if (!R.CanRead(17)) return;

        const uint64 SetterGuid = R.ReadU64();
        const uint8 IconIndex = R.ReadU8();
        const uint64 TargetGuid = R.ReadU64();

        RaidTargets.SetTarget(IconIndex, TargetGuid);
        UE_LOG(LogWowPacket, Log, TEXT("RAID_TARGET_UPDATE: setter=%llu icon=%d target=%llu"), SetterGuid, IconIndex, TargetGuid);
    }

    OnRaidTargetsUpdated.Broadcast(RaidTargets);
}

// ── MSG_RAID_READY_CHECK ───────────────────────────────────────────────────
// uint64 initiatorGuid

void FWowPacketHandler::HandleRaidReadyCheck(FPacketReader& R)
{
    if (!R.CanRead(8)) return;

    const uint64 InitiatorGuid = R.ReadU64();
    ReadyCheck.Begin(InitiatorGuid);
    UE_LOG(LogWowPacket, Log, TEXT("RAID_READY_CHECK: initiator=%llu"), InitiatorGuid);
    OnReadyCheckUpdated.Broadcast(ReadyCheck);
}

// ── MSG_RAID_READY_CHECK_CONFIRM ───────────────────────────────────────────
// uint64 responderGuid, uint8 state

void FWowPacketHandler::HandleRaidReadyCheckConfirm(FPacketReader& R)
{
    if (!R.CanRead(9)) return;

    const uint64 ResponderGuid = R.ReadU64();
    const uint8 ResponseState = R.ReadU8();

    if (!ReadyCheck.bActive)
    {
        ReadyCheck.Begin(0);
    }

    ReadyCheck.SetResponse(ResponderGuid, ResponseState);
    UE_LOG(LogWowPacket, Log, TEXT("RAID_READY_CHECK_CONFIRM: responder=%llu state=%d"), ResponderGuid, ResponseState);
    OnReadyCheckUpdated.Broadcast(ReadyCheck);
}

// ── MSG_RAID_READY_CHECK_FINISHED ──────────────────────────────────────────

void FWowPacketHandler::HandleRaidReadyCheckFinished(FPacketReader& R)
{
    static_cast<void>(R);
    ReadyCheck.Clear();
    UE_LOG(LogWowPacket, Log, TEXT("RAID_READY_CHECK_FINISHED"));
    OnReadyCheckUpdated.Broadcast(ReadyCheck);
}

// ── SMSG_PARTY_COMMAND_RESULT ──────────────────────────────────────────────
// uint32 command, FString member, uint32 result

void FWowPacketHandler::HandlePartyCommandResult(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    uint32 Command = R.ReadU32();
    FString Member = R.ReadCString();

    if (!R.CanRead(4)) return;
    uint32 Result = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("PARTY_COMMAND_RESULT: command=%d member='%s' result=%d"),
           Command, *Member, Result);

    OnPartyCommandResult.Broadcast(static_cast<uint8>(Command), Member, static_cast<uint8>(Result));
}

// ── SMSG_GROUP_INVITE ───────────────────────────────────────────────────────
// FString inviterName

void FWowPacketHandler::HandleGroupInvite(FPacketReader& R)
{
    FString InviterName = R.ReadCString();
    UE_LOG(LogWowPacket, Log, TEXT("GROUP_INVITE from '%s'"), *InviterName);

    OnGroupInviteReceived.Broadcast(InviterName);
}

// ── SMSG_SHOWTAXINODES ──────────────────────────────────────────────────────
// uint32 windowFlags, uint64 npcGuid, uint32 currentNodeId, variable-length knownNodes bitmask

void FWowPacketHandler::HandleShowTaxiNodes(FPacketReader& R)
{
    if (!R.CanRead(16)) return; // windowFlags + npcGuid + currentNodeId

    uint32 WindowFlags = R.ReadU32();
    uint64 NpcGuid = R.ReadU64();
    uint32 CurrentNodeId = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("SHOWTAXINODES: npc=%llu current=%d flags=0x%X"),
           NpcGuid, CurrentNodeId, WindowFlags);

    // Clear and update taxi data
    TaxiData.Clear();
    TaxiData.NpcGuid = NpcGuid;
    TaxiData.CurrentNodeId = CurrentNodeId;
    TaxiData.bTaxiWindowOpen = true;

    // Load taxi nodes from DBC if not already loaded
    if (TaxiData.AllNodes.IsEmpty())
    {
        if (FDbcStore::Get().IsLoaded())
        {
            const auto& TaxiNodesDbc = FDbcStore::Get().TaxiNodes();
            for (const auto& DbcNode : TaxiNodesDbc.GetAll())
            {
                FWowTaxiNode Node(DbcNode.ID, DbcNode.MapID, DbcNode.X, DbcNode.Y, DbcNode.Z, DbcNode.Name);
                TaxiData.AllNodes.Add(Node);
            }
            UE_LOG(LogWowPacket, Log, TEXT("Loaded %d taxi nodes from DBC"), TaxiData.AllNodes.Num());
        }
    }

    // Read known nodes bitmask (variable length)
    // Each byte contains 8 node flags, calculate how many bytes we need
    int32 MaxNodeId = 256;
    if (!TaxiData.AllNodes.IsEmpty())
    {
        MaxNodeId = 0;
        for (const auto& Node : TaxiData.AllNodes)
        {
            MaxNodeId = FMath::Max(MaxNodeId, (int32)Node.Id);
        }
    }
    const int32 BitmaskBytes = (MaxNodeId + 7) / 8;

    TaxiData.KnownNodes.SetNum(MaxNodeId + 1);
    TaxiData.KnownNodes.Init(false, MaxNodeId + 1);

    for (int32 ByteIndex = 0; ByteIndex < BitmaskBytes && R.CanRead(1); ++ByteIndex)
    {
        uint8 Byte = R.ReadU8();
        for (int32 BitIndex = 0; BitIndex < 8; ++BitIndex)
        {
            const int32 NodeId = ByteIndex * 8 + BitIndex;
            if (NodeId < TaxiData.KnownNodes.Num() && (Byte & (1 << BitIndex)))
            {
                TaxiData.KnownNodes[NodeId] = true;
            }
        }
    }

    int32 KnownCount = 0;
    for (bool bKnown : TaxiData.KnownNodes)
    {
        if (bKnown) KnownCount++;
    }
    UE_LOG(LogWowPacket, Log, TEXT("Player knows %d taxi nodes"), KnownCount);

    OnTaxiNodesShown.Broadcast(NpcGuid, CurrentNodeId);
}

// ── SMSG_ACTIVATETAXIREPLY ──────────────────────────────────────────────────
// uint32 result (0=OK, 1=unspecified error, etc.)

void FWowPacketHandler::HandleActivateTaxiReply(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    uint32 Result = R.ReadU32();
    UE_LOG(LogWowPacket, Log, TEXT("ACTIVATETAXIREPLY: result=%d"), Result);

    OnTaxiActivateReply.Broadcast(static_cast<uint8>(Result));

    // Close taxi window
    TaxiData.bTaxiWindowOpen = false;
}

// ── SMSG_NEW_TAXI_PATH ──────────────────────────────────────────────────────
// No payload, just refresh taxi status

void FWowPacketHandler::HandleNewTaxiPath(FPacketReader& R)
{
    UE_LOG(LogWowPacket, Log, TEXT("NEW_TAXI_PATH: refreshing taxi data"));

    // This packet indicates that the player's taxi path has been updated
    // The client should refresh its taxi information
    TaxiData.bTaxiWindowOpen = false;
}

// ── SMSG_WHO ────────────────────────────────────────────────────────────────
// uint32 displayCount, uint32 matchCount, for each: FString name, FString guildName,
// uint32 level, uint32 classId, uint32 raceId, uint32 zoneId

void FWowPacketHandler::HandleWho(FPacketReader& R)
{
    if (!R.CanRead(8)) return;

    uint32 DisplayCount = R.ReadU32();
    uint32 MatchCount = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("WHO: displaying %d of %d matches"), DisplayCount, MatchCount);

    for (uint32 i = 0; i < DisplayCount; ++i)
    {
        FString Name = R.ReadCString();
        FString PlayerGuild = R.ReadCString();

        if (!R.CanRead(16)) break; // level + class + race + zone
        uint32 Level = R.ReadU32();
        uint32 ClassId = R.ReadU32();
        uint32 RaceId = R.ReadU32();
        uint32 ZoneId = R.ReadU32();

        UE_LOG(LogWowPacket, Verbose, TEXT("  %s <%s> level %d class=%d race=%d zone=%d"),
               *Name, *PlayerGuild, Level, ClassId, RaceId, ZoneId);
    }
}

// ── SMSG_QUESTGIVER_STATUS ───────────────────────────────────────────────
// uint64 guid, uint8 status

void FWowPacketHandler::HandleQuestgiverStatus(FPacketReader& R)
{
    if (!R.CanRead(9)) return;

    uint64 Guid = R.ReadU64();
    uint8 Status = R.ReadU8();

    const TCHAR* StatusMsg = TEXT("Unknown");
    switch (Status)
    {
    case 0: StatusMsg = TEXT("None"); break;
    case 1: StatusMsg = TEXT("Unavailable"); break;
    case 2: StatusMsg = TEXT("Chat"); break;
    case 3: StatusMsg = TEXT("Incomplete"); break;
    case 4: StatusMsg = TEXT("Reward Rep"); break;
    case 5: StatusMsg = TEXT("Available Rep"); break;
    case 6: StatusMsg = TEXT("Available"); break;
    case 7: StatusMsg = TEXT("Reward2"); break;
    case 8: StatusMsg = TEXT("Reward"); break;
    }

    UE_LOG(LogWowPacket, Log, TEXT("QUESTGIVER_STATUS: guid=%llu status=%d (%s)"), Guid, Status, StatusMsg);
}

// ── SMSG_QUEST_UPDATE_ADD_KILL ───────────────────────────────────────────
// uint32 questId, uint32 entry, uint32 count, uint32 required, uint64 guid

void FWowPacketHandler::HandleQuestUpdateAddKill(FPacketReader& R)
{
    if (!R.CanRead(24)) return;

    uint32 QuestId = R.ReadU32();
    uint32 Entry = R.ReadU32();
    uint32 Count = R.ReadU32();
    uint32 Required = R.ReadU32();
    uint64 Guid = R.ReadU64();

    // Update quest log
    for (FWowQuestLogEntry& Quest : QuestLog)
    {
        if (Quest.QuestId == QuestId)
        {
            bool bFoundObjective = false;
            for (FWowQuestObjective& Obj : Quest.Objectives)
            {
                if (Obj.CreatureOrGOId == Entry)
                {
                    Obj.Count = Count;
                    Obj.Required = Required;
                    bFoundObjective = true;
                    break;
                }
            }

            if (!bFoundObjective)
            {
                FWowQuestObjective NewObj;
                NewObj.CreatureOrGOId = Entry;
                NewObj.Count = Count;
                NewObj.Required = Required;
                Quest.Objectives.Add(NewObj);
            }
            break;
        }
    }

    UE_LOG(LogWowPacket, Log, TEXT("QUEST_UPDATE_ADD_KILL: quest=%u entry=%u count=%u/%u"),
           QuestId, Entry, Count, Required);
    OnQuestLogChanged.Broadcast();
}

// ── SMSG_QUEST_UPDATE_COMPLETE ───────────────────────────────────────────
// uint32 questId

void FWowPacketHandler::HandleQuestUpdateComplete(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    uint32 QuestId = R.ReadU32();

    // Update quest log
    for (FWowQuestLogEntry& Quest : QuestLog)
    {
        if (Quest.QuestId == QuestId)
        {
            Quest.State = 1; // Complete
            break;
        }
    }

    UE_LOG(LogWowPacket, Log, TEXT("QUEST_UPDATE_COMPLETE: quest=%u"), QuestId);
    OnQuestComplete.Broadcast(QuestId);
    OnQuestLogChanged.Broadcast();
}

// ── SMSG_TALENTS_INFO ────────────────────────────────────────────────────
// uint8 isPet, uint32 freePoints, then player groups or the pet talent list.
void FWowPacketHandler::ResetTalentState()
{
    Talents.Reset(); TalentGroups.Reset(); PetTalents.Reset(); LastGlyphChanges.Reset();
    FreeTalentPoints=PetFreeTalentPoints=0; ActiveTalentGroup=PreviousTalentGroup=0;
    HasTalentInfo=HasPetTalentInfo=LastTalentPacketValid=LastTalentUpdateIsPet=LastTalentGroupChanged=false;
    ++TalentRevision; ++PlayerTalentRevision; ++PetTalentRevision;
}
void FWowPacketHandler::HandleTalentsInfo(FPacketReader& R)
{
    LastTalentPacketValid=false;
    if(!R.CanRead(5)) return;
    const uint8 IsPet=R.ReadU8(); if(IsPet>1) return;
    const uint32 FreePoints=R.ReadU32();
    const auto ReadTalents=[&](TArray<FWowTalentInfo>& Result)
    {
        if(!R.CanRead(1)) return false;
        const uint8 Count=R.ReadU8(); if(!R.CanRead(int32(Count)*5)) return false;
        for(uint32 I=0;I<Count;++I)
        {
            FWowTalentInfo Entry; Entry.TalentId=R.ReadU32(); Entry.Rank=R.ReadU8();
            if(!Entry.TalentId || Entry.Rank>4) return false;
            if(auto* Prior=Result.FindByPredicate([&](const auto& T){return T.TalentId==Entry.TalentId;})) Prior->Rank=FMath::Max(Prior->Rank,Entry.Rank);
            else Result.Add(Entry);
        }
        return true;
    };
    if(IsPet)
    {
        TArray<FWowTalentInfo> Incoming;
        if(!ReadTalents(Incoming) || R.CanRead(1)) return;
        PetTalents=MoveTemp(Incoming); PetFreeTalentPoints=FreePoints; HasPetTalentInfo=true;
        LastTalentUpdateIsPet=true; LastTalentPacketValid=true; LastTalentGroupChanged=false; LastGlyphChanges.Reset(); ++TalentRevision; ++PetTalentRevision;
        UE_LOG(LogWowPacket,Log,TEXT("TALENTS_INFO: pet, free=%u, %d talents"),FreePoints,PetTalents.Num());
        OnTalentsUpdated.Broadcast(); return;
    }
    if(!R.CanRead(2)) return;
    const uint8 Count=R.ReadU8(),Active=R.ReadU8();
    if(Count>2 || (Count?Active>=Count:Active!=0)) return;
    TArray<FWowTalentGroup> Incoming;
    for(uint32 I=0;I<Count;++I)
    {
        FWowTalentGroup Group;
        if(!ReadTalents(Group.Talents) || !R.CanRead(1)) return;
        const uint8 GlyphCount=R.ReadU8(); if(GlyphCount>6 || !R.CanRead(int32(GlyphCount)*2)) return;
        for(uint32 J=0;J<GlyphCount;++J) Group.Glyphs[J]=R.ReadU16();
        Incoming.Add(MoveTemp(Group));
    }
    if(R.CanRead(1)) return;
    LastGlyphChanges.Reset();
    for(uint8 I=0;I<6;++I)
    {
        const uint16 Before=TalentGroups.IsValidIndex(ActiveTalentGroup)?TalentGroups[ActiveTalentGroup].Glyphs[I]:0;
        const uint16 After=Incoming.IsValidIndex(Active)?Incoming[Active].Glyphs[I]:0;
        if(Before!=After) LastGlyphChanges.Add({I,Before,After});
    }
    PreviousTalentGroup=ActiveTalentGroup; LastTalentGroupChanged=HasTalentInfo && ActiveTalentGroup!=Active;
    TalentGroups=MoveTemp(Incoming); ActiveTalentGroup=Active; FreeTalentPoints=FreePoints;
    HasTalentInfo=true; LastTalentUpdateIsPet=false; LastTalentPacketValid=true;
    Talents=TalentGroups.IsValidIndex(Active)?TalentGroups[Active].Talents:TArray<FWowTalentInfo>(); ++TalentRevision; ++PlayerTalentRevision;
    UE_LOG(LogWowPacket,Log,TEXT("TALENTS_INFO: %d groups, active=%u, free=%u, %d talents"),TalentGroups.Num(),ActiveTalentGroup,FreeTalentPoints,Talents.Num());
    OnTalentsUpdated.Broadcast();
}

// ── SMSG_LEARNED_SPELL ───────────────────────────────────────────────────
// uint32 spellId, uint16 unknown

void FWowPacketHandler::HandleLearnedSpell(FPacketReader& R)
{
    if (!R.CanRead(6)) return;

    uint32 SpellId = R.ReadU32();
    uint16 Unknown = R.ReadU16();

    if(!SpellId || SpellId>0xffffffu) return;
    KnownSpells.Add(SpellId);
    ReplaceSpellActions(0,SpellId);

    UE_LOG(LogWowPacket, Log, TEXT("LEARNED_SPELL: spell=%u"), SpellId);
}

void FWowPacketHandler::ReplaceSpellActions(uint32 OldId,uint32 NewId)
{
    const auto& Spells=FDbcStore::Get().Spells(); const auto* New=Spells.GetById(NewId); bool Changed=false;
    for(int32 I=0;I<FMath::Min(144,ActionButtons.Num());++I)
    {
        const uint32 Packed=ActionButtons[I],Id=Packed&0xffffffu;
        if(!Id || (Packed>>24)!=0 || Id==NewId) continue;
        const auto* Old=Spells.GetById(Id);
        const bool Adjacent=New && Old && New->ProgressionRank()>0 && New->ProgressionRank()==Old->ProgressionRank()+1 && New->SpellName==Old->SpellName;
        if(!(OldId && Id==OldId) && !Adjacent) continue;
        ActionButtons[I]=NewId; Changed=true;
        TArray<uint8> Data; Data.Add(uint8(I)); Data.Append(reinterpret_cast<const uint8*>(&NewId),4);
        OnSendPacket.ExecuteIfBound(WowOpcode::CMSG_SET_ACTION_BUTTON,Data);
    }
    if(Changed) OnActionButtonsUpdated.Broadcast();
}

void FWowPacketHandler::HandleSupersededSpell(FPacketReader& R)
{
    if(!R.CanRead(8) || R.CanRead(9)) return;
    const uint32 OldId=R.ReadU32(),NewId=R.ReadU32();
    if(!OldId || !NewId || OldId==NewId || OldId>0xffffffu || NewId>0xffffffu) return;
    // Superseded ranks are no longer active (e.g. warrior melee abilities).
    // Stackable caster ranks arrive as LEARNED_SPELL and retain prior ranks.
    KnownSpells.Remove(OldId);
    KnownSpells.Add(NewId); ReplaceSpellActions(OldId,NewId);
    UE_LOG(LogWowPacket,Log,TEXT("SUPERCEDED_SPELL: %u -> %u"),OldId,NewId);
}

// ── SMSG_REMOVED_SPELL ───────────────────────────────────────────────────
// uint32 spellId

void FWowPacketHandler::HandleRemovedSpell(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    uint32 SpellId = R.ReadU32();

    KnownSpells.Remove(SpellId);

    UE_LOG(LogWowPacket, Log, TEXT("REMOVED_SPELL: spell=%u"), SpellId);
}

// ── Warden Anti-Cheat ────────────────────────────────────────────────────────

void FWowPacketHandler::InitializeWarden(const TArray<uint8>& SessionKey)
{
    WardenHandler.InitializeEncryption(SessionKey);
}

void FWowPacketHandler::HandleWardenData(FPacketReader& R)
{
    TArray<uint8> Data;
    Data.SetNumUninitialized(R.Remaining());
    if (R.Remaining() > 0)
    {
        FMemory::Memcpy(Data.GetData(), R.Data + R.Pos, R.Remaining());
    }

    WardenHandler.HandleWardenData(Data);
}

void FWowPacketHandler::SendWardenResponse(uint32 Opcode, const TArray<uint8>& Data)
{
    if (OnSendPacket.IsBound())
    {
        OnSendPacket.Execute(Opcode, Data);
    }
}

// ──────────────────────────────────────────────────────────────────────────
// Death / Corpse / Resurrection Handlers
// ──────────────────────────────────────────────────────────────────────────

// ── SMSG_CORPSE_RECLAIM_DELAY ────────────────────────────────────────────
// uint32 delayTimeInSeconds

void FWowPacketHandler::HandleCorpseReclaimDelay(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    uint32 DelayTime = R.ReadU32();
    float DelayInSeconds = static_cast<float>(DelayTime);

    UE_LOG(LogWowPacket, Log, TEXT("CORPSE_RECLAIM_DELAY: delay=%u seconds"), DelayTime);

    OnCorpseReclaimDelay.Broadcast(DelayInSeconds);
}

// ── SMSG_RESURRECT_REQUEST ───────────────────────────────────────────────
// uint64 resurrectorGuid, FString resurrectorName, uint8 unk

void FWowPacketHandler::HandleResurrectRequest(FPacketReader& R)
{
    if (!R.CanRead(8)) return;

    uint64 ResurrectorGuid = R.ReadU64();
    FString ResurrectorName = R.ReadCString();
    uint8 Unknown = R.ReadU8();

    UE_LOG(LogWowPacket, Log, TEXT("RESURRECT_REQUEST: resurrector=%llu name=%s"),
        ResurrectorGuid, *ResurrectorName);

    OnResurrectRequest.Broadcast(ResurrectorName);
}

// ── Teleport Handling ────────────────────────────────────────────────────────

void FWowPacketHandler::HandleMoveTeleport(FPacketReader& R)
{
    // 12340 near teleport: packed mover GUID, server order, MovementInfo.
    if(!R.CanRead(1)) return; const uint8 Mask=R.ReadU8(); int32 Count=0;
    for(int32 I=0;I<8;++I) if(Mask&(1<<I)) ++Count;
    if(!R.CanRead(Count+4)) return; uint64 Guid=0;
    for(int32 I=0;I<8;++I) if(Mask&(1<<I)) Guid|=uint64(R.ReadU8())<<(8*I);
    const uint32 OrderCounter=R.ReadU32(); auto* Entity=EntityManager.Find(Guid);
    if(!Entity || Guid!=EntityManager.LocalPlayerGuid) return;
    FWowMovementInfo Move=Entity->Movement; uint32 Time=0;
    if(!ParseMovementPayload(R,Move,Time)) return;
    Entity->Movement=Move;
    OnTeleportRequest.Broadcast(Guid,OrderCounter,Time,Move.Position,Move.Orientation);
    EntityManager.OnEntityUpdated.Broadcast(*Entity);
    OnPlayerTeleport.Broadcast(CurrentMapId,Move.Position.X,Move.Position.Y,Move.Position.Z);
    UE_LOG(LogWowPacket,Log,TEXT("MOVE_TELEPORT_ACK request: guid=%llu counter=%u map=%u pos=(%.3f,%.3f,%.3f)"),Guid,OrderCounter,CurrentMapId,Move.Position.X,Move.Position.Y,Move.Position.Z);
}

void FWowPacketHandler::HandleTransferPending(FPacketReader& R)
{
    // SMSG_TRANSFER_PENDING
    // uint32 mapId, optional transport info

    uint32 MapId = R.ReadU32();

    // Note: In some versions there may be transport GUID data here,
    // but for basic implementation we'll just handle the map change

    UE_LOG(LogWowPacket, Log, TEXT("TRANSFER_PENDING: mapId=%u"), MapId);

    // This indicates a map transfer is about to happen
    // The client should expect SMSG_NEW_WORLD next
}

void FWowPacketHandler::HandleNewWorld(FPacketReader& R)
{
    // SMSG_NEW_WORLD
    // uint32 mapId + float x + float y + float z + float orientation

    if (!R.CanRead(20)) // 4 + 4*4
    {
        UE_LOG(LogWowPacket, Error, TEXT("NEW_WORLD packet too short"));
        return;
    }

    uint32 MapId = R.ReadU32();
    float X = R.ReadFloat();
    float Y = R.ReadFloat();
    float Z = R.ReadFloat();
    float Orientation = R.ReadFloat();

    UE_LOG(LogWowPacket, Log, TEXT("NEW_WORLD: map=%u pos=(%.1f,%.1f,%.1f) orient=%.2f"),
           MapId, X, Y, Z, Orientation);

    // Clear quest log on map transfer
    QuestLog.Empty();

    // Broadcast map transfer
    OnMapTransfer.Broadcast(MapId, X, Y, Z, Orientation);
    CurrentMapId=MapId;
}

// ── SMSG_NAME_QUERY_RESPONSE ─────────────────────────────────────────────────
// packedGUID, uint8 found, if found: string name, string realmName, uint8 race, uint8 gender, uint8 class

void FWowPacketHandler::HandleNameQueryResponse(FPacketReader& R)
{
    uint64 Guid = R.ReadPackedGuid();
    uint8 Found = R.ReadU8();

    if (Found==0 && R.CanRead(1))
    {
        FString Name = R.ReadCString();
        FString RealmName = R.ReadCString();
        uint8 Race = R.ReadU8();
        uint8 Gender = R.ReadU8();
        uint8 Class = R.ReadU8();

        PlayerNameCache.Add(Guid, Name);

        UE_LOG(LogWowPacket, Log, TEXT("NAME_QUERY_RESPONSE: GUID=%llu, Name=%s, Realm=%s"),
            Guid, *Name, *RealmName);

        OnPlayerNameReceived.Broadcast(Guid, Name);
    }
    else
    {
        UE_LOG(LogWowPacket, Warning, TEXT("NAME_QUERY_RESPONSE: GUID=%llu not found"), Guid);
    }
}

// ── SMSG_CREATURE_QUERY_RESPONSE ─────────────────────────────────────────────────
// uint32 entry, string name, string subName, string iconName, uint32 typeFlags,
// uint32 creatureType, uint32 creatureFamily, uint32 rank, uint32 killCredit[2],
// uint32 displayId[4], float hpMod, float manaMod, uint8 racialLeader

void FWowPacketHandler::HandleCreatureQueryResponse(FPacketReader& R)
{
    if (!R.CanRead(4)) return;

    uint32 Entry = R.ReadU32();
    FString Name = R.ReadCString();
    for(int32 I=0;I<3;++I) R.ReadCString(); // alternate name slots
    FString Title = R.ReadCString(); // SubName follows all four names

    if (!Name.IsEmpty())
    {
        CreatureNameCache.Add(Entry, Name);
        CreatureTitleCache.Add(Entry, Title);

        UE_LOG(LogWowPacket, Log, TEXT("CREATURE_QUERY_RESPONSE: Entry=%u, Name=%s, Title=%s"),
            Entry, *Name, *Title);

        OnCreatureNameReceived.Broadcast(Entry, Name, Title);
    }

    if(R.Remaining()) {R.ReadCString(); if(R.CanRead(8)) {R.ReadU32(); CreatureTypes.Add(Entry,R.ReadU32());}}
}

// ── Cooldown Helper Methods ──────────────────────────────────────────────────

bool FWowPacketHandler::IsSpellOnCooldown(uint32 SpellId) const
{
    const double* ExpiryTime = SpellCooldowns.Find(SpellId);
    return ExpiryTime && FPlatformTime::Seconds() < *ExpiryTime;
}

float FWowPacketHandler::GetSpellCooldownRemaining(uint32 SpellId) const
{
    const double* ExpiryTime = SpellCooldowns.Find(SpellId);
    if (!ExpiryTime)
    {
        return 0.0f;
    }

    double Remaining = *ExpiryTime - FPlatformTime::Seconds();
    return FMath::Max(0.0f, static_cast<float>(Remaining));
}

// ── Emote Handlers ───────────────────────────────────────────────────────

void FWowPacketHandler::HandleEmote(FPacketReader& R)
{
    // SMSG_EMOTE
    // uint32 emoteId + uint64 guid

    if (!R.CanRead(12)) // 4 + 8
    {
        UE_LOG(LogWowPacket, Error, TEXT("EMOTE packet too short"));
        return;
    }

    uint32 EmoteId = R.ReadU32();
    uint64 Guid = R.ReadU64();

    UE_LOG(LogWowPacket, Log, TEXT("EMOTE: EmoteId=%u, GUID=%llu"), EmoteId, Guid);

    // Broadcast emote event for animation system
    OnEmote.Broadcast(Guid, EmoteId);
}

void FWowPacketHandler::HandleTextEmote(FPacketReader& R)
{
    // SMSG_TEXT_EMOTE
    // uint32 textEmoteId + uint32 emoteNum + uint64 senderGuid + string emoteText

    if (!R.CanRead(16)) // 4 + 4 + 8
    {
        UE_LOG(LogWowPacket, Error, TEXT("TEXT_EMOTE packet too short"));
        return;
    }

    uint32 TextEmoteId = R.ReadU32();
    uint32 EmoteNum = R.ReadU32();
    uint64 SenderGuid = R.ReadU64();

    FString EmoteText;
    if (R.CanRead(1))
    {
        EmoteText = R.ReadCString();
    }

    UE_LOG(LogWowPacket, Log, TEXT("TEXT_EMOTE: TextEmoteId=%u, EmoteNum=%u, Sender=%llu, Text=%s"),
           TextEmoteId, EmoteNum, SenderGuid, *EmoteText);

    // Show the text emote in chat
    if (!EmoteText.IsEmpty())
    {
        // Get sender name
        FString SenderName;
        const FString* CachedName = PlayerNameCache.Find(SenderGuid);
        if (CachedName)
        {
            SenderName = *CachedName;
        }
        else
        {
            SenderName = TEXT("Unknown");
        }

        // Broadcast as a special chat message (emote type)
        OnChatMessage.Broadcast(0, 0, SenderGuid, SenderName, EmoteText, TEXT(""));
    }

    // Also trigger the animation if we have the EmoteNum
    if (EmoteNum > 0)
    {
        OnEmote.Broadcast(SenderGuid, EmoteNum);
    }
}

// ── Player progression handlers ─────────────────────────────────────────

void FWowPacketHandler::HandleLevelUpInfo(FPacketReader& R)
{
    // SMSG_LEVELUP_INFO
    // uint32 Level, uint32 HealthGained, uint32 ManaGained, then 5x uint32 stat gains (STR/AGI/STA/INT/SPI)

    if (!R.CanRead(28)) // 7 * 4 bytes
    {
        UE_LOG(LogWowPacket, Warning, TEXT("LEVELUP_INFO: packet too short"));
        return;
    }

    uint32 Level = R.ReadU32();
    uint32 HealthGained = R.ReadU32();
    uint32 ManaGained = R.ReadU32();
    uint32 StrGained = R.ReadU32();
    uint32 AgiGained = R.ReadU32();
    uint32 StaGained = R.ReadU32();
    uint32 IntGained = R.ReadU32();
    uint32 SpiGained = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("LEVELUP_INFO: Level %u, HP+%u, MP+%u, STR+%u, AGI+%u, STA+%u, INT+%u, SPI+%u"),
           Level, HealthGained, ManaGained, StrGained, AgiGained, StaGained, IntGained, SpiGained);

    OnLevelUp.Broadcast(Level);
}

void FWowPacketHandler::HandleLogXPGain(FPacketReader& R)
{
    // SMSG_LOG_XPGAIN
    // uint64 VictimGuid, uint32 XPAmount, uint8 Type (0=kill, 1=non-kill)

    if (!R.CanRead(13)) // 8 + 4 + 1
    {
        UE_LOG(LogWowPacket, Warning, TEXT("LOG_XPGAIN: packet too short"));
        return;
    }

    uint64 VictimGuid = R.ReadU64();
    uint32 XPAmount = R.ReadU32();
    uint8 Type = R.ReadU8();

    UE_LOG(LogWowPacket, Log, TEXT("LOG_XPGAIN: +%u XP from GUID %llu (type %u)"), XPAmount, VictimGuid, Type);

    OnXPGain.Broadcast(XPAmount, Type);
}

void FWowPacketHandler::HandleExplorationExperience(FPacketReader& R)
{
    // SMSG_EXPLORATION_EXPERIENCE
    // uint32 AreaId, uint32 XPAmount

    if (!R.CanRead(8)) // 4 + 4
    {
        UE_LOG(LogWowPacket, Warning, TEXT("EXPLORATION_EXPERIENCE: packet too short"));
        return;
    }

    uint32 AreaId = R.ReadU32();
    uint32 XPAmount = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("EXPLORATION_EXPERIENCE: +%u XP for exploring area %u"), XPAmount, AreaId);

    // Broadcast as XP gain (type 1 = non-kill)
    OnXPGain.Broadcast(XPAmount, 1);
}

void FWowPacketHandler::HandleEnvironmentalDamageLog(FPacketReader& R)
{
    // SMSG_ENVIRONMENTAL_DAMAGE_LOG
    // uint64 VictimGuid, uint8 DamageType, uint32 Damage, uint32 Absorbed, uint32 Resisted

    if (!R.CanRead(21)) // 8 + 1 + 4 + 4 + 4
    {
        UE_LOG(LogWowPacket, Warning, TEXT("ENVIRONMENTAL_DAMAGE_LOG: packet too short"));
        return;
    }

    uint64 VictimGuid = R.ReadU64();
    uint8 DamageType = R.ReadU8();
    uint32 Damage = R.ReadU32();
    uint32 Absorbed = R.ReadU32();
    uint32 Resisted = R.ReadU32();
    static const TCHAR* Types[]={TEXT("EXHAUSTION"),TEXT("DROWNING"),TEXT("FALLING"),TEXT("LAVA"),TEXT("SLIME"),TEXT("FIRE")};
    FWowCombatEntry Entry; Entry.Event=TEXT("ENVIRONMENTAL_DAMAGE"); Entry.Destination=VictimGuid;
    Entry.Values={FWowCombatValue::S(DamageType<UE_ARRAY_COUNT(Types)?Types[DamageType]:TEXT("UNKNOWN")),FWowCombatValue::N(Damage),FWowCombatValue::N(0),FWowCombatValue::N(DamageType==3 || DamageType==5?4:DamageType==4?8:1),FWowCombatValue::N(Resisted),FWowCombatValue::N(0),FWowCombatValue::N(Absorbed),FWowCombatValue::B(false),FWowCombatValue::B(false),FWowCombatValue::B(false)};
    RecordCombat(MoveTemp(Entry));

    UE_LOG(LogWowPacket, Log, TEXT("ENVIRONMENTAL_DAMAGE_LOG: GUID %llu, type %u, %u damage (%u absorbed, %u resisted)"),
           VictimGuid, DamageType, Damage, Absorbed, Resisted);
}

void FWowPacketHandler::HandleBindPointUpdate(FPacketReader& R)
{
    // SMSG_BINDPOINTUPDATE
    // float X, float Y, float Z, uint32 MapId, uint32 AreaId

    if (!R.CanRead(20)) // 3*4 + 4 + 4
    {
        UE_LOG(LogWowPacket, Warning, TEXT("BINDPOINTUPDATE: packet too short"));
        return;
    }

    float X = R.ReadFloat();
    float Y = R.ReadFloat();
    float Z = R.ReadFloat();
    uint32 MapId = R.ReadU32();
    uint32 AreaId = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("BINDPOINTUPDATE: Map %u, Area %u, Position (%.2f, %.2f, %.2f)"),
           MapId, AreaId, X, Y, Z);

    OnBindPointUpdate.Broadcast(X, Y, Z, MapId, AreaId);
}

void FWowPacketHandler::HandlePlayedTime(FPacketReader& R)
{
    // SMSG_PLAYED_TIME
    // uint32 TotalTime, uint32 LevelTime, uint8 TriggerEvent

    if (!R.CanRead(9)) // 4 + 4 + 1
    {
        UE_LOG(LogWowPacket, Warning, TEXT("PLAYED_TIME: packet too short"));
        return;
    }

    uint32 TotalTime = R.ReadU32();
    TotalPlayedTime=TotalTime; PlayedTimeReceived=FPlatformTime::Seconds(); PlayedTimeRequested=true;
    uint32 LevelTime = R.ReadU32();
    uint8 TriggerEvent = R.ReadU8();

    uint32 TotalHours = TotalTime / 3600;
    uint32 TotalMinutes = (TotalTime % 3600) / 60;
    uint32 LevelHours = LevelTime / 3600;
    uint32 LevelMinutes = (LevelTime % 3600) / 60;

    UE_LOG(LogWowPacket, Log, TEXT("PLAYED_TIME: Total %uh %um, This Level %uh %um (event %u)"),
           TotalHours, TotalMinutes, LevelHours, LevelMinutes, TriggerEvent);
}

// ── World state handlers ────────────────────────────────────────────────

void FWowPacketHandler::HandleWeather(FPacketReader& R)
{
    // SMSG_WEATHER
    // uint32 WeatherType, float Grade, uint8 Sound

    if (!R.CanRead(9)) // 4 + 4 + 1
    {
        UE_LOG(LogWowPacket, Warning, TEXT("WEATHER: packet too short"));
        return;
    }

    uint32 WeatherType = R.ReadU32();
    float Grade = R.ReadFloat();
    uint8 Sound = R.ReadU8();

    UE_LOG(LogWowPacket, Log, TEXT("WEATHER: Type %u, Grade %.2f, Sound %u"), WeatherType, Grade, Sound);

    OnWeatherUpdate.Broadcast(WeatherType, Grade, Sound);
}

void FWowPacketHandler::HandleInitWorldStates(FPacketReader& R)
{
    // SMSG_INIT_WORLD_STATES
    // uint32 MapId, uint32 AreaId, uint32 SubAreaId, uint16 Count, then Count x (uint32 Field, uint32 Value)

    if (!R.CanRead(14)) // 4 + 4 + 4 + 2
    {
        UE_LOG(LogWowPacket, Warning, TEXT("INIT_WORLD_STATES: packet too short"));
        return;
    }

    uint32 MapId = R.ReadU32();
    uint32 AreaId = R.ReadU32();
    uint32 SubAreaId = R.ReadU32();
    uint16 Count = R.ReadU16();

    UE_LOG(LogWowPacket, Log, TEXT("INIT_WORLD_STATES: Map %u, Area %u, SubArea %u, %u states"), MapId, AreaId, SubAreaId, Count);

    if(R.Remaining()!=int64(Count)*8) return;
    ServerAreaMapId=MapId; ServerZoneId=AreaId; ServerAreaId=SubAreaId?SubAreaId:AreaId;

    // Clear existing world states
    WorldStates.Empty();

    // Read each world state
    for (uint16 i = 0; i < Count && R.CanRead(8); ++i)
    {
        uint32 Field = R.ReadU32();
        uint32 Value = R.ReadU32();
        WorldStates.Add(Field, Value);
        UE_LOG(LogWowPacket, VeryVerbose, TEXT("  World State %u = %u"), Field, Value);
    }

    OnWorldStatesInit.Broadcast(MapId, AreaId, SubAreaId);
}

void FWowPacketHandler::HandleUpdateWorldState(FPacketReader& R)
{
    // SMSG_UPDATE_WORLD_STATE
    // uint32 Field, uint32 Value

    if (!R.CanRead(8)) // 4 + 4
    {
        UE_LOG(LogWowPacket, Warning, TEXT("UPDATE_WORLD_STATE: packet too short"));
        return;
    }

    uint32 Field = R.ReadU32();
    uint32 Value = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("UPDATE_WORLD_STATE: Field %u = %u"), Field, Value);

    // Update world state
    WorldStates.FindOrAdd(Field) = Value;

    OnWorldStateUpdate.Broadcast(Field, Value);
}

// ── Account/Party handlers ──────────────────────────────────────────────

void FWowPacketHandler::HandleSetProficiency(FPacketReader& R)
{
    // SMSG_SET_PROFICIENCY
    // uint8 ItemClass, uint32 ItemSubclassMask

    if (!R.CanRead(5)) // 1 + 4
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SET_PROFICIENCY: packet too short"));
        return;
    }

    uint8 ItemClass = R.ReadU8();
    uint32 ItemSubclassMask = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("SET_PROFICIENCY: ItemClass %u, SubclassMask 0x%08X"), ItemClass, ItemSubclassMask);

    OnProficiencySet.Broadcast(ItemClass, ItemSubclassMask);
}

void FWowPacketHandler::HandleAccountDataTimes(FPacketReader& R)
{
    // SMSG_ACCOUNT_DATA_TIMES
    // uint32 Time, uint8 Mask, then up to 8x uint32 timestamps

    if (!R.CanRead(5)) // 4 + 1
    {
        UE_LOG(LogWowPacket, Warning, TEXT("ACCOUNT_DATA_TIMES: packet too short"));
        return;
    }

    uint32 Time = R.ReadU32();
    uint8 Mask = R.ReadU8();

    UE_LOG(LogWowPacket, Log, TEXT("ACCOUNT_DATA_TIMES: Time %u, Mask 0x%02X"), Time, Mask);

    // Read timestamps for each enabled bit
    for (int32 i = 0; i < 8; ++i)
    {
        if ((Mask & (1 << i)) && R.CanRead(4))
        {
            uint32 Timestamp = R.ReadU32();
            UE_LOG(LogWowPacket, VeryVerbose, TEXT("  Account data %d timestamp: %u"), i, Timestamp);
        }
    }

    // No action needed - just prevents server disconnect
}

void FWowPacketHandler::HandlePartyMemberStats(FPacketReader& R)
{
    // SMSG_PARTY_MEMBER_STATS
    // Complex packet with variable structure

    if (!R.CanRead(9)) // packed guid + mask
    {
        UE_LOG(LogWowPacket, Warning, TEXT("PARTY_MEMBER_STATS: packet too short"));
        return;
    }

    uint64 MemberGuid = R.ReadPackedGuid();
    uint32 Mask = R.ReadU32();

    UE_LOG(LogWowPacket, Log, TEXT("PARTY_MEMBER_STATS: Member GUID %llu, Mask 0x%08X"), MemberGuid, Mask);

    // For now, just skip the rest - full implementation would parse all the conditional fields
    // This prevents errors when receiving these packets
}

// ── NPC interaction handlers ────────────────────────────────────────────



// ── Mail system handlers ────────────────────────────────────────────

void FWowPacketHandler::HandleShowMailbox(FPacketReader& R)
{
    if (!R.CanRead(8))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SHOW_MAILBOX: packet too short"));
        return;
    }

    CurrentMailboxGuid = R.ReadU64();

    UE_LOG(LogWowPacket, Log, TEXT("SHOW_MAILBOX: mailbox GUID %llu"), CurrentMailboxGuid);
    OnMailboxShown.Broadcast(CurrentMailboxGuid);
}

void FWowPacketHandler::HandleMailListResult(FPacketReader& R)
{
    MailListPending=false;
    if(!R.CanRead(5)) return;
    const uint32 Total=R.ReadU32(); const uint8 Count=R.ReadU8(); TArray<FWowMailMessage> Parsed;
    const auto ReadText=[](FPacketReader& Reader,FString& Out)
    {
        const int32 Start=Reader.Pos; Out=Reader.ReadCString(); return Reader.Pos>Start && Reader.Data[Reader.Pos-1]==0;
    };
    for(uint32 I=0;I<Count;++I)
    {
        if(!R.CanRead(2)) return;
        const int32 Start=R.Pos; const uint16 Size=R.ReadU16(); const int32 End=Start+Size;
        if(Size<2 || End>R.Size) return;
        FPacketReader M(R.Data+R.Pos,End-R.Pos); FWowMailMessage Row;
        if(!M.CanRead(5)) return;
        Row.MessageId=M.ReadU32(); Row.MessageType=M.ReadU8();
        if(!M.CanRead(Row.MessageType==0?8:4)) return;
        if(Row.MessageType==0) Row.SenderGuid=M.ReadU64(); else Row.SenderEntry=M.ReadU32();
        if(!M.CanRead(28)) return;
        Row.COD=M.ReadU32(); Row.Package=M.ReadU32(); Row.Stationery=M.ReadU32(); Row.Money=M.ReadU32(); Row.Checked=M.ReadU32(); Row.DaysLeft=M.ReadFloat(); Row.MailTemplateId=M.ReadU32();
        if(!ReadText(M,Row.Subject) || !ReadText(M,Row.Body) || !M.CanRead(1)) return;
        const uint8 Items=M.ReadU8(); if(Items>12 || !M.CanRead(Items*118)) return;
        for(uint32 N=0;N<Items;++N)
        {
            FWowMailAttachment Item; Item.AttachmentIndex=M.ReadU8(); Item.ItemGuidLow=M.ReadU32(); Item.ItemEntry=M.ReadU32(); M.Skip(84);
            Item.RandomPropertyId=int32(M.ReadU32()); Item.SuffixFactor=M.ReadU32(); Item.Count=M.ReadU32(); Item.Charges=M.ReadU32(); Item.MaxDurability=M.ReadU32(); Item.Durability=M.ReadU32(); M.ReadU8(); Row.Attachments.Add(Item);
        }
        Row.ReceivedAt=FPlatformTime::Seconds(); Parsed.Add(MoveTemp(Row)); R.Pos=End;
    }
    MailInbox=MoveTemp(Parsed); MailInboxTotalCount=FMath::Max(Total,uint32(Count));
    OnMailListReceived.Broadcast(MailInbox);
}

void FWowPacketHandler::HandlePetSpells(FPacketReader& R)
{
    if(!R.CanRead(8)) return;
    FWowPetActionBarState Incoming; Incoming.PetGuid=R.ReadU64();
    if(Incoming.PetGuid)
    {
        if(!R.CanRead(10+WOW_PET_ACTION_SLOT_COUNT*4+1)) return;
        Incoming.Family=R.ReadU16(); Incoming.DurationMs=R.ReadU32(); Incoming.ReactState=R.ReadU8(); Incoming.CommandState=R.ReadU8(); Incoming.Flags=R.ReadU16();
        for(int32 I=0;I<WOW_PET_ACTION_SLOT_COUNT;++I) Incoming.ActionSlots[I].SetPackedData(R.ReadU32());
        const uint8 SpellCount=R.ReadU8(); if(!R.CanRead(int32(SpellCount)*4+1)) return;
        for(uint32 I=0;I<SpellCount;++I) { const uint32 Id=R.ReadU32()&0x00ffffff; if(Id) Incoming.KnownSpells.Add(Id); }
        const uint8 Count=R.ReadU8(); if(!R.CanRead(int32(Count)*14)) return;
        for(uint32 I=0;I<Count;++I)
        {
            const uint32 Id=R.ReadU32(); R.ReadU16(); const uint32 SpellMs=R.ReadU32(),CategoryMs=R.ReadU32(),Ms=FMath::Max(SpellMs,CategoryMs);
            if(Id && Ms) Incoming.SpellCooldownExpirySeconds.Add(Id,FPlatformTime::Seconds()+Ms/1000.);
        }
    }
    if(R.CanRead(1)) return;
    if(Incoming.PetGuid!=PetActionBar.PetGuid)
    {
        PetTalents.Reset(); PetFreeTalentPoints=0; HasPetTalentInfo=false; ++PetTalentRevision; ++TalentRevision;
    }
    PetActionBar=MoveTemp(Incoming);
    UE_LOG(LogWowPacket,Log,TEXT("PET_SPELLS: pet=%llu family=%u actions=%d spells=%d"),PetActionBar.PetGuid,PetActionBar.Family,PetActionBar.ActionSlots.Num(),PetActionBar.KnownSpells.Num());
    OnPetBarUpdated.Broadcast(PetActionBar);
}

void FWowPacketHandler::HandleDuelRequested(FPacketReader& R)
{
    if (!R.CanRead(16))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("DUEL_REQUESTED: packet too short"));
        return;
    }

    const uint64 ArbiterGuid = R.ReadU64();
    const uint64 InitiatorGuid = R.ReadU64();

    CurrentDuel.BeginRequest(ArbiterGuid, InitiatorGuid);
    UE_LOG(LogWowPacket, Log, TEXT("DUEL_REQUESTED: arbiter=%llu initiator=%llu"), ArbiterGuid, InitiatorGuid);
    OnDuelUpdated.Broadcast(CurrentDuel);
}

void FWowPacketHandler::HandleDuelOutOfBounds(FPacketReader& R)
{
    static_cast<void>(R);
    CurrentDuel.SetInBounds(false);
    UE_LOG(LogWowPacket, Log, TEXT("DUEL_OUTOFBOUNDS"));
    OnDuelUpdated.Broadcast(CurrentDuel);
}

void FWowPacketHandler::HandleDuelInBounds(FPacketReader& R)
{
    static_cast<void>(R);
    CurrentDuel.SetInBounds(true);
    UE_LOG(LogWowPacket, Log, TEXT("DUEL_INBOUNDS"));
    OnDuelUpdated.Broadcast(CurrentDuel);
}

void FWowPacketHandler::HandleDuelComplete(FPacketReader& R)
{
    if (!R.CanRead(1))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("DUEL_COMPLETE: packet too short"));
        return;
    }

    const bool bFinishedNormally = (R.ReadU8() != 0);
    if (bFinishedNormally)
    {
        CurrentDuel.MarkCompleted();
    }
    else
    {
        CurrentDuel.MarkInterrupted();
    }

    UE_LOG(LogWowPacket, Log, TEXT("DUEL_COMPLETE: normal=%d"), bFinishedNormally ? 1 : 0);
    OnDuelUpdated.Broadcast(CurrentDuel);
}

void FWowPacketHandler::HandleDuelWinner(FPacketReader& R)
{
    if (!R.CanRead(1))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("DUEL_WINNER: packet too short"));
        return;
    }

    const uint8 ResultReason = R.ReadU8();
    const FString WinnerName = R.ReadCString();
    const FString LoserName = R.ReadCString();

    CurrentDuel.SetWinner(ResultReason, WinnerName, LoserName);
    UE_LOG(LogWowPacket, Log, TEXT("DUEL_WINNER: reason=%u winner='%s' loser='%s'"),
        ResultReason, *WinnerName, *LoserName);
    OnDuelUpdated.Broadcast(CurrentDuel);
}

void FWowPacketHandler::HandleDuelCountdown(FPacketReader& R)
{
    if (!R.CanRead(4))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("DUEL_COUNTDOWN: packet too short"));
        return;
    }

    const uint32 CountdownMilliseconds = R.ReadU32();
    CurrentDuel.BeginCountdown(static_cast<double>(CountdownMilliseconds) / 1000.0);
    UE_LOG(LogWowPacket, Log, TEXT("DUEL_COUNTDOWN: duration_ms=%u"), CountdownMilliseconds);
    OnDuelUpdated.Broadcast(CurrentDuel);
}

void FWowPacketHandler::HandleTradeStatus(FPacketReader& R)
{
    if(!R.CanRead(4)) return;
    const uint32 Status=R.ReadU32();
    const int32 Extra=Status==WowTradeStatus::BEGIN_TRADE?8:Status==WowTradeStatus::OPEN_WINDOW?4:Status==WowTradeStatus::CLOSE_WINDOW?9:(Status==22 || Status==23)?1:0;
    if(!R.CanRead(Extra)) return;
    auto Next=CurrentTrade; Next.Status=Status; Next.Result=Next.LimitedItem=0; Next.TargetResult=false; Next.ErrorSlot=255;
    if(Status==WowTradeStatus::BEGIN_TRADE)
    {
        Next=FWowTradeState(); Next.Status=Status; Next.TraderGuid=R.ReadU64();
    }
    else if(Status==WowTradeStatus::OPEN_WINDOW)
    {
        const uint64 Partner=Next.TraderGuid; const bool UseCursor=Next.UseCursorOnOpen; Next=FWowTradeState(); Next.Status=Status; Next.TraderGuid=Partner; Next.UseCursorOnOpen=UseCursor;
        Next.TradeId=R.ReadU32(); Next.bTradeOpen=true;
    }
    else if(Status==WowTradeStatus::TRADE_ACCEPT) Next.bTargetAccepted=true;
    else if(Status==WowTradeStatus::UNACCEPTED) Next.bTargetAccepted=false;
    else if(Status==WowTradeStatus::BACK_TO_TRADE) { Next.bLocalAccepted=false; Next.bTargetAccepted=false; }
    else if(Status==22 || Status==23)
    {
        Next.ErrorSlot=R.ReadU8(); Next.PendingKind=0; Next.PendingGuid=0; Next.PendingSlot=-1;
        if(Next.ErrorSlot<7) Next.LocalGuids[Next.ErrorSlot]=0;
        else if(Next.ErrorSlot==255) Next.PlayerMoney=0;
        Next.bLocalAccepted=Next.bTargetAccepted=false;
    }
    else
    {
        if(Status==WowTradeStatus::CLOSE_WINDOW) { Next.Result=R.ReadU32(); Next.TargetResult=R.ReadU8()!=0; Next.LimitedItem=R.ReadU32(); }
        Next.bTradeOpen=false; Next.TraderGuid=0; Next.bLocalAccepted=Next.bTargetAccepted=false;
        Next.PlayerMoney=Next.TargetMoney=Next.PlayerSpell=Next.TargetSpell=0;
        Next.PlayerItems.Empty(); Next.TargetItems.Empty(); for(auto& Guid:Next.LocalGuids) Guid=0;
        Next.PendingKind=0; Next.PendingGuid=0; Next.PendingSlot=-1;
        Next.Split=FWowTradeState::FSplit(); Next.ReturnCursorGuid=0;
    }
    CurrentTrade=MoveTemp(Next); OnTradeUpdated.Broadcast(CurrentTrade);
}

void FWowPacketHandler::HandleTradeStatusExtended(FPacketReader& R)
{
    // These words are state indices, not counts. The remaining packet is a sparse
    // list of complete 73-byte slot records, including empty records.
    if(!R.CanRead(21) || (R.Remaining()-21)%73 || R.Remaining()>21+7*73 || !CurrentTrade.bTradeOpen) return;
    const uint8 Side=R.ReadU8(); const uint32 Id=R.ReadU32(),Revision=R.ReadU32(),State=R.ReadU32();
    const uint32 Money=R.ReadU32(),Spell=R.ReadU32();
    if(Side>1 || Id!=CurrentTrade.TradeId || Revision<CurrentTrade.SnapshotRevision[Side]) return;
    TArray<FWowTradeItem> Items; Items.SetNum(7); uint8 Seen=0;
    while(R.Remaining())
    {
        FWowTradeItem Item; Item.Slot=R.ReadU8();
        if(Item.Slot>=7 || (Seen&(1<<Item.Slot))) return;
        Seen|=uint8(1<<Item.Slot);
        Item.ItemId=R.ReadU32(); Item.DisplayId=R.ReadU32(); Item.Count=R.ReadU32(); Item.bWrapped=R.ReadU32()!=0;
        Item.GiftCreatorGuid=R.ReadU64(); Item.PermanentEnchantId=R.ReadU32(); Item.GemEnchantId1=R.ReadU32(); Item.GemEnchantId2=R.ReadU32(); Item.GemEnchantId3=R.ReadU32();
        Item.CreatorGuid=R.ReadU64(); Item.Charges=R.ReadU32(); Item.SuffixFactor=R.ReadU32(); Item.RandomPropertyId=int32(R.ReadU32());
        Item.LockId=R.ReadU32(); Item.MaxDurability=R.ReadU32(); Item.Durability=R.ReadU32(); Items[Item.Slot]=Item;
    }
    auto& T=CurrentTrade; const bool WasAccepted=T.bLocalAccepted; T.SnapshotRevision[Side]=Revision; T.StateIndex[Side]=State; T.bLocalAccepted=T.bTargetAccepted=false;
    if(WasAccepted) OnSendPacket.ExecuteIfBound(0x011B,{});
    if(Side) { T.TargetMoney=Money; T.TargetSpell=Spell; T.TargetItems=MoveTemp(Items); }
    else
    {
        T.PlayerMoney=Money; T.PlayerSpell=Spell; T.PlayerItems=MoveTemp(Items);
        for(int32 I=0;I<7;++I) if(const auto* Item=EntityManager.FindItem(T.LocalGuids[I]); !Item || Item->Entry!=T.PlayerItems[I].ItemId || uint32(Item->GetStackCount())!=T.PlayerItems[I].Count) T.LocalGuids[I]=0;
    }
    OnTradeUpdated.Broadcast(T);
}

// ── Bank system handlers ────────────────────────────────────────────

void FWowPacketHandler::HandleShowBank(FPacketReader& R)
{
    // SMSG_SHOW_BANK
    // uint64 bankerGuid

    if (!R.CanRead(8))
    {
        UE_LOG(LogWowPacket, Warning, TEXT("SHOW_BANK: packet too short"));
        return;
    }

    uint64 BankerGuid = R.ReadU64();
    CurrentBankerGuid=BankerGuid;

    UE_LOG(LogWowPacket, Log, TEXT("SHOW_BANK: Bank opened by banker GUID %llu"), BankerGuid);
    OnBankOpened.Broadcast(BankerGuid);
}
