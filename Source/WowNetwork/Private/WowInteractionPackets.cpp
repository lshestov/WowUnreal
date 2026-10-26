#include "WowPacketHandler.h"
#include "WowUpdateFields.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowInteraction, Log, All);

namespace
{
bool ReadQuests(FPacketReader& R, uint32 Count, TArray<FWowGossipQuest>& Quests)
{
    if (Count > 64) return false;
    for (uint32 I=0; I<Count; ++I)
    {
        if (!R.CanRead(18)) return false;
        FWowGossipQuest Q;
        Q.Id=R.ReadU32(); Q.Icon=R.ReadU32(); Q.Level=static_cast<int32>(R.ReadU32());
        Q.Flags=R.ReadU32(); Q.bRepeatable=R.ReadU8()!=0; Q.Title=R.ReadCString();
        Quests.Add(MoveTemp(Q));
    }
    return true;
}
bool ReadRewards(FPacketReader& R, FWowQuestDetails& Quest)
{
    for (auto* Items : {&Quest.ChoiceRewards, &Quest.RewardItems})
    {
        if (!R.CanRead(4)) return false;
        const uint32 Count=R.ReadU32();
        if (Count>6 || !R.CanRead(Count*12)) return false;
        for (uint32 I=0; I<Count; ++I)
        {
            FWowQuestRewardItem Item;
            Item.ItemId=R.ReadU32(); Item.Count=R.ReadU32(); Item.DisplayId=R.ReadU32(); Items->Add(Item);
        }
    }
    if (!R.CanRead(8)) return false;
    Quest.RewardMoney=R.ReadU32(); Quest.RewardXP=R.ReadU32(); return true;
}
}

void FWowPacketHandler::HandleGossipMessage(FPacketReader& R)
{
    if (!R.CanRead(20)) return;
    FWowGossipMenu Menu;
    Menu.Guid=R.ReadU64(); Menu.MenuId=R.ReadU32(); Menu.TextId=R.ReadU32();
    const uint32 Count=R.ReadU32();
    if (Count>64) return;
    for (uint32 I=0; I<Count; ++I)
    {
        if (!R.CanRead(12)) return;
        FWowGossipOption Option;
        Option.Id=R.ReadU32(); Option.Icon=R.ReadU8(); Option.bCoded=R.ReadU8()!=0; Option.Money=R.ReadU32();
        Option.Text=R.ReadCString(); Option.Confirmation=R.ReadCString(); Menu.Options.Add(MoveTemp(Option));
    }
    if (!R.CanRead(4) || !ReadQuests(R,R.ReadU32(),Menu.Quests)) return;
    GossipMenu=MoveTemp(Menu);
    UE_LOG(LogWowInteraction,Log,TEXT("Gossip: guid=%llu menu=%u options=%d quests=%d"),GossipMenu.Guid,GossipMenu.MenuId,GossipMenu.Options.Num(),GossipMenu.Quests.Num());
    OnGossipMessage.Broadcast(GossipMenu.Guid,GossipMenu.TextId,GossipMenu.MenuId);
    OnGossipMenu.Broadcast(GossipMenu);
}

void FWowPacketHandler::HandleQuestgiverQuestList(FPacketReader& R)
{
    if (!R.CanRead(9)) return;
    FWowGossipMenu Menu;
    Menu.Guid=R.ReadU64(); Menu.Greeting=R.ReadCString();
    if (!R.CanRead(9)) return;
    R.Skip(8); const uint8 Count=R.ReadU8();
    if (!ReadQuests(R,Count,Menu.Quests)) return;
    GossipMenu=MoveTemp(Menu);
    UE_LOG(LogWowInteraction,Log,TEXT("QuestList: guid=%llu quests=%d"),GossipMenu.Guid,GossipMenu.Quests.Num());
    OnGossipMenu.Broadcast(GossipMenu);
}

void FWowPacketHandler::HandleQuestgiverQuestDetails(FPacketReader& R)
{
    if (!R.CanRead(20)) return;
    FWowQuestDetails Quest;
    Quest.QuestGiverGuid=R.ReadU64(); R.ReadU64(); // divider GUID, present in 3.3.5a
    Quest.QuestId=R.ReadU32(); Quest.Title=R.ReadCString(); Quest.Details=R.ReadCString(); Quest.Objectives=R.ReadCString();
    if (!R.CanRead(10)) return;
    R.Skip(10); // activateAccept, flags, suggested players, finished
    if (!ReadRewards(R,Quest)) return;
    QuestDetails=MoveTemp(Quest);
    UE_LOG(LogWowInteraction,Log,TEXT("QuestDetails: id=%u title=%s choices=%d"),QuestDetails.QuestId,*QuestDetails.Title,QuestDetails.ChoiceRewards.Num());
    OnQuestDialog.Broadcast(QuestDetails);
}

void FWowPacketHandler::HandleQuestgiverOfferReward(FPacketReader& R)
{
    if (!R.CanRead(12)) return;
    FWowQuestDetails Quest;
    Quest.QuestGiverGuid=R.ReadU64(); Quest.QuestId=R.ReadU32(); Quest.Title=R.ReadCString(); Quest.Details=R.ReadCString();
    if (!R.CanRead(13)) return;
    R.Skip(9); const uint32 Emotes=R.ReadU32();
    if (Emotes>32 || !R.CanRead(Emotes*8)) return;
    R.Skip(Emotes*8);
    if (!ReadRewards(R,Quest)) return;
    QuestDetails=MoveTemp(Quest);
    UE_LOG(LogWowInteraction,Log,TEXT("QuestReward: id=%u choices=%d"),QuestDetails.QuestId,QuestDetails.ChoiceRewards.Num());
    OnQuestRewardDialog.Broadcast(QuestDetails);
}

void FWowPacketHandler::HandleLootResponse(FPacketReader& R)
{
    if (!R.CanRead(9)) return;
    FWowLootState State; State.Guid=R.ReadU64(); State.Type=R.ReadU8();
    if (!State.Type)
    {
        UE_LOG(LogWowInteraction,Warning,TEXT("Loot denied: guid=%llu reason=%u"),State.Guid,R.ReadU8());
        return;
    }
    if (!R.CanRead(5)) return;
    State.Gold=R.ReadU32(); const uint8 Count=R.ReadU8();
    if (!R.CanRead(Count*22)) return;
    for (uint32 I=0; I<Count; ++I)
    {
        FWowLootItem Item;
        Item.Index=R.ReadU8(); Item.ItemId=R.ReadU32(); Item.Count=R.ReadU32(); Item.DisplayId=R.ReadU32();
        Item.RandomSuffix=R.ReadU32(); Item.RandomProperty=R.ReadU32(); Item.SlotType=R.ReadU8();
        State.Items.Add(Item);
    }
    Loot=MoveTemp(State);
    UE_LOG(LogWowInteraction,Log,TEXT("LootOpened: guid=%llu gold=%u items=%d"),Loot.Guid,Loot.Gold,Loot.Items.Num());
    OnLootOpened.Broadcast(Loot.Guid,Loot.Type,Loot.Gold,Loot.Items);
}

void FWowPacketHandler::HandleLootRemoved(FPacketReader& R)
{
    if (!R.CanRead(1)) return;
    const uint8 Slot=R.ReadU8();
    Loot.Items.RemoveAll([Slot](const FWowLootItem& Item){return Item.Index==Slot;});
    OnLootOpened.Broadcast(Loot.Guid,Loot.Type,Loot.Gold,Loot.Items);
}

void FWowPacketHandler::HandleLootClearMoney(FPacketReader&)
{
    Loot.Gold=0;
    OnLootOpened.Broadcast(Loot.Guid,Loot.Type,Loot.Gold,Loot.Items);
}

void FWowPacketHandler::HandleLootReleaseResponse(FPacketReader& R)
{
    if (!R.CanRead(9)) return;
    const uint64 Guid=R.ReadU64(); const bool Released=R.ReadU8()!=0;
    if (Released && Guid==Loot.Guid) { Loot=FWowLootState(); OnLootClosed.Broadcast(); }
}

void FWowPacketHandler::HandleItemPushResult(FPacketReader& R)
{
    if (!R.CanRead(45)) return;
    const uint64 Guid=R.ReadU64(); R.Skip(12);
    const uint8 Bag=R.ReadU8(); const uint32 Slot=R.ReadU32(), Item=R.ReadU32(); R.Skip(8);
    const uint32 Count=R.ReadU32(), Total=R.ReadU32();
    UE_LOG(LogWowInteraction,Log,TEXT("ItemReceived: player=%llu item=%u count=%u total=%u bag=%u slot=%u"),Guid,Item,Count,Total,Bag,Slot);
}

void FWowPacketHandler::RefreshQuestLog()
{
    const auto* Player=EntityManager.GetLocalPlayer();
    if (!Player) return;
    TArray<FWowQuestLogEntry> Updated;
    bool Changed=false;
    for (int32 I=0; I<25; ++I)
    {
        const uint16 Field=PlayerField::BASE+0x000A+I*5;
        const uint32 Id=Player->GetField(Field);
        if (!Id) continue;
        FWowQuestLogEntry Quest;
        if (const auto* Old=QuestLog.FindByPredicate([Id](const auto& Q){return Q.QuestId==Id;})) Quest=*Old;
        Quest.QuestId=Id; Quest.State=Player->GetField(Field+1);
        Updated.Add(Quest);
    }
    Changed=Updated.Num()!=QuestLog.Num();
    for (int32 I=0; !Changed && I<Updated.Num(); ++I)
        Changed=Updated[I].QuestId!=QuestLog[I].QuestId || Updated[I].State!=QuestLog[I].State;
    if (Changed) { QuestLog=MoveTemp(Updated); OnQuestLogChanged.Broadcast(); }
}

void FWowPacketHandler::HandleItemQueryResponse(FPacketReader& R)
{
    if (!R.CanRead(16)) return;
    FWowItemTemplate Item;
    Item.Id=R.ReadU32(); if (Item.Id&0x80000000) return;
    Item.Class=R.ReadU32(); Item.Subclass=R.ReadU32(); R.Skip(4);
    Item.Name=R.ReadCString(); R.ReadCString(); R.ReadCString(); R.ReadCString();
    if (!R.CanRead(44)) return;
    Item.DisplayId=R.ReadU32(); Item.Quality=R.ReadU32(); Item.Flags=R.ReadU32(); Item.Flags2=R.ReadU32(); Item.BuyPrice=R.ReadU32(); Item.SellPrice=R.ReadU32();
    Item.InventoryType=R.ReadU32(); Item.AllowableClass=R.ReadU32(); Item.AllowableRace=R.ReadU32(); Item.ItemLevel=R.ReadU32(); Item.RequiredLevel=R.ReadU32();
    // The remainder follows AzerothCore's WotLK SMSG_ITEM_QUERY_SINGLE_RESPONSE.
    if (R.CanRead(44))
    {
        Item.RequiredSkill=R.ReadU32(); Item.RequiredSkillRank=R.ReadU32(); Item.RequiredSpell=R.ReadU32(); R.Skip(8);
        Item.RequiredReputationFaction=R.ReadU32(); Item.RequiredReputationRank=R.ReadU32(); R.Skip(4);
        Item.MaxStack=FMath::Max(int32(R.ReadU32()),1); Item.ContainerSlots=R.ReadU32();
        const uint32 Stats=R.ReadU32();
        if (Stats>10 || !R.CanRead(Stats*8+261)) return;
        for(uint32 I=0; I<Stats; ++I) { const uint32 Type=R.ReadU32(); Item.Stats.Emplace(Type,int32(R.ReadU32())); }
        R.Skip(8); Item.DamageMin=R.ReadFloat(); Item.DamageMax=R.ReadFloat(); R.Skip(16);
        Item.Armor=R.ReadU32(); R.Skip(24); Item.Delay=R.ReadU32(); R.Skip(8);
        for(int32 I=0; I<5; ++I)
        {
            const uint32 Spell=R.ReadU32(),Trigger=R.ReadU32(); R.Skip(16);
            if(Spell && (Trigger==0 || Trigger==5) && !Item.UseSpellId) Item.UseSpellId=Spell;
        }
        Item.Bonding=R.ReadU32(); Item.Description=R.ReadCString();
        if(!R.CanRead(64)) return;
        R.Skip(44); Item.MaxDurability=R.ReadU32(); R.Skip(8); Item.BagFamily=R.ReadU32();
    }
    const auto& Stored=ItemTemplates.Add(Item.Id,MoveTemp(Item));
    bool LootChanged=false;
    for (auto& LootItem : Loot.Items) if (LootItem.ItemId==Stored.Id) { LootItem.Name=Stored.Name; LootItem.Quality=Stored.Quality; LootChanged=true; }
    if (LootChanged) OnLootOpened.Broadcast(Loot.Guid,Loot.Type,Loot.Gold,Loot.Items);
    OnPlayerInventoryUpdate.Broadcast();
}
