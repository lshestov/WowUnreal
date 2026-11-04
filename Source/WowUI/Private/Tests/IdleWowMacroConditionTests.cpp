#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "WowLuaVM.h"
#include "../LuaApi/LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Mpq/MpqManager.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS && __has_include("lua.h")
namespace
{
struct FMacroConditionFixture
{
    FWowLuaVM Lua;
    FWowLuaContext Context;
    TStrongObjectPtr<UWowConnectionManager> Connection{NewObject<UWowConnectionManager>()};
    FMacroConditionFixture()
    {
        Lua.Initialize();
        Context.ConnectionManager=Connection.Get();
        Context.EntityManager=&Connection->PacketHandler.EntityManager;
        WowLuaApi::SetContext(Lua.GetState(),&Context);
    }
    ~FMacroConditionFixture() { WowLuaApi::ClearContext(Lua.GetState()); Lua.Shutdown(); }
};
bool OpenClientData(FMpqManager& Mpq)
{
    FString Path;
    return FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path) && Mpq.Initialize(Path);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudMacroConditions,"WowUnreal.Hud.MacroConditions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudMacroConditions::RunTest(const FString&)
{
    FMpqManager Mpq;
    if(!TestTrue(TEXT("Open local build-12340 client archives"),OpenClientData(Mpq))) return false;
    TArray<uint8> Bytes;
    if(!TestTrue(TEXT("Read original ChatFrame handlers"),Mpq.ReadFile(TEXT("Interface\\FrameXML\\ChatFrame.lua"),Bytes))) return false;
    Bytes.Add(0);
    const FString Source=UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData()));
    const int32 Begin=Source.Find(TEXT("function SecureCmdItemParse(item)"));
    const int32 End=Source.Find(TEXT("-- Pre-populate the secure command hash table"));
    if(!TestTrue(TEXT("Find stock item parser and secure command section"),Begin>=0 && End>Begin)) return false;
    FMacroConditionFixture F;
    if(!TestTrue(TEXT("Load unmodified original secure command handlers"),F.Lua.ExecuteString(TEXT("local SecureCmdList={}\n")+Source.Mid(Begin,End-Begin)+TEXT("\nQAMacroCommands=SecureCmdList")))) return false;
    FString Checks;
    if(!TestTrue(TEXT("Read dedicated macro condition scenarios"),FFileHelper::LoadFileToString(Checks,*FPaths::Combine(FPaths::ProjectDir(),TEXT("Scripts/Verification/MacroConditions.lua"))))) return false;
    return TestTrue(TEXT("Condition matrix and actual stock command success/refusal boundaries"),F.Lua.ExecuteString(Checks,TEXT("=MacroConditions")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudMacroConditionState,"WowUnreal.Hud.MacroConditionState",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudMacroConditionState::RunTest(const FString&)
{
    FMpqManager Mpq;
    if(!TestTrue(TEXT("Open local client DBCs"),OpenClientData(Mpq))) return false;
    auto& Dbc=FDbcStore::Get();
    if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq);
    FMacroConditionFixture F;
    auto& Entities=F.Connection->PacketHandler.EntityManager;
    Entities.LocalPlayerGuid=1;
    auto& Player=Entities.PromoteToTyped(1,WowTypeMask::PLAYER|WowTypeMask::UNIT);
    Player.SetField(UnitField::HEALTH,100); Player.SetField(UnitField::MAXHEALTH,100);
    const auto Check=[&](const TCHAR* Name,const TCHAR* Code) { return TestTrue(Name,F.Lua.ExecuteString(Code)); };
    Player.Movement.MoveFlags=0x02000000;
    Check(TEXT("Actual FLYING bit selects flying macro branch"),TEXT("assert(IsFlying() and SecureCmdOptionParse('[flying] Fly; Ground')=='Fly')"));
    Player.Movement.MoveFlags=0x00800000;
    Check(TEXT("DESCENDING alone does not imply flight"),TEXT("assert(not IsFlying() and SecureCmdOptionParse('[noflying] Ground')=='Ground')"));
    Player.Movement.MoveFlags=0x01000000;
    Check(TEXT("CAN_FLY without flight remains grounded"),TEXT("assert(not IsFlying())"));
    Player.Movement.MoveFlags=0x00000002;
    Check(TEXT("Walking backward is not falling"),TEXT("assert(not IsFalling())"));
    Player.Movement.MoveFlags=0x00001000;
    Check(TEXT("Actual FALLING bit reaches Lua"),TEXT("assert(IsFalling())"));
    Player.Movement.MoveFlags=0x00200000;
    Check(TEXT("Swimming does not imply flight or falling"),TEXT("assert(IsSwimming() and not IsFlying() and not IsFalling() and SecureCmdOptionParse('[swimming,noflying] Swim')=='Swim')"));
    auto& Aura=Player.Auras[0]; Aura.bActive=true;
    for(uint32 Spell : {1784u,1785u,1786u,1787u,5215u,6783u,9913u,58984u})
    {
        if(!TestNotNull(TEXT("Client contains the stealth rank"),Dbc.Spells().GetById(Spell))) return false;
        Aura.SpellId=Spell; Aura.DurationMs=0;
        Check(TEXT("All stealth ranks and Shadowmeld satisfy stealth"),TEXT("assert(IsStealthed() and SecureCmdOptionParse('[stealth] Hidden; Visible')=='Hidden')"));
    }
    Aura.bActive=false;
    Check(TEXT("Removed stealth aura cannot satisfy the macro condition"),TEXT("assert(not IsStealthed() and SecureCmdOptionParse('[nostealth] Visible')=='Visible')"));
    Aura.bActive=true; Aura.SpellId=1785; Aura.DurationMs=1000; Aura.RemainingMs=500; Aura.UpdatedAt=FPlatformTime::Seconds()-2;
    Check(TEXT("Expired aura is ignored before the next removal update"),TEXT("assert(not IsStealthed())"));
    Aura.UpdatedAt=FPlatformTime::Seconds(); Aura.RemainingMs=10000;
    Check(TEXT("Unexpired timed stealth still satisfies the condition"),TEXT("assert(IsStealthed())"));
    Aura.SpellId=66; Aura.DurationMs=0;
    Check(TEXT("Invisibility is not stealth"),TEXT("assert(not IsStealthed())"));
    Aura.bActive=false;
    Check(TEXT("Implicit and explicit target semantics use actual unit APIs"),TEXT(R"LUA(
        local action,unit=SecureCmdOptionParse('[@player,party] Other; [@player,help,nodead] Self')
        assert(action=='Self' and unit=='player')
        action,unit=SecureCmdOptionParse('[nomod] Default; [] Default')
        assert(action=='Default' and unit==nil)
        action,unit=SecureCmdOptionParse('[help,@focus] Missing; [help,@player] Self')
        assert(action=='Self' and unit=='player')
    )LUA"));
    bool Control=false;
    F.Context.KeyDown=[&](const FKey& Key) { return Control && Key==EKeys::LeftControl; };
    Check(TEXT("SELFCAST condition follows the actual saved assignment"),TEXT("SetModifiedClick('SELFCAST','CTRL'); assert(SecureCmdOptionParse('[mod:SELFCAST] Self; Plain')=='Plain')"));
    Control=true;
    Check(TEXT("SELFCAST condition accepts the configured key and uppercase argument"),TEXT("assert(SecureCmdOptionParse('[modifier:SELFCAST] Self; Plain')=='Self' and SecureCmdOptionParse('[nomod:SELFCAST] Hidden')==nil)"));
    return true;
}
#endif
