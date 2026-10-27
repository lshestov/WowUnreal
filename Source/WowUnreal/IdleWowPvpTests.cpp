#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
#include "Formats/Dbc/DbcStore.h"
#include "Mpq/MpqManager.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FWire {TArray<uint8> B; template<typename T> FWire& Add(T V) {B.Append(reinterpret_cast<const uint8*>(&V),sizeof(V)); return *this;} FWire& Text(const char* S) {B.Append(reinterpret_cast<const uint8*>(S),FCStringAnsi::Strlen(S)+1); return *this;}};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPvpPackets,"WowUnreal.Gameplay.PvpPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FPvpPackets::RunTest(const FString&)
{
    FWowPacketHandler H; FWire Queued; Queued.Add(uint32(1)).Add(uint8(0)).Add(uint8(0)).Add(uint32(2)).Add(uint16(0x1F90)).Add(uint8(10)).Add(uint8(19)).Add(uint32(0)).Add(uint8(0)).Add(uint32(1)).Add(uint32(45000)).Add(uint32(12000));
    for(int32 I=0;I<Queued.B.Num();++I) {auto B=Queued.B; B.SetNum(I); H.HandlePacket(0x2D4,B);}
    TestEqual(TEXT("Truncated queue packets cannot publish an incomplete status"),H.Pvp.Queues[1].Status,0u);
    H.HandlePacket(0x2D4,Queued.B); TestTrue(TEXT("Actual slot, type, bracket, wait and elapsed time are retained"),H.Pvp.Queues[1].Status==1 && H.Pvp.Queues[1].Type==2 && H.Pvp.Queues[1].Wait==45000 && H.Pvp.Queues[1].Elapsed==12000);
    FWire Confirm; Confirm.Add(uint32(1)).Add(uint8(0)).Add(uint8(0)).Add(uint32(2)).Add(uint16(0x1F90)).Add(uint8(10)).Add(uint8(19)).Add(uint32(7)).Add(uint8(0)).Add(uint32(2)).Add(uint32(489)).Add(uint64(0)).Add(uint32(80000)); H.HandlePacket(0x2D4,Confirm.B);
    TestTrue(TEXT("Invitation carries map and a separate countdown, without an elapsed queue field"),H.Pvp.Queues[1].Status==2 && H.Pvp.Queues[1].Expiry==80000 && H.Pvp.Queues[1].Map==489);
    FWire Score; Score.Add(uint8(0)).Add(uint8(1)).Add(uint8(1)).Add(uint32(1)).Add(uint64(42)).Add(uint32(3)).Add(uint32(9)).Add(uint32(2)).Add(uint32(120)).Add(uint32(1234)).Add(uint32(456)).Add(uint32(2)).Add(uint32(1)).Add(uint32(2));
    for(int32 I=0;I<Score.B.Num();++I) {auto B=Score.B; B.SetNum(I); H.HandlePacket(0x2E0,B);}
    TestTrue(TEXT("All scoreboard truncations preserve the previous complete snapshot"),H.Pvp.Scores.IsEmpty() && H.Pvp.Winner==-1);
    H.HandlePacket(0x2E0,Score.B); TestTrue(TEXT("Battleground score layout includes daily kills, deaths, bonus honor and objective columns"),H.Pvp.Scores.Num()==1 && H.Pvp.Winner==1 && H.Pvp.Scores[0].Honor==120 && H.Pvp.Scores[0].Extra[1]==2);
    FWire Arena; Arena.Add(uint8(1)); for(int I=0;I<2;++I) Arena.Add(uint32(5)).Add(uint32(20)).Add(uint32(1500)); Arena.Text("Alpha").Text("Beta").Add(uint8(0)).Add(uint32(1)).Add(uint64(43)).Add(uint32(4)).Add(uint8(0)).Add(uint32(567)).Add(uint32(890)).Add(uint32(0));
    H.HandlePacket(0x2E0,Arena.B); TestTrue(TEXT("Arena scores use their own shorter wire layout and team identity"),H.Pvp.ScoreArena && H.Pvp.Winner==-1 && H.Pvp.Scores.Num()==1 && H.Pvp.Scores[0].Damage==567 && H.Pvp.Teams[1].Name==TEXT("Beta"));
    auto Extra=Arena.B; Extra.Add(0); const auto Before=H.Pvp.ScoreRevision; H.HandlePacket(0x2E0,Extra); TestEqual(TEXT("Trailing data cannot replace a scoreboard"),H.Pvp.ScoreRevision,Before);
    H.EntityManager.LocalPlayerGuid=1; auto& Player=H.EntityManager.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER); Player.SetField(UnitField::END+0x454,7);
    TArray<uint32> Requests; H.OnSendPacket.BindLambda([&](uint32 Op,const TArray<uint8>& B){if(B.Num()==4) Requests.Add(Op);});
    FWire Member; Member.Add(uint8(3)).Add(uint8(2)).Text("Member").Text("Arena"); H.HandlePacket(0x357,Member.B);
    TestTrue(TEXT("Actual member events refresh identity and roster for the player's team"),Requests.Num()==2 && Requests[0]==0x34B && Requests[1]==0x34D);
    Requests.Reset(); auto Invalid=Member.B; Invalid.Add(0); H.HandlePacket(0x357,Invalid); TestTrue(TEXT("Malformed events cannot trigger roster requests"),Requests.IsEmpty());
    FWire Invite; Invite.Add(uint32(1)).Add(uint32(4197)).Add(uint32(1900000000)); H.HandlePacket(0x4DE,Invite.B); TestTrue(TEXT("Wintergrasp entry invitation is authoritative and carries its zone/deadline"),H.Pvp.WorldQueues.Contains(1) && H.Pvp.WorldQueues[1].Status==2 && H.Pvp.WorldEvents.Num()==1);
    const auto Sequence=H.Pvp.NextWorldEvent; H.ResetPvpState(); TestTrue(TEXT("Session reset clears all queues, scoreboard, arena and world-PvP state without reusing event IDs"),H.Pvp.WorldQueues.IsEmpty() && H.Pvp.Scores.IsEmpty() && H.Pvp.ArenaTeams.IsEmpty() && !H.Pvp.Queues[1].Status && H.Pvp.NextWorldEvent==Sequence);
    return true;
}
#endif
