#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
#include "WowConnectionManager.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FWire
{
    TArray<uint8> Data;
    template<typename T> FWire& Add(T Value) { Data.Append(reinterpret_cast<const uint8*>(&Value),sizeof(Value)); return *this; }
    FWire& Text(const char* Value) { Data.Append(reinterpret_cast<const uint8*>(Value),FCStringAnsi::Strlen(Value)+1); return *this; }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayQuestPackets,"WowUnreal.Gameplay.QuestPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayQuestPackets::RunTest(const FString&)
{
    FWowPacketHandler Handler;
    const uint64 Guid=0xF130000337000001ULL;
    FWire Details;
    Details.Add(Guid).Add(uint64(0x0102030405060708)).Add(uint32(783)).Text("A Threat Within").Text("Speak with Marshal McBride.").Text("Find the marshal.")
        .Add(uint8(1)).Add(uint32(0)).Add(uint32(0)).Add(uint8(0))
        .Add(uint32(2)).Add(uint32(80)).Add(uint32(1)).Add(uint32(100)).Add(uint32(6070)).Add(uint32(1)).Add(uint32(200))
        .Add(uint32(1)).Add(uint32(117)).Add(uint32(4)).Add(uint32(300)).Add(uint32(42)).Add(uint32(100));
    int32 Calls=0; Handler.OnQuestDialog.AddLambda([&Calls](const auto&){++Calls;});
    Handler.HandlePacket(WowOpcode::SMSG_QUESTGIVER_QUEST_DETAILS,Details.Data);
    TestEqual(TEXT("Divider GUID does not shift quest id"),Handler.QuestDetails.QuestId,uint32(783));
    TestEqual(TEXT("Actual title is preserved"),Handler.QuestDetails.Title,FString(TEXT("A Threat Within")));
    TestEqual(TEXT("Choice rewards are parsed"),Handler.QuestDetails.ChoiceRewards.Num(),2);
    TestEqual(TEXT("Money follows both reward arrays"),Handler.QuestDetails.RewardMoney,uint32(42));
    Details.Data.SetNum(Details.Data.Num()-2);
    Handler.HandlePacket(WowOpcode::SMSG_QUESTGIVER_QUEST_DETAILS,Details.Data);
    TestEqual(TEXT("Truncated packet does not replace a valid dialog"),Calls,1);
    const auto Accept=UWowConnectionManager::BuildQuestAcceptPacket(1,7);
    const TArray<uint8> Expected={1,0,0,0,0,0,0,0,7,0,0,0,0,0,0,0};
    TestTrue(TEXT("Accept includes the server's final uint32 field"),Accept==Expected);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayGossipPackets,"WowUnreal.Gameplay.GossipPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayGossipPackets::RunTest(const FString&)
{
    FWowPacketHandler Handler;
    FWire Menu;
    Menu.Add(uint64(123)).Add(uint32(99)).Add(uint32(777)).Add(uint32(1))
        .Add(uint32(8)).Add(uint8(0)).Add(uint8(0)).Add(uint32(0)).Text("Tell me more.").Text("")
        .Add(uint32(1)).Add(uint32(783)).Add(uint32(2)).Add(uint32(1)).Add(uint32(0)).Add(uint8(0)).Text("A Threat Within");
    Handler.HandlePacket(WowOpcode::SMSG_GOSSIP_MESSAGE,Menu.Data);
    TestEqual(TEXT("Menu and text ids retain wire order"),Handler.GossipMenu.MenuId,uint32(99));
    TestEqual(TEXT("Text id"),Handler.GossipMenu.TextId,uint32(777));
    TestEqual(TEXT("Conversation option is retained for UI"),Handler.GossipMenu.Options.Num(),1);
    TestEqual(TEXT("Quest option is retained for UI"),Handler.GossipMenu.Quests.Num(),1);
    FWire QuestList;
    QuestList.Add(uint64(123)).Text("Welcome").Add(uint32(0)).Add(uint32(0)).Add(uint8(1))
        .Add(uint32(7)).Add(uint32(2)).Add(uint32(2)).Add(uint32(0)).Add(uint8(0)).Text("Kobold Camp Cleanup");
    Handler.HandlePacket(WowOpcode::SMSG_QUESTGIVER_QUEST_LIST,QuestList.Data);
    TestTrue(TEXT("Questgiver-only list also opens a selectable quest"),Handler.GossipMenu.Quests.Num()==1 && Handler.GossipMenu.Quests[0].Id==7);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayLootPackets,"WowUnreal.Gameplay.LootAcknowledgements",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayLootPackets::RunTest(const FString&)
{
    FWowPacketHandler Handler;
    FWire Loot;
    const uint64 Guid=0xF13000000600ABCDULL;
    Loot.Add(Guid).Add(uint8(1)).Add(uint32(19)).Add(uint8(2));
    for (uint8 Slot : {uint8(2),uint8(7)}) Loot.Add(Slot).Add(uint32(100+Slot)).Add(uint32(3)).Add(uint32(88)).Add(uint32(0)).Add(uint32(0)).Add(uint8(0));
    Handler.HandlePacket(WowOpcode::SMSG_LOOT_RESPONSE,Loot.Data);
    TestEqual(TEXT("Loot GUID is fixed-width, not packed"),Handler.Loot.Guid,Guid);
    TestEqual(TEXT("Non-contiguous loot slots retained"),Handler.Loot.Items.Num(),2);
    TestEqual(TEXT("Money parsed"),Handler.Loot.Gold,uint32(19));
    FWowLootItem Permission;
    Permission.SlotType=4; TestTrue(TEXT("Solo owner loot can be collected"),Permission.CanLoot());
    Permission.SlotType=1; TestFalse(TEXT("Ongoing group roll cannot be collected"),Permission.CanLoot());
    Handler.HandlePacket(0x0162,{2});
    TestTrue(TEXT("Only acknowledged slot disappears"),Handler.Loot.Items.Num()==1 && Handler.Loot.Items[0].Index==7);
    Handler.HandlePacket(0x0165,{});
    TestEqual(TEXT("Money acknowledgement clears currency"),Handler.Loot.Gold,uint32(0));
    FWire Release; Release.Add(Guid).Add(uint8(1));
    Handler.HandlePacket(WowOpcode::SMSG_LOOT_RELEASE_RESPONSE,Release.Data);
    TestEqual(TEXT("Close acknowledgement releases corpse"),Handler.Loot.Guid,uint64(0));
    TestEqual(TEXT("Close is distinct from looting money"),int32(WowOpcode::CMSG_LOOT_RELEASE),0x015F);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayQuestLog,"WowUnreal.Gameplay.QuestLogAndActions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayQuestLog::RunTest(const FString&)
{
    TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    auto& Handler=Connection->PacketHandler;
    Handler.EntityManager.LocalPlayerGuid=1;
    auto& Player=Handler.EntityManager.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    Player.SetField(PlayerField::BASE+0x000A,783);
    Handler.RefreshQuestLog();
    TestTrue(TEXT("Accepted quest is derived from authoritative player slots"),Handler.QuestLog.Num()==1 && Handler.QuestLog[0].QuestId==783);
    Player.SetField(PlayerField::BASE+0x000B,1); Handler.RefreshQuestLog();
    TestEqual(TEXT("Completion state follows fields"),Handler.QuestLog[0].State,uint32(1));
    Player.SetField(PlayerField::BASE+0x000A,0); Handler.RefreshQuestLog();
    TestEqual(TEXT("Abandoned quest is removed"),Handler.QuestLog.Num(),0);
    Player.SetField(UnitField::BYTES_2,17u<<24);
    TestEqual(TEXT("Keyboard 1 invokes the visible battle stance action"),Connection->GetVisibleActionSlot(0),72);
    Player.SetField(UnitField::BYTES_2,18u<<24);
    TestEqual(TEXT("Keyboard page changes with stance"),Connection->GetVisibleActionSlot(1),85);
    TestEqual(TEXT("NPC interaction flags use WotLK offset"),int32(UnitField::NPC_FLAGS),int32(ObjectField::END+0x004C));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayItemTemplate,"WowUnreal.Gameplay.ItemTemplate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayItemTemplate::RunTest(const FString&)
{
    FWowPacketHandler Handler;
    FWire Item;
    Item.Add(uint32(1367)).Add(uint32(4)).Add(uint32(1)).Add(uint32(0)).Text("Ragged Leather Boots").Text("").Text("").Text("")
        .Add(uint32(14454)).Add(uint32(0)).Add(uint32(0)).Add(uint32(0)).Add(uint32(100)).Add(uint32(20))
        .Add(uint32(8)).Add(uint32(0xffffffff)).Add(uint32(0xffffffff)).Add(uint32(2)).Add(uint32(1));
    Handler.HandlePacket(WowOpcode::SMSG_ITEM_QUERY_SINGLE_RESPONSE,Item.Data);
    const auto* Template=Handler.ItemTemplates.Find(1367);
    TestTrue(TEXT("Item metadata supplies its actual name and equipment type"),Template && Template->Name==TEXT("Ragged Leather Boots") && Template->InventoryType==8 && Template->RequiredLevel==1);
    return true;
}
#endif
