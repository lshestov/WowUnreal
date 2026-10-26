#include "Misc/AutomationTest.h"
#include "WowFrameManager.h"
#include "WowFrameXmlParser.h"
#include "WowEventSystem.h"
#include "WowLuaVM.h"
#include "../LuaApi/LuaApiRegistry.h"
#include "WowEntityManager.h"
#include "WowConnectionManager.h"
#include "WowMacros.h"
#include "WowCombatLogUi.h"
#include "WowMultiLineEditBox.h"
#include "WowBackdropWidget.h"
#include "WowTextBlock.h"
#include "WowSavedVariables.h"
#include "WowAddonLoader.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudClassTalents,"WowUnreal.Hud.ClassTalents",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudClassTalents::RunTest(const FString&)
{
    FString DataPath; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),DataPath)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(DataPath)) return false;
    if(!FDbcStore::Get().IsLoaded()) FDbcStore::Get().LoadAll(Mpq);
    if(!FDbcStore::Get().IsDeferredLoaded()) FDbcStore::Get().LoadDeferred(Mpq);
    FHudFixture F; F.Frames.SetMpqManager(&Mpq); FWowAssetCache Cache; F.Frames.SetAssetCache(&Cache);
    TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    auto& H=Connection->PacketHandler; auto& E=H.EntityManager; E.LocalPlayerGuid=1;
    auto& P=E.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER); P.SetField(UnitField::BYTES_0,(1u<<8)|1u); P.SetField(UnitField::LEVEL,20);
    H.TalentGroups.SetNum(1); H.HasTalentInfo=true; H.FreeTalentPoints=11; H.KnownSpells.Append({6603,78,2457}); ++H.PlayerTalentRevision;
    F.Context.ConnectionManager=Connection.Get(); F.Context.EntityManager=&E;
    TestTrue(TEXT("Class tabs, real talent names/icons, rank views and preview events use the canonical API"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(GetNumTalentTabs()==3 and GetNumTalentGroups()==1 and GetActiveTalentGroup()==1 and GetUnspentTalentPoints()==11)
        assert(GetNumTalentTabs(false,true)==0)
        for tab=1,3 do
            local name,icon,spent,background,preview=GetTalentTabInfo(tab)
            assert(name and icon and icon:find('Interface') and spent==0 and background and preview==0)
            if name=='Arms' then ArmsTab=tab end
        end
        assert(ArmsTab)
        for i=1,GetNumTalents(ArmsTab) do
            local name,icon,tier,col,rank,maxRank,exceptional,available,previewRank=GetTalentInfo(ArmsTab,i)
            if name then assert(icon and tier>=1 and col>=1 and col<=4 and rank==0 and previewRank==0 and maxRank>=1 and maxRank<=5) end
            if name=='Improved Heroic Strike' then HeroicIndex=i end
        end
        assert(HeroicIndex and GetTalentLink(ArmsTab,HeroicIndex):find('talent:124:'))
        PreviewObserver=CreateFrame('Frame'); PreviewObserver:RegisterEvent('PREVIEW_TALENT_POINTS_CHANGED')
        PreviewObserver:SetScript('OnEvent',function(self,event,index,tab,group,delta) if index then assert(type(index)=='number' and type(tab)=='number' and group==1); LastPreviewDelta=delta end end)
        AddPreviewTalentPoints(ArmsTab,HeroicIndex,1); assert(LastPreviewDelta==1 and GetGroupPreviewTalentPointsSpent()==1)
        AddPreviewTalentPoints(ArmsTab,HeroicIndex,8)
        assert(select(5,GetTalentInfo(ArmsTab,HeroicIndex))==0 and select(9,GetTalentInfo(ArmsTab,HeroicIndex))==3)
        assert(GetUnspentTalentPoints()==11 and GetPreviewTalentPointsSpent(ArmsTab)==3 and GetGroupPreviewTalentPointsSpent()==3); assert(GetTalentLink(ArmsTab,HeroicIndex,false,false,1,true):find('talent:124:2'))
        AddPreviewTalentPoints(ArmsTab,HeroicIndex,-1); assert(GetGroupPreviewTalentPointsSpent()==2 and LastPreviewDelta==-1)
        for i=1,GetNumTalents(ArmsTab) do if select(3,GetTalentInfo(ArmsTab,i))>=5 then AddPreviewTalentPoints(ArmsTab,i,1); assert(select(9,GetTalentInfo(ArmsTab,i))==0); break end end
        ResetPreviewTalentPoints(ArmsTab); assert(GetGroupPreviewTalentPointsSpent()==0)
        for i=1,GetNumTalents(ArmsTab) do AddPreviewTalentPoints(ArmsTab,i,9) end
        assert(GetGroupPreviewTalentPointsSpent()>0 and GetGroupPreviewTalentPointsSpent()<=11)
        ResetGroupPreviewTalentPoints(); assert(GetGroupPreviewTalentPointsSpent()==0)
        AddPreviewTalentPoints(ArmsTab,HeroicIndex,3); LearnPreviewTalents()
        assert(select(5,GetTalentInfo(ArmsTab,HeroicIndex))==0 and GetGroupPreviewTalentPointsSpent()==3)
    )LUA")));
    TestTrue(TEXT("Preview submission never mutates authoritative talent state"),H.TalentGroups[0].Talents.IsEmpty() && H.FreeTalentPoints==11);
    H.TalentGroups[0].Talents.Add({124,2}); H.FreeTalentPoints=8; ++H.PlayerTalentRevision;
    TestTrue(TEXT("Server acknowledgement replaces the preview with the real rank"),F.Lua.ExecuteString(TEXT("assert(select(5,GetTalentInfo(ArmsTab,HeroicIndex))==3 and select(9,GetTalentInfo(ArmsTab,HeroicIndex))==3 and GetGroupPreviewTalentPointsSpent()==0 and GetUnspentTalentPoints()==8); TalentTip=CreateFrame('GameTooltip','TalentTip',UIParent); TalentTip:SetTalent(ArmsTab,HeroicIndex)")));
    const auto* Tip=F.Frames.GetTooltipState(F.Frames.FindFrame(TEXT("TalentTip")));
    TestTrue(TEXT("Talent tooltip resolves the real spell rather than a placeholder"),Tip && Tip->Lines.Num()>=2 && Tip->Lines[0].Text==TEXT("Improved Heroic Strike"));
    TestTrue(TEXT("Rage formulas are evaluated and talent tooltips show ranks without a cast-time line"),Tip && Tip->Lines.Num()>=3 && Tip->Lines[1].Text==TEXT("Rank 3/3") && !FDbcStore::Get().Spells().Description(12282).Contains(TEXT("$")) && FDbcStore::Get().Spells().Description(12282).Contains(TEXT("1 rage")));
    H.TalentGroups.SetNum(2); H.TalentGroups[1].Talents.Add({130,4}); ++H.PlayerTalentRevision;
    TestTrue(TEXT("Inactive spec points derive from the same total budget"),F.Lua.ExecuteString(TEXT("assert(GetNumTalentGroups()==2 and GetUnspentTalentPoints(false,false,2)==6); SetActiveTalentGroup(2); assert(GetActiveTalentGroup()==1)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudSpellBookViews,"WowUnreal.Hud.SpellBookViews",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudSpellBookViews::RunTest(const FString&)
{
    FString DataPath; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),DataPath)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(DataPath)) return false;
    if(!FDbcStore::Get().IsLoaded()) FDbcStore::Get().LoadAll(Mpq);
    if(!FDbcStore::Get().IsDeferredLoaded()) FDbcStore::Get().LoadDeferred(Mpq);
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    auto& E=Connection->PacketHandler.EntityManager; E.LocalPlayerGuid=1;
    auto& P=E.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    P.SetField(UnitField::BYTES_0,(8u<<8)|1u); // Human mage.
    F.Context.ConnectionManager=Connection.Get(); F.Context.EntityManager=&E;
    auto& Known=Connection->PacketHandler.KnownSpells; Known.Append({6603,2259,133,143,145,116,1449,470});
    uint32 Hidden=0,NotInBook=0,Recipe=0;
    for(const auto& S:FDbcStore::Get().Spells().GetAll())
    {
        if(Known.Contains(S.ID)) continue;
        if(!Hidden && (S.Attributes&0x80)) Hidden=S.ID;
        if(!NotInBook && (S.AttributesEx4&0x8000)) NotInBook=S.ID;
        if(!Recipe && (S.Attributes&0x20)) Recipe=S.ID;
        if(Hidden && NotInBook && Recipe) break;
    }
    if(!TestTrue(TEXT("Real DBC contains the three excluded spell families"),Hidden && NotInBook && Recipe)) return false;
    Known.Append({Hidden,NotInBook,Recipe});
    TestTrue(TEXT("Spellbook filters internal spells, recipes and mounts, sorts skill names and maps highest ranks"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(GetNumSpellTabs()==4)
        local expected={'General','Arcane','Fire','Frost'}; local count,highest=0,0
        for i=1,4 do
            local name,icon,offset,num,highOffset,highNum=GetSpellTabInfo(i)
            assert(name==expected[i] and icon and offset==count and highOffset==highest)
            count=count+num; highest=highest+highNum
            for j=1,highNum do assert(GetSpellName(GetKnownSlotFromHighestRankSlot(highOffset+j),'spell')) end
        end
        assert(count==7 and highest==5 and GetSpellName(8,'spell')==nil)
        assert(FindSpellBookSlotByID(2259) and not FindSpellBookSlotByID(470) and GetNumCompanions('MOUNT')==1)
        local _,_,offset,num,highOffset,highNum=GetSpellTabInfo(3)
        assert(num==3 and highNum==1)
        local name,rank=GetSpellName(GetKnownSlotFromHighestRankSlot(highOffset+1),'spell')
        assert(name=='Fireball' and rank=='Rank 3')
        assert(select(2,GetSpellBookItemInfo(offset+1,'spell'))==133)
        assert(select(2,GetSpellBookItemInfo(offset+3,'spell'))==145)
        assert(GetKnownSlotFromHighestRankSlot(6)==0)
    )LUA")));
    Known.Remove(145);
    TestTrue(TEXT("Learning/unlearning invalidates both views without stale rank slots"),F.Lua.ExecuteString(TEXT("local _,_,_,count,offset,highest=GetSpellTabInfo(3); assert(count==2 and highest==1); assert(select(2,GetSpellName(GetKnownSlotFromHighestRankSlot(offset+1),'spell'))=='Rank 2')")));
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudLifecycle,"WowUnreal.Hud.VisibilityAndInitialization",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudLifecycle::RunTest(const FString&)
{
    FHudFixture F;
    TestTrue(TEXT("Visibility transitions are idempotent and propagate to children"),F.Lua.ExecuteString(TEXT(R"LUA(
        LifeParent=CreateFrame('Frame','LifeParent',UIParent)
        LifeParent:SetSize(100,100); assert(LifeParent:GetHeight()==100 and LifeParent:GetWidth()==100)
        LifeParent:SetSize(100,100); LifeParent:SetPoint('CENTER',UIParent,'CENTER',0,0); LifeParent:Hide()
        LifeChild=CreateFrame('Frame','LifeChild',LifeParent); LifeChild:SetAllPoints(LifeParent)
        local ps,ph,cs,ch=0,0,0,0
        LifeParent:SetScript('OnShow',function(self) ps=ps+1; self:Show() end)
        LifeParent:SetScript('OnHide',function(self) ph=ph+1; self:Hide() end)
        LifeChild:SetScript('OnShow',function() cs=cs+1 end)
        LifeChild:SetScript('OnHide',function() ch=ch+1 end)
        LifeParent:Show(); LifeParent:Show(); assert(ps==1 and cs==1)
        LifeParent:Hide(); LifeParent:Hide(); assert(ph==1 and ch==1)
        LifeChild:Hide(); LifeParent:Show(); assert(ps==2 and cs==1 and ch==1)
        LifeChild:Show(); assert(cs==2)
        function UiAudit_OnLoad(self) self.initialized=true end
    )LUA")));
    const char* Xml="<Ui><Frame name='InitializedFrame'><Size x='40' y='40'/><Anchors><Anchor point='CENTER'/></Anchors><Scripts><OnLoad function='UiAudit_OnLoad'/></Scripts></Frame></Ui>";
    TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Xml),FCStringAnsi::Strlen(Xml));
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("InitializationFixture"));
    if(!TestEqual(TEXT("Fixture frame parsed"),Parsed.Num(),1)) return false;
    F.Frames.CreateFrame(Parsed[0].FrameDef);
    TestTrue(TEXT("Named OnLoad initializes its own persistent frame object"),F.Lua.ExecuteString(TEXT("assert(InitializedFrame.initialized==true)")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudStockVisibility,"WowUnreal.Hud.StockFrameVisibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudStockVisibility::RunTest(const FString&)
{
    FHudFixture F;
    TestTrue(TEXT("Construct stock-named frames and a hidden intermediate ancestor"),F.Lua.ExecuteString(TEXT(R"LUA(
        VisibilityRoot=CreateFrame('Frame','VisibilityRoot',UIParent); VisibilityRoot:SetAllPoints(UIParent)
        VisibilityBranch=CreateFrame('Frame','VisibilityBranch',VisibilityRoot); VisibilityBranch:SetAllPoints(VisibilityRoot)
        VisibilityLeaf=CreateFrame('Button','VisibilityLeaf',VisibilityBranch); VisibilityLeaf:SetAllPoints(VisibilityBranch)
        VisibilityBranch:Hide(); VisibilityRoot:Hide(); VisibilityRoot:Show()
        VisibilityNewLeaf=CreateFrame('Button','VisibilityNewLeaf',VisibilityBranch); VisibilityNewLeaf:SetAllPoints(VisibilityBranch)
        assert(VisibilityLeaf:IsShown() and not VisibilityLeaf:IsVisible())
        ActionButton1=CreateFrame('CheckButton','ActionButton1',UIParent); ActionButton1:SetAllPoints(UIParent); ActionButton1:Hide()
        BonusActionButton1=CreateFrame('CheckButton','BonusActionButton1',UIParent); BonusActionButton1:SetAllPoints(UIParent); BonusActionButton1:Hide()
        ZoneTextFrame=CreateFrame('Frame','ZoneTextFrame',UIParent); ZoneTextFrame:SetAllPoints(UIParent)
        TestFlash=ZoneTextFrame:CreateTexture('TestFlash'); TestFlash:SetAllPoints(ZoneTextFrame); TestFlash:Show()
    )LUA")));
    const auto Leaf=F.Frames.FindFrame(TEXT("VisibilityLeaf"));
    TestTrue(TEXT("Creating and positioning a child respects its hidden parent"),F.Frames.GetWidgetForHandle(F.Frames.FindFrame(TEXT("VisibilityNewLeaf")))->GetVisibility()==ESlateVisibility::Collapsed);
    TestTrue(TEXT("Showing a grandparent cannot reveal a hidden branch"),F.Frames.GetWidgetForHandle(Leaf)->GetVisibility()==ESlateVisibility::Collapsed);
    F.Frames.RefreshLayout(); F.Frames.SyncChildVisibility();
    TestTrue(TEXT("Layout and bootstrap preserve the hidden branch"),F.Frames.GetWidgetForHandle(Leaf)->GetVisibility()==ESlateVisibility::Collapsed);
    TestTrue(TEXT("Stock names do not override FrameXML visibility"),F.Lua.ExecuteString(TEXT("assert(not ActionButton1:IsShown() and not BonusActionButton1:IsShown()); assert(ZoneTextFrame:IsShown() and TestFlash:IsShown())")));
    TestTrue(TEXT("Native action widgets remain collapsed"),F.Frames.GetWidgetForHandle(F.Frames.FindFrame(TEXT("BonusActionButton1")))->GetVisibility()==ESlateVisibility::Collapsed);
    TestTrue(TEXT("Showing the intermediate parent restores its shown descendants"),F.Lua.ExecuteString(TEXT("VisibilityBranch:Show(); assert(VisibilityLeaf:IsVisible())")));
    TestTrue(TEXT("Native descendant is restored"),F.Frames.GetWidgetForHandle(Leaf)->GetVisibility()!=ESlateVisibility::Collapsed);

    const char* Xml="<Ui><Frame name='HiddenTemplate' virtual='true' hidden='true'><Size x='40' y='40'/><Anchors><Anchor point='CENTER'/></Anchors></Frame><Frame name='ExplicitVisible' inherits='HiddenTemplate' hidden='false'/><Frame name='InheritedHidden' inherits='HiddenTemplate'/></Ui>";
    TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Xml),FCStringAnsi::Strlen(Xml));
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("StockVisibilityFixture"));
    if(!TestEqual(TEXT("Visibility fixture parsed"),Parsed.Num(),3)) return false;
    F.Frames.RegisterTemplate(Parsed[0].FrameDef.Name,Parsed[0].FrameDef);
    F.Frames.CreateFrame(Parsed[1].FrameDef); F.Frames.CreateFrame(Parsed[2].FrameDef);
    TestTrue(TEXT("Explicit XML visibility overrides a hidden template"),F.Lua.ExecuteString(TEXT("assert(ExplicitVisible:IsShown() and not InheritedHidden:IsShown())")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudOptionAnchors,"WowUnreal.Hud.StockOptionsAnchors",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudOptionAnchors::RunTest(const FString&)
{
    FHudFixture F;
    TestTrue(TEXT("Original options-list SetPoint overloads"),F.Lua.ExecuteString(TEXT(R"LUA(
        OptionPanel=CreateFrame('Frame','OptionPanel',UIParent); OptionPanel:SetSize(300,400); OptionPanel:SetPoint('TOPLEFT',UIParent,'TOPLEFT',100,-70)
        OptionRow=CreateFrame('Button','OptionRow',OptionPanel); OptionRow:SetSize(175,18); OptionRow:SetPoint('TOPLEFT',OptionPanel,0,-8)
        OptionRowText=OptionRow:CreateFontString('OptionRowText'); OptionRowText:SetText('Controls'); OptionRowText:SetPoint('LEFT',8,2)
        OptionRowToggle=CreateFrame('Button','OptionRowToggle',OptionRow); OptionRowToggle:SetSize(14,14); OptionRowToggle:SetPoint('TOPRIGHT',OptionRow,'TOPRIGHT',-6,-1)
        OptionRowText:SetPoint('RIGHT','$parentToggle','LEFT',-2,0)
        OptionNext=CreateFrame('Button','OptionNext',OptionPanel); OptionNext:SetSize(175,18); OptionNext:SetPoint('TOPLEFT','OptionRow',0,-18)
        OptionLabel=OptionPanel:CreateFontString('OptionLabel'); OptionLabel:SetSize(160,20); OptionLabel:SetPoint('TOPLEFT',OptionPanel,12,-60)
        OptionLabel:SetText('A long options description should wrap inside its explicitly sized text region instead of running beyond the window.')
    )LUA")));
    F.Frames.RefreshLayout();
    const auto Row=F.Frames.GetLayoutRect(F.Frames.FindFrame(TEXT("OptionRow")));
    const auto Next=F.Frames.GetLayoutRect(F.Frames.FindFrame(TEXT("OptionNext")));
    const auto Caption=F.Frames.GetRegionRect(TEXT("OptionRowText"));
    TestTrue(TEXT("Relative frame and numeric offsets retain TOPLEFT"),FMath::IsNearlyEqual(Row.X,100.) && FMath::IsNearlyEqual(Row.Y,78.));
    TestTrue(TEXT("Named relative frame accepts omitted relativePoint"),FMath::IsNearlyEqual(Next.X,100.) && FMath::IsNearlyEqual(Next.Y,96.));
    TestTrue(TEXT("Caption stays within its moving owner"),FMath::IsNearlyEqual(Caption.X,Row.X+8,0.1) && Caption.Y>=Row.Y-20 && Caption.Y<Row.Y+18);
    TestTrue(TEXT("Runtime $parent anchors resolve to the region's owner"),Caption.Z>100 && Caption.Z<175);
    const auto* Label=F.Frames.GetNamedFontString(TEXT("OptionLabel"));
    TestTrue(TEXT("Descriptions wrap even with an explicit height"),Label && Label->GetWrapTextAt()>0 && F.Frames.GetRegionRect(TEXT("OptionLabel")).W>20);
    FWowFrameDef Title; Title.Name=TEXT("DecoratedTitleFrame"); Title.Parent=TEXT("UIParent"); Title.Width=200; Title.Height=40;
    Title.Anchors.Add({EWowAnchorPoint::CENTER,TEXT("UIParent"),EWowAnchorPoint::CENTER,0,0});
    FWowLayer Layer; FWowFontStringElement Text; Text.Name=TEXT("DecoratedTitle"); Text.Text=TEXT("Objectives|TInterface/OptionsFrame/Icon:0:0|t"); Layer.FontStrings.Add(Text); Title.Layers.Add(Layer);
    F.Frames.CreateFrame(Title);
    TestEqual(TEXT("XML icon markup does not print an asset path"),F.Frames.GetNamedFontString(TEXT("DecoratedTitle"))->GetText().ToString(),FString(TEXT("Objectives")));
    TestTrue(TEXT("Lua retains original inline markup"),F.Lua.ExecuteString(TEXT("assert(DecoratedTitle:GetText()=='Objectives|TInterface/OptionsFrame/Icon:0:0|t'); DecoratedTitle:SetText('|cffff0000Updated|r'); assert(DecoratedTitle:GetText()=='|cffff0000Updated|r')")));
    TestEqual(TEXT("Runtime markup also paints plain labels"),F.Frames.GetNamedFontString(TEXT("DecoratedTitle"))->GetText().ToString(),FString(TEXT("Updated")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudBackdrop,"WowUnreal.Hud.StockBackdrops",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudBackdrop::RunTest(const FString&)
{
    FHudFixture F;
    const char* Xml="<Ui><Frame name='BackdropFixture' parent='UIParent'><Size x='300' y='200'/><Anchors><Anchor point='CENTER'/></Anchors><Backdrop bgFile='Background' edgeFile='Border' tile='true'><EdgeSize><AbsValue val='32'/></EdgeSize><TileSize><AbsValue val='16'/></TileSize><BackgroundInsets><AbsInset left='11' right='12' top='13' bottom='14'/></BackgroundInsets><Color r='0.2' g='0.3' b='0.4' a='0.8'/></Backdrop></Frame></Ui>";
    TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Xml),FCStringAnsi::Strlen(Xml));
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("BackdropFixture"));
    if(!TestTrue(TEXT("Stock XML backdrop parsed"),Parsed.Num()==1 && Parsed[0].FrameDef.Backdrop.IsSet())) return false;
    const auto& D=Parsed[0].FrameDef.Backdrop.GetValue();
    TestTrue(TEXT("Nested dimensions and all four insets retained"),D.EdgeSize==32 && D.TileSize==16 && D.InsetLeft==11 && D.InsetRight==12 && D.InsetTop==13 && D.InsetBottom==14);
    F.Frames.CreateFrame(Parsed[0].FrameDef);
    TestTrue(TEXT("Native XML backdrop and Lua mutations share state"),F.Lua.ExecuteString(TEXT(R"LUA(
        local d=BackdropFixture:GetBackdrop(); assert(d.edgeFile=='Border' and d.edgeSize==32 and d.insets.top==13)
        local r,g,b,a=BackdropFixture:GetBackdropColor(); assert(math.abs(r-.2)<.001 and math.abs(a-.8)<.001)
        BackdropFixture:SetBackdropBorderColor(.4,.5,.6,.7); r,g,b,a=BackdropFixture:GetBackdropBorderColor(); assert(math.abs(g-.5)<.001 and math.abs(a-.7)<.001)
        BackdropFixture:SetBackdrop(nil); assert(BackdropFixture:GetBackdrop()==nil)
        BackdropFixture:SetBackdrop({bgFile='Replacement',edgeFile='Edge',edgeSize=8.5,tile=true,tileSize=4,insets={left=2,right=3,top=4,bottom=5}})
        d=BackdropFixture:GetBackdrop(); assert(d.bgFile=='Replacement' and d.edgeSize==8.5 and d.insets.bottom==5)
    )LUA")));
    const auto Quads=UWowBackdropWidget::BuildQuads(D,FVector2f(300,200),1);
    int32 Background=0,Border=0;
    for(const auto& Q:Quads)
    {
        if(Q.Border) ++Border; else { ++Background; TestTrue(TEXT("Background honors XML insets"),Q.Rect.X>=11 && Q.Rect.Y>=13 && Q.Rect.X+Q.Rect.Z<=288.01f && Q.Rect.Y+Q.Rect.W<=186.01f); }
        TestTrue(TEXT("Backdrop atlas UVs do not cross neighboring strips"),Q.UV[0].X>=0 && Q.UV[3].X<=1 && Q.UV[0].Y>=0 && Q.UV[3].Y<=1);
    }
    TestTrue(TEXT("Background tiles and complete border geometry exist"),Background>1 && Border>8);
    TestTrue(TEXT("Tiny addon tile sizes have bounded geometry"),UWowBackdropWidget::BuildQuads(D,FVector2f(100000,100000),1).Num()<4500);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudTextureTemplates,"WowUnreal.Hud.StockButtonTemplates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudTextureTemplates::RunTest(const FString&)
{
    FString DataPath; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),DataPath)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(DataPath)) return false;
    FHudFixture F; F.Frames.SetMpqManager(&Mpq); FWowAssetCache Cache; F.Frames.SetAssetCache(&Cache);
    TestTrue(TEXT("Create font objects for stock button states"),F.Lua.ExecuteString(TEXT("GameFontNormal=CreateFont('GameFontNormal'); GameFontNormal:SetTextColor(1,.8,0); GameFontHighlight=CreateFont('GameFontHighlight'); GameFontHighlight:SetTextColor(1,1,1); GameFontDisable=CreateFont('GameFontDisable'); GameFontDisable:SetTextColor(.5,.5,.5)")));
    TArray<uint8> Bytes; if(!Mpq.ReadFile(TEXT("Interface\\FrameXML\\UIPanelTemplates.xml"),Bytes)) return false;
    for(const auto& Directive:FWowFrameXmlParser::ParseXml(Bytes,TEXT("OriginalButtonTemplates")))
    {
        if(Directive.Type==FWowXmlDirective::EType::Texture) F.Frames.LoadTextureDefinition(Directive.TextureDef,Directive.TextureParent);
        if(Directive.Type==FWowXmlDirective::EType::Frame && Directive.FrameDef.bVirtual) F.Frames.CreateFrame(Directive.FrameDef);
    }
    FWowFrameDef Button; Button.Name=TEXT("SkinButton"); Button.Parent=TEXT("UIParent"); Button.Inherits=TEXT("GameMenuButtonTemplate"); Button.Type=EWowFrameType::Button;
    Button.Width=144; Button.Height=21; Button.Anchors.Add({EWowAnchorPoint::CENTER,TEXT("UIParent"),EWowAnchorPoint::CENTER,0,0}); F.Frames.CreateFrame(Button);
    TestTrue(TEXT("Instantiate original game-menu button"),F.Lua.ExecuteString(TEXT("SkinButton:SetText('Stock button')")));
    const auto Handle=F.Frames.FindFrame(TEXT("SkinButton"));
    const auto* Normal=F.Frames.GetNamedTexture(F.Frames.EnsureButtonTexture(Handle,TEXT("Normal")));
    TestTrue(TEXT("Inherited texture resource loads from MPQ"),Normal && Normal->GetBrush().GetResourceObject());
    if(Normal) { const FBox2f UV=Normal->GetBrush().GetUVRegion(); TestTrue(TEXT("Inherited atlas crop retained"),UV.bIsValid && FMath::IsNearlyEqual(UV.Max.X,.625f) && FMath::IsNearlyEqual(UV.Max.Y,.6875f)); }
    TestTrue(TEXT("Font getters use inherited stock styles"),F.Lua.ExecuteString(TEXT("assert(SkinButton:GetNormalFontObject()==GameFontHighlight); SkinButton:SetDisabledFontObject(GameFontDisable); SkinButton:Disable()")));
    const auto* Text=F.Frames.GetPrimaryTextWidget(Handle);
    TestTrue(TEXT("Disabled caption receives the disabled font color"),Text && FMath::IsNearlyEqual(Text->GetColorAndOpacity().GetSpecifiedColor().R,.5f));
    TestTrue(TEXT("Normal font can change through original Lua API"),F.Lua.ExecuteString(TEXT("SkinButton:Enable(); SkinButton:SetNormalFontObject(GameFontNormal); assert(SkinButton:GetNormalFontObject()==GameFontNormal)")));
    TestTrue(TEXT("Updated normal caption color is visible"),Text && FMath::IsNearlyEqual(Text->GetColorAndOpacity().GetSpecifiedColor().G,.8f));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudInlineLabels,"WowUnreal.Hud.StockInlineLabels",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudInlineLabels::RunTest(const FString&)
{
    const auto Parsed=UWowTextBlock::ParseMarkup(TEXT("Before |cffff0000red|r |TInterface/Icons/Example:16:24:2:-1:64:64:4:36:8:40|t |Hitem:123|h[item]|h || <text>"));
    TestEqual(TEXT("Markup preserves visible text and escaped pipes"),Parsed.Plain,FString(TEXT("Before red  [item] | <text>")));
    if(!TestEqual(TEXT("Inline texture parsed"),Parsed.Icons.Num(),1)) return false;
    const auto& Icon=Parsed.Icons[0];
    TestTrue(TEXT("Icon dimensions, offsets and crop are retained"),Icon.Height==16 && Icon.Width==24 && Icon.OffsetX==2 && Icon.OffsetY==-1 && Icon.Crop.X==.0625f && Icon.Crop.Y==.125f);
    TestTrue(TEXT("Literal markup cannot become a decorator"),Parsed.Rich.Contains(TEXT("&lt;text&gt;")) && Parsed.Rich.Contains(TEXT("wowimage")));
    FString DataPath; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),DataPath)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(DataPath)) return false;
    FHudFixture F; F.Frames.SetMpqManager(&Mpq); FWowAssetCache Cache; F.Frames.SetAssetCache(&Cache);
    TestTrue(TEXT("Inline icons participate in measured width and wrapping"),F.Lua.ExecuteString(TEXT(R"LUA(
        InlineLabel=UIParent:CreateFontString('InlineLabel'); InlineLabel:SetPoint('CENTER',UIParent,'CENTER',0,0)
        InlineLabel:SetText('A'); local before=InlineLabel:GetStringWidth()
        local raw='A|TInterface\\Icons\\INV_Misc_QuestionMark:20:40|t'; InlineLabel:SetText(raw)
        assert(InlineLabel:GetText()==raw and InlineLabel:GetStringWidth()>=before+39)
        InlineLabel:SetWidth(60); InlineLabel:SetText(raw..' multiple wrapped words after the icon')
        assert(InlineLabel:GetHeight()>20)
        NaturalIcon=UIParent:CreateTexture('NaturalIcon'); assert(not NaturalIcon:IsVisible())
        NaturalIcon:SetTexture('Interface\\Icons\\INV_Misc_QuestionMark'); assert(NaturalIcon:GetWidth()>16)
        NaturalIcon:SetPoint('CENTER'); assert(NaturalIcon:IsVisible() and NaturalIcon:GetWidth()>16)
    )LUA")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudInventoryItems,"WowUnreal.Hud.InventoryItems",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudInventoryItems::RunTest(const FString&)
{
    FString DataPath;
    if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),DataPath)) { AddError(TEXT("Supply -wowdata")); return false; }
    FMpqManager Mpq; if(!Mpq.Initialize(DataPath)) return false;
    if(!FDbcStore::Get().IsLoaded()) FDbcStore::Get().LoadAll(Mpq);
    if(!FDbcStore::Get().IsDeferredLoaded()) FDbcStore::Get().LoadDeferred(Mpq);
    FHudFixture F;
    F.Frames.SetMpqManager(&Mpq);
    FWowAssetCache Cache; F.Frames.SetAssetCache(&Cache);
    TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    auto& Entities=Connection->PacketHandler.EntityManager; Entities.LocalPlayerGuid=1;
    auto& Player=Entities.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    const uint64 ItemGuid=0x4000000000000001ULL;
    auto& Item=Entities.PromoteToTyped(ItemGuid,WowTypeMask::OBJECT|WowTypeMask::ITEM);
    Item.Entry=1367; Item.SetField(ItemField::OWNER,1); Item.SetField(ItemField::STACK_COUNT,3); Item.SetField(ItemField::DURABILITY,18); Item.SetField(ItemField::MAX_DURABILITY,40);
    Player.SetField(PlayerField::PACK_SLOT_START,uint32(ItemGuid&0xffffffffULL)); Player.SetField(PlayerField::PACK_SLOT_START+1,uint32(ItemGuid>>32));
    Player.SetField(PlayerField::INV_SLOT_HEAD+7*2,uint32(ItemGuid&0xffffffffULL)); Player.SetField(PlayerField::INV_SLOT_HEAD+7*2+1,uint32(ItemGuid>>32));
    FWowItemTemplate T; T.Id=1367; T.Name=TEXT("Ragged Leather Boots"); T.DisplayId=14454; T.Quality=1; T.InventoryType=8; T.MaxStack=20; T.SellPrice=20;
    Connection->PacketHandler.ItemTemplates.Add(T.Id,T);
    Connection->PacketHandler.ActionButtons.SetNumZeroed(144); Connection->PacketHandler.ActionButtons[0]=(128u<<24)|1367;
    F.Context.ConnectionManager=Connection.Get(); F.Context.EntityManager=&Entities;
    Player.SetField(UnitField::BYTES_0,(1u<<24)|(1u<<8)|1u);
    Player.Auras[5].bActive=true; Player.Auras[5].SpellId=6673; Player.Auras[5].Flags=0x39; Player.Auras[5].CasterGuid=1; Player.Auras[5].Charges=3;
    Player.Auras[5].DurationMs=120000; Player.Auras[5].RemainingMs=90000; Player.Auras[5].UpdatedAt=FPlatformTime::Seconds();
    Player.Auras[31].bActive=true; Player.Auras[31].SpellId=772; Player.Auras[31].Flags=0x81; Player.Auras[31].CasterGuid=2;
    Connection->PacketHandler.KnownSpells.Append({6603,78,6673,2457});
    Player.SetField(PlayerField::SKILL_INFO,95); Player.SetField(PlayerField::SKILL_INFO+1,(100u<<16)|80u); Player.SetField(PlayerField::SKILL_INFO+2,(2u<<16)|3u);
    TestTrue(TEXT("Sparse auras and spellbook pages use actual server spells and DBC icons"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(select(11,UnitBuff('player',1))==6673 and select(11,UnitDebuff('player',1))==772)
        assert(select(11,UnitAura('player',1,'HARMFUL'))==772 and UnitBuff('player',2)==nil)
        local _,_,icon,stacks,_,duration,expiration,caster=UnitBuff('player',1)
        assert(icon and stacks==3 and duration==120 and caster=='player' and expiration-GetTime()>89)
        local total=0
        assert(GetNumSpellTabs()>1 and GetNumSpellTabs()<=8)
        for tab=1,GetNumSpellTabs() do
            local name,tex,offset,count,highOffset,highCount=GetSpellTabInfo(tab)
            assert(name and tex and highCount<=count); total=total+count
            for index=1,count do assert(GetSpellName(offset+index,'spell') and GetSpellTexture(offset+index,'spell')) end
            for index=1,highCount do
                local high=highOffset+index; local slot=GetKnownSlotFromHighestRankSlot(high)
                assert(GetSpellName(high,'spell')==GetSpellName(slot,'spell'))
            end
        end
        assert(total==4)
        assert(GetNumSkillLines()==1)
        local icon,name,active,castable,id=GetShapeshiftFormInfo(1); assert(GetNumShapeshiftForms()==1 and icon and #icon>0 and name=='Battle Stance' and id==2457)
        local _,_,_,rank,temp,bonus,maximum=GetSkillLineInfo(1); assert(rank==80 and temp==3 and bonus==2 and maximum==100)
        local defense,modifier=UnitDefense('player'); assert(defense==80 and modifier==5)
    )LUA")));
    TestTrue(TEXT("Inventory APIs expose actual cached values, links and empty slots"),F.Lua.ExecuteString(TEXT(R"LUA(
        local name,link,q,level,required,class,subclass,stack,equip,icon,price=GetItemInfo(1367)
        assert(name=='Ragged Leather Boots' and stack==20 and equip=='INVTYPE_FEET' and price==20)
        assert(GetItemInfo(link)==name and GetItemInfo(999999)==nil)
        assert(GetItemInfo(GetContainerItemLink(0,1))==name)
        assert(GetContainerNumSlots(0)==16 and GetContainerFreeSlots(0)==15)
        assert(GetContainerItemInfo(0,2)==nil and GetContainerItemID(0,1)==1367)
        assert(select(2,GetContainerItemInfo(0,1))==3)
        assert(GetInventoryItemID('player',8)==1367 and GetInventoryItemCount('player',8)==3)
        assert(GetActionTexture(1)==select(10,GetItemInfo(1367)))
        PickupContainerItem(0,1); PlaceAction(5)
        local kind,id=GetActionInfo(5); assert(kind=='item' and id==1367 and GetContainerItemID(0,1)==1367)
        assert(IsItemAction(5) and GetActionCount(5)==3)
        PickupSpell(78); PlaceAction(5)
        local kind,id=GetActionInfo(5); assert(kind=='spell' and id==78)
        local kind,id=GetCursorInfo(); assert(kind=='item' and id==1367)
        PlaceAction(6); assert(not CursorHasItem() and select(2,GetActionInfo(6))==1367)
        PickupAction(5); PlaceAction(6)
        assert(GetActionInfo(5)==nil and select(2,GetActionInfo(6))==78)
        PlaceAction(5); assert(select(2,GetActionInfo(5))==1367)
        local current,maximum=GetInventoryItemDurability(8); assert(current==18 and maximum==40)
        InvTooltip=CreateFrame('GameTooltip','InvTooltip',UIParent)
        InvOwner=CreateFrame('Frame','InvOwner',UIParent)
        InvTooltip:SetOwner(InvOwner,'ANCHOR_LEFT'); assert(InvTooltip:GetOwner()==InvOwner and InvTooltip:IsOwned(InvOwner))
        InvTooltip:SetBagItem(0,1); assert(InvTooltip:NumLines()>=2)
    )LUA")));
    const auto* Tooltip=F.Frames.GetTooltipState(F.Frames.FindFrame(TEXT("InvTooltip")));
    TestTrue(TEXT("Bag tooltip contains the real item name and durability"),Tooltip && Tooltip->Lines.Num()>=2 && Tooltip->Lines[0].Text==T.Name && Tooltip->Lines.Last().Text==TEXT("Durability 18 / 40"));
    const auto* Display=FDbcStore::Get().ItemDisplayInfo().GetById(14454);
    TestTrue(TEXT("Server display resolves an existing MPQ icon"),Display && F.Frames.LoadUITexture(TEXT("Interface\\Icons\\")+Display->Icon1));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiWindowMovement,"WowUnreal.Hud.WindowMovement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FUiWindowMovement::RunTest(const FString&)
{
    FHudFixture F;
    TestTrue(TEXT("Movement setup uses the public APIs"),F.Lua.ExecuteString(TEXT(R"LUA(
        Move=CreateFrame('Frame','Move',UIParent); Move:SetSize(180,100); Move:SetScale(.8)
        Move:SetPoint('TOPLEFT',UIParent,'TOPLEFT',100,-100); Move:EnableMouse(true)
        assert(not Move:IsMovable() and not Move:IsResizable() and not Move:IsClampedToScreen())
        Move:SetMovable(true); Move:SetResizable(true); Move:SetClampedToScreen(true)
        Move:SetMinResize(120,80); Move:SetMaxResize(260,180)
        assert(Move:IsMovable()==1 and Move:IsResizable()==1 and Move:IsClampedToScreen()==1)
        assert(Move:GetMinResize()==120 and Move:GetMaxResize()==260)
        Move:RegisterForDrag('LeftButton')
        Move:SetScript('OnDragStart',function(self) assert(self:IsDragging()); self:StartMoving(); self:StartMoving() end)
        Move:SetScript('OnDragStop',function(self) self:StopMovingOrSizing() end)
    )LUA")));
    const int64 H=F.Frames.FindFrame(TEXT("Move")); const auto Before=F.Frames.GetLayoutRect(H);
    F.Frames.UpdateMouseHover(100,100); F.Frames.DispatchMouseDown(H,TEXT("LeftButton"));
    TestTrue(TEXT("Registered native drag starts the stock handler"),F.Frames.DispatchDragStart(H,TEXT("LeftButton")));
    F.Frames.UpdateMouseHover(220,150); F.Frames.DispatchDragStop(H,TEXT("LeftButton"));
    const auto After=F.Frames.GetLayoutRect(H);
    TestTrue(TEXT("Drag state ends with native release"),F.Lua.ExecuteString(TEXT("assert(not Move:IsDragging())")));
    TestTrue(TEXT("Movement follows physical pointer delta at non-unit scale"),FMath::IsNearlyEqual(After.X-Before.X,120/F.Frames.GetScreenScale(),.01) && FMath::IsNearlyEqual(After.Y-Before.Y,50/F.Frames.GetScreenScale(),.01));
    F.Frames.UpdateMouseHover(300,200);
    TestTrue(TEXT("Stop prevents later cursor movement from changing the frame"),After.Equals(F.Frames.GetLayoutRect(H),.01));
    TestTrue(TEXT("Resize starts"),F.Lua.ExecuteString(TEXT("Move:StartSizing('BOTTOMRIGHT')")));
    F.Frames.UpdateMouseHover(2000,2000); F.Lua.ExecuteString(TEXT("Move:StopMovingOrSizing()"));
    TestTrue(TEXT("Resize honors max bounds"),F.Lua.ExecuteString(TEXT("assert(math.abs(Move:GetWidth()-260)<.01 and math.abs(Move:GetHeight()-180)<.01); Move:StartMoving()")));
    F.Frames.UpdateMouseHover(-2000,-2000); F.Lua.ExecuteString(TEXT("Move:StopMovingOrSizing()"));
    const auto Clamped=F.Frames.GetLayoutRect(H);
    TestTrue(TEXT("Clamped dragging cannot lose the window off screen"),Clamped.X>=0 && Clamped.Y>=0);
    TestTrue(TEXT("Clamp insets persist and disabling clamp restores unrestricted placement"),F.Lua.ExecuteString(TEXT("Move:SetClampRectInsets(5,6,7,8); local l,r,t,b=Move:GetClampRectInsets(); assert(l==5 and r==6 and t==7 and b==8); Move:SetClampedToScreen(false); Move:ClearAllPoints(); Move:SetPoint('TOPLEFT',UIParent,'TOPLEFT',-500,500); assert(Move:GetLeft()<0)")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiRegionControls,"WowUnreal.Hud.RegionControls",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FUiRegionControls::RunTest(const FString&)
{
    FHudFixture F;
    TestTrue(TEXT("Dynamic texture regions use canonical anchors, dimensions and visibility"),F.Lua.ExecuteString(TEXT(R"LUA(
        Owner=CreateFrame('Frame','Owner',UIParent); Owner:SetSize(300,180); Owner:SetPoint('CENTER'); Owner:SetScale(.8)
        Tex=Owner:CreateTexture('OwnerTexture','BACKGROUND'); Tex:SetTexture(1,0,0,.5)
        Tex:SetSize(60,40); Tex:SetPoint('TOPLEFT',Owner,'TOPLEFT',20,-30)
        assert(Tex:GetParent()==Owner and Tex:GetNumPoints()==1)
        local a,b,c,x,y=Tex:GetPoint(); assert(a=='TOPLEFT' and b==Owner and c=='TOPLEFT' and x==20 and y==-30)
        assert(math.abs(Tex:GetWidth()-60)<.1 and math.abs(Tex:GetHeight()-40)<.1)
        Tex:SetTexCoord(.1,.7,.2,.9); local ulx,uly,llx,lly,urx,ury,lrx,lry=Tex:GetTexCoord()
        assert(math.abs(ulx-.1)<.001 and math.abs(uly-.2)<.001 and math.abs(lrx-.7)<.001 and math.abs(lry-.9)<.001)
        Tex:SetDrawLayer('OVERLAY',3); local layer,sub=Tex:GetDrawLayer(); assert(layer=='OVERLAY' and sub==3)
        Tex:Hide(); assert(not Tex:IsShown()); Tex:Show(); assert(Tex:IsShown())
        Tex:ClearAllPoints(); assert(Tex:GetNumPoints()==0 and Tex:IsShown() and not Tex:IsVisible()); Tex:Show(); assert(not Tex:IsVisible()); Tex:SetAllPoints(Owner); assert(Tex:IsVisible())
        assert(Tex:GetNumPoints()==2 and math.abs(Tex:GetWidth()-Owner:GetWidth())<.1)
        Other=CreateTexture(nil,Owner,'BORDER'); Other:SetSize(30,20); Other:SetPoint('CENTER',Tex,'CENTER')
        assert(Other:GetName()==nil and Other:GetParent()==Owner and select(2,Other:GetPoint())==Tex)
        Label=Owner:CreateFontString('ControlLabel'); Label:SetSize(180,80); Label:SetPoint('CENTER'); Label:SetText('long words that should wrap onto more than one line')
        Label:SetJustifyH('RIGHT'); Label:SetJustifyV('BOTTOM'); assert(Label:GetJustifyH()=='RIGHT' and Label:GetJustifyV()=='BOTTOM')
        Label:SetWordWrap(false); assert(not Label:CanWordWrap()); Label:SetWordWrap(true); assert(Label:CanWordWrap()==1)
        Label:SetNonSpaceWrap(true); assert(Label:CanNonSpaceWrap()==1)
        Label:SetSpacing(3); assert(Label:GetSpacing()==3)
        Label:SetShadowOffset(2,-3); local x,y=Label:GetShadowOffset(); assert(math.abs(x-2)<.001 and math.abs(y+3)<.001)
        Label:SetShadowColor(.2,.3,.4,.5); local r,g,b,a=Label:GetShadowColor(); assert(math.abs(r-.2)<.001 and math.abs(a-.5)<.001)
        Label:SetTextHeight(20); assert(math.abs(Label:GetTextHeight()-20)<1.5)
        ControlFont=CreateFont('ControlFont'); ControlFont:SetFont('Fonts\\FRIZQT__.TTF',14)
        ControlFont:SetJustifyH('LEFT'); ControlFont:SetJustifyV('TOP'); ControlFont:SetSpacing(5); ControlFont:SetWordWrap(false)
        ControlFont:SetShadowOffset(1,-1); ControlFont:SetShadowColor(0,0,0,1)
        Label:SetFontObject(ControlFont); assert(Label:GetJustifyH()=='LEFT' and Label:GetJustifyV()=='TOP' and Label:GetSpacing()==5 and not Label:CanWordWrap())
    )LUA")));
    const auto* Image=F.Frames.GetNamedTexture(TEXT("OwnerTexture")); const auto* Slot=Image?Cast<UCanvasPanelSlot>(Image->Slot):nullptr;
    TestTrue(TEXT("Draw layer/sublevel changes reach the native image"),Slot && Slot->GetZOrder()==303);
    TestTrue(TEXT("Region definitions survive a full layout refresh"),F.Lua.ExecuteString(TEXT("Owner:SetSize(350,200); assert(math.abs(Tex:GetWidth()-350)<.1 and math.abs(Tex:GetHeight()-200)<.1)")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiButtonInputStates,"WowUnreal.Hud.ButtonInputStates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FUiButtonInputStates::RunTest(const FString&)
{
    FHudFixture F;
    FWowFrameDef D; D.Name=TEXT("InputButton"); D.Parent=TEXT("UIParent"); D.Type=EWowFrameType::Button; D.Width=100; D.Height=40;
    D.Anchors.Add({EWowAnchorPoint::TOPLEFT,TEXT("UIParent"),EWowAnchorPoint::TOPLEFT,40,-40}); const auto H=F.Frames.CreateFrame(D);
    F.Frames.RefreshLayout(); const auto R=F.Frames.GetLayoutRect(H); const float S=F.Frames.GetScreenScale();
    TestTrue(TEXT("Button scripts initialize"),F.Lua.ExecuteString(TEXT("EnterCount=0; ClickCount=0; InputButton:SetScript('OnEnter',function() EnterCount=EnterCount+1 end); InputButton:SetScript('OnClick',function() ClickCount=ClickCount+1 end)")));
    F.Frames.UpdateMouseHover(-100,-100); F.Frames.UpdateMouseHover((R.X+50)*S,(R.Y+20)*S);
    TestTrue(TEXT("Enabled hover and locked pressed states work"),F.Lua.ExecuteString(TEXT("assert(EnterCount==1); InputButton:SetButtonState('PUSHED',true); assert(InputButton:GetButtonState()=='PUSHED'); InputButton:Disable(); assert(InputButton:GetButtonState()=='DISABLED' and not InputButton:GetMotionScriptsWhileDisabled())")));
    F.Frames.UpdateMouseHover(-100,-100); F.Frames.UpdateMouseHover((R.X+50)*S,(R.Y+20)*S); F.Frames.DispatchClick(H,TEXT("LeftButton"),false);
    TestTrue(TEXT("Disabled buttons suppress hover/click handlers unless requested"),F.Lua.ExecuteString(TEXT("assert(EnterCount==1 and ClickCount==0); InputButton:SetMotionScriptsWhileDisabled(true); assert(InputButton:GetMotionScriptsWhileDisabled())")));
    F.Frames.UpdateMouseHover(-100,-100); F.Frames.UpdateMouseHover((R.X+50)*S,(R.Y+20)*S);
    TestTrue(TEXT("Opted-in disabled tooltips receive hover, and re-enable releases pressed state"),F.Lua.ExecuteString(TEXT("assert(EnterCount==2); InputButton:Enable(); assert(InputButton:GetButtonState()=='NORMAL'); InputButton:SetHitRectInsets(20,0,0,0); local l,r,t,b=InputButton:GetHitRectInsets(); assert(l==20 and r==0 and t==0 and b==0)")));
    TestTrue(TEXT("Hit insets change actual native picking"),F.Frames.HitTestFrames((R.X+5)*S,(R.Y+20)*S)!=H && F.Frames.HitTestFrames((R.X+50)*S,(R.Y+20)*S)==H);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiStatefulControls,"WowUnreal.Hud.StatefulControls",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FUiStatefulControls::RunTest(const FString&)
{
    FHudFixture F;
    const char* Xml="<Ui><Button name='CaptionButton' text='Accept'><Size x='100' y='24'/><Anchors><Anchor point='CENTER'/></Anchors><ButtonText><FontString inherits='GameFontNormal'/></ButtonText></Button></Ui>";
    TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Xml),FCStringAnsi::Strlen(Xml));
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("ButtonCaption"));
    if(!TestEqual(TEXT("Caption fixture parses"),Parsed.Num(),1)) return false;
    const auto Caption=F.Frames.CreateFrame(Parsed[0].FrameDef);
    TestTrue(TEXT("XML text attributes render captions even with an inherited font definition"),F.Frames.GetNamedFontString(TEXT("CaptionButtonText")) && F.Frames.GetNamedFontString(TEXT("CaptionButtonText"))->GetText().ToString()==TEXT("Accept"));
    TestTrue(TEXT("Buttons support dynamic captions and disabled clicks"),F.Lua.ExecuteString(TEXT("CaptionButton:SetText('Changed'); CaptionClicks=0; CaptionButton:SetScript('OnClick',function() CaptionClicks=CaptionClicks+1 end); CaptionButton:Disable()")));
    F.Frames.DispatchClick(Caption,TEXT("LeftButton"),false);
    TestTrue(TEXT("A disabled button cannot execute its callback"),F.Lua.ExecuteString(TEXT("assert(CaptionClicks==0 and CaptionButton:GetText()=='Changed'); CaptionButton:Enable()")));
    F.Frames.DispatchClick(Caption,TEXT("LeftButton"),false);
    TestTrue(TEXT("An enabled button executes its callback"),F.Lua.ExecuteString(TEXT("assert(CaptionClicks==1)")));
    TestTrue(TEXT("Formatted text, parent-key regions and drag registrations load"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(math.abs(sin(90)-1)<0.00001 and math.abs(cos(180)+1)<0.00001 and math.abs(atan2(1,0)-90)<0.00001)
        assert(math.abs(math.sin(math.pi/2)-1)<0.00001)
        assert(format('%2$s / %1$d',42,'value')=='value / 42')
        assert(string.format('%2$s %1$04d %% %2$s',7,'x')=='x 0007 % x')
        SetGamma(-.25); assert(math.abs(tonumber(GetCVar('gamma'))-.75)<.0001 and math.abs(GetGamma()+.25)<.0001)
        SetGamma(.25); assert(math.abs(tonumber(GetCVar('gamma'))-1.25)<.0001 and math.abs(GetGamma()-.25)<.0001)
        SetGamma(0); assert(math.abs(tonumber(GetCVar('gamma'))-1)<.0001 and math.abs(GetGamma())<.0001)
        assert(GetBindingAction('W')=='MOVEFORWARD' and GetBindingKey('MOVEFORWARD')=='W')
        assert(tonumber(GetCVar('uiScale'))>0 and tonumber(GetCVarDefault('uiScale'))>0)
        assert(SetBinding('F8','TOGGLESPELLBOOK') and GetBindingAction('F8')=='TOGGLESPELLBOOK')
        assert(SetBinding('P') and GetBindingAction('P')=='')
        assert(not SetBinding('F9','UNSUPPORTED_COMMAND'))
        LoadBindings(0); assert(GetBindingAction('P')=='TOGGLESPELLBOOK' and GetBindingAction('F8')=='')
        Anonymous=CreateFrame('Frame'); Anonymous.saved='state'; Anonymous.received=false
        Anonymous:SetScript('OnEvent',function(self,event) assert(self==Anonymous and self.saved=='state'); self.received=true end)
        Anonymous:RegisterEvent('UI_FIXTURE')
        AnonymousChild=CreateFrame('Frame',nil,Anonymous); assert(AnonymousChild:GetParent()==Anonymous)
        assert(CombatLogGetCurrentEntry()==nil and CombatLogAdvanceEntry(-1)==nil)
        Control=CreateFrame('CHECKBUTTON','Control',UIParent)
        assert(Control:IsObjectType('frame') and Control:IsObjectType('Button') and Control:IsObjectType('CheckButton') and not Control:IsObjectType('EditBox'))
        FixtureFont=CreateFont('FixtureFont'); FixtureFont:SetFont('Fonts\\FRIZQT__.TTF',14); FixtureFont:SetTextColor(.2,.4,.6)
        CaptionButtonText:SetFontObject(FixtureFont); assert(CaptionButtonText:GetFontObject()==FixtureFont)
        local r,g,b=CaptionButtonText:GetTextColor(); assert(math.abs(r-.2)<.0001 and math.abs(g-.4)<.0001 and math.abs(b-.6)<.0001)
        Control:SetSize(24,24); Control:SetPoint('TOPLEFT',UIParent,'TOPLEFT',20,-20)
        Control:SetChecked(true); assert(Control:GetChecked()); Control:SetChecked(0); assert(not Control:GetChecked())
        Control:RegisterForDrag('LeftButton')
        Dragged=0; Dropped=0
        Control:SetScript('OnDragStart',function(self,button) assert(button=='LeftButton'); Dragged=Dragged+1 end)
        Control:SetScript('OnReceiveDrag',function() Dropped=Dropped+1 end)
        Message=CreateFrame('ScrollingMessageFrame','Message',UIParent)
        Message:SetSize(200,64); Message:SetPoint('CENTER',UIParent,'CENTER',0,0)
        Message:AddMessage('first',1,0,0); Message:AddMessage('second',0,1,0); assert(Message:GetNumMessages()==2)
        Message:ScrollUp(); Message:ScrollToBottom()
    )LUA")));
    const auto H=F.Frames.FindFrame(TEXT("Control"));
    F.Events.FireEvent(TEXT("UI_FIXTURE"));
    TestTrue(TEXT("Unnamed event frames retain identity and custom state"),F.Lua.ExecuteString(TEXT("assert(Anonymous.received)")));
    F.Frames.DispatchClick(H,TEXT("LeftButton"),false);
    TestTrue(TEXT("A real check-button click changes its state"),F.Frames.GetChecked(H));
    TestTrue(TEXT("Only registered drag buttons start a drag"),!F.Frames.DispatchDragStart(H,TEXT("RightButton")) && F.Frames.DispatchDragStart(H,TEXT("LeftButton")));
    F.Frames.DispatchReceiveDrag(H); F.Frames.DispatchDragStop(H,TEXT("LeftButton"));
    TestTrue(TEXT("Drag callbacks retain typed arguments"),F.Lua.ExecuteString(TEXT("assert(Dragged==1 and Dropped==1)")));
    const auto* Messages=F.Frames.MessageFrames.Find(F.Frames.FindFrame(TEXT("Message")));
    TestTrue(TEXT("Chat history keeps message contents and per-message colors"),Messages && Messages->Lines.Num()==2 && Messages->Lines[1].Text==TEXT("second") && Messages->Lines[1].Color==FLinearColor(0,1,0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FButtonStateRegions,"WowUnreal.Hud.ButtonStateRegions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FButtonStateRegions::RunTest(const FString&)
{
    FHudFixture F;
    const FString Xml=TEXT("<Ui><Button name='MenuTest'><Size x='28' y='58'/><Anchors><Anchor point='CENTER'/></Anchors><Layers><Layer level='BACKGROUND'><Texture name='MenuBackground'/></Layer></Layers><NormalTexture name='$parentNormalTexture'><Size x='64' y='64'/><Anchors><Anchor point='CENTER'/></Anchors><TexCoords left='.1' right='.9' top='.2' bottom='.8'/></NormalTexture></Button></Ui>");
    const FTCHARToUTF8 Utf8(*Xml); TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("ButtonStates"));
    if(!TestEqual(TEXT("Button state fixture parses"),Parsed.Num(),1)) return false;
    const auto* Region=Parsed[0].FrameDef.ButtonTextureRegions.Find(TEXT("Normal"));
    TestTrue(TEXT("Button textures retain their XML rectangle and UVs"),Region && Region->Width==64 && Region->Left==.1f && Region->Bottom==.8f);
    const int64 Handle=F.Frames.CreateFrame(Parsed[0].FrameDef);
    TestTrue(TEXT("State getters expose canonical editable texture regions"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(MenuTest:GetNormalTexture()==MenuTestNormalTexture and MenuTest:GetNormalTexture()==MenuTest:GetNormalTexture())
        MenuTest:GetNormalTexture():SetTexture(1,0,0,1)
        MenuTest:GetPushedTexture():SetTexture(0,1,0,1)
        MenuTest:GetDisabledTexture():SetTexture(0,0,1,1)
        MenuTest:GetHighlightTexture():SetTexture(1,1,1,.5)
        MenuTest:SetButtonState('NORMAL')
        MenuDisabled=0; MenuTest:SetScript('OnDisable',function() MenuDisabled=MenuDisabled+1 end)
    )LUA")));
    F.Frames.RefreshLayout();
    auto* Normal=F.Frames.GetNamedTexture(TEXT("MenuTestNormalTexture"));
    auto* Pushed=F.Frames.GetNamedTexture(TEXT("MenuTestPushedTexture"));
    auto* Disabled=F.Frames.GetNamedTexture(TEXT("MenuTestDisabledTexture"));
    TestTrue(TEXT("Normal state renders an opaque region with its explicit size"),Normal && Normal->GetRenderOpacity()==1 && Normal->GetVisibility()!=ESlateVisibility::Collapsed && Cast<UCanvasPanelSlot>(Normal->Slot)->GetSize().X>60);
    TestTrue(TEXT("Button icons draw above the empty-slot background"),Cast<UCanvasPanelSlot>(Normal->Slot)->GetZOrder()>Cast<UCanvasPanelSlot>(F.Frames.GetNamedTexture(TEXT("MenuBackground"))->Slot)->GetZOrder());
    const auto Rect=F.Frames.GetLayoutRect(Handle);
    F.Frames.UpdateMouseHover((Rect.X+Rect.Z*.5f)*F.Frames.GetScreenScale(),(Rect.Y+Rect.W*.5f)*F.Frames.GetScreenScale());
    TestTrue(TEXT("Reanchoring a child during a mouse handler preserves mouse-over queries"),F.Lua.ExecuteString(TEXT("MenuTest:GetNormalTexture():SetPoint('CENTER',MenuTest,'CENTER',0,0); assert(MenuTest:IsMouseOver())")));
    F.Frames.DispatchMouseDown(Handle,TEXT("LeftButton"));
    TestTrue(TEXT("Custom mouse routing switches to the pushed texture"),F.Frames.GetButtonState(Handle)==TEXT("PUSHED") && Pushed && Pushed->GetVisibility()!=ESlateVisibility::Collapsed && Normal->GetVisibility()==ESlateVisibility::Collapsed);
    F.Frames.DispatchMouseUp(Handle,TEXT("LeftButton"));
    TestTrue(TEXT("Release restores the normal state"),F.Frames.GetButtonState(Handle)==TEXT("NORMAL") && Normal->GetVisibility()!=ESlateVisibility::Collapsed);
    TestTrue(TEXT("Locked pushed state survives release and disabled callbacks are idempotent"),F.Lua.ExecuteString(TEXT("MenuTest:SetButtonState('PUSHED',true); MenuTest:Disable(); MenuTest:Disable(); assert(MenuTest:IsEnabled()==0 and MenuDisabled==1)")));
    TestTrue(TEXT("Disabled texture replaces the normal texture"),Disabled && Disabled->GetVisibility()!=ESlateVisibility::Collapsed && Normal->GetVisibility()==ESlateVisibility::Collapsed);
    TestTrue(TEXT("Explicitly hidden button regions remain hidden on refresh"),F.Lua.ExecuteString(TEXT("MenuTest:Enable(); MenuTest:SetButtonState('NORMAL'); MenuTest:GetNormalTexture():Hide()")));
    F.Frames.RefreshButtonVisuals(Handle);
    TestTrue(TEXT("State refresh respects region visibility"),Normal->GetVisibility()==ESlateVisibility::Collapsed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudIndependentAnchorAxes,"WowUnreal.Hud.IndependentAnchorAxes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudIndependentAnchorAxes::RunTest(const FString&)
{
    FHudFixture F;
    TestTrue(TEXT("Three-edge anchors support a stock talent footer and texture"),F.Lua.ExecuteString(TEXT(R"LUA(
        local p=CreateFrame('Frame','AnchorPanel',UIParent)
        p:SetSize(400,500); p:SetPoint('TOPLEFT',UIParent,'TOPLEFT',20,-30)
        local b=CreateFrame('CheckButton','AnchorFooter',p)
        b:SetSize(300,26); b:SetPoint('LEFT',p,'LEFT',16,0); b:SetPoint('RIGHT',p,'RIGHT',-36,0); b:SetPoint('BOTTOM',p,'BOTTOM',0,81)
        local t=b:CreateTexture('AnchorFooterRegion','ARTWORK')
        t:SetTexture(1,1,1,1); t:SetSize(10,6); t:SetPoint('LEFT',b,'LEFT',4,0); t:SetPoint('RIGHT',b,'RIGHT',-4,0); t:SetPoint('BOTTOM',b,'BOTTOM',0,5)
        b:SetCheckedTexture(1,0,0,1); b:SetDisabledCheckedTexture(0,1,0,1)
        assert(b:GetCheckedTexture()==AnchorFooterCheckedTexture)
        b:GetCheckedTexture():SetSize(64,64); b:GetCheckedTexture():ClearAllPoints(); b:GetCheckedTexture():SetPoint('CENTER',b,'CENTER')
        b:SetChecked(true)
    )LUA")));
    F.Frames.RefreshLayout();
    const auto R=F.Frames.GetLayoutRect(F.Frames.FindFrame(TEXT("AnchorFooter")));
    TestTrue(TEXT("LEFT/RIGHT stretch width without overriding BOTTOM placement"),R.Equals(FVector4(36,423,348,26),.1));
    const auto T=F.Frames.GetRegionRect(TEXT("AnchorFooterRegion"));
    TestTrue(TEXT("Texture regions solve each anchored axis independently"),T.Equals(FVector4(40,438,340,6),.1));
    auto* Checked=F.Frames.GetNamedTexture(TEXT("AnchorFooterCheckedTexture"));
    auto* Disabled=F.Frames.GetNamedTexture(TEXT("AnchorFooterDisabledCheckedTexture"));
    TestTrue(TEXT("Checked texture is an editable visible state region"),Checked && Checked->GetVisibility()!=ESlateVisibility::Collapsed && F.Frames.GetRegionSize(TEXT("AnchorFooterCheckedTexture")).Equals(FVector2D(64,64),.1));
    TestTrue(TEXT("Disabled check marks replace the enabled mark"),F.Lua.ExecuteString(TEXT("AnchorFooter:Disable()")) && Checked->GetVisibility()==ESlateVisibility::Collapsed && Disabled && Disabled->GetVisibility()!=ESlateVisibility::Collapsed);
    TestTrue(TEXT("Unchecking hides both marks"),F.Lua.ExecuteString(TEXT("AnchorFooter:SetChecked(false)")) && Checked->GetVisibility()==ESlateVisibility::Collapsed && Disabled->GetVisibility()==ESlateVisibility::Collapsed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudMacroFlow,"WowUnreal.Hud.MacroFlow",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudMacroFlow::RunTest(const FString&)
{
    FString DataPath; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),DataPath)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(DataPath)) return false;
    if(!FDbcStore::Get().IsLoaded()) FDbcStore::Get().LoadAll(Mpq);
    if(!FDbcStore::Get().IsDeferredLoaded()) FDbcStore::Get().LoadDeferred(Mpq);
    FHudFixture F;
    TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    F.Context.ConnectionManager=Connection.Get(); F.Context.EntityManager=&Connection->PacketHandler.EntityManager;
    auto& H=Connection->PacketHandler; H.KnownSpells.Add(78); H.ActionButtons.SetNumZeroed(144);
    Connection->OnMacroUse.BindLambda([&F](uint32 Id) { return FWowMacros::RunWireMacro(F.Lua.GetState(),Id); });
    TestTrue(TEXT("Account and character macros support editing, display indices and action pickup"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(GetNumMacros()==0 and select(2,GetNumMacros())==0)
        MacroActionListener=CreateFrame('Frame'); MacroActionListener:RegisterEvent('ACTIONBAR_SLOT_CHANGED')
        assert(GetNumMacroIcons()>20)
        AccountMacro=CreateMacro('First',1,'/run MacroTrace=1',false)
        assert(AccountMacro==1 and CreateMacro('Second',2,'',false)==2)
        CharacterMacro=CreateMacro('Character',1,'#showtooltip '..GetSpellInfo(6603)..'\n/run MacroTrace=(MacroTrace or 0)+10',true)
        assert(CharacterMacro==37 and GetMacroIndexByName('Character')==37)
        assert(select(3,GetMacroSpell(CharacterMacro))==6603)
        PickupMacro(CharacterMacro); assert(GetCursorInfo()=='macro' and select(2,GetCursorInfo())==37)
        PlaceAction(84); assert(not GetCursorInfo())
        local kind,id=GetActionInfo(84); assert(kind=='macro' and id==37 and GetActionText(84)=='Character')
        assert(GetActionTexture(84)==select(3,GetSpellInfo(6603)))
        MacroLines={}; MacroRecorder=CreateFrame('Frame','MacroRecorder',UIParent)
        MacroRecorder:RegisterEvent('EXECUTE_CHAT_LINE')
        MacroRecorder:SetScript('OnEvent',function(self,event,line)
            MacroLines[#MacroLines+1]=line
            if line:sub(1,5)=='/run ' then RunScript(line:sub(6)) end
            if line=='/stopmacro' then StopMacro() end
        end)
    )LUA")));
    const uint32 Wire=H.ActionButtons[83]&0xffffff;
    TestTrue(TEXT("Network invocation resolves an opaque macro identity"),Wire>=4096 && Connection->UseActionSlot(83));
    TestTrue(TEXT("Deleting another macro cannot retarget an existing action"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(MacroTrace==10 and #MacroLines==1)
        DeleteMacro(AccountMacro); assert(GetMacroInfo(1)=='Second' and select(2,GetActionInfo(84))==37)
        assert(EditMacro(37,'Renamed',nil,'/run MacroTrace=MacroTrace+4')==37)
        assert(GetActionText(84)=='Renamed')
        RunMacro(37); assert(MacroTrace==14)
        RunMacroText('/run MacroTrace=MacroTrace+1\n/stopmacro\n/run MacroTrace=999')
        assert(MacroTrace==15 and GetRunningMacro()==nil)
        UnitExists=function(unit) return unit=='player' end
        UnitCanAssist=function(a,b) return b=='player' end
        UnitCanAttack=function() return false end
        UnitIsDeadOrGhost=function() return false end
        IsShiftKeyDown=function() return false end
        local action,target=SecureCmdOptionParse('[mod:shift][@player,help,nodead] Heal; Fallback')
        assert(action=='Heal' and target=='player')
        action,target=SecureCmdOptionParse('[harm] Strike; [@player,help] Heal')
        assert(action=='Heal' and target=='player')
        assert(SecureCmdOptionParse('[unknown] Hidden')==nil and SecureCmdOptionParse('[nounknown] Hidden')==nil)
        for i=2,36 do CreateMacro('Slot'..i,1,'',false) end
        assert(GetNumMacros()==36 and not pcall(CreateMacro,'Overflow',1,'',false))
        DeleteMacro(37); assert(not HasAction(84) and GetActionInfo(84)==nil)
    )LUA")));
    TestTrue(TEXT("Deleting a macro clears only actions that reference its stable identity"),H.ActionButtons[83]==0);
    const auto Packet=UWowConnectionManager::BuildUseItemPacket(255,23,0x400000000000000aULL,8690,0x0102000000000003ULL);
    FPacketReader R(Packet);
    TestTrue(TEXT("Targeted item macros encode the full WotLK use-item layout"),R.ReadU8()==255 && R.ReadU8()==23 && R.ReadU8()==0 && R.ReadU32()==8690 && R.ReadU64()==0x400000000000000aULL && R.ReadU32()==0 && R.ReadU8()==0 && R.ReadU32()==2 && R.ReadPackedGuid()==0x0102000000000003ULL && R.Remaining()==0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudCombatHistory,"WowUnreal.Hud.CombatHistory",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudCombatHistory::RunTest(const FString&)
{
    FHudFixture F;
    TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    F.Context.ConnectionManager=Connection.Get(); auto& H=Connection->PacketHandler;
    F.Context.EntityManager=&H.EntityManager; H.EntityManager.LocalPlayerGuid=1;
    H.EntityManager.PromoteToTyped(1,WowTypeMask::PLAYER|WowTypeMask::UNIT);
    const uint64 Victim=0xf130000005000001ULL;
    auto& Unit=H.EntityManager.PromoteToTyped(Victim,WowTypeMask::UNIT); Unit.Entry=5;
    H.PlayerNameCache.Add(1,TEXT("Player")); H.CreatureNameCache.Add(5,TEXT("Wolf"));
    H.OnCombatLog.BindLambda([&](const FWowCombatEntry& E) { FWowCombatLogUi::Dispatch(F.Lua.GetState(),E); });
    TestTrue(TEXT("Combat listeners initialize"),F.Lua.ExecuteString(TEXT(R"LUA(
        CombatRows={}; CombatFiltered=0; CombatListener=CreateFrame('Frame');
        CombatListener:RegisterEvent('COMBAT_LOG_EVENT'); CombatListener:RegisterEvent('COMBAT_LOG_EVENT_UNFILTERED')
        CombatListener:SetScript('OnEvent',function(self,event,...)
            if event=='COMBAT_LOG_EVENT' then CombatFiltered=CombatFiltered+1 else
                local row={...}; CombatRows[#CombatRows+1]=row
                assert(type(row[1])=='number' and type(row[5])=='number')
                assert(select(2,CombatLogGetCurrentEventInfo())==row[2])
            end
        end)
        CombatLogResetFilter()
    )LUA")));
    TArray<uint8> Packet;
    const auto U32=[&](uint32 Value) { Packet.Append(reinterpret_cast<const uint8*>(&Value),4); };
    const auto Float=[&](float Value) { Packet.Append(reinterpret_cast<const uint8*>(&Value),4); };
    const auto Guid=[&](uint64 Value) { uint8 Mask=0; TArray<uint8> Bytes; for(int I=0;I<8;++I) if(uint8 Byte=Value>>(8*I)) { Mask|=1<<I; Bytes.Add(Byte); } Packet.Add(Mask); Packet.Append(Bytes); };
    U32(0x2340); Guid(1); Guid(Victim); U32(75); U32(0); Packet.Add(2);
    U32(1); Float(50); U32(50); U32(4); Float(25); U32(25);
    U32(3); U32(2); U32(4); U32(1); Packet.Add(1); U32(0); U32(0); U32(8);
    H.HandlePacket(WowOpcode::SMSG_ATTACKERSTATEUPDATE,Packet);
    TestTrue(TEXT("Melee packet retains all WotLK damage schools and conditional fields"),F.Lua.ExecuteString(TEXT(R"LUA(
        local e=CombatRows[1]; assert(#CombatRows==1 and e[2]=='SWING_DAMAGE')
        assert(e[3]=='0x0000000000000001' and e[4]=='Player' and e[7]=='Wolf')
        assert(e[9]==75 and e[10]==0 and e[11]==5 and e[12]==5 and e[13]==8 and e[14]==5 and e[15]==true and e[16]==false)
    )LUA")));
    Packet.Pop(); H.HandlePacket(WowOpcode::SMSG_ATTACKERSTATEUPDATE,Packet);
    TestEqual(TEXT("Truncated conditional melee payload does not enter history"),H.CombatHistory.Num(),1);
    const uint64 NpcOwner=0xf130000006000001ULL;
    Unit.Fields.Add(UnitField::SUMMONEDBY,uint32(NpcOwner&0xffffffffULL)); Unit.Fields.Add(UnitField::SUMMONEDBY+1,uint32(NpcOwner>>32));
    TestEqual(TEXT("A creature controlled by another NPC retains NPC control flags"),WowLuaApi::CombatLogFlags(&F.Context,Victim)&0x300u,0x200u);
    Packet.Reset(); Guid(Victim); Guid(1); U32(78); U32(31); U32(0); Packet.Add(1); U32(4); U32(2); Packet.Add(0); Packet.Add(0); U32(3); U32(2); Packet.Add(0);
    H.HandlePacket(0x250,Packet);
    Packet.Reset(); Guid(1); Guid(1); U32(2050); U32(60); U32(17); U32(4); Packet.Add(1); Packet.Add(0);
    H.HandlePacket(0x150,Packet);
    TestTrue(TEXT("Spell events retain typed payloads and history filtering terminates"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(#CombatRows==3 and CombatFiltered==3)
        local d=CombatRows[2]; assert(d[2]=='SPELL_DAMAGE' and d[9]==78 and d[12]==31 and d[15]==2 and d[16]==3 and d[17]==4 and d[18]==true)
        local h=CombatRows[3]; assert(h[2]=='SPELL_HEAL' and h[9]==2050 and h[12]==60 and h[13]==17 and h[14]==4 and h[15]==true)
        CombatLogResetFilter(); CombatLogAddFilter('SPELL_HEAL','0x0000000000000001',nil)
        assert(CombatLogGetNumEntries()==1 and CombatLogGetNumEntries(true)==3)
        CombatLogSetCurrentEntry(0); assert(select(2,CombatLogGetCurrentEntry())=='SPELL_HEAL')
        assert(not CombatLogAdvanceEntry(-1) and CombatLogGetCurrentEntry()==nil)
        CombatLogResetFilter(); CombatLogAddFilter('',nil,nil); assert(CombatLogGetNumEntries()==0)
        CombatLogResetFilter(); CombatLogAddFilter(nil,nil,nil); CombatLogSetCurrentEntry(0)
        local count=0; while CombatLogGetCurrentEntry() do count=count+1; assert(count<=3); if not CombatLogAdvanceEntry(-1) then break end end
        assert(count==3)
        CombatLogClearEntries(); assert(CombatLogGetNumEntries()==0 and CombatLogGetCurrentEntry()==nil)
    )LUA")));
    FWowCombatHistory Ring;
    for(int I=0;I<FWowCombatHistory::Capacity+5;++I) { FWowCombatEntry E; E.Event=TEXT("SWING_DAMAGE"); Ring.Append(MoveTemp(E)); }
    TestTrue(TEXT("Bounded history evicts old serials without aliasing retained entries"),Ring.Num()==FWowCombatHistory::Capacity && !Ring.Find(1) && Ring.Find(Ring.First()) && Ring.Find(Ring.End()-1)->Serial==Ring.End()-1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudMacroPersistence,"WowUnreal.Hud.MacroPersistence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudMacroPersistence::RunTest(const FString&)
{
    FHudFixture F;
    FWowSavedVariables::SetIdentity(TEXT("IdleWowAutomation"),TEXT("Offline"),TEXT("MacroFixture"));
    FWowTocData Toc; Toc.SavedVariables.Add(TEXT("MacroRoundTrip"));
    const FString File=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("WTF/Account/IdleWowAutomation/SavedVariables/MacroRoundTripTest.lua"));
    TestTrue(TEXT("UTF-8 and binary macro data initialize"),F.Lua.ExecuteString(TEXT(R"LUA(
        MacroExpected='Проверка\n/run print("Привет")'..string.char(0,1,31)..'123'
        MacroKey='Имя'..string.char(0)..'0'
        MacroRoundTrip={body=MacroExpected,[MacroKey]=MacroExpected}
    )LUA")));
    FWowSavedVariables::SaveForAddon(F.Lua.GetState(),TEXT("MacroRoundTripTest"),Toc);
    F.Lua.ExecuteString(TEXT("MacroRoundTrip=nil"));
    FWowSavedVariables::LoadForAddon(F.Lua.GetState(),TEXT("MacroRoundTripTest"),Toc);
    TestTrue(TEXT("Macro text and keys retain exact bytes through disk persistence"),F.Lua.ExecuteString(TEXT("assert(MacroRoundTrip.body==MacroExpected and MacroRoundTrip[MacroKey]==MacroExpected)")));
    TestFalse(TEXT("Completed save leaves no temporary file"),IFileManager::Get().FileExists(*(File+TEXT(".tmp"))));
    IFileManager::Get().Delete(*File);
    FWowSavedVariables::SetIdentity(TEXT("WowTestUser"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudMultiLineEditor,"WowUnreal.Hud.MultiLineEditor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudMultiLineEditor::RunTest(const FString&)
{
    FHudFixture F;
    const FString Xml=TEXT("<Ui><EditBox name='MacroEditorTest' multiLine='true' letters='255'><Size x='180' y='64'/><Anchors><Anchor point='CENTER'/></Anchors><Scripts><OnCursorChanged>assert(type(x)=='number' and type(y)=='number' and height>0); CursorEvents=(CursorEvents or 0)+1</OnCursorChanged><OnTextChanged>TextEvents=(TextEvents or 0)+1</OnTextChanged></Scripts></EditBox></Ui>");
    const FTCHARToUTF8 Utf8(*Xml); TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("MacroEditor"));
    if(!TestEqual(TEXT("Multiline edit box parses"),Parsed.Num(),1)) return false;
    const int64 Handle=F.Frames.CreateFrame(Parsed[0].FrameDef);
    TestNotNull(TEXT("XML creates a native multiline editor"),F.Frames.GetMultiLineEditBoxWidget(Handle));
    TestTrue(TEXT("Lua text, cursor placement and insertion operate on real multiline text"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(MacroEditorTest:IsMultiLine())
        MacroEditorTest:SetText('one\ntwo'); MacroEditorTest:SetCursorPosition(4)
        MacroEditorTest:Insert('M\n')
        assert(MacroEditorTest:GetText()=='one\nM\ntwo' and MacroEditorTest:GetCursorPosition()==6 and MacroEditorTest:GetNumLetters()==9, string.format('text=%q position=%s letters=%s',MacroEditorTest:GetText(),MacroEditorTest:GetCursorPosition(),MacroEditorTest:GetNumLetters()))
    )LUA")));
    F.Frames.TickWidgetEvents();
    TestTrue(TEXT("Measured cursor notifications precede the text-change handler"),F.Lua.ExecuteString(TEXT("assert(CursorEvents>0 and TextEvents==1)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiIncrementalGc,"WowUnreal.Performance.UiTransientMemory",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FUiIncrementalGc::RunTest(const FString&)
{
    FHudFixture F;
    F.Lua.SetMemoryLimit(16*1024*1024);
    TestTrue(TEXT("Transient allocation handler loads"),F.Lua.ExecuteString(TEXT(R"LUA(
        BudgetTicks=0
        BudgetFrame=CreateFrame('Frame','BudgetFrame',UIParent)
        BudgetFrame:SetSize(32,32)
        BudgetFrame:SetPoint('CENTER',UIParent,'CENTER',0,0)
        BudgetFrame:SetScript('OnUpdate',function(self,elapsed)
            BudgetTicks=BudgetTicks+1
            local transient={}
            for i=1,1024 do transient[i]=string.rep('x',64)..i..':'..BudgetTicks end
            self.lastValue=transient[1024]
        end)
    )LUA")));
    F.Frames.RefreshLayout();
    TestTrue(TEXT("Allocation handler is attached to a drawn frame"),F.Frames.IsFrameDrawn(F.Frames.FindFrame(TEXT("BudgetFrame"))));
    SIZE_T Peak=0;
    for (int32 I=0; I<180; ++I)
    {
        F.Events.TickOnUpdate(1.f/60);
        Peak=FMath::Max(Peak,F.Lua.GetMemoryUsage());
    }
    TestTrue(TEXT("All callbacks run without dropping updates or exceeding the sandbox"),F.Lua.ExecuteString(TEXT("assert(BudgetTicks==180); assert(BudgetFrame.lastValue==string.rep('x',64)..'1024:180')")));
    TestTrue(TEXT("Incremental collection bounds transient memory"),Peak<F.Lua.GetMemoryLimit()*9/10);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudTextureStyles,"WowUnreal.Hud.TextureStyles",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudTextureStyles::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    FHudFixture F; F.Frames.SetMpqManager(&Mpq); FWowAssetCache Cache; F.Frames.SetAssetCache(&Cache);
    TestTrue(TEXT("Desaturation keeps the editable region and crop state"),F.Lua.ExecuteString(TEXT(R"LUA(
        StyleOwner=CreateFrame('Frame','StyleOwner',UIParent); StyleOwner:SetSize(100,100); StyleOwner:SetPoint('CENTER')
        StyledIcon=StyleOwner:CreateTexture('StyledIcon','ARTWORK'); StyledIcon:SetSize(64,64); StyledIcon:SetPoint('CENTER')
        StyledIcon:SetTexture('Interface\\Icons\\Ability_Kick'); StyledIcon:SetTexCoord(.1,.9,.2,.8); StyledIcon:SetAlpha(.4)
        assert(StyledIcon:SetDesaturated(true) and StyledIcon:IsDesaturated())
    )LUA")));
    auto* Image=F.Frames.GetNamedTexture(TEXT("StyledIcon")); auto* Material=Image?Cast<UMaterialInstanceDynamic>(Image->GetBrush().GetResourceObject()):nullptr;
    if(!TestNotNull(TEXT("Desaturation uses a real UI shader"),Material)) return false;
    float Gray=0; FLinearColor Corner;
    TestTrue(TEXT("Shader retains crop coordinates and desaturation"),Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Desaturation")),Gray) && Gray==1 && Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("UVTL")),Corner) && Corner.Equals(FLinearColor(.1,.2,0,0),.001));
    TestTrue(TEXT("Texture replacement retains style and eight-coordinate UVs round-trip"),F.Lua.ExecuteString(TEXT(R"LUA(
        StyledIcon:SetTexture('Interface\\Icons\\INV_Misc_QuestionMark'); assert(StyledIcon:IsDesaturated() and math.abs(StyledIcon:GetAlpha()-.4)<.00001)
        StyledIcon:SetTexCoord(0,1,1,1,0,0,1,0); local a,b,c,d,e,f,g,h=StyledIcon:GetTexCoord(); assert(a==0 and b==1 and c==1 and d==1 and e==0 and f==0 and g==1 and h==0)
        StyledIcon:SetDesaturated(false); assert(not StyledIcon:IsDesaturated())
    )LUA")));
    Material=Cast<UMaterialInstanceDynamic>(Image->GetBrush().GetResourceObject());
    TestTrue(TEXT("Arbitrary UVs stay active when desaturation is disabled"),Material && Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Desaturation")),Gray) && Gray==0 && Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("UVTL")),Corner) && Corner==FLinearColor(0,1,0,0));
    TestTrue(TEXT("Returning to rectangular coordinates restores direct texture rendering"),F.Lua.ExecuteString(TEXT("StyledIcon:SetTexCoord(1,0,0,1); assert(select(1,StyledIcon:GetTexCoord())==1)")) && Cast<UTexture2D>(Image->GetBrush().GetResourceObject()) && Image->GetRenderTransform().Scale.X==-1);
    TestTrue(TEXT("Additive mode survives texture replacement"),F.Lua.ExecuteString(TEXT("StyledIcon:SetBlendMode('ADD'); StyledIcon:SetTexture('Interface\\\\Icons\\\\Ability_Kick'); assert(StyledIcon:GetBlendMode()=='ADD')")));
    Material=Cast<UMaterialInstanceDynamic>(Image->GetBrush().GetResourceObject());
    TestTrue(TEXT("Additive regions use the additive material without losing alpha"),Material && Material->Parent->GetName()==TEXT("M_WowUiRegionAdd") && FMath::IsNearlyEqual(Image->GetRenderOpacity(),.4f));
    F.Lua.ExecuteString(TEXT("StyledIcon:SetBlendMode('BLEND')"));
    auto* PortraitBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_WowUiImage.M_WowUiImage"));
    auto* Portrait=UMaterialInstanceDynamic::Create(PortraitBase,Image); Portrait->SetTextureParameterValue(TEXT("Image"),F.Frames.LoadUITexture(TEXT("Interface\\Icons\\Ability_Kick"))); Portrait->SetScalarParameterValue(TEXT("Circle"),1);
    auto Brush=Image->GetBrush(); Brush.SetResourceObject(Portrait); Image->SetBrush(Brush);
    TestTrue(TEXT("Ordinary crop changes preserve existing portrait materials"),F.Lua.ExecuteString(TEXT("StyledIcon:SetTexCoord(0,1,0,1)")) && Image->GetBrush().GetResourceObject()==Portrait);
    TestTrue(TEXT("A desaturated portrait preserves its circle and opaque render target"),F.Lua.ExecuteString(TEXT("StyledIcon:SetDesaturated(true)")));
    Material=Cast<UMaterialInstanceDynamic>(Image->GetBrush().GetResourceObject()); float Circle=0,Opaque=0;
    TestTrue(TEXT("Portrait mask remains in the shared material"),Material && Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Circle")),Circle) && Circle==1 && Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OpaqueImage")),Opaque) && Opaque==1);
    TestTrue(TEXT("Button highlight blending remains editable after hover refresh"),F.Lua.ExecuteString(TEXT(R"LUA(
        StyleButton=CreateFrame('Button','StyleButton',UIParent); StyleButton:SetSize(64,64); StyleButton:SetPoint('CENTER')
        StyleButton:SetHighlightTexture('Interface\\Icons\\Ability_Kick'); StyleButton:GetHighlightTexture():SetBlendMode('BLEND'); StyleButton:LockHighlight()
    )LUA")));
    F.Frames.RefreshButtonVisuals(F.Frames.FindFrame(TEXT("StyleButton")));
    TestTrue(TEXT("Hover state does not force an explicitly normal highlight back to additive"),Cast<UTexture2D>(F.Frames.GetNamedTexture(TEXT("StyleButtonHighlightTexture"))->GetBrush().GetResourceObject())!=nullptr);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudSliderControls,"WowUnreal.Hud.SliderControls",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudSliderControls::RunTest(const FString&)
{
    FHudFixture F;
    const FString Xml=TEXT("<Ui><Slider name='RangeSlider' minValue='10' maxValue='30' defaultValue='15' valueStep='2' orientation='VERTICAL'><Size x='32' y='200'/><Anchors><Anchor point='TOPLEFT' relativeTo='UIParent' relativePoint='TOPLEFT' x='20' y='-30'/></Anchors><ThumbTexture name='$parentThumb'><Size x='12' y='24'/><Color r='1' g='1' b='1' a='1'/></ThumbTexture><Scripts><OnMinMaxChanged>MinEvent=min; MaxEvent=max</OnMinMaxChanged></Scripts></Slider></Ui>");
    const FTCHARToUTF8 Utf8(*Xml); TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("SliderFixture")); if(Parsed.Num()!=1) return false;
    const int64 Handle=F.Frames.CreateFrame(Parsed[0].FrameDef); F.Frames.RefreshLayout();
    TestTrue(TEXT("XML range, default, orientation and step reach the canonical slider"),F.Lua.ExecuteString(TEXT("assert(RangeSlider:GetValue()==16 and RangeSlider:GetValueStep()==2 and RangeSlider:GetOrientation()=='VERTICAL'); assert(RangeSlider:GetThumbTexture()==RangeSliderThumb); assert(select(1,RangeSlider:GetMinMaxValues())==10)")));
    TestTrue(TEXT("Min/max changes preserve typed arguments and clamp the current value"),F.Lua.ExecuteString(TEXT("RangeSlider:SetMinMaxValues(20,40); assert(MinEvent==20 and MaxEvent==40 and RangeSlider:GetValue()==20); RangeSlider:SetMinMaxValues(10,30); RangeSlider:SetValue(30)")));
    F.Frames.RefreshLayout(); const auto End=F.Frames.GetRegionRect(TEXT("RangeSliderThumb")); const auto Track=F.Frames.GetLayoutRect(Handle);
    TestTrue(TEXT("Maximum vertical value places the thumb at the bottom"),FMath::IsNearlyEqual(End.Y+End.W,Track.Y+Track.W,.1));
    TestTrue(TEXT("Orientation changes retain the same semantic value"),F.Lua.ExecuteString(TEXT("RangeSlider:SetOrientation('HORIZONTAL'); assert(RangeSlider:GetValue()==30); RangeSlider:SetSize(200,32); RangeSlider:SetValue(10)")));
    F.Frames.RefreshLayout(); const auto Start=F.Frames.GetRegionRect(TEXT("RangeSliderThumb"));
    TestTrue(TEXT("Minimum horizontal value places the thumb at the left"),FMath::IsNearlyEqual(Start.X,Track.X,.1));
    const auto R=F.Frames.GetLayoutRect(Handle); const float Scale=F.Frames.GetScreenScale();
    F.Frames.UpdateMouseHover((R.X+6)*Scale,(R.Y+16)*Scale); F.Frames.DispatchMouseDown(Handle,TEXT("LeftButton"));
    F.Frames.UpdateMouseHover((R.X+R.Z-6)*Scale,(R.Y+16)*Scale); F.Frames.DispatchMouseUp(Handle,TEXT("LeftButton"));
    TestTrue(TEXT("Pointer dragging updates the value over the full thumb travel"),F.Frames.GetSliderValue(Handle)==30);
    TestTrue(TEXT("Disabled sliders keep their value during pointer input"),F.Lua.ExecuteString(TEXT("RangeSlider:Disable(); RangeSlider:SetValue(20)")));
    F.Frames.UpdateMouseHover((R.X+6)*Scale,(R.Y+16)*Scale); F.Frames.DispatchMouseDown(Handle,TEXT("LeftButton")); F.Frames.UpdateMouseHover((R.X+R.Z)*Scale,(R.Y+16)*Scale); F.Frames.DispatchMouseUp(Handle,TEXT("LeftButton"));
    TestEqual(TEXT("Disabled input is ignored"),F.Frames.GetSliderValue(Handle),20.f);
    TestTrue(TEXT("Fractional callbacks preserve the exact value returned by GetValue"),F.Lua.ExecuteString(TEXT("RangeSlider:Enable(); RangeSlider:SetValueStep(.001); FloatEvents=0; RangeSlider:SetScript('OnValueChanged',function(self,value) assert(self:GetValue()==value); FloatEvents=FloatEvents+1 end); RangeSlider:SetValue(24.123); assert(FloatEvents==1)")));
    for(int32 Tick=0;Tick<20;++Tick) F.Frames.TickWidgetEvents();
    TestTrue(TEXT("Polling does not repeat a fractional change"),F.Lua.ExecuteString(TEXT("assert(FloatEvents==1)")));
    TestTrue(TEXT("Existing texture regions keep their identity when assigned as a thumb"),F.Lua.ExecuteString(TEXT("OtherThumb=UIParent:CreateTexture('OtherThumb','ARTWORK'); OtherThumb:SetSize(20,20); OtherThumb:SetTexture(1,1,1,1); RangeSlider:SetThumbTexture(OtherThumb); assert(RangeSlider:GetThumbTexture()==OtherThumb and OtherThumb:GetParent()==RangeSlider); RangeSlider:SetThumbTexture(nil); assert(RangeSlider:GetThumbTexture()==nil)")));
    TestTrue(TEXT("A dynamically created thumb can be restored after removal"),F.Lua.ExecuteString(TEXT("RangeSlider:SetThumbTexture(1,.5,0,1); RangeSlider:GetThumbTexture():SetSize(18,30)")));
    F.Frames.RefreshLayout(); const auto* Dynamic=F.Frames.GetNamedTexture(F.Frames.SliderThumbRegions.FindRef(Handle));
    TestTrue(TEXT("Anchoring a dynamic thumb restores its painted visibility"),Dynamic && Dynamic->GetVisibility()!=ESlateVisibility::Hidden && Dynamic->GetVisibility()!=ESlateVisibility::Collapsed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudScrollContent,"WowUnreal.Hud.ScrollContent",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudScrollContent::RunTest(const FString&)
{
    FHudFixture F;
    const char* Xml="<Ui><ScrollFrame name='Journal'><Size x='220' y='100'/><Anchors><Anchor point='CENTER'/></Anchors><ScrollChild><Frame name='JournalChild'><Size x='220' y='300'/></Frame></ScrollChild><Scripts><OnMouseWheel>WheelValue=delta</OnMouseWheel><OnVerticalScroll>OffsetValue=offset</OnVerticalScroll></Scripts></ScrollFrame></Ui>";
    // A real XML ScrollChild has no anchors; the engine supplies its origin.
    const FString Source=UTF8_TO_TCHAR(Xml);
    const FTCHARToUTF8 Utf8(*Source); TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("JournalScroll"));
    if(!TestEqual(TEXT("Scroll fixture parses"),Parsed.Num(),1)) return false;
    const int64 Scroll=F.Frames.CreateFrame(Parsed[0].FrameDef),Child=F.Frames.FindFrame(TEXT("JournalChild"));
    TestTrue(TEXT("XML wheel handlers enable scrolling and receive numeric deltas"),F.Frames.IsFrameMouseWheelEnabled(Scroll) && F.Frames.DispatchMouseWheel(Scroll,-1) && F.Lua.ExecuteString(TEXT("assert(WheelValue==-1)")));
    F.Frames.RefreshLayout(); const auto Before=F.Frames.GetLayoutRect(Child);
    TestTrue(TEXT("XML registers and anchors a visible scroll child"),F.Frames.GetScrollChild(Scroll)==Child && Before.Z==220 && Before.W==300 && F.Frames.IsFrameDrawn(Child));
    TestTrue(TEXT("Scroll clipping preserves external scroll-bar clicks"),F.Lua.ExecuteString(TEXT(R"LUA(
        Journal:EnableMouse(true)
        ExternalBar=CreateFrame('Button','ExternalBar',Journal); ExternalBar:SetSize(20,100); ExternalBar:SetPoint('TOPLEFT',Journal,'TOPRIGHT',2,0)
        ClippedButton=CreateFrame('Button','ClippedButton',JournalChild); ClippedButton:SetSize(20,20); ClippedButton:SetPoint('TOPLEFT',JournalChild,'TOPRIGHT',2,0)
    )LUA")));
    F.Frames.RefreshLayout(); const auto Bar=F.Frames.GetLayoutRect(F.Frames.FindFrame(TEXT("ExternalBar")));
    const int64 Hit=F.Frames.HitTestFrames((Bar.X+10)*F.Frames.GetScreenScale(),(Bar.Y+10)*F.Frames.GetScreenScale());
    TestEqual(TEXT("Only scroll content is clipped, not sibling scroll bars"),Hit,F.Frames.FindFrame(TEXT("ExternalBar")));
    TestTrue(TEXT("A dedicated viewport clips the scroll child"),F.Frames.GetWidgetForHandle(Child)->GetParent()->GetClipping()==EWidgetClipping::ClipToBoundsAlways && F.Frames.GetWidgetForHandle(Scroll)->GetClipping()==EWidgetClipping::Inherit);
    F.Frames.SetVerticalScroll(Scroll,45); F.Frames.RefreshLayout(); const auto After=F.Frames.GetLayoutRect(Child);
    TestTrue(TEXT("Scroll position notifications carry numeric offsets"),F.Lua.ExecuteString(TEXT("assert(OffsetValue==45)")));
    TestTrue(TEXT("A layout refresh preserves scroll offsets and hit rectangles"),FMath::IsNearlyEqual(After.Y,Before.Y-45));
    TestTrue(TEXT("Text can move out of a hidden source panel into the journal"),F.Lua.ExecuteString(TEXT(R"LUA(
        Source=CreateFrame('Frame','Source',UIParent); Source:Hide()
        Note=Source:CreateFontString('JournalText','OVERLAY','GameFontNormal')
        Note:SetText('A quest paragraph that is visible in its new parent')
        Note:SetParent(JournalChild); Note:SetPoint('TOPLEFT',JournalChild,'TOPLEFT',0,0); Note:SetWidth(200)
        assert(Note:GetParent()==JournalChild and Note:IsVisible() and Note:GetHeight()>0)
        Note:SetText(string.rep('A wrapped quest paragraph with several words. ',35))
        Follower=CreateFrame('Frame','Follower',JournalChild); Follower:SetSize(100,20); Follower:SetPoint('CENTER'); Follower:SetPoint('TOPLEFT',Note,'BOTTOMLEFT',0,-10)
        CaptureKey=CreateFrame('Frame','CaptureKey',UIParent); CaptureKey:SetSize(100,100); CaptureKey:SetPoint('CENTER'); CaptureKey:EnableKeyboard(true)
        CaptureKey:SetScript('OnKeyDown',function(self,key) assert(key=='F8'); SetBinding(key,'TOGGLESPELLBOOK') end)
    )LUA")));
    F.Frames.RefreshLayout();
    TestTrue(TEXT("Frames below wrapped text use its final measured height"),F.Lua.ExecuteString(TEXT("assert(Follower:GetTop()<=Note:GetBottom()-9.5)")));
    TestTrue(TEXT("Scrolling includes content extending beyond the nominal child size"),F.Frames.GetVerticalScrollRange(Scroll)>200);
    TestTrue(TEXT("A ranged slider drives a scroll frame in WoW units"),F.Lua.ExecuteString(TEXT(R"LUA(
        ScrollSlider=CreateFrame('Slider','ScrollSlider',UIParent)
        ScrollSlider:SetMinMaxValues(0,200)
        SliderEvents=0
        ScrollSlider:SetScript('OnValueChanged',function(self,value)
            assert(self:GetValue()==value)
            SliderEvents=SliderEvents+1
            Journal:SetVerticalScroll(value)
        end)
        ScrollSlider:SetValue(75)
    )LUA")));
    for(int32 Tick=0;Tick<20;++Tick) F.Frames.TickWidgetEvents();
    TestTrue(TEXT("Polling cannot decay a programmatic scroll position"),F.Lua.ExecuteString(TEXT("assert(ScrollSlider:GetValue()==75 and Journal:GetVerticalScroll()==75 and SliderEvents==1)")));
    auto* Slider=F.Frames.GetSliderWidget(F.Frames.FindFrame(TEXT("ScrollSlider")));
    if(!TestNotNull(TEXT("Scroll slider has a native thumb"),Slider)) return false;
    Slider->SetValue(80); // Native vertical slider increases upward; WoW scroll offsets increase downward.
    for(int32 Tick=0;Tick<20;++Tick) F.Frames.TickWidgetEvents();
    TestTrue(TEXT("Native slider movement reports a stable raw offset exactly once"),F.Lua.ExecuteString(TEXT("assert(ScrollSlider:GetValue()==120 and Journal:GetVerticalScroll()==120 and SliderEvents==2)")));
    TestTrue(TEXT("Visible keyboard capture reaches the binding handler"),F.Frames.DispatchKeyboard(TEXT("F8"),true));
    TestTrue(TEXT("Capturing a key updates its real binding"),F.Lua.ExecuteString(TEXT("assert(GetBindingAction('F8')=='TOGGLESPELLBOOK'); LoadBindings(2); assert(GetBindingAction('F8')=='')")));
    return true;
}
#endif
