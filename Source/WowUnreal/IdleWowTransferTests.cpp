#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFarMapTransfer,"WowUnreal.Gameplay.FarMapTransfer",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FFarMapTransfer::RunTest(const FString&)
{
    FWowPacketHandler H; H.EntityManager.LocalPlayerGuid=1;
    H.EntityManager.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    H.EntityManager.PromoteToTyped(2,WowTypeMask::OBJECT|WowTypeMask::UNIT);
    H.EntityManager.PromoteToTyped(3,WowTypeMask::OBJECT|WowTypeMask::ITEM);
    H.EntityManager.PromoteToTyped(4,WowTypeMask::OBJECT|WowTypeMask::ITEM|WowTypeMask::CONTAINER);
    H.QuestLog.AddDefaulted(); H.WorldStates.Add(123,1); H.CurrentMapId=0;
    TArray<uint8> B; const auto Add=[&](auto V) {B.Append(reinterpret_cast<const uint8*>(&V),sizeof(V));}; Add(uint32(1)); Add(float(10)); Add(float(20)); Add(float(30)); Add(float(1.25));
    int32 Transfers=0; H.OnMapTransfer.AddLambda([&](uint32 Map,float X,float Y,float Z,float Facing)
    {++Transfers; TestTrue(TEXT("Delegates see the accepted map and position before acknowledging"),Map==1 && H.CurrentMapId==1 && X==10 && Y==20 && Z==30 && Facing==1.25f && H.EntityManager.GetLocalPlayer()->Movement.Position==FVector(10,20,30));});
    for(int32 I=0;I<B.Num();++I) {auto Prefix=B; Prefix.SetNum(I); H.HandlePacket(WowOpcode::SMSG_NEW_WORLD,Prefix);}
    auto Trailing=B; Trailing.Add(0); H.HandlePacket(WowOpcode::SMSG_NEW_WORLD,Trailing);
    auto Invalid=B; const uint32 Nan=0x7FC00000; FMemory::Memcpy(Invalid.GetData()+4,&Nan,4); H.HandlePacket(WowOpcode::SMSG_NEW_WORLD,Invalid);
    TestTrue(TEXT("Malformed transfers cannot change map, visible objects, quests or callbacks"),!Transfers && !H.CurrentMapId && H.EntityManager.Find(2) && H.QuestLog.Num()==1);
    H.HandlePacket(WowOpcode::SMSG_NEW_WORLD,B);
    TestTrue(TEXT("A far transfer retains the player, inventory containers and quests, and removes old visible units/world state"),Transfers==1 && H.EntityManager.Find(1) && !H.EntityManager.Find(2) && H.EntityManager.FindItem(3) && H.EntityManager.FindContainer(4) && H.QuestLog.Num()==1 && H.WorldStates.IsEmpty());
    return true;
}
#endif
