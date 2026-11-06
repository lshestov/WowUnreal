#include "Misc/AutomationTest.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowLuaVM.h"
#include "../LuaApi/LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowCombatLogUi.h"
#include "WowCombatTextUi.h"
#include "Components/CanvasPanel.h"
#include "UObject/StrongObjectPtr.h"
#include "Formats/Dbc/DbcStore.h"
#include "Mpq/MpqManager.h"

#if WITH_DEV_AUTOMATION_TESTS && __has_include("lua.h")
namespace
{
struct FCombatPacket
{
    TArray<uint8> Data;
    void Byte(uint8 V) {Data.Add(V);}
    void Word(uint32 V) {Data.Append(reinterpret_cast<const uint8*>(&V),4);}
    void Wide(uint64 V) {Data.Append(reinterpret_cast<const uint8*>(&V),8);}
    void Float(float V) {Data.Append(reinterpret_cast<const uint8*>(&V),4);}
    void Guid(uint64 V)
    {
        uint8 Mask=0; TArray<uint8> Bytes;
        for(int32 I=0;I<8;++I) if(uint8 B=V>>(8*I)) {Mask|=1<<I; Bytes.Add(B);}
        Byte(Mask); Data.Append(Bytes);
    }
};
struct FCombatFixture
{
    FWowLuaVM Lua;
    FWowFrameManager Frames;
    FWowEventSystem Events;
    FWowLuaContext Context;
    TStrongObjectPtr<UCanvasPanel> Canvas{NewObject<UCanvasPanel>()};
    TStrongObjectPtr<UWowConnectionManager> Connection{NewObject<UWowConnectionManager>()};
    FCombatFixture()
    {
        Lua.Initialize(); Context.FrameManager=&Frames; Context.EventSystem=&Events;
        Context.ConnectionManager=Connection.Get(); Context.EntityManager=&Connection->PacketHandler.EntityManager;
        WowLuaApi::SetContext(Lua.GetState(),&Context);
        Events.SetLuaVM(&Lua); Events.SetFrameManager(&Frames); Frames.SetEventSystem(&Events); Frames.Initialize(Canvas.Get());
        Events.CreateFrameObject(Frames.FindFrame(TEXT("UIParent")),TEXT("UIParent"));
        auto& H=Connection->PacketHandler; H.EntityManager.LocalPlayerGuid=1;
        H.EntityManager.PromoteToTyped(1,WowTypeMask::PLAYER|WowTypeMask::UNIT);
        H.OnResolveCombatFlags.BindLambda([&](uint64 Id) {return WowLuaApi::CombatLogFlags(&Context,Id);});
        H.OnCombatLog.BindLambda([&](const FWowCombatEntry& E) {FWowCombatLogUi::Dispatch(Lua.GetState(),E);});
    }
    ~FCombatFixture() {WowLuaApi::ClearContext(Lua.GetState()); Lua.Shutdown();}
    bool Listen()
    {
        return Lua.ExecuteString(TEXT(R"LUA(
            Rows={}; Filtered={}; Errors=0
            local function pack(...) return {n=select('#',...),...} end
            Probe=CreateFrame('Frame')
            for _,e in ipairs({'COMBAT_LOG_EVENT_UNFILTERED','COMBAT_LOG_EVENT','UI_ERROR_MESSAGE'}) do Probe:RegisterEvent(e) end
            Probe:SetScript('OnEvent',function(self,event,...)
                if event=='UI_ERROR_MESSAGE' then Errors=Errors+1; return end
                local row=pack(...); local current=pack(CombatLogGetCurrentEventInfo())
                assert(row.n==current.n)
                for i=1,row.n do assert(row[i]==current[i] and type(row[i])==type(current[i])) end
                table.insert(event=='COMBAT_LOG_EVENT_UNFILTERED' and Rows or Filtered,row)
            end)
            CombatLogResetFilter()
        )LUA"));
    }
};
bool LoadCombatDbc()
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& Dbc=FDbcStore::Get(); if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq);
    if(!Dbc.IsDeferredLoaded()) Dbc.LoadDeferred(Mpq);
    return Dbc.Spells().GetById(139) && Dbc.Spells().GetById(133);
}
constexpr uint64 Victim=0xF130000005000001ULL;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatWrathTuple,"WowUnreal.Hud.CombatWrathTuple",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCombatWrathTuple::RunTest(const FString&)
{
    FCombatFixture F; auto& H=F.Connection->PacketHandler;
    if(!TestTrue(TEXT("Install independent typed addon listener"),F.Listen())) return false;
    TestTrue(TEXT("Only matching heals reach the filtered listener"),F.Lua.ExecuteString(TEXT("CombatLogAddFilter('SPELL_HEAL','0x0000000000000001',nil,2050)")));
    FCombatPacket Heal; Heal.Guid(Victim); Heal.Guid(1); Heal.Word(2050); Heal.Word(60); Heal.Word(17); Heal.Word(4); Heal.Byte(1); Heal.Byte(0);
    H.HandlePacket(0x150,Heal.Data);
    FCombatPacket Damage; Damage.Guid(Victim); Damage.Guid(1); Damage.Word(78); Damage.Word(31); Damage.Word(0); Damage.Byte(1);
    Damage.Word(0); Damage.Word(0); Damage.Byte(0); Damage.Byte(0); Damage.Word(0); Damage.Word(2); Damage.Byte(0);
    H.HandlePacket(0x250,Damage.Data);
    FCombatPacket Environment; Environment.Wide(Victim); Environment.Byte(3); Environment.Word(19); Environment.Word(2); Environment.Word(7);
    H.HandlePacket(WowOpcode::SMSG_ENVIRONMENTAL_DAMAGE_LOG,Environment.Data);
    TestTrue(TEXT("Wrath suffix flags are number/nil; zero mitigation stays nil; GUIDs stay full strings"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(#Rows==3 and #Filtered==1)
        local h,d,e=Rows[1],Rows[2],Rows[3]
        assert(h.n==15 and h[2]=='SPELL_HEAL' and h[6]=='0xF130000005000001')
        assert(h[12]==60 and h[13]==17 and h[14]==4 and h[15]==1 and type(h[15])=='number')
        assert(d.n==20 and d[2]=='SPELL_DAMAGE' and d[15]==nil and d[16]==nil and d[17]==nil)
        assert(d[18]==1 and d[19]==nil and d[20]==nil)
        assert(e.n==18 and e[2]=='ENVIRONMENTAL_DAMAGE' and e[3]=='0x0000000000000000' and e[4]==nil)
        assert(e[9]=='LAVA' and e[10]==19 and e[12]==4 and e[13]==2 and e[14]==nil and e[15]==7)
        assert(e[16]==nil and e[17]==nil and e[18]==nil)
        CombatLogSetCurrentEntry(1,true)
        local history={n=select('#',CombatLogGetCurrentEntry()),CombatLogGetCurrentEntry()}
        assert(history.n==h.n); for i=1,h.n do assert(history[i]==h[i]) end
    )LUA")));
    const int32 Before=H.CombatHistory.Num();
    for(int32 Size=0;Size<Heal.Data.Num();++Size) {auto Short=Heal.Data; Short.SetNum(Size); H.HandlePacket(0x150,Short);}
    AddExpectedError(TEXT("ENVIRONMENTAL_DAMAGE_LOG: packet too short"),EAutomationExpectedErrorFlags::Contains,Environment.Data.Num());
    for(int32 Size=0;Size<Environment.Data.Num();++Size) {auto Short=Environment.Data; Short.SetNum(Size); H.HandlePacket(WowOpcode::SMSG_ENVIRONMENTAL_DAMAGE_LOG,Short);}
    TestEqual(TEXT("Every heal/environment truncation remains silent"),H.CombatHistory.Num(),Before);
    // A split remains DAMAGE_SPLIT even at zero effective damage, with the original caster.
    Damage.Data.Reset(); Damage.Guid(Victim); Damage.Guid(1); Damage.Word(78); Damage.Word(0); Damage.Word(0); Damage.Byte(1);
    Damage.Word(5); Damage.Word(0); Damage.Byte(0); Damage.Byte(0); Damage.Word(0); Damage.Word(0x18); Damage.Byte(0);
    H.HandlePacket(0x250,Damage.Data);
    TestTrue(TEXT("Fully absorbed splits keep the damage family and caster"),F.Lua.ExecuteString(TEXT("local e=Rows[4]; assert(e.n==20 and e[2]=='DAMAGE_SPLIT' and e[3]=='0x0000000000000001' and e[12]==0 and e[17]==5)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatResourcesAndCreation,"WowUnreal.Hud.CombatResourcesAndCreation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCombatResourcesAndCreation::RunTest(const FString&)
{
    if(!TestTrue(TEXT("Read periodic classification from the user's 12340 MPQ"),LoadCombatDbc())) return false;
    FCombatFixture F; auto& H=F.Connection->PacketHandler; if(!F.Listen()) return false;
    FCombatPacket P; P.Guid(1); P.Word(1766); P.Word(3);
    P.Word(8); P.Word(2);
    P.Guid(Victim); P.Word(9); P.Word(1); P.Float(0); // true drain: nil gain
    P.Guid(Victim); P.Word(9); P.Word(1); P.Float(1); // raw gain 9, display gain 0: still leech
    P.Word(28); P.Word(1); P.Guid(0xF110000005000001ULL); // GO from summon effect: create
    P.Word(50); P.Word(1); P.Guid(Victim); // unit from object effect: summon
    auto Short=P.Data; Short.Pop(); H.HandlePacket(0x24C,Short);
    TestEqual(TEXT("Invalid last record rolls back all execute effects"),H.CombatHistory.Num(),0);
    H.HandlePacket(0x24C,P.Data);
    TestTrue(TEXT("Raw gain chooses leech before display conversion; GUID type chooses create/summon"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(#Rows==4 and Rows[1].n==14 and Rows[2].n==14)
        assert(Rows[1][2]=='SPELL_DRAIN' and Rows[1][12]==0 and Rows[1][13]==1 and Rows[1][14]==nil)
        assert(Rows[2][2]=='SPELL_LEECH' and Rows[2][12]==0 and Rows[2][13]==1 and Rows[2][14]==nil)
        assert(Rows[3][2]=='SPELL_CREATE' and Rows[3][6]=='0xF110000005000001')
        assert(Rows[4][2]=='SPELL_SUMMON' and Rows[4][6]=='0xF130000005000001')
    )LUA")));
    // Renew 139 has AttributesEx3=0x80 and uses PERIODICAURALOG for its ticks.
    // Vampiric Embrace's triggered heal has the actual 12340 TREAT_AS_PERIODIC flag.
    const auto* PeriodicHeal=FDbcStore::Get().Spells().GetById(15290);
    if(!TestTrue(TEXT("Vampiric Embrace heal carries TREAT_AS_PERIODIC"),PeriodicHeal && (PeriodicHeal->AttributesEx3&0x2000000))) return false;
    FCombatPacket Heal; Heal.Guid(1); Heal.Guid(1); Heal.Word(15290); Heal.Word(5); Heal.Word(0); Heal.Word(0); Heal.Byte(0); Heal.Byte(0);
    H.HandlePacket(0x150,Heal.Data);
    TestTrue(TEXT("Heal-log uses periodic DBC flag and numeric zero overheal/absorb, nil critical"),F.Lua.ExecuteString(TEXT("local e=Rows[5]; assert(e.n==15 and e[2]=='SPELL_PERIODIC_HEAL' and e[12]==5 and e[13]==0 and e[14]==0 and e[15]==nil)")));
    FCombatPacket Zero; Zero.Guid(1); Zero.Guid(1); Zero.Word(2050); Zero.Word(0); Zero.Word(0); Zero.Word(0); Zero.Byte(0); Zero.Byte(0);
    H.HandlePacket(0x150,Zero.Data);
    Zero.Data.Reset(); Zero.Guid(1); Zero.Guid(1); Zero.Word(78); Zero.Word(1); Zero.Word(0); H.HandlePacket(0x151,Zero.Data);
    Zero.Data.Reset(); Zero.Guid(1); Zero.Word(1766); Zero.Word(1); Zero.Word(19); Zero.Word(1); Zero.Guid(Victim); Zero.Word(0); H.HandlePacket(0x24C,Zero.Data);
    TestEqual(TEXT("Zero heal/energize/extra attacks do not invent rows"),H.CombatHistory.Num(),5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatCastTrailers,"WowUnreal.Hud.CombatCastTrailers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCombatCastTrailers::RunTest(const FString&)
{
    if(!TestTrue(TEXT("Read Fireball's cast time from the user's MPQ"),LoadCombatDbc())) return false;
    FCombatFixture F; auto& H=F.Connection->PacketHandler; if(!F.Listen()) return false;
    int32 Starts=0,Launches=0,Results=0;
    H.OnSpellStart.AddLambda([&](uint64,uint32,uint32,int32) {++Starts;});
    H.OnSpellLaunch.AddLambda([&](const FWowSpellLaunch&) {++Launches;});
    H.OnCastResult.BindLambda([&](uint64,uint32,uint8,bool,uint8) {++Results;});
    const uint32 Flags=0x800|0x200000|0x20;
    const auto Tail=[](FCombatPacket& P) {P.Word(101); P.Byte(3); P.Byte(1); P.Byte(127); P.Word(5996); P.Word(24);};
    FCombatPacket Start; Start.Guid(1); Start.Guid(1); Start.Byte(17); Start.Word(133); Start.Word(Flags|0x4000000); Start.Word(2000); Start.Word(0);
    Tail(Start); Start.Word(1); Start.Word(2);
    for(int32 Size=0;Size<Start.Data.Num();++Size) {auto Short=Start.Data; Short.SetNum(Size); H.HandlePacket(WowOpcode::SMSG_SPELL_START,Short);}
    TestTrue(TEXT("Every start truncation is silent and cannot create a cast"),Starts==0 && H.Casts.IsEmpty() && H.CombatHistory.Num()==0);
    H.HandlePacket(WowOpcode::SMSG_SPELL_START,Start.Data);
    TestTrue(TEXT("Complete optional start fields publish once"),Starts==1 && H.Casts.Contains(1) && H.CombatHistory.Num()==1);
    FCombatPacket Go; Go.Guid(1); Go.Guid(1); Go.Byte(17); Go.Word(133); Go.Word(Flags|0x20000|0x80000); Go.Word(321);
    Go.Byte(1); Go.Wide(Victim); Go.Byte(0); Go.Word(0x40); Go.Guid(0); Go.Float(1); Go.Float(2); Go.Float(3);
    Go.Word(101); Go.Byte(3); Go.Byte(1); Go.Byte(127); Go.Float(.5f); Go.Word(100); Go.Word(5996); Go.Word(24); Go.Word(1); Go.Word(2); Go.Byte(0);
    for(int32 Size=0;Size<Go.Data.Num();++Size) {auto Short=Go.Data; Short.SetNum(Size); H.HandlePacket(WowOpcode::SMSG_SPELL_GO,Short);}
    TestTrue(TEXT("Every go truncation preserves the active cast and publishes no result/miss/launch"),Launches==0 && Results==0 && H.Casts.Contains(1) && H.CombatHistory.Num()==1);
    H.HandlePacket(WowOpcode::SMSG_SPELL_GO,Go.Data);
    TestTrue(TEXT("A full go completes the request and launches exactly once"),Launches==1 && Results==1 && !H.Casts.Contains(1));
    TestTrue(TEXT("Prepare localized failure filters"),F.Lua.ExecuteString(TEXT("CombatLogAddFilter('SPELL_CAST_FAILED','0x0000000000000001',nil,133); SPELL_FAILED_MOVING='Moving'; SPELL_FAILED_INTERRUPTED='Interrupted'")));
    const auto Fail=[&](uint8 Counter,uint8 Reason,bool Private)
    {
        FCombatPacket P; if(!Private) P.Guid(1); P.Byte(Counter); P.Word(133); P.Byte(Reason);
        H.HandlePacket(Private?0x130:WowOpcode::SMSG_SPELL_FAILURE,P.Data);
    };
    const int32 Before=H.CombatHistory.Num();
    Fail(18,51,true); Fail(18,51,false); Fail(19,40,false); Fail(19,40,true);
    TestEqual(TEXT("Complementary rejection/interruption packets add one row per attempt"),H.CombatHistory.Num(),Before+2);
    TestTrue(TEXT("Both listeners see matching localized failures once, without extra UI errors"),F.Lua.ExecuteString(TEXT("assert(#Filtered==3 and Errors==2); local a,b=Filtered[2],Filtered[3]; assert(a[2]=='SPELL_CAST_FAILED' and a[12]=='Moving' and a.n==12); assert(b[12]=='Interrupted' and b.n==12 and b[6]=='0x0000000000000000')")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatNoPowerFeedback,"WowUnreal.Hud.CombatResourceFailureFeedback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCombatNoPowerFeedback::RunTest(const FString&)
{
    if(!LoadCombatDbc()) return false;
    FString Path; FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path);
    FMpqManager Mpq; TArray<uint8> Strings;
    if(!TestTrue(TEXT("Read original localized resource errors"),Mpq.Initialize(Path) && Mpq.ReadFile(TEXT("Interface\\FrameXML\\GlobalStrings.lua"),Strings))) return false;
    FCombatFixture F; auto& H=F.Connection->PacketHandler;
    if(!F.Lua.ExecuteBuffer(Strings,TEXT("GlobalStrings.lua")) || !F.Listen()) return false;
    auto* Player=H.EntityManager.GetLocalPlayer();
    Player->SetField(UnitField::BYTES_0,3|(1<<8)|(1u<<24));
    Player->SetField(UnitField::POWER2,114); Player->SetField(UnitField::MAXPOWER2,1000);
    H.KnownSpells.Add(78); H.ActionButtons.SetNumZeroed(144); H.ActionButtons[73]=78;
    const auto* Strike=FDbcStore::Get().Spells().GetById(78);
    if(!TestNotNull(TEXT("Original Heroic Strike definition"),Strike)) return false;
    TestEqual(TEXT("Rank-one Heroic Strike costs 15 displayed rage / 150 wire units"),Strike->ManaCost,150u);
    TestTrue(TEXT("11.4 rage is displayed as 11 and cannot afford Heroic Strike"),F.Lua.ExecuteString(TEXT(
        "assert(UnitPower('player')==11 and UnitPowerMax('player')==100); local usable,low=IsUsableAction(74); assert(not usable and low); "
        "ResourceErrors={}; ResourceProbe=CreateFrame('Frame'); ResourceProbe:RegisterEvent('UI_ERROR_MESSAGE'); "
        "ResourceProbe:SetScript('OnEvent',function(self,event,text) table.insert(ResourceErrors,text) end)")));
    const auto Reject=[&](uint32 Spell,uint8 Counter)
    {
        FCombatPacket P; P.Byte(Counter); P.Word(Spell); P.Byte(85);
        H.HandlePacket(0x130,P.Data);
        P.Data.Reset(); P.Guid(1); P.Byte(Counter); P.Word(Spell); P.Byte(85);
        H.HandlePacket(WowOpcode::SMSG_SPELL_FAILURE,P.Data);
    };
    Reject(78,1); Reject(133,2);
    TestTrue(TEXT("Private/public failures yield one resource-specific UI and combat message each"),F.Lua.ExecuteString(TEXT(
        "assert(#ResourceErrors==2 and ResourceErrors[1]==ERR_OUT_OF_RAGE and ResourceErrors[2]==ERR_OUT_OF_MANA); "
        "assert(#Rows==2 and Rows[1][12]==ERR_OUT_OF_RAGE and Rows[2][12]==ERR_OUT_OF_MANA); "
        "assert(ResourceErrors[1]~=SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW)")));
    Player->SetField(UnitField::POWER2,150);
    TestTrue(TEXT("Exactly 15 rage becomes usable without inventing extra resource"),F.Lua.ExecuteString(TEXT(
        "assert(UnitPower('player')==15); local usable,low=IsUsableAction(74); assert(usable and not low)")));
    // Use the spell's power type, including when the caster now displays a
    // different resource (forms can change between a request and its failure).
    Player->SetField(UnitField::BYTES_0,3|(1<<8));
    Reject(78,3);
    TestTrue(TEXT("A changed active resource cannot turn a rage rejection into a mana error"),F.Lua.ExecuteString(TEXT(
        "assert(#ResourceErrors==3 and ResourceErrors[3]==ERR_OUT_OF_RAGE and Rows[3][12]==ERR_OUT_OF_RAGE)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatMissFamilies,"WowUnreal.Hud.CombatMissFamilies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCombatMissFamilies::RunTest(const FString&)
{
    FCombatFixture F; auto& H=F.Connection->PacketHandler; if(!F.Listen()) return false;
    FCombatPacket P; P.Wide(1); P.Wide(Victim); P.Word(78); P.Byte(0); H.HandlePacket(0x263,P.Data);
    P.Data.Last()=1; H.HandlePacket(0x263,P.Data);
    P.Data.Last()=0; H.HandlePacket(0x260,P.Data);
    TestTrue(TEXT("Immune and proc-resist packets preserve the original miss families and suffix lengths"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(#Rows==3 and Rows[1][2]=='DAMAGE_SHIELD_MISSED' and Rows[1][12]=='IMMUNE' and Rows[1].n==12)
        assert(Rows[2][2]=='SPELL_PERIODIC_MISSED' and Rows[2][12]=='IMMUNE' and Rows[2].n==12)
        assert(Rows[3][2]=='SPELL_MISSED' and Rows[3][12]=='RESIST' and Rows[3][13]==0 and Rows[3].n==13)
    )LUA")));
    FCombatPacket Debug; Debug.Word(78); Debug.Wide(1); Debug.Byte(1); Debug.Word(1); Debug.Wide(Victim); Debug.Byte(10); Debug.Float(.1f); Debug.Float(.2f);
    H.HandlePacket(0x24B,Debug.Data);
    auto Short=Debug.Data; Short.Pop(); H.HandlePacket(0x24B,Short);
    TestEqual(TEXT("Diagnostic miss records never invent shield rows"),H.CombatHistory.Num(),3);
    // A reflected spell can itself be resisted; report that result after REFLECT.
    FCombatPacket Go; Go.Guid(1); Go.Guid(1); Go.Byte(8); Go.Word(78); Go.Word(0); Go.Word(321); Go.Byte(0); Go.Byte(1); Go.Wide(Victim); Go.Byte(11); Go.Byte(2); Go.Word(2); Go.Guid(Victim);
    H.HandlePacket(WowOpcode::SMSG_SPELL_GO,Go.Data);
    TestTrue(TEXT("Reflected result is a second ordered miss against the original caster, without duplicate launch rows"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(#Rows==6 and Rows[4][2]=='SPELL_CAST_SUCCESS')
        assert(Rows[5][2]=='SPELL_MISSED' and Rows[5][6]=='0xF130000005000001' and Rows[5][12]=='REFLECT')
        assert(Rows[6][2]=='SPELL_MISSED' and Rows[6][6]=='0x0000000000000001' and Rows[6][12]=='RESIST' and Rows[6][13]==0)
    )LUA")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatFloatingDelivery,"WowUnreal.Hud.CombatFloatingDelivery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FCombatFloatingDelivery::RunTest(const FString&)
{
    FCombatFixture F; auto& H=F.Connection->PacketHandler;
    FWowFrameDef World; World.Name=TEXT("WorldFrame"); World.Type=EWowFrameType::WorldFrame; World.bSetAllPoints=true;
    F.Frames.CreateFrame(World);
    TestTrue(TEXT("Widgetless setAllPoints WorldFrame exposes its viewport dimensions to original combat text"),F.Lua.ExecuteString(TEXT(
        "assert(WorldFrame:GetWidth()>100 and WorldFrame:GetHeight()==768); assert(WorldFrame:GetWidth()==UIParent:GetWidth())")));
    H.PlayerNameCache.Add(Victim,TEXT("Healer"));
    TestTrue(TEXT("Combat displays use Wrath defaults and a separate typed event listener"),F.Lua.ExecuteString(TEXT(R"LUA(
        for _,c in ipairs({'CombatDamage','CombatHealing','enableCombatText','CombatLogPeriodicSpells','PetMeleeDamage','PetSpellDamage'}) do assert(GetCVarDefault(c)=='1'); SetCVar(c,1) end
        CombatTextSetActiveUnit('player'); FloatingRows={}; FloatingWatch=CreateFrame('Frame')
        FloatingWatch:RegisterEvent('COMBAT_TEXT_UPDATE'); FloatingWatch:SetScript('OnEvent',function(self,event,...) table.insert(FloatingRows,{n=select('#',...),...}) end)
        -- A chat combat-log filter must never suppress floating damage numbers.
        CombatLogResetFilter(); CombatLogAddFilter('SPELL_HEAL')
    )LUA")));
    const auto Damage=[&](uint64 Destination,uint64 Source,uint32 Amount)
    {
        FCombatPacket P; P.Guid(Destination); P.Guid(Source); P.Word(133); P.Word(Amount); P.Word(0); P.Byte(4);
        P.Word(0); P.Word(0); P.Byte(0); P.Byte(0); P.Word(0); P.Word(0); P.Byte(0); H.HandlePacket(0x250,P.Data);
    };
    Damage(1,Victim,17);
    FCombatPacket Heal; Heal.Guid(1); Heal.Guid(Victim); Heal.Word(2050); Heal.Word(30); Heal.Word(5); Heal.Word(0); Heal.Byte(1); Heal.Byte(0); H.HandlePacket(0x150,Heal.Data);
    FCombatPacket Miss; Miss.Wide(Victim); Miss.Wide(1); Miss.Word(133); Miss.Byte(0); H.HandlePacket(0x260,Miss.Data);
    TestTrue(TEXT("Real packet delivery supplies stock incoming damage, critical heals and spell resists"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(#FloatingRows==3)
        assert(FloatingRows[1].n==2 and FloatingRows[1][1]=='SPELL_DAMAGE' and FloatingRows[1][2]==17 and type(FloatingRows[1][2])=='number')
        assert(FloatingRows[2].n==3 and FloatingRows[2][1]=='HEAL_CRIT' and FloatingRows[2][2]=='Healer' and FloatingRows[2][3]==30)
        assert(FloatingRows[3].n==1 and FloatingRows[3][1]=='SPELL_RESIST')
    )LUA")));
    Damage(Victim,1,24); Damage(Victim,2,99);
    int32 Active=0; uint64 Incoming=0,Outgoing=0; FWowCombatTextUi::GetCounts(F.Lua.GetState(),Active,Incoming,Outgoing);
    TestEqual(TEXT("Own outgoing damage is queued even when filtered from chat; strangers are excluded"),Outgoing,uint64(1));
    TestEqual(TEXT("A queued label is not reported as rendered before it is projected"),Active,0);
    TestTrue(TEXT("Disable both independent damage displays"),F.Lua.ExecuteString(TEXT("SetCVar('CombatDamage',0); SetCVar('enableCombatText',0)")));
    Damage(1,Victim,18); Damage(Victim,1,25);
    FWowCombatTextUi::GetCounts(F.Lua.GetState(),Active,Incoming,Outgoing);
    TestEqual(TEXT("Disabling the native display stops outgoing labels"),Outgoing,uint64(1));
    TestTrue(TEXT("Disabling incoming combat text suppresses its event without touching combat history"),F.Lua.ExecuteString(TEXT("assert(#FloatingRows==3 and CombatLogGetNumEntries(true)==7); SetCVar('CombatDamage',1); SetCVar('enableCombatText',1)")));
    // These are already-normalized history events from periodic/resource handlers.
    FWowCombatEntry E; E.Source=Victim; E.Destination=1; E.Event=TEXT("SPELL_PERIODIC_DAMAGE"); E.Values={FWowCombatValue::N(9)};
    FWowCombatLogUi::Dispatch(F.Lua.GetState(),E);
    E.Event=TEXT("SPELL_PERIODIC_DRAIN"); E.Values={FWowCombatValue::N(7),FWowCombatValue::N(0)}; FWowCombatLogUi::Dispatch(F.Lua.GetState(),E);
    E.Source=1; E.Destination=Victim; E.Event=TEXT("SPELL_PERIODIC_LEECH"); E.Values.Add(FWowCombatValue::N(4)); FWowCombatLogUi::Dispatch(F.Lua.GetState(),E);
    TestTrue(TEXT("Periodic incoming damage stays red; drains and caster gains retain their distinct event types"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(#FloatingRows==6 and FloatingRows[4][1]=='DAMAGE' and FloatingRows[4][2]==9)
        assert(FloatingRows[5][1]=='ENERGIZE' and FloatingRows[5][2]==-7 and FloatingRows[5][3]=='MANA')
        assert(FloatingRows[6][1]=='PERIODIC_ENERGIZE' and FloatingRows[6][2]==4 and FloatingRows[6][3]=='MANA')
    )LUA")));
    return true;
}
#endif
