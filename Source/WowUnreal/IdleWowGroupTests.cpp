#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FGroupWire
{
    TArray<uint8> Bytes;
    template<typename T> FGroupWire& Add(T V) {Bytes.Append(reinterpret_cast<const uint8*>(&V),sizeof(V)); return *this;}
    FGroupWire& Text(const char* V) {Bytes.Append(reinterpret_cast<const uint8*>(V),FCStringAnsi::Strlen(V)+1); return *this;}
    FGroupWire& Packed(uint64 Guid)
    {const int32 At=Bytes.Add(0); for(int32 I=0;I<8;++I) if(const uint8 B=uint8(Guid>>(I*8))) {Bytes[At]|=1<<I; Bytes.Add(B);} return *this;}
};
FGroupWire GroupRoster()
{
    FGroupWire R; R.Add(uint8(2)).Add(uint8(0)).Add(uint8(1)).Add(uint8(2)).Add(uint64(123)).Add(uint32(1)).Add(uint32(2));
    R.Text("Second").Add(uint64(2)).Add(uint8(1)).Add(uint8(0)).Add(uint8(1)).Add(uint8(4));
    R.Text("Third").Add(uint64(3)).Add(uint8(1)).Add(uint8(1)).Add(uint8(2)).Add(uint8(8));
    R.Add(uint64(1)).Add(uint8(2)).Add(uint64(3)).Add(uint8(3)).Add(uint8(1)).Add(uint8(2)).Add(uint8(1)); return R;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroupRosterPackets,"WowUnreal.Gameplay.GroupRosterPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGroupRosterPackets::RunTest(const FString&)
{
    FWowPacketHandler H; H.EntityManager.LocalPlayerGuid=1; int32 Events=0,Stats=0,Names=0,WorldCreates=0;
    H.OnGroupUpdated.AddLambda([&]{++Events;}); H.EntityManager.OnEntityCreated.AddLambda([&](const auto&){++WorldCreates;});
    H.OnSendPacket.BindLambda([&](uint32 Op,const TArray<uint8>& Data){if(Op==0x27F && Data.Num()==8) ++Stats; if(Op==0x50 && Data.Num()==8) ++Names;});
    H.PlayerIdentities.Add(2,{1,1,8,TEXT("")}); const auto Wire=GroupRoster(); H.HandlePacket(0x7D,Wire.Bytes);
    TestTrue(TEXT("Roster retains subgroup, leader, loot and difficulty state"),H.GroupInfo.IsRaidGroup() && H.GroupInfo.MemberCount==3 && H.GroupInfo.LeaderGuid==1 && H.GroupInfo.LootMethod==2 && H.GroupInfo.MasterLooter==3 && H.GroupInfo.RaidDifficulty==2 && H.GroupInfo.RaidHeroic==1);
    TestTrue(TEXT("Party subset includes only the local raid subgroup"),H.GroupInfo.PartyCount()==1 && H.GroupInfo.PartyMember(0)->Guid==2 && !H.GroupInfo.PartyMember(1));
    TestTrue(TEXT("New group members request real remote stats and identity once"),Stats==2 && Names==2 && Events==1);
    TestTrue(TEXT("Remote unit views never enter the streamed entity manager"),H.RemoteGroupMembers.Num()==2 && H.FindUiUnit(2) && !H.EntityManager.Find(2) && WorldCreates==0);
    TestEqual(TEXT("Queried class identity initializes the remote player"),H.FindUiUnit(2)->GetFieldByte(UnitField::BYTES_0,1),uint8(8));
    const uint32 Revision=H.GroupRevision;
    for(int32 N=0;N<Wire.Bytes.Num();++N) {TArray<uint8> Cut(Wire.Bytes.GetData(),N); H.HandlePacket(0x7D,Cut); TestEqual(TEXT("Every truncated roster preserves the last complete group"),H.GroupRevision,Revision);}
    auto Excess=Wire.Bytes; const uint32 Count=MAX_uint32; FMemory::Memcpy(Excess.GetData()+16,&Count,4); H.HandlePacket(0x7D,Excess);
    TestEqual(TEXT("Unbounded roster count is rejected before reserving memory"),H.GroupRevision,Revision);
    H.HandlePacket(0x7D,Wire.Bytes); TestTrue(TEXT("Roster refresh does not repeat initial member requests"),Stats==2 && Names==2);
    H.GroupInfo.LeaderGuid=2; FGroupWire Leader; Leader.Text("Third"); H.HandlePacket(0x79,Leader.Bytes); TestEqual(TEXT("Leader packet resolves the actual named member"),H.GroupInfo.LeaderGuid,uint64(3));
    FGroupWire Invite; Invite.Add(uint8(1)).Text("Inviter").Add(uint32(0)).Add(uint8(0)).Add(uint32(0)); H.HandlePacket(0x6F,Invite.Bytes); TestEqual(TEXT("Invite skips its initial acceptability flag"),H.PendingGroupInviter,FString(TEXT("Inviter")));
    int32 Results=0; H.OnPartyCommandResult.AddLambda([&](uint8 Command,const FString& Member,uint8 Result){if(Command==4 && Member==TEXT("Third") && Result==14) ++Results;});
    FGroupWire Error; Error.Add(uint32(4)).Text("Third").Add(uint32(14)).Add(uint32(30));
    for(int32 N=0;N<Error.Bytes.Num();++N) H.HandlePacket(0x7F,TArray<uint8>(Error.Bytes.GetData(),N));
    TestEqual(TEXT("Incomplete group errors cannot notify the UI"),Results,0);
    H.HandlePacket(0x7F,Error.Bytes); TestTrue(TEXT("Complete group error retains its operation and formatting value"),Results==1 && H.LastPartyCommandValue==30);
    FGroupWire Party; Party.Add(uint8(0)).Add(uint8(0)).Add(uint8(0)).Add(uint8(0)).Add(uint64(123)).Add(uint32(2)).Add(uint32(1));
    Party.Text("Second").Add(uint64(2)).Add(uint8(1)).Add(uint8(0)).Add(uint8(0)).Add(uint8(0));
    Party.Add(uint64(1)).Add(uint8(3)).Add(uint64(0)).Add(uint8(2)).Add(uint8(0)).Add(uint8(0)).Add(uint8(0));
    H.ReadyCheck.Begin(1); H.RaidTargets.SetTarget(7,2); H.HandlePacket(0x7D,Party.Bytes);
    TestTrue(TEXT("Party roster refresh preserves active ready checks and target marks"),H.GroupInfo.IsPartyGroup() && H.ReadyCheck.bActive && H.RaidTargets.GetIconIndex(2)==7);
    H.HandlePacket(0x7C,{}); TestTrue(TEXT("Group destruction clears remote views and pending identity"),H.GroupInfo.IsEmpty() && H.RemoteGroupMembers.IsEmpty() && H.PendingGroupInviter.IsEmpty() && !H.FindUiUnit(2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroupRemoteStatsPackets,"WowUnreal.Gameplay.RemoteGroupStatsPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGroupRemoteStatsPackets::RunTest(const FString&)
{
    FWowPacketHandler H; H.EntityManager.LocalPlayerGuid=1; H.HandlePacket(0x7D,GroupRoster().Bytes);
    const uint64 Pet=0xf14000000000000a;
    FGroupWire R; R.Add(uint8(0)).Packed(2).Add(uint32(0xfffff)).Add(uint16(0xc3)).Add(uint32(50)).Add(uint32(100)).Add(uint8(1));
    R.Add(uint16(150)).Add(uint16(1000)).Add(uint16(20)).Add(uint16(12)).Add(uint16(int16(-8890))).Add(uint16(int16(-80)));
    R.Add((uint64(1)<<1)|(uint64(1)<<63)).Add(uint32(139)).Add(uint8(0x10)).Add(uint32(78)).Add(uint8(0));
    R.Add(Pet).Text("Pet").Add(uint16(299)).Add(uint32(7)).Add(uint32(10)).Add(uint8(2)).Add(uint16(24)).Add(uint16(100));
    R.Add(uint64(1)<<3).Add(uint32(740)).Add(uint8(0x10)).Add(uint32(123)); H.HandlePacket(0x2F2,R.Bytes);
    const auto& S=H.RemoteGroupMembers[2];
    TestTrue(TEXT("Full stats decode health, rage, level and signed world coordinates"),S.Player.GetHealth()==50 && S.Player.GetMaxHealth()==100 && S.Player.GetPowerTypeId()==1 && S.Player.GetPower(1)==150 && S.Player.GetLevel()==20 && S.HasPosition && S.Player.Movement.Position.X==-8890 && S.Player.Movement.Position.Y==-80);
    TestTrue(TEXT("Aura masks retain high slots without inventing durations"),S.Player.Auras.Num()==64 && S.Player.Auras[1].SpellId==139 && S.Player.Auras[63].SpellId==78 && S.Player.Auras[1].DurationMs==0);
    TestTrue(TEXT("Pet stats and owner identity remain separate"),S.Pet.Guid==Pet && S.PetName==TEXT("Pet") && S.Pet.GetHealth()==7 && S.Pet.GetPower(2)==24 && S.Pet.Auras[3].SpellId==740 && S.Player.GetField64(UnitField::SUMMON)==Pet && H.FindUiUnit(Pet) && !H.EntityManager.Find(Pet));
    TestTrue(TEXT("Status flags preserve online, PvP, AFK and DND"),(S.Status&1) && (S.Player.GetFieldByte(UnitField::BYTES_2,1)&1) && (S.Player.GetField(PlayerField::FLAGS)&3)==3 && S.VehicleSeat==123);
    const uint32 Revision=H.RemoteGroupRevision;
    for(int32 N=0;N<R.Bytes.Num();++N) {TArray<uint8> Cut(R.Bytes.GetData(),N); H.HandlePacket(0x2F2,Cut); TestEqual(TEXT("Every truncated stats packet preserves the complete cached snapshot"),H.RemoteGroupRevision,Revision);}
    FGroupWire Delta; Delta.Packed(2).Add(uint32(2)).Add(uint32(25)); H.HandlePacket(0x7E,Delta.Bytes);
    TestTrue(TEXT("Delta health retains the authoritative power type and unchanged auras"),H.RemoteGroupMembers[2].Player.GetHealth()==25 && H.RemoteGroupMembers[2].Player.GetPowerTypeId()==1 && H.RemoteGroupMembers[2].Player.Auras[63].SpellId==78);
    FGroupWire Remove; Remove.Packed(2).Add(uint32(0x400)).Add(uint64(0)); H.HandlePacket(0x7E,Remove.Bytes);
    TestTrue(TEXT("Explicit pet removal clears stale pet fields and owner summon"),H.RemoteGroupMembers[2].Pet.Guid==0 && H.RemoteGroupMembers[2].Player.GetField64(UnitField::SUMMON)==0 && !H.FindUiUnit(Pet));
    FGroupWire Offline; Offline.Add(uint8(0)).Packed(2).Add(uint32(1)).Add(uint16(0)); H.HandlePacket(0x2F2,Offline.Bytes);
    TestTrue(TEXT("Offline full snapshot resets omitted power type and aura/pet data"),H.RemoteGroupMembers[2].Status==0 && H.RemoteGroupMembers[2].Player.GetPowerTypeId()==0 && H.RemoteGroupMembers[2].Player.Auras.IsEmpty() && H.RemoteGroupMembers[2].Pet.Guid==0);
    FGroupWire Stranger; Stranger.Packed(99).Add(uint32(2)).Add(uint32(999)); H.HandlePacket(0x7E,Stranger.Bytes); TestFalse(TEXT("Unrelated stats cannot create a group member"),H.RemoteGroupMembers.Contains(99));
    return true;
}
#endif
