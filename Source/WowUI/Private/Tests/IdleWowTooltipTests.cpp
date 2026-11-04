#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "WowLuaVM.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowConnectionManager.h"
#include "../LuaApi/LuaApiRegistry.h"
#include "Formats/Dbc/DbcStore.h"
#include "Mpq/MpqManager.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS && __has_include("lua.h")
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudTooltipReuse,"WowUnreal.Hud.TooltipReuse",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudTooltipReuse::RunTest(const FString&)
{
    FString Path;
    FMpqManager Mpq;
    if(!TestTrue(TEXT("Open original 12340 archives"),FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path) && Mpq.Initialize(Path))) return false;
    auto& Dbc=FDbcStore::Get();
    if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq);
    if(!Dbc.IsDeferredLoaded()) Dbc.LoadDeferred(Mpq);
    const auto& Spells=Dbc.Spells();
    FSpellTextStats Stats;
    TestEqual(TEXT("Hamstring without its glyph omits the entire conditional clause"),
        Spells.Description(1715,20,false,Stats),FString(TEXT("Maims the enemy, reducing movement speed by 50% for 15 sec.")));
    Stats.ActiveSpells.Add(58372);
    const FString Glyphed=Spells.Description(1715,20,false,Stats);
    TestTrue(TEXT("The selected glyph clause resolves its referenced effects"),
        Glyphed.Contains(TEXT("immobilize")) && !Glyphed.Contains(TEXT("$")) && !Glyphed.Contains(TEXT("[")));
    Stats.ActiveSpells.Reset();
    TestTrue(TEXT("Base Cleave selects the original single adjacent target"),Spells.Description(845,20,false,Stats).Contains(TEXT("his nearest ally")));
    Stats.ActiveSpells.Add(58366);
    TestTrue(TEXT("Glyph of Cleaving selects two adjacent targets"),Spells.Description(845,20,false,Stats).Contains(TEXT("his two nearest allies")));
    TSet<uint32> KnownSpells;
    Stats.KnownSpells=&KnownSpells;
    const FString BasicBloodrage=Spells.Description(2687,20,false,Stats);
    KnownSpells.Add(12296);
    const FString ImprovedBloodrage=Spells.Description(2687,20,false,Stats);
    TestTrue(TEXT("Known talent clauses select and evaluate the actual Bloodrage formula"),
        !BasicBloodrage.Contains(TEXT("$")) && !ImprovedBloodrage.Contains(TEXT("$")) && BasicBloodrage!=ImprovedBloodrage);
    TestTrue(TEXT("The real weapon-oil description uses the player's level gate"),
        Spells.Description(37360,67).Contains(TEXT("Cannot be used on items")) &&
        !Spells.Description(37360,68).Contains(TEXT("Cannot be used on items")) && !Spells.Description(37360,68).Contains(TEXT("$")));

    FWowLuaVM Lua;
    FWowFrameManager Frames;
    FWowEventSystem Events;
    FWowLuaContext Context;
    TStrongObjectPtr<UCanvasPanel> Canvas(NewObject<UCanvasPanel>());
    TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    if(!Lua.Initialize()) return false;
    Context.FrameManager=&Frames; Context.EventSystem=&Events; Context.ConnectionManager=Connection.Get();
    Context.EntityManager=&Connection->PacketHandler.EntityManager;
    WowLuaApi::SetContext(Lua.GetState(),&Context);
    Events.SetLuaVM(&Lua); Events.SetFrameManager(&Frames);
    Frames.SetEventSystem(&Events); Frames.Initialize(Canvas.Get()); Frames.SetSpellSource(Connection.Get());
    Events.CreateFrameObject(Frames.FindFrame(TEXT("UIParent")),TEXT("UIParent"));
    auto& Entities=*Context.EntityManager;
    Entities.LocalPlayerGuid=1;
    auto& Player=Entities.PromoteToTyped(1,WowTypeMask::PLAYER|WowTypeMask::UNIT);
    Player.SetField(UnitField::LEVEL,20);
    Player.SetField(UnitField::HEALTH,200);
    auto& Aura=Player.Auras[0];
    Aura.bActive=true; Aura.SpellId=21084; Aura.Level=20; Aura.CasterGuid=1;
    Aura.Flags=0x39; Aura.DurationMs=Aura.RemainingMs=1800000; Aura.UpdatedAt=FPlatformTime::Seconds();
    TestTrue(TEXT("Tooltip content can change from 36 rows to an aura"),Lua.ExecuteString(TEXT(R"LUA(
        ReuseTip=CreateFrame('GameTooltip','ReuseTip',UIParent)
        ReuseTip:SetOwner(UIParent,'ANCHOR_NONE'); ReuseTip:SetPoint('CENTER')
        for i=1,36 do ReuseTip:AddDoubleLine('Previous item row '..i,'Right '..i) end
        ReuseTip:Show()
        ReuseTip:SetUnitBuff('player',1)
        assert(ReuseTip:IsShown() and ReuseTip:GetSpell()=='Seal of Righteousness')
        for i=ReuseTip:NumLines()+1,36 do
            assert(not _G['ReuseTipTextLeft'..i]:IsShown(),'left row survived reuse')
            assert(not _G['ReuseTipTextRight'..i]:IsShown(),'right row survived reuse')
        end
    )LUA")));
    Aura.bActive=false;
    TestTrue(TEXT("A removed aura clears display, spell context and owner"),Lua.ExecuteString(TEXT(R"LUA(
        ReuseTip:SetUnitBuff('player',1)
        assert(not ReuseTip:IsShown() and ReuseTip:NumLines()==0 and ReuseTip:GetSpell()==nil)
        assert(not ReuseTip:IsOwned(UIParent) and ReuseTip:GetOwner()==nil)
    )LUA")));
    TestTrue(TEXT("First double line can use a larger title font than following single lines"),Lua.ExecuteString(TEXT(R"LUA(
        ReuseTip:SetOwner(UIParent,'ANCHOR_NONE')
        ReuseTip:SetPoint('CENTER')
        ReuseTip:ClearLines()
        ReuseTip:AddDoubleLine('Long weapon title','Speed 2.00')
        ReuseTipTextLeft1:SetFont('Fonts\\FRIZQT__.TTF',24)
        ReuseTipTextRight1:SetFont('Fonts\\FRIZQT__.TTF',24)
        ReuseTip:AddLine('Short')
        ReuseTip:Show()
    )LUA")));
    Frames.RefreshLayout();
    const auto Left=Frames.GetRegionRect(TEXT("ReuseTipTextLeft1"));
    const auto Right=Frames.GetRegionRect(TEXT("ReuseTipTextRight1"));
    const auto Rect=Frames.GetLayoutRect(Frames.FindFrame(TEXT("ReuseTip")));
    TestTrue(TEXT("The reused tooltip has an anchor and a screen rectangle"),Rect.Z>0 && Rect.W>0);
    TestTrue(FString::Printf(TEXT("Mixed rows preserve the column gap and border (left %.2f + %.2f, right %.2f + %.2f, tooltip %.2f + %.2f)"),
        Left.X,Left.Z,Right.X,Right.Z,Rect.X,Rect.Z),
        Left.Z>0 && Right.Z>0 && Left.X+Left.Z+8<=Right.X && Right.X+Right.Z<=Rect.X+Rect.Z-7);
    WowLuaApi::ClearContext(Lua.GetState());
    Lua.Shutdown();
    return true;
}
#endif
