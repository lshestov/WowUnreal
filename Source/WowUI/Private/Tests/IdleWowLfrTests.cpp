#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
namespace
{
struct FWire
{
    TArray<uint8> B;
    template<typename T> FWire& Add(T V) {B.Append(reinterpret_cast<const uint8*>(&V),sizeof(V)); return *this;}
    FWire& Text(const char* S) {B.Append(reinterpret_cast<const uint8*>(S),FCStringAnsi::Strlen(S)+1); return *this;}
    void Player(uint64 Guid,uint64 Group,uint8 Roles)
    {
        Add(Guid).Add(uint32(Group?0x3f:0xb3)); Add(uint8(80)).Add(uint8(8)).Add(uint8(1));
        for(int I=0;I<3;++I) Add(uint8(0)); for(int I=0;I<6;++I) Add(uint32(0));
        Add(12.5f).Add(8.5f); for(int I=0;I<4;++I) Add(uint32(100)); Add(uint32(1)).Add(42.5f);
        for(int I=0;I<6;++I) Add(uint32(0)); Text("Owned raid listing");
        if(Group) Add(uint8(Guid==11)).Add(Group); Add(Roles).Add(uint32(12));
        if(!Group) Add(uint64(123)).Add(uint32(1));
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLfrPacketsTest,"WowUnreal.UI.RaidBrowserPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FLfrPacketsTest::RunTest(const FString&)
{
    FWowPacketHandler H; H.Lfg.Raid.SearchDungeon=42;
    const auto Atomic=[&](const TArray<uint8>& Bytes)
    {
        const uint32 Before=H.Lfg.Raid.Revision;
        for(int32 N=0;N<Bytes.Num();++N) {TArray<uint8> Part; Part.Append(Bytes.GetData(),N); H.HandlePacket(0x360,Part); if(!TestEqual(TEXT("Truncation cannot mutate the current raid snapshot"),H.Lfg.Raid.Revision,Before)) return false;}
        auto Trailing=Bytes; Trailing.Add(0); H.HandlePacket(0x360,Trailing); TestEqual(TEXT("Trailing bytes are rejected atomically"),H.Lfg.Raid.Revision,Before);
        H.HandlePacket(0x360,Bytes); return TestEqual(TEXT("A complete raid list commits once"),H.Lfg.Raid.Revision,Before+1);
    };
    FWire Full; Full.Add(uint32(2)).Add(uint32(42)).Add(uint8(0)).Add(uint32(1)).Add(uint32(0));
    Full.Add(uint64(900)).Add(uint32(0x92)).Text("Owned group").Add(uint8(0)).Add(uint8(0)).Add(uint8(0)).Add(uint64(123)).Add(uint32(3));
    Full.Add(uint32(3)).Add(uint32(0)); Full.Player(11,900,1); Full.Player(22,900,0); Full.Player(33,0,8);
    if(!Atomic(Full.B)) return false;
    TestTrue(TEXT("Server group, character stats, solo roles and encounter masks retain their wire alignment"),H.Lfg.Raid.Groups.Num()==1 && H.Lfg.Raid.Players.Num()==3 && H.Lfg.Raid.Players[11].Leader && !H.Lfg.Raid.Players[22].Leader && H.Lfg.Raid.Players[33].Roles==8 && H.Lfg.Raid.Players[33].ItemLevel==42.5f && H.Lfg.Raid.Groups[900].Encounters==3);
    FWire Delta; Delta.Add(uint32(2)).Add(uint32(42)).Add(uint8(1)).Add(uint32(1)).Add(uint64(33)).Add(uint32(1)).Add(uint32(0));
    Delta.Add(uint64(900)).Add(uint32(2)).Text("Updated group").Add(uint32(0)).Add(uint32(0));
    if(!Atomic(Delta.B)) return false;
    TestTrue(TEXT("Differences remove departed players and preserve fields absent from a group update"),!H.Lfg.Raid.Players.Contains(33) && H.Lfg.Raid.Groups[900].Comment==TEXT("Updated group") && H.Lfg.Raid.Groups[900].Encounters==3);
    const uint32 Before=H.Lfg.Raid.Revision; auto Stale=Full.B; Stale[4]=44; H.HandlePacket(0x360,Stale); TestEqual(TEXT("Late results from a different selected raid are ignored"),H.Lfg.Raid.Revision,Before);
    FWire Huge; Huge.Add(uint32(2)).Add(uint32(42)).Add(uint8(0)).Add(uint32(2001)).Add(uint32(0)); H.HandlePacket(0x360,Huge.B); TestEqual(TEXT("Unbounded result counts cannot allocate a list"),H.Lfg.Raid.Revision,Before);
    FWire Empty; Empty.Add(uint32(2)).Add(uint32(42)).Add(uint8(0)).Add(uint32(0)).Add(uint32(0)).Add(uint32(0)).Add(uint32(0)); H.HandlePacket(0x360,Empty.B);
    TestTrue(TEXT("An empty complete snapshot clears previous listings"),H.Lfg.Raid.Ready && H.Lfg.Raid.Players.IsEmpty() && H.Lfg.Raid.Groups.IsEmpty());
    H.HandlePacket(0x369,{1}); TestTrue(TEXT("Raid listing activity accepts only the one-byte boolean"),H.Lfg.Raid.Listed); H.HandlePacket(0x369,{2}); TestTrue(TEXT("Invalid activity cannot clear a listing"),H.Lfg.Raid.Listed);
    H.ResetLfgState(); TestTrue(TEXT("Session reset clears subscriptions and listing activity"),!H.Lfg.Raid.SearchDungeon && !H.Lfg.Raid.Ready && !H.Lfg.Raid.Listed);
    return true;
}
#endif
