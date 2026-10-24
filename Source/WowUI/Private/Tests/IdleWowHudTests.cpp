#include "Misc/AutomationTest.h"
#include "WowFrameManager.h"
#include "WowFrameXmlParser.h"
#include "WowEventSystem.h"
#include "WowLuaVM.h"
#include "../LuaApi/LuaApiRegistry.h"
#include "WowEntityManager.h"
#include "WowConnectionManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Mpq/MpqManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowAssetCache.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS && __has_include("lua.h")
namespace
{
struct FHudFixture
{
    FWowLuaVM Lua;
    FWowFrameManager Frames;
    FWowEventSystem Events;
    FWowLuaContext Context;
    TStrongObjectPtr<UCanvasPanel> Canvas{NewObject<UCanvasPanel>()};
    FHudFixture()
    {
        Lua.Initialize();
        Context.FrameManager = &Frames; Context.EventSystem = &Events;
        WowLuaApi::SetContext(Lua.GetState(), &Context);
        Events.SetLuaVM(&Lua); Events.SetFrameManager(&Frames);
        Frames.SetEventSystem(&Events); Frames.Initialize(Canvas.Get());
        Events.CreateFrameObject(Frames.FindFrame(TEXT("UIParent")), TEXT("UIParent"));
    }
    ~FHudFixture() { WowLuaApi::ClearContext(Lua.GetState()); Lua.Shutdown(); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudAnonymousHierarchy, "WowUnreal.Hud.AnonymousHierarchy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FHudAnonymousHierarchy::RunTest(const FString&)
{
    FHudFixture F;
    FWowFrameDef Parent; Parent.Name=TEXT("UnitPanel"); Parent.Parent=TEXT("UIParent"); Parent.Width=232; Parent.Height=100;
    Parent.Anchors.Add({EWowAnchorPoint::TOPLEFT,TEXT("UIParent"),EWowAnchorPoint::TOPLEFT,20,-10});
    FWowFrameDef Inner; Inner.bSetAllPoints=true;
    FWowFrameDef Second; Second.bSetAllPoints=true;
    FWowLayer Layer; FWowTextureElement Border; Border.Name=TEXT("UnitBorder"); Layer.Textures.Add(Border); Second.Layers.Add(Layer);
    Inner.Children.Add(Second); Parent.Children.Add(Inner);
    const int64 H=F.Frames.CreateFrame(Parent);
    const auto Children=F.Frames.GetChildHandles(H);
    TestEqual(TEXT("Anonymous container stays attached to its parent"),Children.Num(),1);
    if (Children.Num()!=1) return false;
    const auto Grandchildren=F.Frames.GetChildHandles(Children[0]);
    TestEqual(TEXT("Second anonymous level does not escape to UIParent"),Grandchildren.Num(),1);
    if (Grandchildren.Num()!=1) return false;
    F.Frames.RefreshLayout();
    TestTrue(TEXT("Anonymous layout has the exact panel rectangle"),F.Frames.GetLayoutRect(H).Equals(F.Frames.GetLayoutRect(Grandchildren[0]),0.01));
    auto* Image=F.Frames.GetNamedTexture(TEXT("UnitBorder"));
    const auto* Slot=Image?Cast<UCanvasPanelSlot>(Image->Slot):nullptr;
    TestTrue(TEXT("Unspecified texture rectangle fills its owner instead of natural image size"),Slot && Slot->GetAnchors().Maximum==FVector2D(1,1));
    TestTrue(TEXT("Anonymous frame has no public Lua name"),F.Lua.ExecuteString(TEXT("assert(UnitPanel:GetChildren():GetName() == nil)")));
    FWowFrameDef Health; Health.Name=TEXT("PanelHealth"); Health.Parent=TEXT("UnitPanel"); Health.Type=EWowFrameType::StatusBar;
    Health.Width=100; Health.Height=12; Health.Anchors.Add({EWowAnchorPoint::CENTER,TEXT("UnitPanel"),EWowAnchorPoint::CENTER,0,0});
    const auto HealthHandle=F.Frames.CreateFrame(Health);
    F.Frames.RefreshLayout();
    const auto* TextGroupSlot=Cast<UCanvasPanelSlot>(F.Frames.GetWidgetForHandle(Children[0])->Slot);
    const auto* HealthSlot=Cast<UCanvasPanelSlot>(F.Frames.GetWidgetForHandle(HealthHandle)->Slot);
    TestTrue(TEXT("Anonymous text groups draw above sibling health fills"),TextGroupSlot && HealthSlot && TextGroupSlot->GetZOrder()>HealthSlot->GetZOrder());
    const char* Xml="<Ui><Frame name='Sized' virtual='true'><Size x='204' y='140'/></Frame></Ui>";
    TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Xml),FCStringAnsi::Strlen(Xml));
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("HudSizeFixture"));
    TestTrue(TEXT("Direct XML Size attributes retain layout dimensions"),Parsed.Num()==1 && Parsed[0].FrameDef.Width==204 && Parsed[0].FrameDef.Height==140);
    FWowFrameDef Menu; Menu.Name=TEXT("UnpositionedMenu"); Menu.Width=150; Menu.Height=30; Menu.Parent=TEXT("UIParent");
    const auto MenuHandle=F.Frames.CreateFrame(Menu);
    TestTrue(TEXT("Unpositioned menus do not appear in the center of the world"),F.Frames.GetWidgetForHandle(MenuHandle)->GetVisibility()==ESlateVisibility::Collapsed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudScaledAnchors, "WowUnreal.Hud.ScaledAnchorsAndInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FHudScaledAnchors::RunTest(const FString&)
{
    FHudFixture F;
    TestTrue(TEXT("Construct nested scaled controls using Lua"),F.Lua.ExecuteString(TEXT(R"LUA(
        HudPanel=CreateFrame("Frame","HudPanel",UIParent)
        HudPanel:SetSize(200,100); HudPanel:SetPoint("TOPLEFT",UIParent,"TOPLEFT",100,-50)
        HudButton=CreateFrame("Button","HudButton",HudPanel)
        HudButton:SetSize(36,36); HudButton:SetPoint("TOPLEFT",HudPanel,"TOPLEFT",20,-10)
        HudPanel:SetScale(.75); HudButton:SetScale(.8)
        assert(HudPanel:GetScale()==.75)
        HudButton:ClearAllPoints(); HudButton:SetPoint('TOPLEFT',20,-10)
        HudFill=CreateFrame("Frame","HudFill",UIParent); HudFill:SetAllPoints(HudButton)
    )LUA")));
    F.Frames.RefreshLayout();
    const auto Button=F.Frames.FindFrame(TEXT("HudButton"));
    const auto Rect=F.Frames.GetLayoutRect(Button);
    TestTrue(TEXT("Numeric SetPoint overload keeps a parent-relative anchor"),F.Frames.GetFrameDef(Button)->Anchors[0].RelativeTo.IsEmpty());
    TestTrue(TEXT("Nested scale determines visible width"),FMath::IsNearlyEqual(Rect.Z,21.6,0.01));
    TestTrue(TEXT("SetAllPoints follows a different owner"),Rect.Equals(F.Frames.GetLayoutRect(F.Frames.FindFrame(TEXT("HudFill"))),0.01));
    const float Scale=F.Frames.GetScreenScale();
    TestEqual(TEXT("Click rectangle follows the drawn scaled control"),F.Frames.HitTestFrames((Rect.X+Rect.Z/2)*Scale,(Rect.Y+Rect.W/2)*Scale),Button);
    TestTrue(TEXT("Moving ancestor updates descendant anchors"),F.Lua.ExecuteString(TEXT("HudPanel:SetPoint('TOPLEFT',UIParent,'TOPLEFT',300,-70)")));
    F.Frames.RefreshLayout();
    const auto Moved=F.Frames.GetLayoutRect(Button);
    TestTrue(TEXT("Child moved with its parent"),Moved.X>Rect.X+100);
    F.Frames.SetFrameVisible(F.Frames.FindFrame(TEXT("HudPanel")),false);
    TestEqual(TEXT("Hidden ancestor prevents clicks on descendant"),F.Frames.HitTestFrames((Moved.X+5)*Scale,(Moved.Y+5)*Scale),int64(-1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudLiveResources, "WowUnreal.Hud.LiveResourcesAndStance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FHudLiveResources::RunTest(const FString&)
{
    FHudFixture F;
    TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    auto& Entities=Connection->PacketHandler.EntityManager;
    Entities.LocalPlayerGuid=1;
    auto& Unit=Entities.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    Unit.SetField(UnitField::BYTES_0,(1u<<24)|(1u<<8)|1u);
    Unit.SetField(UnitField::BYTES_2,17u<<24);
    Unit.SetField(UnitField::HEALTH,125); Unit.SetField(UnitField::MAXHEALTH,200);
    Unit.SetField(UnitField::POWER1+1,900); Unit.SetField(UnitField::MAXPOWER1+1,1000);
    F.Context.EntityManager=&Entities; F.Context.ConnectionManager=Connection.Get();
    TestTrue(TEXT("Resource defaults and rage units match server fields"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(UnitHealth('player')==125 and UnitHealthMax('player')==200)
        assert(UnitPower('player')==90 and UnitPowerMax('player')==100)
        assert(UnitPower('player',1,true)==900 and GetBonusBarOffset()==1)
        local _,_,r,g,b,a,_,_,dock=GetChatWindowInfo(2)
        assert(r==0 and g==0 and b==0 and type(a)=='number' and type(dock)=='number')
        assert(type(GetChatWindowMessages(1))=='string' and select('#',GetChatWindowChannels(1))==0)
        SetChatWindowLocked(2,true); assert(select(8,GetChatWindowInfo(2))==true); SetChatWindowLocked(2,false)
        assert(GetChatWindowSavedPosition(1)==nil)
        SetChatWindowSavedPosition(2,'TOPRIGHT',.2,.4)
        local point,x,y=GetChatWindowSavedPosition(2); assert(point=='TOPRIGHT' and math.abs(x-.2)<.0001 and math.abs(y-.4)<.0001)
        LiveHealth=CreateFrame('StatusBar','LiveHealth',UIParent)
        LiveHealth:RegisterEvent('UNIT_HEALTH')
        LiveHealth:SetScript('OnEvent',function(self) self:SetMinMaxValues(0,UnitHealthMax('player')); self:SetValue(UnitHealth('player')) end)
    )LUA")));
    F.Events.FireEvent(TEXT("UNIT_HEALTH"),{TEXT("player")});
    auto* Bar=F.Frames.GetStatusBarWidget(F.Frames.FindFrame(TEXT("LiveHealth")));
    TestTrue(TEXT("Server event updates visible health fill"),Bar && FMath::IsNearlyEqual(Bar->GetPercent(),.625f));
    Unit.SetField(UnitField::HEALTH,50);
    F.Events.FireEvent(TEXT("UNIT_HEALTH"),{TEXT("player")});
    TestTrue(TEXT("Subsequent health change updates without a login timer"),Bar && FMath::IsNearlyEqual(Bar->GetPercent(),.25f));
    Unit.SetField(UnitField::BYTES_2,18u<<24);
    TestTrue(TEXT("Different stance selects its saved action page"),F.Lua.ExecuteString(TEXT("assert(GetBonusBarOffset()==2)")));
    TestTrue(TEXT("Chat receiver expects the WotLK event tuple"),F.Lua.ExecuteString(TEXT(R"LUA(
        ChatProbe=CreateFrame('Frame','ChatProbe',UIParent)
        ChatProbe:RegisterEvent('CHAT_MSG_SYSTEM'); ChatProbe:RegisterEvent('CHAT_MSG_SAY')
        ChatProbe:SetScript('OnEvent',function(self,event,...)
            local text,author,language,channel,_,_,zone,index,_,_,line,guid=...
            assert(type(text)=='string' and type(zone)=='number' and type(index)=='number' and type(line)=='number')
            self.lastEvent=event; self.lastText=text
        end)
    )LUA")));
    F.Events.FireChatMessage(0,0,0,TEXT(""),TEXT("Server welcome"),TEXT(""));
    TestTrue(TEXT("System message retains its type and text"),F.Lua.ExecuteString(TEXT("assert(ChatProbe.lastEvent=='CHAT_MSG_SYSTEM' and ChatProbe.lastText=='Server welcome')")));
    TestTrue(TEXT("Raw opcode does not emit a second empty message"),FWowEventSystem::OpcodeToEvent(0x0096).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudMpqIcons, "WowUnreal.Hud.MpqActionIcons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FHudMpqIcons::RunTest(const FString&)
{
    FString DataPath;
    if (!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),DataPath)) { AddError(TEXT("Supply -wowdata=<existing Data>")); return false; }
    FMpqManager Mpq;
    if (!TestTrue(TEXT("Local MPQ opens"),Mpq.Initialize(DataPath))) return false;
    if (!FDbcStore::Get().IsLoaded()) FDbcStore::Get().LoadAll(Mpq);
    if (!FDbcStore::Get().IsDeferredLoaded()) FDbcStore::Get().LoadDeferred(Mpq);
    FHudFixture F;
    FWowAssetCache Cache;
    F.Frames.SetMpqManager(&Mpq); F.Frames.SetAssetCache(&Cache);
    TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    F.Context.ConnectionManager=Connection.Get();
    Connection->PacketHandler.ActionButtons.SetNumZeroed(144);
    Connection->PacketHandler.ActionButtons[72]=6603;
    Connection->PacketHandler.ActionButtons[73]=78;
    FWowFrameDef Button; Button.Name=TEXT("SavedAction"); Button.Type=EWowFrameType::Button;
    Button.Parent=TEXT("UIParent"); Button.Width=Button.Height=36;
    Button.Anchors.Add({EWowAnchorPoint::TOPLEFT,TEXT("UIParent"),EWowAnchorPoint::TOPLEFT,10,-10});
    FWowLayer Layer; FWowTextureElement Icon; Icon.Name=TEXT("SavedActionIcon"); Layer.Textures.Add(Icon); Button.Layers.Add(Layer);
    F.Frames.CreateFrame(Button);
    TestTrue(TEXT("Saved spell resolves its real MPQ icon"),F.Lua.ExecuteString(TEXT("SavedActionIcon:SetTexture(GetActionTexture(73))")));
    auto* Image=F.Frames.GetNamedTexture(TEXT("SavedActionIcon"));
    auto* First=Image?Image->GetBrush().GetResourceObject():nullptr;
    TestTrue(TEXT("Icon has an actual runtime texture"),First && Image->GetRenderOpacity()==1);
    TestTrue(TEXT("Changing action replaces the icon"),F.Lua.ExecuteString(TEXT("SavedActionIcon:SetTexture(GetActionTexture(74))")));
    TestTrue(TEXT("Different spell has different image"),Image && Image->GetBrush().GetResourceObject()!=First);
    TestTrue(TEXT("Empty action clears its previous image"),F.Lua.ExecuteString(TEXT("SavedActionIcon:SetTexture(GetActionTexture(75))")));
    TestTrue(TEXT("Old spell icon is no longer drawn"),Image && !Image->GetBrush().GetResourceObject() && Image->GetRenderOpacity()==0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplaySpellRange,"WowUnreal.Gameplay.SpellRangeAndResources",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplaySpellRange::RunTest(const FString&)
{
    FString Path;
    if (!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) { AddError(TEXT("Supply -wowdata")); return false; }
    FMpqManager Mpq; if (!Mpq.Initialize(Path)) return false;
    if (!FDbcStore::Get().IsLoaded()) FDbcStore::Get().LoadAll(Mpq);
    if (!FDbcStore::Get().IsDeferredLoaded()) FDbcStore::Get().LoadDeferred(Mpq);
    FHudFixture F;
    TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    auto& Entities=Connection->PacketHandler.EntityManager; Entities.LocalPlayerGuid=1;
    auto& Player=Entities.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    Player.SetField(UnitField::BYTES_0,(1u<<24)|(1u<<8)|1u);
    auto& Target=Entities.PromoteToTyped(2,WowTypeMask::OBJECT|WowTypeMask::UNIT);
    FWowGroupMember Member; Member.Guid=2; Connection->PacketHandler.GroupInfo.Members.Add(Member);
    F.Context.EntityManager=&Entities; F.Context.ConnectionManager=Connection.Get();
    Connection->PacketHandler.KnownSpells.Append({6603,78});
    Connection->PacketHandler.ActionButtons.SetNumZeroed(144);
    Connection->PacketHandler.ActionButtons[72]=6603; Connection->PacketHandler.ActionButtons[73]=78;
    Target.Movement.Position=FVector(20,0,0);
    TestTrue(TEXT("A melee action and spell both report numeric out-of-range"),F.Lua.ExecuteString(TEXT("assert(IsActionInRange(73,'party1')==0 and IsSpellInRange('Heroic Strike','party1')==0)")));
    Target.Movement.Position=FVector(3,0,0);
    TestTrue(TEXT("Moving into melee range changes both APIs"),F.Lua.ExecuteString(TEXT("assert(IsActionInRange(73,'party1')==1 and IsSpellInRange('Heroic Strike','party1')==1)")));
    TestTrue(TEXT("Missing target is unknown rather than assumed in range"),F.Lua.ExecuteString(TEXT("assert(IsSpellInRange('Heroic Strike','missing')==nil)")));
    TestTrue(TEXT("Implicit action target does not default to the player"),F.Lua.ExecuteString(TEXT("assert(IsActionInRange(73)==nil)")));
    const auto* Spell=FDbcStore::Get().Spells().GetById(78);
    TestTrue(TEXT("Real spell cost and range exist"),Spell && Spell->ManaCost>0 && FDbcStore::Get().Spells().GetRange(Spell->RangeIndex));
    if (!Spell) return false;
    Player.SetField(UnitField::POWER1+1,0);
    TestTrue(TEXT("Rage ability is unavailable with zero rage"),F.Lua.ExecuteString(TEXT("local usable,noPower=IsUsableAction(74); assert(not usable and noPower)")));
    Player.SetField(UnitField::POWER1+1,Spell->ManaCost);
    TestTrue(TEXT("Rage ability becomes usable at actual DBC cost"),F.Lua.ExecuteString(TEXT("local usable,noPower=IsUsableAction(74); assert(usable and not noPower)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplaySecureAction,"WowUnreal.Gameplay.VisibleActionClick",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FGameplaySecureAction::RunTest(const FString&)
{
    FHudFixture F;
    TestTrue(TEXT("A stance button carries its live action slot"),F.Lua.ExecuteString(TEXT("PageButton=CreateFrame('CheckButton','BonusActionButton1',UIParent); PageButton:SetAttribute('type','action'); PageButton:SetAttribute('action',85)")));
    int32 Slot=INDEX_NONE;
    F.Frames.OnSecureActionDispatch.BindLambda([&Slot](int32 Value){Slot=Value;});
    F.Frames.DispatchClick(F.Frames.FindFrame(TEXT("BonusActionButton1")),TEXT("LeftButton"),false);
    TestEqual(TEXT("Mouse action uses current page, not a fixed battle slot"),Slot,84);
    return true;
}
#endif
