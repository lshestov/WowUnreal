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
        assert(GetBindingAction('W')=='MOVEFORWARD' and GetBindingKey('MOVEFORWARD')=='W')
        assert(SetBinding('F8','TOGGLESPELLBOOK') and GetBindingAction('F8')=='TOGGLESPELLBOOK')
        assert(SetBinding('P') and GetBindingAction('P')=='')
        assert(not SetBinding('F9','UNSUPPORTED_COMMAND'))
        LoadBindings(0); assert(GetBindingAction('P')=='TOGGLESPELLBOOK' and GetBindingAction('F8')=='')
        Anonymous=CreateFrame('Frame'); Anonymous.saved='state'; Anonymous.received=false
        Anonymous:SetScript('OnEvent',function(self,event) assert(self==Anonymous and self.saved=='state'); self.received=true end)
        Anonymous:RegisterEvent('UI_FIXTURE')
        AnonymousChild=CreateFrame('Frame',nil,Anonymous); assert(AnonymousChild:GetParent()==Anonymous)
        assert(CombatLogGetCurrentEntry()==nil and CombatLogAdvanceEntry(-1)==nil)
        Control=CreateFrame('CheckButton','Control',UIParent)
        assert(Control:IsObjectType('frame') and Control:IsObjectType('Button') and Control:IsObjectType('CheckButton') and not Control:IsObjectType('EditBox'))
        FixtureFont=CreateFont('FixtureFont'); FixtureFont:SetFont('Fonts\\FRIZQT__.TTF',14); FixtureFont:SetTextColor(.2,.4,.6)
        CaptionButtonText:SetFontObject(FixtureFont); assert(CaptionButtonText:GetFontObject()==FixtureFont)
        local r,g,b=CaptionButtonText:GetTextColor(); assert(math.abs(r-.2)<.0001 and math.abs(g-.4)<.0001 and math.abs(b-.6)<.0001)
        Control:SetSize(24,24); Control:SetPoint('TOPLEFT',UIParent,'TOPLEFT',20,-20)
        Control:SetChecked(true); assert(Control:GetChecked()); Control:SetChecked(false); assert(not Control:GetChecked())
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
    Slider->SetValue(120);
    for(int32 Tick=0;Tick<20;++Tick) F.Frames.TickWidgetEvents();
    TestTrue(TEXT("Native slider movement reports a stable raw offset exactly once"),F.Lua.ExecuteString(TEXT("assert(ScrollSlider:GetValue()==120 and Journal:GetVerticalScroll()==120 and SliderEvents==2)")));
    TestTrue(TEXT("Visible keyboard capture reaches the binding handler"),F.Frames.DispatchKeyboard(TEXT("F8"),true));
    TestTrue(TEXT("Capturing a key updates its real binding"),F.Lua.ExecuteString(TEXT("assert(GetBindingAction('F8')=='TOGGLESPELLBOOK'); LoadBindings(2); assert(GetBindingAction('F8')=='')")));
    return true;
}
#endif
