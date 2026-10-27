#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
#include "WowConnectionManager.h"
#include "UObject/StrongObjectPtr.h"
#include "WowPcmSoundWave.h"
#include "Mpq/MpqManager.h"
#include "Formats/Dbc/DbcStore.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplaySpellRanks,"WowUnreal.Gameplay.SpellRankReplacement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplaySpellRanks::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& D=FDbcStore::Get(); if(!D.IsLoaded()) D.LoadAll(Mpq); if(!D.IsDeferredLoaded()) D.LoadDeferred(Mpq);
    FWowPacketHandler H; H.KnownSpells.Add(78); H.ActionButtons.Init(0,144); H.ActionButtons[72]=78; H.ActionButtons[10]=78; H.ActionButtons[11]=78|(128u<<24); H.ActionButtons[12]=78|(64u<<24);
    int32 Events=0; TArray<TArray<uint8>> Requests;
    H.OnActionButtonsUpdated.AddLambda([&]{++Events;}); H.OnSendPacket.BindLambda([&](uint32 Op,const TArray<uint8>& Data){if(Op==WowOpcode::CMSG_SET_ACTION_BUTTON)Requests.Add(Data);});
    FWire Learn; Learn.Add(uint32(284)).Add(uint16(0)); H.HandlePacket(WowOpcode::SMSG_LEARNED_SPELL,Learn.Data);
    TestTrue(TEXT("Learning the next rank updates spell buttons and retains downranking"),H.KnownSpells.Contains(78) && H.KnownSpells.Contains(284) && H.ActionButtons[72]==284 && H.ActionButtons[10]==284 && Events==1 && Requests.Num()==2);
    TestTrue(TEXT("Item and macro actions are never rewritten as spells"),H.ActionButtons[11]==(78|(128u<<24)) && H.ActionButtons[12]==(78|(64u<<24)));
    if(Requests.Num()==2) { FPacketReader R(Requests[0]); TestTrue(TEXT("Updated slots use exact server persistence packets"),Requests[0].Num()==5 && R.ReadU8()==10 && R.ReadU32()==284); }
    FWire Upgrade; Upgrade.Add(uint32(78)).Add(uint32(284)); H.HandlePacket(WowOpcode::SMSG_SUPERCEDED_SPELL,Upgrade.Data);
    TestTrue(TEXT("Superseded melee ranks are removed rather than exposed as castable"),!H.KnownSpells.Contains(78) && H.KnownSpells.Contains(284) && Requests.Num()==2);
    FWire Downgrade; Downgrade.Add(uint32(284)).Add(uint32(78)); H.HandlePacket(WowOpcode::SMSG_SUPERCEDED_SPELL,Downgrade.Data);
    TestTrue(TEXT("An authoritative downgrade restores buttons and removes the high rank"),!H.KnownSpells.Contains(284) && H.KnownSpells.Contains(78) && H.ActionButtons[72]==78 && Events==2 && Requests.Num()==4);
    auto Bad=Downgrade.Data; Bad.SetNum(7); H.HandlePacket(WowOpcode::SMSG_SUPERCEDED_SPELL,Bad); Bad=Downgrade.Data; Bad.Add(0); H.HandlePacket(WowOpcode::SMSG_SUPERCEDED_SPELL,Bad);
    TestTrue(TEXT("Malformed replacements cannot mutate state or send action writes"),Requests.Num()==4 && Events==2 && H.ActionButtons[72]==78);
    H.HandlePacket(WowOpcode::SMSG_LEARNED_SPELL,Learn.Data); const int32 Before=Requests.Num(); H.HandlePacket(WowOpcode::SMSG_LEARNED_SPELL,Learn.Data);
    TestEqual(TEXT("Duplicate learned packets do not rewrite unchanged actions"),Requests.Num(),Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayGlyphTargeting,"WowUnreal.Gameplay.GlyphTargeting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayGlyphTargeting::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& D=FDbcStore::Get(); if(!D.IsLoaded()) D.LoadAll(Mpq); if(!D.IsDeferredLoaded()) D.LoadDeferred(Mpq);
    TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>()); auto& H=Connection->PacketHandler; auto& E=H.EntityManager;
    E.LocalPlayerGuid=1; auto& Player=E.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    const uint64 Guid=0x4000000000000021ull; auto& Item=E.PromoteToTyped(Guid,WowTypeMask::OBJECT|WowTypeMask::ITEM); Item.SetField(ItemField::STACK_COUNT,2);
    Player.SetField(PlayerField::PACK_SLOT_START,uint32(Guid&0xffffffffull)); Player.SetField(PlayerField::PACK_SLOT_START+1,uint32(Guid>>32));
    Player.SetField(PlayerField::GLYPH_SLOTS_1,21); Player.SetField(PlayerField::GLYPH_SLOTS_1+1,22); Player.SetField(PlayerField::GLYPHS_ENABLED,3);
    TestTrue(TEXT("A glyph item enters targeting without consuming its stack"),Connection->BeginGlyphUse(255,23,Guid,58403) && Connection->HasPendingGlyph() && Item.GetField(ItemField::STACK_COUNT)==2);
    TestTrue(TEXT("Only matching slot types accept the selected glyph"),Connection->GlyphMatchesSocket(0) && !Connection->GlyphMatchesSocket(1) && !Connection->GlyphMatchesSocket(6));
    H.ActiveTalentGroup=1; TestFalse(TEXT("Switching specs invalidates the staged glyph use"),Connection->HasPendingGlyph()); H.ActiveTalentGroup=0;
    Player.SetField(PlayerField::PACK_SLOT_START,0); Player.SetField(PlayerField::PACK_SLOT_START+1,0);
    TestTrue(TEXT("Moving or replacing the item prevents applying a stale slot"),!Connection->HasPendingGlyph() && !Connection->GlyphMatchesSocket(0) && !Connection->PlaceGlyph(0));
    Connection->CancelGlyphUse(); TestTrue(TEXT("Cancellation clears targeting without inventory changes"),Connection->PendingGlyph.ItemGuid==0 && Item.GetField(ItemField::STACK_COUNT)==2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayGlyphPackets,"WowUnreal.Gameplay.GlyphPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayGlyphPackets::RunTest(const FString&)
{
    const uint64 Guid=0x4000000000000021ull; const auto Data=UWowConnectionManager::BuildUseItemPacket(255,23,Guid,58403,0,5); FPacketReader R(Data);
    TestTrue(TEXT("Glyph use retains the ordinary item-use header"),Data.Num()==24 && R.ReadU8()==255 && R.ReadU8()==23 && R.ReadU8()==0 && R.ReadU32()==58403 && R.ReadU64()==Guid);
    TestTrue(TEXT("Selected glyph socket is a full uint32 before cast flags and target mask"),R.ReadU32()==5 && R.ReadU8()==0 && R.ReadU32()==0);
    TestTrue(TEXT("An out-of-range socket cannot become an item-use packet"),UWowConnectionManager::BuildUseItemPacket(255,23,Guid,58403,0,6).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayTalentRequests,"WowUnreal.Gameplay.TalentRequests",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayTalentRequests::RunTest(const FString&)
{
    const uint64 Pet=0xf140000000000123ULL;
    const auto One=UWowConnectionManager::BuildLearnTalentPacket(124,2); FPacketReader R(One);
    TestTrue(TEXT("Single player rank request uses two uint32 fields"),One.Num()==8 && R.ReadU32()==124 && R.ReadU32()==2);
    const auto PetOne=UWowConnectionManager::BuildLearnTalentPacket(124,2,Pet); FPacketReader P(PetOne);
    TestTrue(TEXT("Pet rank request prefixes the pet GUID"),PetOne.Num()==16 && P.ReadU64()==Pet && P.ReadU32()==124 && P.ReadU32()==2);
    TArray<FWowTalentInfo> Talents={{124,2},{130,4}}; const auto Batch=UWowConnectionManager::BuildPreviewTalentsPacket(Talents); FPacketReader B(Batch);
    TestTrue(TEXT("Preview batch retains full-width count and zero-based ranks"),Batch.Num()==20 && B.ReadU32()==2 && B.ReadU32()==124 && B.ReadU32()==2 && B.ReadU32()==130 && B.ReadU32()==4);
    const auto PetBatch=UWowConnectionManager::BuildPreviewTalentsPacket(Talents,Pet); FPacketReader PB(PetBatch);
    TestTrue(TEXT("Pet preview prefix precedes the batch count"),PetBatch.Num()==28 && PB.ReadU64()==Pet && PB.ReadU32()==2);
    TestTrue(TEXT("Invalid ranks and empty batches cannot become packets"),UWowConnectionManager::BuildLearnTalentPacket(124,5).IsEmpty() && UWowConnectionManager::BuildPreviewTalentsPacket({}).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayTalentPackets,"WowUnreal.Gameplay.TalentPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayTalentPackets::RunTest(const FString&)
{
    FWowPacketHandler H; int32 Updates=0; H.OnTalentsUpdated.AddLambda([&](){++Updates;});
    FWire Player; Player.Add(uint8(0)).Add(uint32(7)).Add(uint8(2)).Add(uint8(1));
    Player.Add(uint8(2)).Add(uint32(123)).Add(uint8(0)).Add(uint32(456)).Add(uint8(4)).Add(uint8(6));
    for(uint16 I=101;I<=106;++I) Player.Add(I);
    Player.Add(uint8(1)).Add(uint32(123)).Add(uint8(1)).Add(uint8(6));
    for(const uint16 I:{201,0,203,0,205,0}) Player.Add(I);
    H.HandlePacket(WowOpcode::SMSG_TALENTS_INFO,Player.Data);
    TestTrue(TEXT("Player header identifies two specs, active spec and authoritative free points"),H.LastTalentPacketValid && H.HasTalentInfo && H.TalentGroups.Num()==2 && H.ActiveTalentGroup==1 && H.FreeTalentPoints==7);
    if(H.TalentGroups.Num()!=2) return false;
    TestTrue(TEXT("Both talent lists and each spec's six glyphs survive parsing"),H.TalentGroups[0].Talents.Num()==2 && H.TalentGroups[1].Talents.Num()==1 && H.TalentGroups[0].Glyphs[5]==106 && H.TalentGroups[1].Glyphs[2]==203);
    TestTrue(TEXT("Legacy active view uses zero-based wire ranks"),H.Talents.Num()==1 && H.Talents[0].TalentId==123 && H.Talents[0].Rank==1 && H.LastGlyphChanges.Num()==3);
    FWire Pet; Pet.Add(uint8(1)).Add(uint32(3)).Add(uint8(1)).Add(uint32(70)).Add(uint8(2)); H.HandlePacket(WowOpcode::SMSG_TALENTS_INFO,Pet.Data);
    TestTrue(TEXT("Pet updates preserve both player specs and player points"),H.HasPetTalentInfo && H.LastTalentUpdateIsPet && H.PetFreeTalentPoints==3 && H.PetTalents.Num()==1 && H.PetTalents[0].Rank==2 && H.ActiveTalentGroup==1 && H.FreeTalentPoints==7 && H.Talents.Num()==1);
    const uint64 Revision=H.TalentRevision; const int32 ValidUpdates=Updates;
    auto Truncated=Player.Data; Truncated.SetNum(Truncated.Num()-1); H.HandlePacket(WowOpcode::SMSG_TALENTS_INFO,Truncated);
    TestTrue(TEXT("Truncated glyph tail cannot replace valid state or fire its delegate"),!H.LastTalentPacketValid && H.TalentRevision==Revision && Updates==ValidUpdates && H.TalentGroups[1].Glyphs[2]==203);
    auto InvalidGroup=Player.Data; InvalidGroup[6]=2; H.HandlePacket(WowOpcode::SMSG_TALENTS_INFO,InvalidGroup);
    TestTrue(TEXT("Out-of-range active spec is rejected transactionally"),!H.LastTalentPacketValid && H.TalentRevision==Revision);
    FWire InvalidRank; InvalidRank.Add(uint8(1)).Add(uint32(4)).Add(uint8(1)).Add(uint32(70)).Add(uint8(5)); H.HandlePacket(WowOpcode::SMSG_TALENTS_INFO,InvalidRank.Data);
    TestTrue(TEXT("Invalid ranks cannot corrupt the pet cache"),!H.LastTalentPacketValid && H.PetFreeTalentPoints==3 && H.PetTalents[0].Rank==2);
    auto Switch=Player.Data; Switch[6]=0; H.HandlePacket(WowOpcode::SMSG_TALENTS_INFO,Switch);
    TestTrue(TEXT("Spec switches retain the previous group and changed glyph slots"),H.LastTalentPacketValid && H.LastTalentGroupChanged && H.PreviousTalentGroup==1 && H.ActiveTalentGroup==0 && H.Talents.Num()==2 && H.LastGlyphChanges.Num()==6);
    FWire Duplicate; Duplicate.Add(uint8(0)).Add(uint32(0)).Add(uint8(1)).Add(uint8(0)).Add(uint8(2)).Add(uint32(55)).Add(uint8(0)).Add(uint32(55)).Add(uint8(3)).Add(uint8(0));
    H.HandlePacket(WowOpcode::SMSG_TALENTS_INFO,Duplicate.Data);
    TestTrue(TEXT("Duplicate rank records coalesce to the highest rank"),H.LastTalentPacketValid && H.Talents.Num()==1 && H.Talents[0].Rank==3);
    H.ResetTalentState();
    TestTrue(TEXT("Character changes clear player, pet and glyph state together"),!H.HasTalentInfo && !H.HasPetTalentInfo && H.TalentGroups.IsEmpty() && H.PetTalents.IsEmpty() && H.Talents.IsEmpty() && !H.FreeTalentPoints && !H.PetFreeTalentPoints && H.LastGlyphChanges.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayChatAndFaction,"WowUnreal.Gameplay.ChatAndReputation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayChatAndFaction::RunTest(const FString&)
{
    const auto Payload=UWowConnectionManager::BuildChatPacket(TEXT("Привет"),7,TEXT("Smrleadhcwxg"),7);
    FPacketReader Reader(Payload); TestEqual(TEXT("Whisper type"),Reader.ReadU32(),7u); TestEqual(TEXT("Common language"),Reader.ReadU32(),7u);
    TestEqual(TEXT("Whisper target precedes the body"),Reader.ReadCString(),FString(TEXT("Smrleadhcwxg"))); TestEqual(TEXT("UTF-8 message body round trips"),Reader.ReadCString(),FString(TEXT("Привет")));
    const auto Long=UWowConnectionManager::BuildChatPacket(FString::ChrN(200,TCHAR(0x044f)),1,TEXT(""),7);
    TestEqual(TEXT("Byte limit never splits a Cyrillic code point"),Long.Num(),8+254+1);
    FWowPacketHandler Handler; FWire Factions; Factions.Add(uint32(2)).Add(uint8(1)).Add(int32(-300)).Add(uint8(3)).Add(int32(450)); Handler.HandlePacket(0x0122,Factions.Data);
    TestEqual(TEXT("Faction flags retain visibility and war state"),Handler.Reputations[1].Flags,uint8(3)); TestEqual(TEXT("Signed faction reputation is preserved"),Handler.Reputations[0].Standing,-300);
    FWire Update; Update.Add(float(0)).Add(uint8(0)).Add(uint32(1)).Add(uint32(0)).Add(int32(1000)); Handler.HandlePacket(0x0124,Update.Data);
    TestEqual(TEXT("Faction standing updates address the correct row"),Handler.Reputations[0].Standing,1000);
    Update.Data.SetNum(Update.Data.Num()-1); Handler.HandlePacket(0x0124,Update.Data); TestEqual(TEXT("Truncated standing cannot overwrite cached state"),Handler.Reputations[0].Standing,1000);
    FString LastSender,LastMessage; int32 ChatEvents=0;
    uint32 ChatAchievement=0;
    Handler.OnChatMessage.AddLambda([&](uint8,uint32,uint64,const FString& Name,const FString& Message,const FString&,uint32 Achievement) { LastSender=Name; LastMessage=Message; ChatAchievement=Achievement; ++ChatEvents; });
    FWire Gm; Gm.Add(uint8(1)).Add(uint32(7)).Add(uint64(1)).Add(uint32(0)).Add(uint32(7)).Text("Tester").Add(uint64(0)).Add(uint32(6)).Text("Hello").Add(uint8(3));
    Handler.HandlePacket(0x03B3,Gm.Data); TestEqual(TEXT("GM chat includes the explicit sender name"),LastSender,FString(TEXT("Tester"))); TestEqual(TEXT("GM chat body is aligned after the name and receiver"),LastMessage,FString(TEXT("Hello")));
    Gm.Data.SetNum(Gm.Data.Num()-2); Handler.HandlePacket(0x03B3,Gm.Data); TestEqual(TEXT("Truncated GM chat is rejected"),ChatEvents,1);
    FWire Earned; Earned.Add(uint8(48)).Add(uint32(0)).Add(uint64(1)).Add(uint32(0)).Add(uint64(0)).Add(uint32(3)).Text("$a").Add(uint8(0)).Add(uint32(6));
    Handler.HandlePacket(0x0096,Earned.Data); TestEqual(TEXT("Achievement chat preserves the appended achievement ID"),ChatAchievement,6u);
    Earned.Data.SetNum(Earned.Data.Num()-1); Handler.HandlePacket(0x0096,Earned.Data); TestEqual(TEXT("Truncated achievement chat never reaches the UI"),ChatEvents,2);
    FWire Name; Name.Add(uint8(1)).Add(uint8(1)).Add(uint8(0)).Text("Tester").Text("").Add(uint8(1)).Add(uint8(0)).Add(uint8(1)).Add(uint8(0));
    Handler.HandlePacket(0x0051,Name.Data); TestEqual(TEXT("A zero unknown flag identifies a successful name query"),Handler.PlayerNameCache.FindRef(1),FString(TEXT("Tester")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayQuestPackets,"WowUnreal.Gameplay.QuestPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayQuestPackets::RunTest(const FString&)
{
    const TArray<uint8> Utf8={0xd0,0x9f,0xd1,0x80,0xd0,0xb8,0xd0,0xb2,0xd0,0xb5,0xd1,0x82,0}; FPacketReader TextReader(Utf8);
    TestEqual(TEXT("Network strings decode UTF-8 for chat and localized quest text"),TextReader.ReadCString(),FString(TEXT("Привет")));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayQuestTemplate,"WowUnreal.Gameplay.QuestTemplate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayQuestTemplate::RunTest(const FString&)
{
    FWowPacketHandler Handler; FWire Wire;
    Wire.Add(uint32(783)).Add(uint32(2)).Add(int32(1)).Add(uint32(1)).Add(int32(12)).Add(uint32(0)).Add(uint32(1));
    for(int32 I=0; I<6; ++I) Wire.Add(uint32(0));
    Wire.Add(int32(42)); for(int32 I=0; I<6; ++I) Wire.Add(uint32(0));
    Wire.Add(uint32(0x1000)); for(int32 I=0; I<5; ++I) Wire.Add(uint32(0));
    for(int32 I=0; I<39; ++I) Wire.Add(uint32(0));
    Wire.Text("A Threat Within").Text("Speak with Marshal McBride.").Text("The server quest description.").Text("").Text("All objectives completed.");
    for(int32 I=0; I<4; ++I) Wire.Add(uint32(I==0?80:0)).Add(uint32(I==0?3:0)).Add(uint32(0)).Add(uint32(0));
    for(int32 I=0; I<6; ++I) Wire.Add(uint32(I==0?6948:0)).Add(uint32(I==0?1:0));
    Wire.Text("Kobold slain").Text("").Text("").Text("");
    int32 Updates=0; Handler.OnQuestLogChanged.AddLambda([&Updates](){++Updates;});
    Handler.HandlePacket(WowOpcode::SMSG_QUEST_QUERY_RESPONSE,Wire.Data);
    const auto* Quest=Handler.QuestTemplates.Find(783);
    TestTrue(TEXT("Quest query retains real title, level, reward and objectives"),Quest && Quest->Title==TEXT("A Threat Within") && Quest->Level==1 && Quest->Money==42 && Quest->Flags==0x1000 && Quest->CreatureId[0]==80 && Quest->CreatureCount[0]==3 && Quest->ItemId[0]==6948 && Quest->ObjectiveText[0]==TEXT("Kobold slain"));
    TestEqual(TEXT("Quest metadata notifies the journal"),Updates,1);
    Wire.Data.SetNum(100); Handler.HandlePacket(WowOpcode::SMSG_QUEST_QUERY_RESPONSE,Wire.Data);
    TestEqual(TEXT("Truncated query cannot replace valid metadata"),Updates,1);
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
    const uint32 LootRevision=Handler.Loot.Revision;
    for(int32 N=0;N<Loot.Data.Num();++N) {TArray<uint8> Cut(Loot.Data.GetData(),N); Handler.HandlePacket(WowOpcode::SMSG_LOOT_RESPONSE,Cut); TestEqual(TEXT("Every incomplete response preserves the previous complete window"),Handler.Loot.Revision,LootRevision);}
    auto Extra=Loot.Data; Extra.Add(0); Handler.HandlePacket(WowOpcode::SMSG_LOOT_RESPONSE,Extra);
    auto DuplicateSlots=Loot.Data; DuplicateSlots[36]=DuplicateSlots[14]; Handler.HandlePacket(WowOpcode::SMSG_LOOT_RESPONSE,DuplicateSlots);
    TestEqual(TEXT("Trailing bytes and duplicate server slots reject the complete response"),Handler.Loot.Revision,LootRevision);
    const uint32 FirstOpen=Handler.Loot.OpenSerial; Handler.HandlePacket(WowOpcode::SMSG_LOOT_RESPONSE,Loot.Data);
    TestTrue(TEXT("Another valid response for the same corpse starts a new stable set of display slots"),Handler.Loot.OpenSerial>FirstOpen && Handler.Loot.Items.Num()==2);
    FWowLootItem Permission;
    Permission.SlotType=4; TestTrue(TEXT("Solo owner loot can be collected"),Permission.CanLoot());
    Permission.SlotType=1; TestFalse(TEXT("Ongoing group roll cannot be collected"),Permission.CanLoot());
    Handler.HandlePacket(0x0162,{2});
    TestTrue(TEXT("Acknowledgement preserves all initial display rows and marks only the matching server slot"),Handler.Loot.Items.Num()==2 && Handler.Loot.Items[0].bLooted && !Handler.Loot.Items[1].bLooted && Handler.Loot.Items[1].Index==7);
    Handler.HandlePacket(0x0165,{});
    TestEqual(TEXT("Money acknowledgement clears currency"),Handler.Loot.Gold,uint32(0));
    TestTrue(TEXT("The cleared money row retains its original place"),Handler.Loot.HasMoneySlot);
    FWire Release; Release.Add(Guid).Add(uint8(1));
    Handler.HandlePacket(WowOpcode::SMSG_LOOT_RELEASE_RESPONSE,Release.Data);
    TestEqual(TEXT("Close acknowledgement releases corpse"),Handler.Loot.Guid,uint64(0));
    TestEqual(TEXT("Close is distinct from looting money"),int32(WowOpcode::CMSG_LOOT_RELEASE),0x015F);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayLootRollPackets,"WowUnreal.Gameplay.LootRollPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayLootRollPackets::RunTest(const FString&)
{
    FWowPacketHandler H; H.EntityManager.LocalPlayerGuid=1;
    const uint64 Guid=0x4000000000000015ULL; FWire Start;
    Start.Add(Guid).Add(uint32(0)).Add(uint32(7)).Add(uint32(1206)).Add(uint32(33)).Add(int32(-9)).Add(uint32(2)).Add(uint32(60000)).Add(uint8(7));
    for(int32 N=0;N<Start.Data.Num();++N) {TArray<uint8> Cut(Start.Data.GetData(),N); H.HandlePacket(0x02A1,Cut); TestEqual(TEXT("Truncated starts cannot create a partial roll"),H.LootRolls.Num(),0);}
    H.HandlePacket(0x02A1,Start.Data); auto* R=H.FindLootRoll(Guid,7);
    if(!TestNotNull(TEXT("A complete start creates a typed roll"),R)) return false;
    const uint32 Id=R->Id; TestTrue(TEXT("Server eligibility, random suffix and countdown are retained"),R->VoteMask==7 && R->RandomProperty==-9 && R->RandomSuffix==33 && R->Count==2 && R->MillisecondsLeft()>59000);
    FWire Vote; Vote.Add(uint64(0)).Add(uint32(7)).Add(uint64(1)).Add(uint32(1206)).Add(uint32(33)).Add(int32(-9)).Add(uint8(0)).Add(uint8(0)).Add(uint8(0));
    H.HandlePacket(0x02A2,Vote.Data);
    TestTrue(TEXT("AzerothCore empty-GUID need selection is resolved from the unique drop identity"),H.LootRolls[Id].Choice==1 && H.LootRollEvents.Num()==1 && H.LootRollEvents[0].Choice==1);
    FWire Won; Won.Add(Guid).Add(uint32(7)).Add(uint32(1206)).Add(uint32(33)).Add(int32(-9)).Add(uint64(1)).Add(uint8(42)).Add(uint8(1));
    const uint32 Before=H.LootRollRevision;
    for(int32 N=0;N<Won.Data.Num();++N) {TArray<uint8> Cut(Won.Data.GetData(),N); H.HandlePacket(0x029F,Cut); TestEqual(TEXT("Truncated result cannot close an active roll"),H.LootRollRevision,Before);}
    H.HandlePacket(0x029F,Won.Data); H.HandlePacket(0x029F,Won.Data);
    TestTrue(TEXT("Winner is authoritative and duplicate completion is ignored"),H.LootRolls[Id].Finished && H.LootRolls[Id].Winner==1 && H.LootRolls[Id].WinnerRoll==42 && H.LootRollEvents.Num()==2);
    H.ResetLootState(); H.HandlePacket(0x02A1,Start.Data); FWire Passed;
    TestTrue(TEXT("Session reset never reuses a public roll ID retained by an old UI context"),H.FindLootRoll(Guid,7)->Id>Id);
    Passed.Add(Guid).Add(uint32(7)).Add(uint32(1206)).Add(int32(-9)).Add(uint32(33)); H.HandlePacket(0x029E,Passed.Data);
    TestTrue(TEXT("All-passed packet has property before suffix, unlike the other roll packets"),H.FindLootRoll(Guid,7)->Finished && H.LootRollEvents.Last().Kind==2);
    H.PendingLootGuid=99; H.PlayerNameCache.Add(1,TEXT("Alpha")); H.PlayerNameCache.Add(2,TEXT("Beta")); FWire Master;
    Master.Add(uint8(2)).Add(uint64(1)).Add(uint64(2)); H.HandlePacket(0x02A4,Master.Data);
    TestTrue(TEXT("Master list belongs to the pending corpse before its loot response"),H.MasterLootGuid==99 && H.MasterLootCandidates==TArray<uint64>{1,2});
    const uint32 MasterRevision=H.MasterLootRevision; Master.Data.Add(0); H.HandlePacket(0x02A4,Master.Data);
    FWire Duplicate; Duplicate.Add(uint8(2)).Add(uint64(1)).Add(uint64(1)); H.HandlePacket(0x02A4,Duplicate.Data);
    TestEqual(TEXT("Trailing bytes and duplicate candidates cannot replace the list"),H.MasterLootRevision,MasterRevision);
    FWire Receipt; Receipt.Add(uint64(1)).Add(uint32(0)).Add(uint32(0)).Add(uint32(1)).Add(uint8(255)).Add(MAX_uint32).Add(uint32(1206)).Add(uint32(33)).Add(int32(-9)).Add(uint32(2)).Add(uint32(5));
    for(int32 N=0;N<Receipt.Data.Num();++N) {TArray<uint8> Cut(Receipt.Data.GetData(),N); H.HandlePacket(0x0166,Cut); TestEqual(TEXT("Incomplete item receipts cannot generate a loot message"),H.LootNotifications.Num(),0);}
    H.HandlePacket(0x0166,Receipt.Data);
    TestTrue(TEXT("Item receipt retains the awarded stack and signed property rather than total owned count"),H.LootNotifications.Num()==1 && H.LootNotifications[0].Count==2 && H.LootNotifications[0].RandomProperty==-9 && !H.LootNotifications[0].Received);
    FWire Money; Money.Add(uint32(19)).Add(uint8(0)); H.HandlePacket(0x0163,Money.Data);
    TestTrue(TEXT("Money notification preserves the actual group share"),H.LootNotifications.Num()==2 && H.LootNotifications[1].Money && H.LootNotifications[1].Count==19 && !H.LootNotifications[1].Sole);
    H.ResetLootState(); TestTrue(TEXT("Logout removes loot, rolls, errors and candidate state together"),!H.Loot.Open && H.LootRolls.IsEmpty() && H.LootRollEvents.IsEmpty() && H.MasterLootCandidates.IsEmpty());
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
    for(int32 I=0; I<7; ++I) Item.Add(uint32(0)); // requirements
    Item.Add(uint32(0)).Add(uint32(20)).Add(uint32(0)).Add(uint32(1)).Add(uint32(7)).Add(int32(5));
    Item.Add(uint32(0)).Add(uint32(0)); // scaling
    Item.Add(2.f).Add(3.f).Add(uint32(0)).Add(0.f).Add(0.f).Add(uint32(0));
    Item.Add(uint32(32)); for(int32 I=0; I<6; ++I) Item.Add(uint32(0));
    Item.Add(uint32(1500)).Add(uint32(0)).Add(0.f);
    for(int32 I=0; I<5; ++I) { Item.Add(uint32(I==0?6995:0)).Add(uint32(0)); for(int32 J=0; J<4; ++J) Item.Add(uint32(0)); }
    Item.Add(uint32(1)).Text("A server supplied description");
    for(int32 I=0; I<16; ++I) Item.Add(uint32(I==11?40:I==14?8:0));
    Handler.HandlePacket(WowOpcode::SMSG_ITEM_QUERY_SINGLE_RESPONSE,Item.Data);
    const auto* Template=Handler.ItemTemplates.Find(1367);
    TestTrue(TEXT("Item metadata supplies its actual name and equipment type"),Template && Template->Name==TEXT("Ragged Leather Boots") && Template->InventoryType==8 && Template->RequiredLevel==1);
    TestTrue(TEXT("Item tooltip and use metadata follow the complete WotLK response"),Template && Template->SellPrice==20 && Template->ItemLevel==2 && Template->MaxStack==20 && Template->Armor==32 && Template->Delay==1500 && Template->UseSpellId==6995 && Template->Description==TEXT("A server supplied description") && Template->MaxDurability==40 && Template->BagFamily==8 && Template->Stats.Num()==1 && Template->Stats[0].Key==7 && Template->Stats[0].Value==5);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayAuraPackets,"WowUnreal.Gameplay.AuraPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayAuraPackets::RunTest(const FString&)
{
    FWowPacketHandler H;
    auto& Unit=H.EntityManager.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    Unit.Auras[0].bActive=true; Unit.Auras[0].SpellId=999;
    FWire All;
    All.Add(uint8(1)).Add(uint8(1)); // packed target guid
    All.Add(uint8(5)).Add(uint32(6673)).Add(uint8(0x39)).Add(uint8(20)).Add(uint8(3)).Add(uint32(120000)).Add(uint32(90000));
    All.Add(uint8(31)).Add(uint32(772)).Add(uint8(0xA1)).Add(uint8(20)).Add(uint8(0)).Add(uint8(1)).Add(uint8(2)).Add(uint32(15000)).Add(uint32(10000));
    int32 Changes=0; H.OnUnitAuraUpdate.AddLambda([&](uint64 Guid){if(Guid==1) ++Changes;});
    H.HandlePacket(0x495,All.Data);
    TestFalse(TEXT("Aura snapshot clears absent old slots"),Unit.Auras[0].bActive);
    TestTrue(TEXT("Snapshot consumes both sparse slots and implicit self caster"),Unit.Auras[5].bActive && Unit.Auras[5].CasterGuid==1 && Unit.Auras[5].DurationMs==120000 && Unit.Auras[5].RemainingMs==90000);
    TestTrue(TEXT("External caster flag and timer remain byte-aligned"),Unit.Auras[31].bActive && Unit.Auras[31].CasterGuid==2 && Unit.Auras[31].RemainingMs==10000);
    FWire Remove; Remove.Add(uint8(1)).Add(uint8(1)).Add(uint8(5)).Add(uint32(0));
    H.HandlePacket(0x496,Remove.Data);
    TestTrue(TEXT("Single update removes only its slot and notifies the correct unit"),!Unit.Auras[5].bActive && Unit.Auras[31].bActive && Changes==2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTraversalMovementPackets,"WowUnreal.Traversal.MovementPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FTraversalMovementPackets::RunTest(const FString&)
{
    FWowMovementInfo Move;
    Move.Position=FVector(1,2,3); Move.Orientation=0;
    const TArray<uint8> Ground={1,1, 0,0,0,0, 0,0, 0xE8,3,0,0, 0,0,0x80,0x3F, 0,0,0,0x40, 0,0,0x40,0x40, 0,0,0,0, 0,0,0,0};
    TestTrue(TEXT("Ground packet matches WotLK packed GUID and millisecond layout"),UWowConnectionManager::BuildMovementPacket(1,Move,1000)==Ground);
    Move.MoveFlags=0x1001; Move.FallTime=250; Move.JumpZSpeed=6; Move.JumpSinAngle=-1; Move.JumpCosAngle=0; Move.JumpXYSpeed=7;
    const auto Jump=UWowConnectionManager::BuildMovementPacket(1,Move,1250);
    TestEqual(TEXT("Falling adds all four required jump floats"),Jump.Num(),Ground.Num()+16);
    FPacketReader Reader(Jump); Reader.Skip(Ground.Num()-4);
    TestEqual(TEXT("Actual fall time is retained"),Reader.ReadU32(),250u);
    TestEqual(TEXT("Vertical speed is in yards per second"),Reader.ReadFloat(),6.f);
    TestEqual(TEXT("WoW horizontal sine"),Reader.ReadFloat(),-1.f);
    TestEqual(TEXT("WoW horizontal cosine"),Reader.ReadFloat(),0.f);
    TestEqual(TEXT("Horizontal speed"),Reader.ReadFloat(),7.f);
    TestEqual(TEXT("Server consumes every byte of a jump packet"),Reader.Remaining(),0);
    Move.MoveFlags=0x200000; Move.Pitch=.5f;
    const auto Swim=UWowConnectionManager::BuildMovementPacket(1,Move,1500);
    TestEqual(TEXT("Swimming includes pitch before fall time"),Swim.Num(),Ground.Num()+4);
    FPacketReader SwimReader(Swim); SwimReader.Skip(Ground.Num()-4);
    TestEqual(TEXT("Pitch does not shift fall time"),SwimReader.ReadFloat(),.5f);
    TestEqual(TEXT("Fall time after pitch"),SwimReader.ReadU32(),250u);
    TestEqual(TEXT("Keep-alive is the client opcode, not the server cheat opcode"),int32(WowOpcode::CMSG_KEEP_ALIVE),0x407);
    FWowPacketHandler Handler;
    TArray<uint8> Response;
    Handler.OnSendPacket.BindLambda([&Response](uint32 Opcode,const TArray<uint8>& Data){if(Opcode==WowOpcode::CMSG_TIME_SYNC_RESP)Response=Data;});
    const uint32 Before=uint32(uint64(FPlatformTime::Seconds()*1000.0));
    Handler.HandlePacket(WowOpcode::SMSG_TIME_SYNC_REQ,{9,0,0,0});
    const uint32 After=uint32(uint64(FPlatformTime::Seconds()*1000.0));
    FPacketReader TimeReader(Response);
    TestEqual(TEXT("Time sync echoes the server counter"),TimeReader.ReadU32(),9u);
    TestTrue(TEXT("Time sync uses the movement millisecond clock"),uint32(TimeReader.ReadU32()-Before)<=uint32(After-Before));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiCursorAndAudio,"WowUnreal.Gameplay.CursorAndAudio",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FUiCursorAndAudio::RunTest(const FString&)
{
    auto* Connection=NewObject<UWowConnectionManager>(); TArray<bool> States;
    auto& Entities=Connection->PacketHandler.EntityManager; Entities.LocalPlayerGuid=1;
    auto& Player=Entities.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    auto& CursorItem=Entities.PromoteToTyped(77,WowTypeMask::OBJECT|WowTypeMask::ITEM); CursorItem.Entry=6948; CursorItem.SetField(ItemField::STACK_COUNT,1);
    Player.SetField(PlayerField::PACK_SLOT_START,77);
    Connection->OnCursorChanged.AddLambda([&](){States.Add(Connection->HasCursorPayload());});
    Connection->PickupBagItemCursor(255,23,6948); Connection->PlaceCursorIntoActionSlot(11);
    TestTrue(TEXT("Pickup shows empty buttons; placing an item clears the cursor without moving the physical item"),States==TArray<bool>({true,false}) && Connection->PacketHandler.ActionButtons[11]==0x80001B24);
    auto Pcm=MakeShared<TArray<uint8>,ESPMode::ThreadSafe>(); Pcm->Append({1,2,3,4});
    auto* Wave=NewObject<UWowPcmSoundWave>(); Wave->Pcm=Pcm;
    uint8 Buffer[8];
    TestEqual(TEXT("Finite audio always completes the mixer render quantum"),Wave->GeneratePCMData(Buffer,4),8);
    TestTrue(TEXT("Final PCM samples are followed by silence"),Buffer[0]==1 && Buffer[3]==4 && Buffer[4]==0 && Buffer[7]==0);
    TestEqual(TEXT("Subsequent requests cannot spin on a zero-byte buffer"),Wave->GeneratePCMData(Buffer,4),8);
    TestTrue(TEXT("Finished audio is silent"),Buffer[0]==0 && Buffer[7]==0);
    auto* Loop=NewObject<UWowPcmSoundWave>(); Loop->Pcm=Pcm; Loop->bRepeat=true; Loop->GeneratePCMData(Buffer,4);
    TestTrue(TEXT("Ambient loops cross the buffer boundary without a gap"),Buffer[0]==1 && Buffer[4]==1 && Buffer[7]==4);
    return true;
}
#endif
