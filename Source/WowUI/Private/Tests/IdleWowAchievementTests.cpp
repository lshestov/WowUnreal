#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
#include "WowConnectionManager.h"
#include "WowFrameManager.h"
#include "WowLuaVM.h"
#include "WowEventSystem.h"
#include "LuaApi/LuaApiRegistry.h"
#include "Formats/Dbc/DbcStore.h"
#include "Mpq/MpqManager.h"
#include "Components/CanvasPanel.h"
#include "UObject/StrongObjectPtr.h"
namespace
{
constexpr uint32 When=(26u<<24)|(9u<<20)|(26u<<14)|(2u<<11)|(10u<<6);
struct FWire
{
    TArray<uint8> B;
    template<typename T> FWire& Add(T Value) {B.Append(reinterpret_cast<const uint8*>(&Value),sizeof(Value)); return *this;}
    FWire& Packed(uint64 Value) {uint8 Mask=0; TArray<uint8> Tail; for(int I=0;I<8;++I) if(const uint8 V=uint8(Value>>(I*8))) {Mask|=1<<I; Tail.Add(V);} B.Add(Mask); B.Append(Tail); return *this;}
    FWire& Progress(uint32 Id,uint64 Counter,uint64 Owner=1) {return Add(Id).Packed(Counter).Packed(Owner).Add(uint32(0)).Add(When).Add(uint32(12)).Add(uint32(20));}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAchievementPacketsTest,"WowUnreal.UI.AchievementPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FAchievementPacketsTest::RunTest(const FString&)
{
    FWowPacketHandler H; H.EntityManager.LocalPlayerGuid=1;
    const auto Atomic=[&](uint32 Op,const TArray<uint8>& Bytes,bool Comparison=false)
    {
        const uint32 Before=Comparison?H.Achievements.ComparisonRevision:H.Achievements.Revision;
        for(int N=0;N<Bytes.Num();++N) {TArray<uint8> Part; Part.Append(Bytes.GetData(),N); H.HandlePacket(Op,Part); if(!TestEqual(TEXT("Truncated achievement data cannot mutate snapshots"),Comparison?H.Achievements.ComparisonRevision:H.Achievements.Revision,Before)) return false;}
        auto Extra=Bytes; Extra.Add(0); H.HandlePacket(Op,Extra); TestEqual(TEXT("Trailing bytes are rejected atomically"),Comparison?H.Achievements.ComparisonRevision:H.Achievements.Revision,Before);
        H.HandlePacket(Op,Bytes); return TestEqual(TEXT("Valid achievement data commits exactly once"),Comparison?H.Achievements.ComparisonRevision:H.Achievements.Revision,Before+1);
    };
    FWire Full; Full.Add(uint32(6)).Add(When).Add(MAX_uint32).Progress(123,65537).Add(MAX_uint32);
    if(!Atomic(0x47D,Full.B)) return false;
    TestTrue(TEXT("Completed dates and packed 64-bit counters preserve alignment"),H.Achievements.Own.Ready && H.Achievements.Own.Completed[6]==When && H.Achievements.Own.Progress[123].Counter==65537 && H.Achievements.Own.Progress[123].Elapsed==12);
    FWire Criteria; Criteria.Progress(123,0x100000001ull); if(!Atomic(0x46A,Criteria.B)) return false;
    TestEqual(TEXT("Criteria counters retain their high bytes"),H.Achievements.Own.Progress[123].Counter,0x100000001ull);
    FWire Earned; Earned.Packed(1).Add(uint32(964)).Add(When).Add(uint32(0)); if(!Atomic(0x468,Earned.B)) return false;
    H.HandlePacket(0x468,Earned.B); TestTrue(TEXT("Duplicate notifications do not enqueue duplicate toasts"),H.Achievements.Earned.Num()==1 && H.Achievements.Earned[0]==964);
    FWire Other; Other.Packed(2).Add(uint32(7)).Add(When).Add(uint32(0)); H.HandlePacket(0x468,Other.B); TestFalse(TEXT("Nearby players' achievements are not earned by the local player"),H.Achievements.Own.Completed.Contains(7));
    H.Achievements.CompareGuid=2; FWire Inspect; Inspect.Packed(2).Add(uint32(7)).Add(When).Add(MAX_uint32).Progress(123,9,2).Add(MAX_uint32); if(!Atomic(0x46C,Inspect.B,true)) return false;
    TestTrue(TEXT("Comparison replies are isolated from own progress"),H.Achievements.Comparison.Completed.Contains(7) && H.Achievements.Comparison.Progress[123].Counter==9 && !H.Achievements.Own.Completed.Contains(7));
    H.Achievements.CompareGuid=3; const uint32 Revision=H.Achievements.ComparisonRevision; H.HandlePacket(0x46C,Inspect.B); TestEqual(TEXT("A late inspect reply cannot replace a new target"),H.Achievements.ComparisonRevision,Revision);
    FWire Delete; Delete.Add(uint32(964)); H.HandlePacket(0x49F,Delete.B); TestFalse(TEXT("Deletion removes completed and queued notification state"),H.Achievements.Own.Completed.Contains(964) || H.Achievements.Earned.Contains(964));
    H.ResetAchievementState(); TestTrue(TEXT("Session reset clears own/comparison data and subscriptions"),!H.Achievements.Own.Ready && H.Achievements.Own.Progress.IsEmpty() && !H.Achievements.Comparison.Ready && !H.Achievements.CompareGuid);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAchievementCatalogTest,"WowUnreal.Hud.AchievementCatalogAndState",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FAchievementCatalogTest::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& D=FDbcStore::Get(); if(!D.IsLoaded()) D.LoadAll(Mpq); if(!D.IsDeferredLoaded()) D.LoadDeferred(Mpq);
    if(!TestEqual(TEXT("Original achievement catalog loaded from MPQ"),D.Achievements().Achievements().Num(),1817)) return false;
    TestEqual(TEXT("Original criteria catalog loaded"),D.Achievements().Criteria().Num(),7655);
    FWowLuaVM Lua; Lua.Initialize(); FWowFrameManager Frames; FWowEventSystem Events; FWowLuaContext Context;
    TStrongObjectPtr<UCanvasPanel> Canvas(NewObject<UCanvasPanel>()); TStrongObjectPtr<UWowConnectionManager> Conn(NewObject<UWowConnectionManager>());
    Context.FrameManager=&Frames; Context.EventSystem=&Events; Context.ConnectionManager=Conn.Get(); Context.EntityManager=&Conn->PacketHandler.EntityManager;
    WowLuaApi::SetContext(Lua.GetState(),&Context); Events.SetLuaVM(&Lua); Events.SetFrameManager(&Frames); Frames.SetEventSystem(&Events); Frames.Initialize(Canvas.Get());
    auto& H=Conn->PacketHandler; H.EntityManager.LocalPlayerGuid=1; auto& P=H.EntityManager.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER); P.SetField(UnitField::LEVEL,20); P.SetField(UnitField::BYTES_0,1|(8u<<8)); H.Achievements.Own.Ready=true;
    uint32 Criterion=0; for(const auto& Pair:D.Achievements().Criteria()) if(Pair.Value.Achievement==6) {Criterion=Pair.Key; break;} if(!TestTrue(TEXT("Level achievement has an actual criterion"),Criterion!=0)) return false;
    H.Achievements.Own.Progress.FindOrAdd(Criterion).Counter=5;
    TestTrue(TEXT("Original tuples, categories and authoritative criteria progress"),Lua.ExecuteString(TEXT(R"LUA(
        local id,name,points,completed,month,day,year,description,flags,icon=GetAchievementInfo(6)
        assert(id==6 and name=='Level 10' and points==10 and not completed and month==nil and #description>0 and #icon>0)
        assert(GetCategoryInfo(92)=='General' and GetAchievementCategory(6)==92 and #GetCategoryList()>0 and #GetStatisticsCategoryList()>0)
        assert(GetCategoryNumAchievements(-1)>900 and GetCategoryNumAchievements(-2)>300 and GetAchievementInfo(-1,1) and GetAchievementInfo(-2,1))
        assert(GetPreviousAchievement(7)==6 and GetNextAchievement(6)==7)
        local text,kind,done,quantity,required=GetAchievementCriteriaInfo(6,1); assert(kind==5 and not done and quantity==5 and required==10)
        assert(CanShowAchievementUI() and GetTotalAchievementPoints()==0 and not HasCompletedAnyAchievement())
        AchievementEvents={}; AchievementWatch=CreateFrame('Frame'); for _,e in ipairs({'ACHIEVEMENT_EARNED','CRITERIA_UPDATE','INSPECT_ACHIEVEMENT_READY','CHAT_MSG_ACHIEVEMENT'}) do AchievementWatch:RegisterEvent(e) end
        AchievementWatch:SetScript('OnEvent',function(self,e,id) AchievementEvents[e]=id or true end)
    )LUA")));
    WowLuaApi::UpdateAchievementState(Lua.GetState());
    Events.FireChatMessage(48,0,1,TEXT("Tester"),TEXT("%s has earned $a!"),TEXT(""),6);
    TestTrue(TEXT("Achievement chat reaches the stock formatter with a real link"),Lua.ExecuteString(TEXT("assert(AchievementEvents.CHAT_MSG_ACHIEVEMENT:find('achievement:6:') and not AchievementEvents.CHAT_MSG_ACHIEVEMENT:find('$a',1,true))")));
    TestTrue(TEXT("Tracking is bounded, idempotent and returned as original varargs"),Lua.ExecuteString(TEXT("AddTrackedAchievement(964); AddTrackedAchievement(964); assert(GetNumTrackedAchievements()==1 and GetTrackedAchievements()==964 and IsTrackedAchievement(964))")));
    H.Achievements.Own.Completed.Add(6,When); H.Achievements.Earned.Add(6); ++H.Achievements.Revision; WowLuaApi::UpdateAchievementState(Lua.GetState());
    TestTrue(TEXT("A real completion updates points, dates, links and typed events"),Lua.ExecuteString(TEXT("local id,name,points,done,m,d,y=GetAchievementInfo(6); assert(done and m==10 and d==27 and y==26 and GetTotalAchievementPoints()==10 and HasCompletedAnyAchievement()); assert(type(AchievementEvents.ACHIEVEMENT_EARNED)=='number' and AchievementEvents.ACHIEVEMENT_EARNED==6); assert(GetAchievementLink(6):find('achievement:6:')); AchTip=CreateFrame('GameTooltip','AchTip'); AchTip:SetHyperlink(GetAchievementLink(6)); assert(AchTip:NumLines()>=2)")));
    H.Achievements.Own.Completed.Add(964,When); H.Achievements.Earned.Add(964); ++H.Achievements.Revision; WowLuaApi::UpdateAchievementState(Lua.GetState());
    TestTrue(TEXT("Completed achievements leave the tracker automatically"),Lua.ExecuteString(TEXT("assert(GetNumTrackedAchievements()==0 and not IsTrackedAchievement(964))")));
    H.Achievements.Comparison.Ready=true; H.Achievements.Comparison.Completed.Add(7,When); ++H.Achievements.ComparisonRevision; WowLuaApi::UpdateAchievementState(Lua.GetState());
    TestTrue(TEXT("Comparison returns the other player's progress without changing own achievements"),Lua.ExecuteString(TEXT("assert(GetAchievementComparisonInfo(7) and not select(4,GetAchievementInfo(7)) and GetComparisonAchievementPoints()==10 and AchievementEvents.INSPECT_ACHIEVEMENT_READY); ClearAchievementComparisonUnit(); assert(not GetAchievementComparisonInfo(7) and GetComparisonAchievementPoints()==0)")));
    TestTrue(TEXT("The raw opcode cannot emit a second empty achievement event"),FWowEventSystem::OpcodeToEvent(0x468).IsEmpty());
    TestTrue(TEXT("An incomplete achievement can still be watched"),Lua.ExecuteString(TEXT("AddTrackedAchievement(503); assert(IsTrackedAchievement(503))")));
    H.EntityManager.LocalPlayerGuid=2; WowLuaApi::UpdateAchievementState(Lua.GetState());
    TestTrue(TEXT("A new character cannot inherit the previous in-memory tracking table"),Lua.ExecuteString(TEXT("assert(GetNumTrackedAchievements()==0 and #WowUnrealTrackedAchievements==0)")));
    WowLuaApi::ClearContext(Lua.GetState()); Lua.Shutdown(); return true;
}
#endif
