#include "Misc/AutomationTest.h"
#include "WowConnectionManager.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowLuaVM.h"
#include "../LuaApi/LuaApiRegistry.h"
#include "Formats/Dbc/DbcStore.h"
#include "Mpq/MpqManager.h"
#include "Components/CanvasPanel.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if WITH_DEV_AUTOMATION_TESTS && __has_include("lua.h")
namespace
{
struct FLfgWire
{
    TArray<uint8> B;
    template<typename T> FLfgWire& Add(T V) {B.Append(reinterpret_cast<const uint8*>(&V),sizeof(V)); return *this;}
    FLfgWire& Text(const char* S) {B.Append(reinterpret_cast<const uint8*>(S),FCStringAnsi::Strlen(S)+1); return *this;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLfgPacketsTest,"WowUnreal.UI.LfgPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FLfgPacketsTest::RunTest(const FString&)
{
    FWowPacketHandler H;
    const auto Atomic=[&](uint16 Op,const TArray<uint8>& B,auto Revision)
    {
        const uint32 Before=Revision();
        for(int32 N=0;N<B.Num();++N) {H.HandlePacket(Op,TArray<uint8>(B.GetData(),N)); TestEqual(TEXT("Truncated LFG packets cannot publish partial state"),Revision(),Before);}
        auto Extra=B; Extra.Add(0); H.HandlePacket(Op,Extra); TestEqual(TEXT("Trailing LFG bytes are rejected"),Revision(),Before);
        H.HandlePacket(Op,B); TestEqual(TEXT("A complete packet publishes exactly once"),Revision(),Before+1);
    };
    FLfgWire Info; Info.Add(uint8(1)).Add(uint32(258|(6u<<24))).Add(uint8(0)).Add(uint32(100)).Add(uint32(200)).Add(uint32(10)).Add(uint32(20)).Add(uint8(1)).Add(uint32(51999)).Add(uint32(123)).Add(uint32(1)).Add(uint32(1)).Add(uint32(999|(5u<<24))).Add(uint32(4));
    Atomic(0x36F,Info.B,[&]{return H.Lfg.InfoRevision;});
    TestTrue(TEXT("Rewards, amounts and locks retain server values and strip entry type bits"),H.Lfg.InfoReady && H.Lfg.Rewards[258].Experience==200 && H.Lfg.Rewards[258].Items[0].Id==51999 && H.Lfg.Locks.FindRef(999)==4);
    FLfgWire Party; Party.Add(uint8(1)).Add(uint64(42)).Add(uint32(1)).Add(uint32(999)).Add(uint32(1025)); Atomic(0x372,Party.B,[&]{return H.Lfg.InfoRevision;});
    TestEqual(TEXT("Party lock reasons stay associated with the actual member"),H.Lfg.PartyLocks[42].FindRef(999),1025u);
    FLfgWire Queue; Queue.Add(uint32(258)).Add(int32(-1)).Add(int32(60)).Add(int32(10)).Add(int32(20)).Add(int32(80)).Add(uint8(1)).Add(uint8(1)).Add(uint8(2)).Add(uint32(30)); Atomic(0x365,Queue.B,[&]{return H.Lfg.QueueRevision;});
    TestTrue(TEXT("Unknown wait and role needs preserve the wire contract"),H.Lfg.Queue.Average==-1 && H.Lfg.Queue.Damage==2 && H.Lfg.Queue.Elapsed==30 && H.Lfg.Queue.At>0);
    FLfgWire Update; Update.Add(uint8(12)).Add(uint8(1)).Add(uint8(1)).Add(uint8(0)).Add(uint8(0)).Add(uint8(1)).Add(uint32(258|(6u<<24))).Text("LFG test"); Atomic(0x367,Update.B,[&]{return H.Lfg.Revision;});
    TestTrue(TEXT("Queue state and selection require an authoritative player update"),H.Lfg.Player.Queued && H.Lfg.Player.Dungeons==TArray<uint32>{258} && H.Lfg.Player.Comment==TEXT("LFG test"));
    const auto At=H.Lfg.Player.QueuedAt; H.HandlePacket(0x367,Update.B); TestEqual(TEXT("Repeated queue updates preserve the initial timestamp"),H.Lfg.Player.QueuedAt,At);
    FLfgWire Result; Result.Add(uint32(17)).Add(uint32(2)); H.HandlePacket(0x364,Result.B); TestEqual(TEXT("Empty lock data is legitimately omitted from join results"),H.Lfg.Result,17u);
    FLfgWire Empty; Empty.Add(uint8(0)).Add(uint32(0)); H.HandlePacket(0x36F,Empty.B); TestTrue(TEXT("An empty authoritative snapshot clears obsolete locks and rewards"),H.Lfg.Rewards.IsEmpty() && H.Lfg.Locks.IsEmpty() && H.Lfg.InfoReady);
    H.ResetLfgState(); TestTrue(TEXT("Connection reset removes previous character LFG data"),!H.Lfg.InfoReady && H.Lfg.PartyLocks.IsEmpty() && !H.Lfg.Player.Queued && !H.Lfg.Queue.At);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLfgUiTest,"WowUnreal.Hud.LfgCatalogAndState",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FLfgUiTest::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& D=FDbcStore::Get(); if(!D.IsLoaded()) D.LoadAll(Mpq); if(!D.IsDeferredLoaded()) D.LoadDeferred(Mpq);
    if(!TestEqual(TEXT("Original dungeon catalog loaded"),D.LfgUi().Dungeons().Num(),195)) return false;
    FWowLuaVM Lua; Lua.Initialize(); FWowFrameManager Frames; FWowEventSystem Events; FWowLuaContext Context;
    TStrongObjectPtr<UCanvasPanel> Canvas(NewObject<UCanvasPanel>()); TStrongObjectPtr<UWowConnectionManager> Conn(NewObject<UWowConnectionManager>());
    Context.FrameManager=&Frames; Context.EventSystem=&Events; Context.ConnectionManager=Conn.Get(); Context.EntityManager=&Conn->PacketHandler.EntityManager;
    WowLuaApi::SetContext(Lua.GetState(),&Context); Events.SetLuaVM(&Lua); Events.SetFrameManager(&Frames); Frames.SetEventSystem(&Events); Frames.Initialize(Canvas.Get());
    auto& H=Conn->PacketHandler; H.EntityManager.LocalPlayerGuid=1; auto& P=H.EntityManager.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER); P.SetField(UnitField::LEVEL,20); P.SetField(UnitField::BYTES_0,1|(8u<<8)); H.Lfg.InfoReady=true; H.Lfg.Rewards.Add(258,{});
    TestTrue(TEXT("Stock tuple, negative headers, in-place tables and class roles"),Lua.ExecuteString(TEXT(R"LUA(
        local data=GetLFDChoiceInfo(); assert(data[258][1]=='Random Classic Dungeon' and #data[258]==14)
        local order=GetLFDChoiceOrder(); assert(order[1]<0 and data[order[1]])
        local name,kind,min,max,rec,rmin,rmax,expansion,group=GetLFGDungeonInfo(258); assert(kind==6 and min==15 and max==58 and group<0)
        local tank,heal,dps=GetAvailableRoles(); assert(not tank and not heal and dps)
        SetLFGRoles(true,true,true,true); local lead,t,h,d=GetLFGRoles(); assert(lead and not t and not h and d)
        assert(GetLFGMode()==nil and GetRandomDungeonBestChoice()==258 and IsLFGDungeonJoinable(258))
        SetLFGDungeonEnabled(258,true); local enabled={obsolete=true}; assert(GetLFDChoiceEnabledState(enabled)==enabled and enabled[258] and enabled.obsolete==nil)
        SetLFGHeaderCollapsed(-1,true); assert(GetLFDChoiceCollapseState()[-1])
    )LUA")));
    H.Lfg.Player.Has=true; H.Lfg.Player.Queued=true; H.Lfg.Player.Dungeons={258}; H.Lfg.Queue.Dungeon=258; H.Lfg.Queue.At=FPlatformTime::Seconds(); H.Lfg.Queue.Elapsed=30;
    TestTrue(TEXT("Queue mode comes from server and original table argument is updated in place"),Lua.ExecuteString(TEXT("assert(GetLFGMode()=='queued'); local queued={old=true}; GetLFGQueuedList(queued); assert(queued[258] and queued.old==nil); local valid,_,tank,heal,dps,kind,name,avg,tw,hw,dw,mw,at=GetLFGQueueStats(); assert(valid and tank==1 and heal==1 and dps==3 and kind==6 and avg==-1 and GetTime()-at>=30)")));
    H.Lfg.Locks.Add(258,4); TestTrue(TEXT("Server restrictions override locally eligible levels"),Lua.ExecuteString(TEXT("assert(not IsLFGDungeonJoinable(258) and GetLFDChoiceLockedState()[258]==4 and GetRandomDungeonBestChoice()==nil)")));
    WowLuaApi::ClearContext(Lua.GetState()); Lua.Shutdown(); return true;
}
#endif
