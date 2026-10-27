#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FGuildWire
{
    TArray<uint8> Data;
    template<typename T> FGuildWire& Add(T Value) {Data.Append(reinterpret_cast<const uint8*>(&Value),sizeof(Value)); return *this;}
    FGuildWire& Text(const char* Value) {Data.Append(reinterpret_cast<const uint8*>(Value),FCStringAnsi::Strlen(Value)+1); return *this;}
    FGuildWire& Packed(uint64 Guid) {const int32 Mask=Data.Add(0); for(int32 I=0;I<8;++I) if(const uint8 B=uint8(Guid>>(I*8))) {Data[Mask]|=1<<I; Data.Add(B);} return *this;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayGuildPackets,"WowUnreal.Gameplay.GuildPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayGuildPackets::RunTest(const FString&)
{
    FWowPacketHandler H; H.EntityManager.LocalPlayerGuid=1;
    FGuildWire Query; Query.Add(uint32(7)).Text("QA UI Guild"); for(int32 I=0;I<10;++I) Query.Text(I==0?"Guild Master":I==1?"Officer":"Member"); for(uint32 I=0;I<5;++I) Query.Add(I+1); Query.Add(uint32(5));
    for(int32 N=0;N<Query.Data.Num();++N) {auto B=Query.Data; B.SetNum(N); H.HandlePacket(0x55,B);}
    TestTrue(TEXT("No truncated metadata query can publish a partial guild"),H.Guilds.IsEmpty());
    H.HandlePacket(0x55,Query.Data); TestTrue(TEXT("Query metadata contains ten wire rank names, actual rank count and five emblem fields"),H.Guilds.Contains(7) && H.Guilds[7].Ranks.Num()==5 && H.Guilds[7].Ranks[1]==TEXT("Officer") && H.Guilds[7].Emblem[4]==5);
    FGuildWire Roster; Roster.Add(uint32(2)).Text("Welcome").Text("Guild information").Add(uint32(2));
    for(int32 I=0;I<2;++I) {Roster.Add(uint32(I?0x4003:0x1DF1FF)).Add(uint32(10000)); for(int32 T=0;T<6;++T) Roster.Add(uint32(7)).Add(uint32(5));}
    Roster.Add(uint64(1)).Add(uint8(1)).Text("Uiqalpha").Add(uint32(0)).Add(uint8(80)).Add(uint8(8)).Add(uint8(0)).Add(uint32(12)).Text("Leader").Text("Private");
    Roster.Add(uint64(2)).Add(uint8(0)).Text("Uiqbeta").Add(uint32(1)).Add(uint8(20)).Add(uint8(1)).Add(uint8(1)).Add(uint32(14)).Add(float(1.5)).Text("Offline").Text("Secret");
    const uint32 Before=H.GuildRevision;
    for(int32 N=0;N<Roster.Data.Num();++N) {auto B=Roster.Data; B.SetNum(N); H.HandlePacket(0x8A,B);}
    TestTrue(TEXT("Every truncated roster leaves all live state and revision unchanged"),H.GuildRoster.IsEmpty() && H.GuildRevision==Before);
    H.HandlePacket(0x8A,Roster.Data); TestTrue(TEXT("Full roster retains offline duration, status flags, notes and all six bank permission pairs"),H.GuildRoster.Num()==2 && H.GuildRoster[1].DaysOffline==1.5f && H.GuildRoster[1].OfficerNote==TEXT("Secret") && H.GuildRankAccess[1].TabSlots[5]==5 && H.GuildCanRepair);
    auto Extra=Roster.Data; Extra.Add(0); H.HandlePacket(0x8A,Extra); TestEqual(TEXT("Trailing junk cannot increment the accepted roster revision"),H.GuildRevision,Before+1);
    FGuildWire Invite; Invite.Text("Uiqalpha").Text("QA UI Guild"); H.HandlePacket(0x83,Invite.Data); TestEqual(TEXT("Guild invitations preserve their actual inviter"),H.GuildInviter,FString(TEXT("Uiqalpha")));
    FGuildWire Event; Event.Add(uint8(2)).Add(uint8(1)).Text("Changed MOTD"); H.HandlePacket(0x92,Event.Data); TestEqual(TEXT("Only a validated MOTD event updates the text"),H.GuildMotd,FString(TEXT("Changed MOTD")));
    FGuildWire Log; Log.Add(uint8(2)).Add(uint8(2)).Add(uint64(1)).Add(uint32(3600)).Add(uint8(3)).Add(uint64(1)).Add(uint64(2)).Add(uint8(1)).Add(uint32(60)); H.HandlePacket(0x3FF,Log.Data);
    TestTrue(TEXT("Join and promotion logs use different exact payload layouts"),H.GuildLogs.Num()==2 && H.GuildLogs[0].Second==0 && H.GuildLogs[1].Second==2 && H.GuildLogs[1].Rank==1);
    H.HandlePacket(0x1F1,{0,0,0,0}); TestFalse(TEXT("An unsolicited emblem acknowledgement cannot invent success"),H.Tabard.HasResult);
    FGuildWire Vendor; Vendor.Add(uint64(123)); H.HandlePacket(0x1F2,Vendor.Data); TestTrue(TEXT("Tabard opens only for an actual server-provided vendor"),H.Tabard.Open && H.Tabard.Vendor==123);
    H.Tabard.Pending=true; H.HandlePacket(0x1F1,{0,0,0,0}); TestTrue(TEXT("Saving waits for a real server result"),H.Tabard.HasResult && !H.Tabard.Pending && H.Tabard.Error==0);
    const uint32 Sequence=H.NextGuildEvent; H.ResetGuildState(); TestTrue(TEXT("Session reset removes roster, names, rights and invites without recycling event sequence IDs"),H.Guilds.IsEmpty() && H.GuildRoster.IsEmpty() && H.GuildInvitation.IsEmpty() && H.NextGuildEvent==Sequence && !H.Tabard.HasResult && !H.Tabard.Open);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayInspectPackets,"WowUnreal.Gameplay.InspectPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplayInspectPackets::RunTest(const FString&)
{
    FWowPacketHandler H; H.ResetInspectState(7); const uint32 Revision=H.Inspect.Revision;
    FGuildWire Wire; Wire.Packed(7).Add(uint32(11)).Add(uint8(1)).Add(uint8(0)).Add(uint8(1)).Add(uint32(44)).Add(uint8(2)).Add(uint8(6));
    for(uint16 I=0;I<6;++I) Wire.Add(uint16(100+I));
    Wire.Add(uint32(1<<4)).Add(uint32(38)).Add(uint16(1)).Add(uint16(123)).Add(int16(-9)).Packed(17).Add(uint32(333));
    for(int32 N=0;N<Wire.Data.Num();++N) {auto B=Wire.Data; B.SetNum(N); H.HandlePacket(0x3F4,B);}
    TestTrue(TEXT("All truncated inspect packets leave the current view untouched"),!H.Inspect.Ready && H.Inspect.Revision==Revision);
    H.HandlePacket(0x3F4,Wire.Data);
    TestTrue(TEXT("Inspect snapshots contain the requested player's actual talent group and equipment"),H.Inspect.Ready && H.Inspect.Groups.Num()==1 && H.Inspect.Groups[0].Talents[0].Rank==2 && H.Inspect.Groups[0].Glyphs[5]==105 && H.Inspect.Items.Contains(5));
    if(H.Inspect.Items.Contains(5)) {const auto& I=H.Inspect.Items[5]; TestTrue(TEXT("Equipment retains signed random properties, suffix seed, enchantments and creator"),I.Entry==38 && int32(I.GetField(ItemField::RANDOM_PROPERTIES_ID))==-9 && I.GetField(ItemField::PROPERTY_SEED)==333 && I.GetField(ItemField::ENCHANTMENT_1_1)==123 && I.GetField64(ItemField::CREATOR)==17);}
    FGuildWire Honor; Honor.Add(uint64(7)).Add(uint8(0)).Add(uint32(3|(5u<<16))).Add(uint32(9)).Add(uint32(11)).Add(uint32(200)); H.HandlePacket(0x2D6,Honor.Data);
    TestTrue(TEXT("Honor stats split the packed daily kills correctly"),H.Inspect.HonorReady && H.Inspect.TodayKills==3 && H.Inspect.YesterdayKills==5 && H.Inspect.LifetimeKills==200);
    H.ResetInspectState(8); H.HandlePacket(0x3F4,Wire.Data); H.HandlePacket(0x2D6,Honor.Data);
    TestTrue(TEXT("Late replies for the previous target cannot leak into a new inspect view"),!H.Inspect.Ready && !H.Inspect.HonorReady && H.Inspect.Items.IsEmpty());
    return true;
}
#endif
