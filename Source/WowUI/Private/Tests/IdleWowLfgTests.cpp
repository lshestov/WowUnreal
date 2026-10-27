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
    FLfgWire Role; Role.Add(uint32(2)).Add(uint8(1)).Add(uint8(1)).Add(uint32(6)).Add(uint8(2)).Add(uint64(42)).Add(uint8(1)).Add(uint32(2)).Add(uint8(20)).Add(uint64(1)).Add(uint8(0)).Add(uint32(0)).Add(uint8(20)); Atomic(0x363,Role.B,[&]{return H.Lfg.RoleRevision;});
    TestTrue(TEXT("Role check keeps leader-first ordering and unanswered participants"),H.Lfg.RoleCheck.State==2 && H.Lfg.RoleCheck.Members[0].Guid==42 && !H.Lfg.RoleCheck.Members[1].Ready);
    FLfgWire Chosen; Chosen.Add(uint64(1)).Add(uint8(1)).Add(uint32(8)); Atomic(0x2BB,Chosen.B,[&]{return H.Lfg.ChosenRevision;});
    FLfgWire Proposal; Proposal.Add(uint32(6)).Add(uint8(0)).Add(uint32(99)).Add(uint32(3)).Add(uint8(0)).Add(uint8(2));
    Proposal.Add(uint32(2)).Add(uint8(0)).Add(uint8(0)).Add(uint8(1)).Add(uint8(1)).Add(uint8(1)); Proposal.Add(uint32(8)).Add(uint8(1)).Add(uint8(0)).Add(uint8(1)).Add(uint8(0)).Add(uint8(0)); Atomic(0x361,Proposal.B,[&]{return H.Lfg.ProposalRevision;});
    TestTrue(TEXT("Proposal preserves the server ID, boss mask and each answer"),H.Lfg.Proposal.Id==99 && H.Lfg.Proposal.Encounters==3 && H.Lfg.Proposal.Members[0].Accepted && H.Lfg.Proposal.Members[1].Self && !H.Lfg.Proposal.Members[1].Answered);
    auto Duplicate=Proposal.B; Duplicate[19]=1; const auto Before=H.Lfg.ProposalRevision; H.HandlePacket(0x361,Duplicate); TestEqual(TEXT("Two self rows cannot replace a proposal"),H.Lfg.ProposalRevision,Before);
    H.HandlePacket(0x368,{7,0}); TestTrue(TEXT("Authoritative queue removal ends pending proposals and role checks even when no separate result arrives"),H.Lfg.Proposal.State==1 && H.Lfg.RoleCheck.State==5);
    FLfgWire Boot; Boot.Add(uint8(1)).Add(uint8(0)).Add(uint8(0)).Add(uint64(42)).Add(uint32(1)).Add(uint32(1)).Add(uint32(90)).Add(uint32(3)).Text("Owned QA test"); Atomic(0x36D,Boot.B,[&]{return H.Lfg.BootRevision;});
    TestTrue(TEXT("Boot state retains victim, reason, votes and a monotonic deadline"),H.Lfg.Boot.Victim==42 && H.Lfg.Boot.Agrees==1 && H.Lfg.Boot.Reason==TEXT("Owned QA test") && H.Lfg.Boot.Deadline>FPlatformTime::Seconds());
    FLfgWire Final; Final.Add(uint32(258)).Add(uint32(6)).Add(uint8(0)).Add(uint32(1)).Add(uint32(100)).Add(uint32(200)).Add(uint32(0)).Add(uint32(0)).Add(uint8(1)).Add(uint32(51999)).Add(uint32(123)).Add(uint32(1)); Atomic(0x1FF,Final.B,[&]{return H.Lfg.RewardRevision;});
    TestTrue(TEXT("Completion reward skips the wire count without shifting amounts/items"),H.Lfg.RewardSpecific==6 && H.Lfg.FinalReward.Experience==200 && H.Lfg.FinalReward.Items[0].Id==51999);
    H.GroupInfo.RawGroupType=WowGroupType::LFG; H.GroupInfo.LfgDungeon=6; H.Lfg.Party.Queued=true; H.Lfg.ContinueDungeon=6;
    H.HandlePacket(0x1FF,Final.B);
    TestTrue(TEXT("Completion ends replacement and queue state without requiring another group packet"),H.GroupInfo.LfgState==2 && !H.Lfg.Party.Queued && !H.Lfg.ContinueDungeon);
    H.ResetLfgState(); TestTrue(TEXT("Reset clears outstanding role, proposal, vote and reward snapshots"),!H.Lfg.Proposal.Valid && H.Lfg.RoleCheck.Members.IsEmpty() && !H.Lfg.Boot.Progress && H.Lfg.FinalReward.Items.IsEmpty());
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
    H.Lfg.Player.QueuedAt=H.Lfg.Queue.At+1;
    TestTrue(TEXT("Requeueing cannot expose statistics from an earlier search"),Lua.ExecuteString(TEXT("assert(not GetLFGQueueStats())")));
    H.Lfg.Player.QueuedAt=0;
    H.Lfg.Locks.Add(258,4); TestTrue(TEXT("Server restrictions override locally eligible levels"),Lua.ExecuteString(TEXT("assert(not IsLFGDungeonJoinable(258) and GetLFDChoiceLockedState()[258]==4 and GetRandomDungeonBestChoice()==nil)")));
    TestTrue(TEXT("Original encounter definitions are available"),D.LfgUi().Encounters().Num()>0);
    H.Lfg.RoleCheck.State=2; H.Lfg.RoleCheck.Dungeons={6}; H.Lfg.RoleCheck.Members.Add({1,0,20,false});
    TestTrue(TEXT("Observe original role and proposal events"),Lua.ExecuteString(TEXT("LfgEvents={}; LfgWatch=CreateFrame('Frame'); for _,e in ipairs({'LFG_ROLE_CHECK_SHOW','LFG_ROLE_CHECK_HIDE','LFG_PROPOSAL_SHOW','LFG_PROPOSAL_UPDATE'}) do LfgWatch:RegisterEvent(e) end; LfgWatch:SetScript('OnEvent',function(self,e) LfgEvents[e]=(LfgEvents[e] or 0)+1 end)")));
    ++H.Lfg.RoleRevision; WowLuaApi::UpdateLfgState(Lua.GetState()); TestTrue(TEXT("A server role request shows its popup once"),Lua.ExecuteString(TEXT("assert(LfgEvents.LFG_ROLE_CHECK_SHOW==1)")));
    TestTrue(TEXT("Pending roles override the queue and expose actual dungeon slots"),Lua.ExecuteString(TEXT("assert(GetLFGMode()=='rolecheck'); local active,slots,members=GetLFGRoleUpdate(); assert(active and slots==1 and members==1); local kind,id=GetLFGRoleUpdateSlot(1); assert(kind==1 and id==6)")));
    H.Lfg.RoleCheck.State=1; ++H.Lfg.RoleRevision; H.Lfg.Proposal.Valid=true; H.Lfg.Proposal.Dungeon=6; H.Lfg.Proposal.Id=99; H.Lfg.Proposal.Members.Add({8,true,false,false,false,false}); ++H.Lfg.ProposalRevision;
    WowLuaApi::UpdateLfgState(Lua.GetState()); TestTrue(TEXT("Completing roles hides the popup and a new server proposal opens once"),Lua.ExecuteString(TEXT("assert(LfgEvents.LFG_ROLE_CHECK_HIDE==1 and LfgEvents.LFG_PROPOSAL_SHOW==1 and LfgEvents.LFG_PROPOSAL_UPDATE==1)")));
    TestTrue(TEXT("Stock proposal tuples preserve pending and accepted booleans"),Lua.ExecuteString(TEXT("assert(GetLFGMode()=='proposal'); local exists,kind,id,name,texture,role,answered,total,done,members=GetLFGProposal(); assert(exists and kind==1 and id==6 and name=='Deadmines' and role=='DAMAGER' and not answered and members==1); local leader,role,level,responded,accepted=GetLFGProposalMember(1); assert(not leader and role=='DAMAGER' and level==20 and not responded and not accepted)")));
    H.Lfg.Proposal.Members[0].Answered=H.Lfg.Proposal.Members[0].Accepted=true; ++H.Lfg.ProposalRevision; WowLuaApi::UpdateLfgState(Lua.GetState()); TestTrue(TEXT("Only a received proposal answer changes readiness; updates do not reopen the dialog"),Lua.ExecuteString(TEXT("assert(select(7,GetLFGProposal())); assert(select(4,GetLFGProposalMember(1)) and select(5,GetLFGProposalMember(1))); assert(LfgEvents.LFG_PROPOSAL_SHOW==1 and LfgEvents.LFG_PROPOSAL_UPDATE==2)")));
    H.GroupInfo.RawGroupType=WowGroupType::LFG|WowGroupType::LFG_RESTRICTED; H.GroupInfo.LfgDungeon=6; H.CurrentMapId=36;
    TestTrue(TEXT("Restrictions and dungeon presence follow the real LFG group and selected map"),Lua.ExecuteString(TEXT("assert(HasLFGRestrictions() and IsInLFGDungeon())")));
    TestTrue(TEXT("Original static-popup boot-vote API is registered"),Lua.ExecuteString(TEXT("assert(type(SetLFGBootVote)=='function' and SetLFGBootVote==VoteInBootProposal)")));
    H.Lfg.Raid.SearchDungeon=42; H.Lfg.Raid.Ready=true;
    FWowLfgState::FRaidGroup RaidGroup; RaidGroup.Guid=900; RaidGroup.Comment=TEXT("Owned group"); RaidGroup.Encounters=3; H.Lfg.Raid.Groups.Add(900,RaidGroup);
    FWowLfgState::FRaidPlayer Leader; Leader.Guid=11; Leader.Group=900; Leader.Leader=true; Leader.Level=80; Leader.Class=1; Leader.Roles=1; H.Lfg.Raid.Players.Add(11,Leader);
    auto Member=Leader; Member.Guid=22; Member.Leader=false; Member.Class=5; Member.Roles=0; H.Lfg.Raid.Players.Add(22,Member);
    auto Solo=Leader; Solo.Guid=33; Solo.Group=0; Solo.Leader=false; Solo.Class=8; Solo.Roles=8; H.Lfg.Raid.Players.Add(33,Solo);
    H.PlayerNameCache.Add(11,TEXT("Alpha")); H.PlayerNameCache.Add(22,TEXT("Beta")); H.PlayerNameCache.Add(33,TEXT("Zeta"));
    FWowFriendInfo Friend; Friend.Guid=22; Friend.Name=TEXT("Beta"); H.FriendsList.Add(Friend);
    TestTrue(TEXT("Original raid-browser tuples contain real group, solo, friend and encounter state"),Lua.ExecuteString(TEXT(R"LUA(
        local count,total=SearchLFGGetNumResults(); assert(count==2 and total==2 and SearchLFGGetJoinedID()==42)
        local name,level,area,class,comment,members,status,token,bosses,done,lead,tank,heal,dps=SearchLFGGetResults(1)
        assert(name=='Alpha' and level==80 and comment=='Owned group' and members==2 and token=='WARRIOR' and bosses>0 and done==2 and lead and not tank and not heal and not dps)
        local name,level,relation=SearchLFGGetPartyResults(1,2); assert(name=='Beta' and level==80 and relation=='friend')
        assert(SearchLFGGetResults(3)==nil and SearchLFGGetPartyResults(2,1)==nil)
        assert(SearchLFGGetEncounterResults(1,1)~=nil)
        SearchLFGSort('name'); assert(SearchLFGGetResults(1)=='Zeta' and select(14,SearchLFGGetResults(1)))
        SearchLFGSort('name'); assert(SearchLFGGetResults(1)=='Alpha')
        LfrTip=CreateFrame('GameTooltip','LfrTip'); LfrTip:AddLine('Tank'); LfrTip:AddTexture('Interface\\LFGFrame\\LFGRole',0,.25,0,1)
        LfrTip:AddDoubleLine('Owned group','2'); LfrTip:AddTexture(''); assert(LfrTip:NumLines()==2)
    )LUA")));
    WowLuaApi::ClearContext(Lua.GetState()); Lua.Shutdown(); return true;
}
#endif
