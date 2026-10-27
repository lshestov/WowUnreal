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
#include "../WowUiCursorRenderer.h"
#include "../WowMapUi.h"
#include "../WowMinimapUi.h"
#include "Formats/DbcParser.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/EditableTextBox.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Mpq/MpqManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "WowAssetCache.h"
#include "UObject/StrongObjectPtr.h"
#include "Coord/WowCoordinate.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudFontAlignment,"WowUnreal.Hud.FontAlignmentInheritance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudFontAlignment::RunTest(const FString&)
{
    FHudFixture F;
    const char* Xml=R"XML(<Ui>
      <Font name="AlignedBase" justifyH="LEFT" justifyV="BOTTOM"/><Font name="AlignedDerived" inherits="AlignedBase"/>
      <Frame name="AlignedPanel" parent="UIParent"><Size x="300" y="100"/><Anchors><Anchor point="CENTER"/></Anchors><Layers><Layer>
        <FontString name="AlignedDefault" text="Centered title"><Size x="280" y="20"/><Anchors><Anchor point="TOP"/></Anchors></FontString>
        <FontString name="AlignedInherited" inherits="AlignedDerived" text="Inherited"><Size x="280" y="20"/><Anchors><Anchor point="CENTER"/></Anchors></FontString>
        <FontString name="AlignedOverride" inherits="AlignedDerived" text="Override" justifyH="RIGHT" justifyV="TOP"><Size x="280" y="20"/><Anchors><Anchor point="BOTTOM"/></Anchors></FontString>
      </Layer></Layers></Frame></Ui>)XML";
    TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Xml),FCStringAnsi::Strlen(Xml));
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("FontAlignmentFixture"));
    for(const auto& Dir:Parsed) if(Dir.Type==FWowXmlDirective::EType::Font) FWowEventSystem::RegisterFontDefinition(&F.Lua,nullptr,Dir); else if(Dir.Type==FWowXmlDirective::EType::Frame) F.Frames.CreateFrame(Dir.FrameDef);
    TestTrue(TEXT("Default, inherited and explicit XML alignment reach rendered labels"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(AlignedDefault:GetJustifyH()=='CENTER' and AlignedDefault:GetJustifyV()=='MIDDLE')
        assert(AlignedInherited:GetJustifyH()=='LEFT' and AlignedInherited:GetJustifyV()=='BOTTOM')
        assert(AlignedOverride:GetJustifyH()=='RIGHT' and AlignedOverride:GetJustifyV()=='TOP')
        local plain=AlignedPanel:CreateFontString('AlignedDynamic'); assert(plain:GetJustifyH()=='CENTER')
        local inherited=AlignedPanel:CreateFontString('AlignedDynamicInherited',nil,'AlignedDerived'); assert(inherited:GetJustifyH()=='LEFT' and inherited:GetJustifyV()=='BOTTOM')
        inherited:SetJustifyH('RIGHT'); assert(inherited:GetJustifyH()=='RIGHT')
    )LUA")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudUnitReactions,"WowUnreal.Hud.UnitReactions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudUnitReactions::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false; auto& D=FDbcStore::Get();
    if(!D.IsLoaded()) D.LoadAll(Mpq); if(!D.IsDeferredLoaded()) D.LoadDeferred(Mpq);
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> C(NewObject<UWowConnectionManager>());
    auto& H=C->PacketHandler; auto& E=H.EntityManager; E.LocalPlayerGuid=1;
    auto& P=E.PromoteToTyped(1,WowTypeMask::UNIT|WowTypeMask::PLAYER);
    auto& N=E.PromoteToTyped(0xf130000000000064,WowTypeMask::UNIT);
    auto& Other=E.PromoteToTyped(2,WowTypeMask::UNIT|WowTypeMask::PLAYER);
    P.SetField(UnitField::FACTIONTEMPLATE,1); P.SetField(UnitField::FLAGS,8); P.SetField(UnitField::BYTES_0,1|(1<<8));
    N.SetField(UnitField::FACTIONTEMPLATE,12); Other.SetField(UnitField::FACTIONTEMPLATE,2); Other.SetField(UnitField::FLAGS,8);
    F.Context.EntityManager=&E; F.Context.ConnectionManager=C.Get(); C->SendSetSelection(N.Guid);
    H.Reputations.SetNum(128); const FDbcStore::FFaction* Stormwind=nullptr;
    for(const auto& Row:D.Factions()) if(Row.Id==72) Stormwind=&Row;
    if(!TestNotNull(TEXT("Original Stormwind faction metadata exists"),Stormwind)) return false;
    auto& Rep=H.Reputations[Stormwind->Index]; Rep.Flags=1;
    TestTrue(TEXT("Original friendly NPC frame is green, self is Friendly, not fabricated Exalted"),F.Lua.ExecuteString(TEXT("assert(UnitReaction('target','player')==5 and UnitReaction('player','player')==5 and UnitIsFriend('player','target') and not UnitCanAttack('player','target'))")));
    TestTrue(TEXT("FrameXML selection colour is green for a friendly NPC"),F.Lua.ExecuteString(TEXT("local r,g,b=UnitSelectionColor('target'); assert(r==0 and g==1 and b==0); assert(UnitPlayerOrPetInParty('target')==nil and UnitPlayerOrPetInRaid('target')==nil)")));
    Rep.Standing=42000-Stormwind->BaseFor(1,1);
    TestTrue(TEXT("NPC standing and player attitude have distinct directions"),WowLuaApi::UnitReactionRank(&F.Context,&N,&P)==8 && WowLuaApi::UnitReactionRank(&F.Context,&P,&N)==5);
    Rep.Flags|=2;
    TestTrue(TEXT("At War changes the player's attitude without fabricating NPC standing"),WowLuaApi::UnitReactionRank(&F.Context,&P,&N)==2 && WowLuaApi::UnitReactionRank(&F.Context,&N,&P)==8 && WowLuaApi::CanUnitAttack(&F.Context,&P,&N));
    H.ForcedReactions.Add(72,1);
    TestTrue(TEXT("Server-forced faction reaction overrides both reputation directions"),WowLuaApi::UnitReactionRank(&F.Context,&N,&P)==1 && WowLuaApi::UnitReactionRank(&F.Context,&P,&N)==1);
    H.ForcedReactions.Reset(); Rep.Flags=1; P.SetField(UnitField::FLAGS_2,4);
    TestEqual(TEXT("Ignore-reputation units use faction-template masks"),WowLuaApi::UnitReactionRank(&F.Context,&N,&P),5);
    P.SetField(UnitField::FLAGS_2,0); Rep.Standing=0;
    N.SetField(UnitField::FACTIONTEMPLATE,121); P.SetField(PlayerField::FLAGS,0x100); H.ForcedReactions.Add(21,8);
    TestTrue(TEXT("Contested guard hostility precedes its forced attitude; player direction retains forced override"),WowLuaApi::UnitReactionRank(&F.Context,&N,&P)==2 && WowLuaApi::UnitReactionRank(&F.Context,&P,&N)==8);
    P.SetField(PlayerField::FLAGS,0); H.ForcedReactions.Reset();
    N.SetField(UnitField::FACTIONTEMPLATE,32);
    TestTrue(TEXT("Neutral Northshire wolves are yellow and remain attackable"),F.Lua.ExecuteString(TEXT("assert(UnitReaction('target','player')==4 and not UnitIsEnemy('player','target') and UnitCanAttack('player','target'))")));
    TestTrue(TEXT("FrameXML selection colour is yellow for a neutral wolf"),F.Lua.ExecuteString(TEXT("local r,g,b=UnitSelectionColor('target'); assert(r==1 and g==1 and b==0)")));
    N.SetField(UnitField::FLAGS,0x100);
    TestFalse(TEXT("PC immunity blocks attack independently from neutral reaction"),WowLuaApi::CanUnitAttack(&F.Context,&P,&N)); N.SetField(UnitField::FLAGS,0);
    N.SetField(UnitField::FACTIONTEMPLATE,14);
    TestEqual(TEXT("Hostile creature faction uses actual enemy mask"),WowLuaApi::UnitReactionRank(&F.Context,&N,&P),2);
    N.SetField(UnitField::FACTIONTEMPLATE,1630); P.SetField(UnitField::FACTIONTEMPLATE,35);
    TestEqual(TEXT("Default hostility applies without a matching group mask"),D.ItemUi().DefaultReaction(1630,35),2);
    P.SetField(UnitField::FACTIONTEMPLATE,1);
    TestTrue(TEXT("Opposing players need PvP eligibility in addition to hostility"),WowLuaApi::UnitReactionRank(&F.Context,&P,&Other)==2 && !WowLuaApi::CanUnitAttack(&F.Context,&P,&Other));
    TestTrue(TEXT("Unflagged opposing player uses the original blue selection colour"),WowLuaApi::UnitSelectionColor(&F.Context,&Other).Equals(FLinearColor(0,0,1,1)));
    Other.SetField(UnitField::BYTES_2,1<<8); TestTrue(TEXT("Flagged opposing player can be attacked"),WowLuaApi::CanUnitAttack(&F.Context,&P,&Other));
    TestTrue(TEXT("One-way PvP eligibility uses the original yellow selection colour"),WowLuaApi::UnitSelectionColor(&F.Context,&Other).Equals(FLinearColor(1,1,0,1)));
    P.SetField(UnitField::BYTES_2,1<<8);
    TestTrue(TEXT("Mutual PvP eligibility uses red"),WowLuaApi::UnitSelectionColor(&F.Context,&Other).Equals(FLinearColor(1,0,0,1)));
    P.SetField(UnitField::BYTES_2,0);
    Other.SetField(UnitField::BYTES_2,9<<8); TestFalse(TEXT("Sanctuary blocks a flagged opponent"),WowLuaApi::CanUnitAttack(&F.Context,&P,&Other));
    Other.SetField(UnitField::FACTIONTEMPLATE,1); Other.SetField(UnitField::BYTES_2,4<<8); P.SetField(UnitField::BYTES_2,4<<8);
    TestTrue(TEXT("FFA permits combat between same-faction players"),WowLuaApi::UnitReactionRank(&F.Context,&P,&Other)==2 && WowLuaApi::CanUnitAttack(&F.Context,&P,&Other));
    FWowGroupMember Member; Member.Guid=2; H.GroupInfo.Members.Add(Member); H.GroupInfo.MemberCount=2;
    TestTrue(TEXT("Player/pet membership APIs return the native numeric-or-nil shape"),F.Lua.ExecuteString(TEXT("assert(UnitPlayerOrPetInParty('party1')==1 and UnitPlayerOrPetInRaid('party1')==nil)")));
    TestEqual(TEXT("Party membership precedes FFA reaction"),WowLuaApi::UnitReactionRank(&F.Context,&P,&Other),5);
    P.SetField(PlayerField::BASE,99); Other.SetField(PlayerField::BASE,99); P.SetField(PlayerField::BASE+8,1); Other.SetField(PlayerField::BASE+8,2);
    TestTrue(TEXT("Opposing duel teams precede party friendship"),WowLuaApi::UnitReactionRank(&F.Context,&P,&Other)==2 && WowLuaApi::CanUnitAttack(&F.Context,&P,&Other));
    auto& Pet=E.PromoteToTyped(0xf140000000000008,WowTypeMask::UNIT); Pet.SetField(UnitField::FLAGS,8); Pet.SetField(UnitField::SUMMONEDBY,1);
    TestEqual(TEXT("Owner and controlled pet are friendly"),WowLuaApi::UnitReactionRank(&F.Context,&P,&Pet),5);
    C->SendSetSelection(N.Guid); N.SetField(UnitField::FACTIONTEMPLATE,12); P.SetField(UnitField::BYTES_2,0); P.SetField(PlayerField::BASE+8,0);
    TestTrue(TEXT("Reaction listener is registered"),F.Lua.ExecuteString(TEXT("ReactionProbe=CreateFrame('Frame'); ReactionProbe:RegisterEvent('UNIT_FACTION'); ReactionProbe:SetScript('OnEvent',function(self,event,unit) if unit=='target' then reactionEvents=(reactionEvents or 0)+1 end end)")));
    WowLuaApi::UpdateUnitReactions(F.Lua.GetState()); H.ForcedReactions.Add(72,2); WowLuaApi::UpdateUnitReactions(F.Lua.GetState()); WowLuaApi::UpdateUnitReactions(F.Lua.GetState());
    TestTrue(TEXT("Changed reputation/force refreshes an existing target once without retargeting"),F.Lua.ExecuteString(TEXT("assert(reactionEvents==1 and UnitReaction('target','player')==2)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudForcedReactionPackets,"WowUnreal.UI.ForcedReactionPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudForcedReactionPackets::RunTest(const FString&)
{
    FWowPacketHandler H; TArray<uint8> Data;
    const auto U32=[&](uint32 V) {Data.Append(reinterpret_cast<const uint8*>(&V),4);};
    U32(2); U32(72); U32(0); U32(76); U32(7); H.HandlePacket(0x02A5,Data);
    TestTrue(TEXT("Wire reputation ranks are zero-based; Lua reactions are one-based"),H.ForcedReactions.FindRef(72)==1 && H.ForcedReactions.FindRef(76)==8);
    for(int32 N=0;N<Data.Num();++N) {TArray<uint8> Cut(Data.GetData(),N); H.HandlePacket(0x02A5,Cut); TestEqual(TEXT("Every truncated packet preserves complete previous state"),H.ForcedReactions.Num(),2);}
    Data.Add(0); H.HandlePacket(0x02A5,Data); TestEqual(TEXT("Trailing data is rejected"),H.ForcedReactions.Num(),2);
    Data.Reset(); U32(1); U32(72); U32(8); H.HandlePacket(0x02A5,Data); TestEqual(TEXT("Out-of-range rank cannot overwrite reaction"),H.ForcedReactions.FindRef(72),uint8(1));
    Data.Reset(); U32(2); U32(72); U32(1); U32(72); U32(2); H.HandlePacket(0x02A5,Data); TestEqual(TEXT("Duplicate faction rejects the complete replacement"),H.ForcedReactions.Num(),2);
    Data.Reset(); U32(0); H.HandlePacket(0x02A5,Data); TestEqual(TEXT("Empty authoritative list clears overrides"),H.ForcedReactions.Num(),0);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudMinimapTracking,"WowUnreal.Hud.MinimapTracking",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudMinimapTracking::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& Dbc=FDbcStore::Get(); if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq); if(!Dbc.IsDeferredLoaded()) Dbc.LoadDeferred(Mpq);
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> C(NewObject<UWowConnectionManager>()); auto& H=C->PacketHandler; H.EntityManager.LocalPlayerGuid=1;
    H.EntityManager.PromoteToTyped(1,WowTypeMask::PLAYER|WowTypeMask::UNIT); auto* P=H.EntityManager.GetLocalPlayer(); P->SetField(UnitField::HEALTH,449); P->SetField(UnitField::FACTIONTEMPLATE,12); P->SetField(UnitField::BYTES_0,1|(1<<8));
    F.Context.ConnectionManager=C.Get(); F.Context.EntityManager=&H.EntityManager;
    if(!TestTrue(TEXT("Stock single-selection menu, tuple types, saved common tracking, zoom and textures"),F.Lua.ExecuteString(TEXT(R"LUA(
        IdleWowUIState={}; Mini=CreateFrame('Minimap','Minimap',UIParent); Mini:SetSize(140,140); Mini:SetPoint('CENTER')
        assert(GetNumTrackingTypes()==13 and GetTrackingInfo(0)==nil and GetTrackingInfo(14)==nil)
        local name,icon,active,category=GetTrackingInfo(1); assert(name=='Repair' and icon=='Interface\\Minimap\\Tracking\\Repair' and active==nil and category=='other' and select('#',GetTrackingInfo(1))==4)
        SetTracking(1); assert(select(3,GetTrackingInfo(1))==1 and GetTrackingTexture()==icon and GetCVar('minimapTrackedInfo')=='MINIMAP_TRACKING_REPAIR')
        TrackTip=CreateFrame('GameTooltip','TrackTip',UIParent); TrackTip:SetOwner(Mini,'ANCHOR_RIGHT'); TrackTip:SetTracking(); assert(TrackTip:IsShown() and TrackTip:NumLines()==1 and TrackTip:GetSpell()==nil)
        SetTracking(2); assert(select(3,GetTrackingInfo(1))==nil and select(3,GetTrackingInfo(2))==1)
        SetTracking(999); assert(select(3,GetTrackingInfo(2))==1); SetTracking(nil); assert(GetTrackingTexture()=='Interface\\Minimap\\Tracking\\None')
        ZoomEvents=0; Mini:RegisterEvent('MINIMAP_UPDATE_ZOOM'); Mini:SetScript('OnEvent',function() ZoomEvents=ZoomEvents+1 end)
        Mini:SetZoom(5); Mini:SetZoom(5); assert(Mini:GetZoom()==5 and Mini:GetZoomLevels()==6 and ZoomEvents==1 and GetCVar('minimapZoom')=='5')
        Mini:SetZoom(9); assert(Mini:GetZoom()==5 and ZoomEvents==1); Mini:SetZoom(-1); assert(Mini:GetZoom()==0 and ZoomEvents==2)
        Mini:SetPlayerTexture('CustomArrow'); Mini:SetPlayerTextureWidth(18); Mini:SetPlayerTextureHeight(20); Mini:SetBlipTexture('CustomBlips'); Mini:SetStaticArrowTexture('CustomNorth')
    )LUA")))) return false;
    auto& V=WowMinimapUi::View(&F.Context); TestTrue(TEXT("Custom minimap assets retain their dimensions"),V.PlayerTexture==TEXT("CustomArrow") && V.PlayerSize.Equals(FVector2D(18,20)) && V.BlipTexture==TEXT("CustomBlips") && V.StaticArrowTexture==TEXT("CustomNorth"));
    P->SetField(UnitField::BYTES_0,1|(8<<8)); TestEqual(TEXT("Mage does not offer ammunition, poisons or stables"),WowMinimapUi::Tracking(&F.Context).Num(),12);
    H.KnownSpells.Add(1494); const auto Rows=WowMinimapUi::Tracking(&F.Context); TestTrue(TEXT("Only learned tracking auras enter the spell section"),Rows.Num()==13 && Rows[0].Spell==1494);
    P->Auras[0].bActive=true; P->Auras[0].SpellId=1494; WowLuaApi::UpdateMinimapState(F.Lua.GetState());
    TestTrue(TEXT("Server aura acknowledges the learned tracking spell"),F.Lua.ExecuteString(TEXT("local name,icon,active,category=GetTrackingInfo(1); assert(name=='Track Beasts' and active==1 and category=='spell' and GetTrackingTexture()==icon and GetCVar('minimapTrackedInfo')=='')")));
    P->Auras[0].bActive=false; WowLuaApi::UpdateMinimapState(F.Lua.GetState()); TestEqual(TEXT("Removed aura stops spell tracking"),WowMinimapUi::TrackingSpell(&F.Context),0u);
    auto& N=H.EntityManager.PromoteToTyped(2,WowTypeMask::UNIT); N.Entry=10; N.Movement.Position=FVector(10,0,0); N.SetField(UnitField::HEALTH,50); N.SetField(UnitField::FACTIONTEMPLATE,12); N.SetField(UnitField::NPC_FLAGS,0x1000); H.CreatureNameCache.Add(10,TEXT("Repair vendor"));
    V.CommonKey=TEXT("MINIMAP_TRACKING_REPAIR"); auto Blips=WowMinimapUi::Blips(&F.Context);
    TestTrue(TEXT("Only matching nearby friendly vendors appear"),Blips.Num()==1 && Blips[0].Guid==2 && Blips[0].Icon==8 && Blips[0].Name==TEXT("Repair vendor"));
    N.SetField(UnitField::FACTIONTEMPLATE,29); TestTrue(TEXT("Hostile vendor flags do not create friendly service dots"),WowMinimapUi::Blips(&F.Context).IsEmpty()); N.SetField(UnitField::FACTIONTEMPLATE,12);
    V.CommonKey.Empty(); H.QuestGiverStatuses.Add(2,10); Blips=WowMinimapUi::Blips(&F.Context); TestTrue(TEXT("Actual reward status is a completed quest dot"),Blips.Num()==1 && Blips[0].Icon==10);
    H.QuestGiverStatuses.Add(2,2); TestTrue(TEXT("Trivial available quests require their tracking selection"),WowMinimapUi::Blips(&F.Context).IsEmpty()); V.CommonKey=TEXT("MINIMAP_TRACKING_TRIVIAL_QUESTS"); Blips=WowMinimapUi::Blips(&F.Context); TestTrue(TEXT("Low-level selection exposes the real quest status"),Blips.Num()==1 && Blips[0].Icon==9);
    H.QuestGiverStatuses.Empty(); V.CommonKey.Empty(); H.CreatureTypes.Add(10,1); P->SetField(PlayerField::BASE+0x36A,1); Blips=WowMinimapUi::Blips(&F.Context); TestTrue(TEXT("Tracking creature bitmasks uses DBC faction reaction"),Blips.Num()==1 && Blips[0].Icon==7);
    N.SetField(UnitField::BYTES_1,4<<16); TestTrue(TEXT("Untrackable units remain hidden"),WowMinimapUi::Blips(&F.Context).IsEmpty()); N.SetField(UnitField::BYTES_1,0); N.Movement.Position.X=200; TestTrue(TEXT("Dots outside the stock 0.8-radius bound are culled"),WowMinimapUi::Blips(&F.Context).IsEmpty()); H.EntityManager.Remove(2);
    auto& G=H.EntityManager.PromoteToTyped(3,WowTypeMask::GAMEOBJECT); G.Entry=100; G.SetField(GameObjectField::BYTES_1,19<<8); H.GameObjectTemplates.Add(100); H.GameObjectTemplates[100].Name=TEXT("Actual mailbox"); V.CommonKey=TEXT("MINIMAP_TRACKING_MAILBOX"); Blips=WowMinimapUi::Blips(&F.Context); TestTrue(TEXT("Mailboxes use the real gameobject type and query name"),Blips.Num()==1 && Blips[0].Guid==3 && Blips[0].Name==TEXT("Actual mailbox"));
    G.SetField(GameObjectField::FLAGS,0x10); TestTrue(TEXT("Not-selectable gameobjects do not leak tracking markers"),WowMinimapUi::Blips(&F.Context).IsEmpty());
    const auto East=WowMinimapUi::Offset(&F.Context,FVector::ZeroVector,FVector(0,-10,0),100,PI/2); V.Rotates=true; const auto Rotated=WowMinimapUi::Offset(&F.Context,FVector::ZeroVector,FVector(0,-10,0),100,PI/2); TestTrue(TEXT("Rotating minimap transforms markers into player-facing coordinates"),East.Equals(FVector2D(.05,0),.0001) && Rotated.Equals(FVector2D(0,.05),.0001));
    V.Rotates=false; F.Frames.MinimapZoom=0; TestTrue(TEXT("Stock outdoor and indoor radii differ"),FMath::IsNearlyEqual(WowMinimapUi::Radius(&F.Context),233.33333f,.001f) && WowMinimapUi::Radius(&F.Context,true)==150);
    TestTrue(TEXT("Local ping delivers numeric normalized coordinates through the original event"),F.Lua.ExecuteString(TEXT("Mini:RegisterEvent('MINIMAP_PING'); Mini:SetScript('OnEvent',function(self,event,unit,x,y) if event=='MINIMAP_PING' then assert(unit=='player' and type(x)=='number' and type(y)=='number'); PingSeen=true end end); Mini:PingLocation(14,7)"))); WowLuaApi::UpdateMinimapState(F.Lua.GetState());
    TestTrue(TEXT("Local ping is immediate without a group packet and responds to zoom"),F.Lua.ExecuteString(TEXT("assert(PingSeen); local x,y=Mini:GetPingPosition(); assert(math.abs(x-.1)<.001 and math.abs(y-.05)<.001); Mini:SetZoom(5); local xx,yy=Mini:GetPingPosition(); assert(xx>x and yy>y)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudMinimapPackets,"WowUnreal.Hud.MinimapPackets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudMinimapPackets::RunTest(const FString&)
{
    FWowPacketHandler H; H.EntityManager.LocalPlayerGuid=1; TArray<uint8> Data;
    const auto U32=[&](uint32 V){Data.Append(reinterpret_cast<const uint8*>(&V),4);}; const auto U64=[&](uint64 V){Data.Append(reinterpret_cast<const uint8*>(&V),8);}; const auto Float=[&](float V){Data.Append(reinterpret_cast<const uint8*>(&V),4);};
    U32(2); U64(2); Data.Add(10); U64(3); Data.Add(8);
    H.QuestGiverStatuses.Add(9,5); for(int32 N=0;N<Data.Num();++N) {TArray<uint8> Prefix; Prefix.Append(Data.GetData(),N); H.HandlePacket(0x418,Prefix); TestTrue(TEXT("Partial status lists cannot publish earlier rows"),H.QuestGiverStatuses.Num()==1 && H.QuestGiverStatuses.FindRef(9)==5);}
    H.HandlePacket(0x418,Data); TestTrue(TEXT("Complete status list replaces stale rows atomically"),H.QuestGiverStatuses.Num()==2 && H.QuestGiverStatuses.FindRef(2)==10 && !H.QuestGiverStatuses.Contains(9));
    auto Invalid=Data; Invalid[12]=11; H.HandlePacket(0x418,Invalid); TestEqual(TEXT("Invalid status list does not publish"),H.QuestGiverStatuses.FindRef(2),uint8(10));
    Data.Reset(); U64(4); Data.Add(7); H.HandlePacket(0x183,Data); TestEqual(TEXT("Single questgiver updates use the same cache"),H.QuestGiverStatuses.FindRef(4),uint8(7));
    Data.Reset(); U32(0); H.HandlePacket(0x418,Data); TestTrue(TEXT("A valid empty status list clears markers"),H.QuestGiverStatuses.IsEmpty());
    Data.Reset(); U32(100); U32(19); U32(5); for(const char* S:{"Mailbox","","","","","",""}) {Data.Append(reinterpret_cast<const uint8*>(S),FCStringAnsi::Strlen(S)); Data.Add(0);} for(int32 I=0;I<24;++I) U32(I); Float(1); for(int32 I=0;I<6;++I) U32(0);
    H.GameObjectQueries.Add(100,1); for(int32 N=0;N<Data.Num();++N) {TArray<uint8> Prefix; Prefix.Append(Data.GetData(),N); H.HandlePacket(0x05F,Prefix); TestTrue(TEXT("Partial object queries retain pending state without publishing"),H.GameObjectTemplates.IsEmpty() && H.GameObjectQueries.Contains(100));}
    H.HandlePacket(0x05F,Data); TestTrue(TEXT("Original query layout supplies names, type and all lock data"),H.GameObjectTemplates.Contains(100) && H.GameObjectTemplates[100].Name==TEXT("Mailbox") && H.GameObjectTemplates[100].Type==19 && H.GameObjectTemplates[100].Data[23]==23 && !H.GameObjectQueries.Contains(100));
    H.GameObjectQueries.Add(101,1); Data.Reset(); U32(0x80000065); H.HandlePacket(0x05F,Data); TestTrue(TEXT("Missing query results are cached only for requested entries"),H.MissingGameObjects.Contains(101) && !H.GameObjectQueries.Contains(101));
    Data.Reset(); U64(1); Float(-8895); Float(-88); for(int32 N=0;N<Data.Num();++N) {TArray<uint8> Prefix; Prefix.Append(Data.GetData(),N); H.HandlePacket(0x1D5,Prefix); TestEqual(TEXT("Partial ping is atomic"),H.MinimapPingRevision,0u);} H.HandlePacket(0x1D5,Data); TestTrue(TEXT("Ping carries absolute world coordinates"),H.MinimapPingRevision==1 && H.MinimapPing.Position.Equals(FVector(-8895,-88,0)));
    Data[0]=9; H.HandlePacket(0x1D5,Data); TestEqual(TEXT("Non-group ping cannot spoof a marker"),H.MinimapPingRevision,1u);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudAreaZones,"WowUnreal.Hud.AreaZones",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudAreaZones::RunTest(const FString&)
{
    TestEqual(TEXT("World-state counters do not invent zone changes"),FWowEventSystem::OpcodeToEvent(0x2C3),FString(TEXT("UPDATE_WORLD_STATES")));
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& Dbc=FDbcStore::Get(); if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq); if(!Dbc.IsDeferredLoaded()) Dbc.LoadDeferred(Mpq);
    for(const auto Tile:{FIntPoint(10,9),FIntPoint(32,32),FIntPoint(31,48)}) for(int32 X:{0,5,15}) for(int32 Y:{0,11,15})
    {
        const auto Position=FWowCoordinate::AdtToUE(Tile.X*FWowCoordinate::TILE_SIZE+(X+.5f)*FWowCoordinate::CHUNK_SIZE,0,Tile.Y*FWowCoordinate::TILE_SIZE+(Y+.5f)*FWowCoordinate::CHUNK_SIZE);
        TestEqual(TEXT("Centered coordinates preserve the actual east/west chunk"),FWowCoordinate::WorldToChunk(Position).X,X);
        TestEqual(TEXT("Centered coordinates preserve the actual north/south chunk"),FWowCoordinate::WorldToChunk(Position).Y,Y);
    }
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> C(NewObject<UWowConnectionManager>()); auto& H=C->PacketHandler;
    F.Context.ConnectionManager=C.Get(); F.Context.EntityManager=&H.EntityManager; H.EntityManager.LocalPlayerGuid=1;
    auto& Player=H.EntityManager.PromoteToTyped(1,WowTypeMask::PLAYER|WowTypeMask::UNIT); Player.SetField(UnitField::BYTES_0,1|(1<<8));
    H.ServerAreaMapId=H.CurrentMapId=0; H.ServerZoneId=12; H.ServerAreaId=9;
    TestTrue(TEXT("Server area initializes the real Northshire subzone before terrain"),F.Lua.ExecuteString(TEXT("assert(GetZoneText()=='Elwynn Forest' and GetRealZoneText()=='Elwynn Forest' and GetSubZoneText()=='Northshire Valley' and GetMinimapZoneText()=='Northshire Valley' and not IsFlyableArea())")));
    uint32 Area=1519; F.Context.ResolveArea=[&]{return Area;};
    TestTrue(TEXT("Loaded terrain replaces the fallback with the actual city"),F.Lua.ExecuteString(TEXT("assert(GetZoneText()=='Stormwind City' and GetSubZoneText()=='' and GetMinimapZoneText()=='Stormwind City')")));
    Area=14;
    TestTrue(TEXT("An area from a different map cannot override current server state"),F.Lua.ExecuteString(TEXT("assert(GetMinimapZoneText()=='Northshire Valley')")));
    H.CurrentMapId=1;
    TestTrue(TEXT("Actual map changes select the correct zone rather than Elwynn"),F.Lua.ExecuteString(TEXT("assert(GetZoneText()=='Durotar' and GetSubZoneText()=='')")));
    H.CurrentMapId=530;
    TestTrue(TEXT("Unknown positions do not invent a zone"),F.Lua.ExecuteString(TEXT("assert(GetZoneText()=='' and GetMinimapZoneText()=='' and not IsFlyableArea())")));
    Area=3483;
    TestTrue(TEXT("Real Outland areas enable flight conditions"),F.Lua.ExecuteString(TEXT("assert(GetZoneText()=='Hellfire Peninsula' and IsFlyableArea())")));
    H.CurrentMapId=571; Area=4395;
    TestTrue(TEXT("Dalaran respects its actual no-fly area flag"),F.Lua.ExecuteString(TEXT("assert(GetZoneText()=='Dalaran' and not IsFlyableArea())")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudInteriorAreas,"WowUnreal.Hud.InteriorAreas",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudInteriorAreas::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& Dbc=FDbcStore::Get(); if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq); if(!Dbc.IsDeferredLoaded()) Dbc.LoadDeferred(Mpq);
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> C(NewObject<UWowConnectionManager>()); auto& H=C->PacketHandler;
    H.EntityManager.LocalPlayerGuid=1; H.EntityManager.PromoteToTyped(1,WowTypeMask::PLAYER|WowTypeMask::UNIT); F.Context.ConnectionManager=C.Get(); F.Context.EntityManager=&H.EntityManager; F.Context.ResolveArea=[]{return 9u;};
    auto& W=WowMinimapUi::View(&F.Context).Location; W.ZoneName=TEXT("Northshire Abbey"); W.SubzoneName=TEXT("Main Hall"); W.Indoors=W.Minimap=true;
    TestTrue(TEXT("Original WMO names override display text while the real channel zone stays Elwynn"),F.Lua.ExecuteString(TEXT("assert(GetRealZoneText()=='Elwynn Forest' and GetZoneText()=='Northshire Abbey' and GetSubZoneText()=='Main Hall' and GetMinimapZoneText()=='Main Hall' and IsIndoors() and not IsOutdoors())")));
    F.Context.ResolveArea=[]{return 24u;}; TestTrue(TEXT("A root name already equal to the terrain subzone does not replace its parent zone"),F.Lua.ExecuteString(TEXT("assert(GetZoneText()=='Elwynn Forest' and GetSubZoneText()=='Main Hall')")));
    W=FWowWmoLocation(); F.Context.ResolveArea=[]{return 9u;}; TestTrue(TEXT("Leaving the building restores terrain text and outdoor state"),F.Lua.ExecuteString(TEXT("assert(GetZoneText()=='Elwynn Forest' and GetSubZoneText()=='Northshire Valley' and GetMinimapZoneText()=='Northshire Valley' and IsOutdoors() and not IsIndoors())")));
    H.CurrentMapId=571; F.Context.ResolveArea=[]{return 4395u;};
    const auto* Floor=Dbc.MapUi().Floors().FindByPredicate([](const auto& A){return A.MapId==571 && A.Index==2;}); if(!TestNotNull(TEXT("Actual Dalaran underground floor definition exists"),Floor)) return false;
    W.FloorId=Floor->Id; H.EntityManager.GetLocalPlayer()->Movement.Position=FVector((Floor->MinY+Floor->MaxY)/2,(Floor->MinX+Floor->MaxX)/2,600);
    TestTrue(TEXT("Current-zone map automatically follows the resolved WMO dungeon floor and player projection"),F.Lua.ExecuteString(TEXT("SetMapToCurrentZone(); assert(GetMapInfo()=='Dalaran' and GetCurrentMapDungeonLevel()==2); local x,y=GetPlayerMapPosition('player'); assert(math.abs(x-.5)<.001 and math.abs(y-.5)<.001)")));
    W.FloorId=0; return TestTrue(TEXT("Returning outside restores Dalaran's terrain floor"),F.Lua.ExecuteString(TEXT("SetMapToCurrentZone(); assert(GetCurrentMapDungeonLevel()==1)")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudFrameReparent,"WowUnreal.Hud.FrameReparentVisibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudFrameReparent::RunTest(const FString&)
{
    FHudFixture F;
    return TestTrue(TEXT("Reparenting updates child traversal, event visibility and descendant geometry without stale links"),F.Lua.ExecuteString(TEXT(R"LUA(
        TreeA=CreateFrame('Frame','TreeA',UIParent); TreeA:SetSize(200,200); TreeA:SetPoint('TOPLEFT',UIParent,'TOPLEFT',0,0)
        TreeB=CreateFrame('Frame','TreeB',UIParent); TreeB:SetSize(100,100); TreeB:SetPoint('TOPLEFT',UIParent,'TOPLEFT',300,0)
        TreeChild=CreateFrame('Frame','TreeChild',TreeA); TreeChild:SetSize(40,40); TreeChild:SetPoint('TOPLEFT',TreeB,'TOPLEFT',10,-10)
        TreeLeaf=CreateFrame('Frame','TreeLeaf',TreeChild); TreeLeaf:SetAllPoints(TreeChild)
        assert(TreeA:GetNumChildren()==1 and TreeB:GetNumChildren()==0 and TreeChild:GetNumChildren()==1)
        TreeShows=0; TreeHides=0; TreeLeaf:SetScript('OnShow',function() TreeShows=TreeShows+1 end); TreeLeaf:SetScript('OnHide',function() TreeHides=TreeHides+1 end)
        TreeChild:SetParent(TreeB); assert(TreeA:GetNumChildren()==0 and TreeB:GetNumChildren()==1 and TreeB:GetChildren()==TreeChild)
        TreeA:Hide(); assert(TreeLeaf:IsVisible() and TreeHides==0)
        TreeB:Hide(); assert(not TreeLeaf:IsVisible() and TreeHides==1); TreeB:Show(); assert(TreeLeaf:IsVisible() and TreeShows==1)
        TreeB:SetScale(.5); assert(math.abs(TreeChild:GetWidth()-40)<.001 and math.abs(TreeLeaf:GetWidth()-40)<.001)
    )LUA")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudWorldMap,"WowUnreal.Hud.WorldMapApi",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudWorldMap::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& Dbc=FDbcStore::Get(); if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq); if(!Dbc.IsDeferredLoaded()) Dbc.LoadDeferred(Mpq);
    FWowAssetCache Cache; FHudFixture F; F.Frames.SetMpqManager(&Mpq); F.Frames.SetAssetCache(&Cache); TStrongObjectPtr<UWowConnectionManager> C(NewObject<UWowConnectionManager>()); auto& H=C->PacketHandler; H.EntityManager.LocalPlayerGuid=1;
    H.EntityManager.PromoteToTyped(1,WowTypeMask::PLAYER|WowTypeMask::UNIT); auto* P=H.EntityManager.GetLocalPlayer(); P->Movement.Position={-8895,-88,86}; P->SetField(UnitField::HEALTH,449);
    F.Context.ConnectionManager=C.Get(); F.Context.EntityManager=&H.EntityManager; F.Context.ResolveArea=[]{return 9u;};
    TestTrue(TEXT("Original map selection uses sorted localized zones and raw DBC IDs"),F.Lua.ExecuteString(TEXT(R"LUA(
        SetMapToCurrentZone(); assert(GetCurrentMapAreaID()==31 and GetCurrentMapContinent()==2 and GetCurrentMapZone()>0)
        assert(GetMapInfo()=='Elwynn' and select('#',GetMapInfo())==3)
        local x,y=GetPlayerMapPosition('player'); assert(x>0 and x<1 and y>0 and y<1); assert(GetPlayerMapPosition('target')==0)
        local zone=GetCurrentMapZone(); assert(select(zone,GetMapZones(2))=='Elwynn Forest')
        SetMapZoom(2); assert(GetMapInfo()=='Azeroth' and GetCurrentMapZone()==0 and GetCurrentMapAreaID()==15)
        local x,y=GetPlayerMapPosition('player'); assert(x>0 and x<1 and y>0 and y<1)
        assert(UpdateMapHighlight(x,y)=='Elwynn Forest','physical continent hit mask'); ProcessMapClick(x,y); assert(GetMapInfo()=='Elwynn'); SetMapZoom(2)
        ZoomOut(); assert(GetCurrentMapContinent()==0 and GetMapInfo()==nil); x,y=GetPlayerMapPosition('player'); assert(x>0 and x<1 and y>0 and y<1)
        ZoomOut(); assert(GetCurrentMapContinent()==-1 and GetPlayerMapPosition('player')==0)
        SetMapByID(504); assert(GetCurrentMapAreaID()==505 and GetMapInfo()=='Dalaran' and GetCurrentMapDungeonLevel()==1 and GetNumDungeonMapLevels()==2)
        SetDungeonMapLevel(2); assert(GetCurrentMapDungeonLevel()==2); SetMapToCurrentZone(); assert(GetCurrentMapAreaID()==31)
        local f=CreateFrame('QuestPOIFrame','MapBlobFixture',UIParent); assert(f:GetObjectType()=='QuestPOIFrame')
        f:SetFillTexture('Interface\\WorldMap\\UI-QuestBlob-Inside'); f:SetBorderTexture('Interface\\WorldMap\\UI-QuestBlob-Outside')
        f:SetAlpha(255); assert(f:GetAlpha()==1); f:SetFillAlpha(500); f:SetBorderAlpha(-4); f:SetMergeThreshold(1); f:SetNumSplinePoints(1)
        CreateWorldMapArrowFrame(UIParent); assert(PlayerArrowFrame and PlayerArrowEffectFrame)
        assert(GetCVarDefault('questPOI')=='1' and GetCVarBool('questPOI')); SetupFullscreenScale(MapBlobFixture); assert(MapBlobFixture:GetScale()>0 and MapBlobFixture:GetScale()<=1)
        assert(GetNumBattlefieldVehicles()==0 and GetBattlefieldVehicleInfo(1)==nil)
    )LUA")));
    FWowQuestLogEntry Q; Q.QuestId=783; H.QuestLog.Add(Q); FWowQuestTemplate T; T.Id=783; T.CreatureId[0]=1; T.CreatureCount[0]=5; T.CompletedText=TEXT("Return to Marshal McBride"); H.QuestTemplates.Add(783,T);
    FWowPacketHandler::FQuestPoi Poi; Poi.MapId=0; Poi.AreaId=30; Poi.Objective=0; Poi.Points={{-8900,-100},{-8880,-100},{-8880,-80},{-8900,-80}}; H.QuestPois.Add(783,{Poi});
    auto ReturnPoi=Poi; ReturnPoi.Objective=-1; ReturnPoi.AreaId=301; H.QuestPois[783].Insert(ReturnPoi,0);
    TestTrue(TEXT("Real quest polygons and objective indexes feed original map APIs"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(QuestMapUpdateAllQuests()==1); local q,i=QuestPOIGetQuestIDByVisibleIndex(1); assert(q==783 and i==1)
        SelectQuestLogEntry(2); assert(GetQuestLogCompletionText(1)=='Return to Marshal McBride' and GetQuestLogCompletionText()=='','explicit quest index must not read the selection')
        QuestPOIUpdateIcons(); local complete,x,y,objective=QuestPOIGetIconInfo(783); assert(not complete and x>0 and y>0 and objective==0)
        assert(GetQuestWorldMapAreaID(783)==30); MapBlobFixture:DrawQuestBlob(783,true)
        local log,n=MapBlobFixture:UpdateMouseOverTooltip(x,y); assert(log==1 and n==1 and MapBlobFixture:GetTooltipIndex(1)==0)
        MapBlobFixture:DrawQuestBlob(783,false); assert(MapBlobFixture:UpdateMouseOverTooltip(x,y)==nil)
        SetMapZoom(2); assert(QuestMapUpdateAllQuests()==0); SetMapToCurrentZone()
    )LUA")));
    const auto* Blob=WowMapUi::View(&F.Context).Blobs.Find(F.Frames.FindFrame(TEXT("MapBlobFixture")));
    H.QuestTemplates[783].Objectives=TEXT("Speak with Marshal McBride."); H.QuestTemplates[783].CompletedText.Empty();
    TestTrue(TEXT("An empty completion string falls back to actual quest objectives"),F.Lua.ExecuteString(TEXT("assert(GetQuestLogCompletionText(1)=='Speak with Marshal McBride.')")));
    H.QuestTemplates[783].CompletedText=TEXT("Other completion"); H.QuestTemplates[783].Flags|=0x40000;
    TestTrue(TEXT("Auto-complete quest flags use the objective summary"),F.Lua.ExecuteString(TEXT("assert(GetQuestLogCompletionText(1)=='Speak with Marshal McBride.')")));
    TestTrue(TEXT("Blob properties use the original bounded ranges"),Blob && Blob->FillAlpha==255 && Blob->BorderAlpha==0 && Blob->MergeThreshold==.5f && Blob->SplinePoints==8);
    TestTrue(TEXT("Original map art can be queued while retaining Lua texture state"),F.Lua.ExecuteString(TEXT("MapQueueTex=UIParent:CreateTexture('MapQueueTex'); MapQueueTex:SetTexture('Interface\\\\WorldMap\\\\Elwynn\\\\Elwynn1'); MapQueueTex:SetTexCoord(.2,.8,.1,.9); MapQueueTex:SetAlpha(.4); assert(MapQueueTex:GetTexture()=='Interface\\\\WorldMap\\\\Elwynn\\\\Elwynn1')")));
    TestEqual(TEXT("The map does not decode a new atlas in its Lua callback"),F.Context.Map->PendingArt.Num(),1);
    WowLuaApi::UpdateMapArt(F.Lua.GetState()); auto* Art=F.Frames.GetNamedTexture(TEXT("MapQueueTex"));
    TestTrue(TEXT("The deferred upload preserves opacity and cropped UVs"),Art && Art->GetBrush().GetResourceObject() && FMath::IsNearlyEqual(Art->GetRenderOpacity(),.4f) && FBox2f(Art->GetBrush().GetUVRegion()).Min.Equals(FVector2f(.2f,.1f)));
    F.Lua.ExecuteString(TEXT("MapQueueTex:SetTexture('Interface\\\\WorldMap\\\\Elwynn\\\\Elwynn2'); MapQueueTex:SetTexture(nil)")); WowLuaApi::UpdateMapArt(F.Lua.GetState());
    TestTrue(TEXT("Clearing pending art prevents a stale asynchronous result"),Art && Art->GetBrush().DrawAs==ESlateBrushDrawType::NoDrawType && F.Context.Map->PendingArt.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudChannels,"WowUnreal.Hud.ChannelApi",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudChannels::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& Dbc=FDbcStore::Get(); if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq); if(!Dbc.IsDeferredLoaded()) Dbc.LoadDeferred(Mpq);
    TestTrue(TEXT("Client channel definitions include localized shortcuts"),Dbc.ItemUi().ChatChannels().Num()>2);
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> C(NewObject<UWowConnectionManager>()); auto& H=C->PacketHandler; H.EntityManager.LocalPlayerGuid=1; H.PlayerNameCache.Add(1,TEXT("Main"));
    FWowChannel Channel; Channel.Name=TEXT("QaChannel"); Channel.Number=4; Channel.Flags=1; Channel.Count=1; Channel.RosterKnown=true; Channel.Members.Add({1,3}); H.Channels.Add(Channel);
    F.Context.ConnectionManager=C.Get(); F.Context.EntityManager=&H.EntityManager;
    F.Frames.UpdateMouseHover(100*F.Frames.GetScreenScale(),200*F.Frames.GetScreenScale());
    return TestTrue(TEXT("Original channel lists, member roles, selection, categories and window filters use real cache state"),F.Lua.ExecuteString(TEXT(R"LUA(
        local x,y=GetCursorPosition(); local scale=UIParent:GetEffectiveScale(); assert(math.abs(x/scale-100)<.001 and math.abs(y/scale-(UIParent:GetHeight()-200))<.001,'viewport-local bottom-left cursor')
        CursorMenu=CreateFrame('Frame','CursorMenu',UIParent); CursorMenu:SetSize(100,80); CursorMenu:SetPoint('TOPLEFT',nil,'BOTTOMLEFT',100,200)
        local _,_,relative=CursorMenu:GetPoint(); assert(relative=='BOTTOMLEFT' and math.abs(CursorMenu:GetTop()-200)<.001,'nil relative frame retains explicit anchor')
        local n,name,unknown=GetChannelName('qachannel'); assert(n==4 and name=='QaChannel' and unknown==0)
        assert(GetChannelName(4)==4 and GetChannelName(9)==0 and select('#',GetChannelList())==2)
        local number,channel=GetChannelList(); assert(number==4 and channel=='QaChannel')
        assert(GetChatWindowChannels(1)=='QaChannel' and GetChatWindowChannels(2)==nil)
        AddChatWindowChannel(2,'QaChannel'); assert(GetChatWindowChannels(2)=='QaChannel'); RemoveChatWindowChannel(2,'QaChannel'); assert(GetChatWindowChannels(2)==nil)
        local custom,selected
        for i=1,GetNumDisplayChannels() do local name,header,collapsed,num,count,active,category=GetChannelDisplayInfo(i); if header and category=='CHANNEL_CATEGORY_CUSTOM' then custom=i end; if not header and num==4 then assert(name=='QaChannel' and active==1 and count==1 and category=='CHANNEL_CATEGORY_CUSTOM'); selected=i end end
        assert(custom and selected); SetSelectedDisplayChannel(selected); assert(GetSelectedDisplayChannel()==selected and GetNumChannelMembers(selected)==1)
        local name,owner,moderator,muted=GetChannelRosterInfo(selected,1); assert(name=='Main' and owner==1 and moderator==1 and muted==nil and select('#',GetChannelRosterInfo(selected,1))==6)
        assert(IsDisplayChannelOwner(selected)==1,'owner indexed'); assert(IsDisplayChannelModerator(selected)==1,'moderator indexed'); assert(IsDisplayChannelOwner()==1,'owner selected'); assert(IsDisplayChannelModerator()==1,'moderator selected'); assert(GetActiveVoiceChannel()==nil,'voice disabled')
        CollapseChannelHeader(custom); assert(GetSelectedDisplayChannel()==nil,'collapsed selection '..tostring(GetSelectedDisplayChannel())); ExpandChannelHeader(custom); assert(GetSelectedDisplayChannel()==selected,'expanded selection '..tostring(GetSelectedDisplayChannel())..' expected '..selected)
    )LUA")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudLinkTooltips,"WowUnreal.Hud.LinkTooltips",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudLinkTooltips::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& Dbc=FDbcStore::Get(); if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq); if(!Dbc.IsDeferredLoaded()) Dbc.LoadDeferred(Mpq);
    FHudFixture F;
    return TestTrue(TEXT("Spell links use original tooltip metadata and invalid links clear the previous context"),F.Lua.ExecuteString(TEXT(R"LUA(
        LinkTip=CreateFrame('GameTooltip','LinkTip',UIParent); LinkTip:SetOwner(UIParent,'ANCHOR_NONE'); LinkTip:SetHyperlink('|cff71d5ff|Hspell:6603|h[Attack]|h|r')
        local name,rank,id=LinkTip:GetSpell(); assert(name==GetSpellInfo(6603) and id==6603 and type(rank)=='string' and select('#',LinkTip:GetSpell())==3)
        LinkTip:SetHyperlink('unsupported:6603'); assert(not LinkTip:IsShown() and LinkTip:GetSpell()==nil)
        LinkTipClose=CreateFrame('Button','LinkTipClose',LinkTip); LinkTipClose:SetSize(32,32); LinkTipClose:SetPoint('TOPRIGHT',LinkTip,'TOPRIGHT',0,0)
        LinkTip:SetText('Long reference title'); local width=LinkTip:GetWidth(); LinkTipClose:Hide(); LinkTip:SetText('Long reference title'); assert(width>=LinkTip:GetWidth()+31)
        LinkColor=UIParent:CreateTexture('LinkColor'); LinkColor:SetVertexColor(.2,.3,.4,.6); LinkColor:SetVertexColor(nil,nil,nil)
        local r,g,b,a=LinkColor:GetVertexColor(); assert(r==0 and g==0 and b==0 and math.abs(a-.6)<.001)
        LinkColor:SetVertexColor(2,-1,.5); r,g,b,a=LinkColor:GetVertexColor(); assert(r==1 and g==0 and b==.5 and math.abs(a-.6)<.001)
    )LUA")));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudHyperlinks,"WowUnreal.Hud.ChatHyperlinks",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudHyperlinks::RunTest(const FString&)
{
    const auto Parsed=UWowTextBlock::ParseMarkup(TEXT("|cff00ff00|Hitem:2589:0|h[Cloth <tag>]|h|r / |Hspell:6603|h[Attack]|h"));
    if(!TestEqual(TEXT("Hyperlinks retain independent data records"),Parsed.Links.Num(),2)) return false;
    TestTrue(TEXT("Link payloads and original colored text survive rendering"),Parsed.Links[0].Data==TEXT("item:2589:0") && Parsed.Links[0].Text.StartsWith(TEXT("|cff00ff00|Hitem:")) && Parsed.Plain==TEXT("[Cloth <tag>] / [Attack]") && Parsed.Rich.Contains(TEXT("&lt;tag&gt;")));
    FHudFixture F;
    if(!F.Lua.ExecuteString(TEXT(R"LUA(
        LinkFrame=CreateFrame('ScrollingMessageFrame','LinkFrame',UIParent); LinkFrame:SetSize(220,120); LinkFrame:SetPoint('TOPLEFT',UIParent,'TOPLEFT',40,-40); LinkFrame:EnableMouse(false); assert(LinkFrame:GetHyperlinksEnabled()); LinkFrame:SetHyperlinksEnabled(true)
        assert(LinkFrame:GetHyperlinksEnabled()); LinkClicks=0; LinkEnter=0; LinkLeave=0
        LinkFrame:SetScript('OnHyperlinkClick',function(self,data,text,button) assert(data=='item:2589:0' and text:find('%[Cloth%]') and button=='RightButton'); LinkClicks=LinkClicks+1 end)
        LinkFrame:SetScript('OnHyperlinkEnter',function(self,data,text) assert(data=='item:2589:0'); LinkEnter=LinkEnter+1 end)
        LinkFrame:SetScript('OnHyperlinkLeave',function() LinkLeave=LinkLeave+1 end)
        LinkFrame:AddMessage('hello |cff00ff00|Hitem:2589:0|h[Cloth]|h|r')
    )LUA"))) return false;
    const int64 Handle=F.Frames.FindFrame(TEXT("LinkFrame")); F.Frames.DrawMessages(Handle); F.Canvas->TakeWidget()->SlatePrepass();
    FVector2D Point;
    if(!TestTrue(TEXT("Real text-layout blocks supply a visible hyperlink point"),F.Frames.MessageHyperlinkPoint(Handle,TEXT("item:2589:0"),Point))) return false;
    F.Frames.UpdateMouseHover(Point.X,Point.Y); F.Frames.DispatchMouseDown(Handle,TEXT("RightButton")); F.Frames.DispatchMouseUp(Handle,TEXT("RightButton")); F.Frames.UpdateMouseHover(0,0);
    TestTrue(TEXT("Hover, leave and right-click deliver the original data/text/button tuple"),F.Lua.ExecuteString(TEXT("assert(LinkClicks==1 and LinkEnter==1 and LinkLeave==1); LinkFrame:SetHyperlinksEnabled(false); assert(not LinkFrame:GetHyperlinksEnabled())")));
    TestFalse(TEXT("Disabled hyperlinks cannot be selected"),F.Frames.MessageHyperlinkPoint(Handle,TEXT("item:2589:0"),Point));
    TestTrue(TEXT("Wrapped links retain their data and individual text blocks"),F.Lua.ExecuteString(TEXT("LinkFrame:SetHyperlinksEnabled(true); LinkFrame:Clear(); LinkFrame:SetSize(110,180); LinkFrame:AddMessage('|Hitem:2589:0|h[Cloth] multiple wrapped words|h')")));
    F.Frames.DrawMessages(Handle); F.Canvas->TakeWidget()->SlatePrepass(); int32 Blocks=0;
    for(const auto& Widget:F.Frames.MessageFrames[Handle].Widgets) if(const auto* Label=Cast<UWowTextBlock>(Widget.Get()); Label && Label->IsVisible()) Blocks+=Label->HyperlinkRects().Num();
    TestTrue(TEXT("A wrapped link has multiple physical hit areas"),Blocks>1 && F.Frames.MessageHyperlinkPoint(Handle,TEXT("item:2589:0"),Point));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudInlineLabels,"WowUnreal.Hud.StockInlineLabels",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudInlineLabels::RunTest(const FString&)
{
    const auto Parsed=UWowTextBlock::ParseMarkup(TEXT("Before |cffff0000red|r |TInterface/Icons/Example:16:24:2:-1:64:64:4:36:8:40|t |Hitem:123|h[item]|h || <text>"));
    TestEqual(TEXT("Markup preserves visible text and escaped pipes"),Parsed.Plain,FString(TEXT("Before red  [item] | <text>")));
    TestEqual(TEXT("Mail expiration grammar chooses visible plural forms"),UWowTextBlock::ParseMarkup(TEXT("|cff00ff001 |4Day:Days;|r / 20 |4Day:Days; / 22 |4день:дня:дней; / 11 |4день:дня:дней;")).Plain,FString(TEXT("1 Day / 20 Days / 22 дня / 11 дней")));
    TestEqual(TEXT("Malformed and escaped grammar remains literal"),UWowTextBlock::ParseMarkup(TEXT("||4Day:Days; |4bad")).Plain,FString(TEXT("|4Day:Days; |4bad")));
    if(!TestEqual(TEXT("Inline texture parsed"),Parsed.Icons.Num(),1)) return false;
    const auto& Icon=Parsed.Icons[0];
    TestTrue(TEXT("Icon dimensions, offsets and crop are retained"),Icon.Height==16 && Icon.Width==24 && Icon.OffsetX==2 && Icon.OffsetY==-1 && Icon.Crop.X==.0625f && Icon.Crop.Y==.125f);
    TestTrue(TEXT("Literal markup cannot become a decorator"),Parsed.Rich.Contains(TEXT("&lt;text&gt;")) && Parsed.Rich.Contains(TEXT("wowimage")));
    TestTrue(TEXT("Portrait-to-texture accepts a stock global name and missing item art"),FHudFixture().Lua.ExecuteString(TEXT("PortraitFixture=UIParent:CreateTexture('PortraitFixture'); SetPortraitToTexture('PortraitFixture',nil); assert(PortraitFixture:GetTexture()==nil); SetPortraitToTexture(PortraitFixture,nil)")));
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudNestedVirtualFrames,"WowUnreal.Hud.NestedVirtualFrames",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudNestedVirtualFrames::RunTest(const FString&)
{
    FHudFixture F;
    FWowFrameDef Parent; Parent.Name=TEXT("NestedVirtualParent"); Parent.Parent=TEXT("UIParent"); Parent.Width=300; Parent.Height=100;
    FWowFrameDef Child; Child.Name=TEXT("NestedVirtualPurchase"); Child.Type=EWowFrameType::Button; Child.bVirtual=true; Child.Width=120; Child.Height=20;
    Child.Scripts.Add({TEXT("OnLoad"),TEXT("self.ready=true"),TEXT("")}); Parent.Children.Add(Child);
    F.Frames.CreateFrame(Parent); const auto Handle=F.Frames.FindFrame(Child.Name);
    TestTrue(TEXT("A nested virtual attribute does not suppress the concrete widget"),Handle>=0 && F.Frames.GetWidgetForHandle(Handle)!=nullptr);
    TestTrue(TEXT("Nested child scripts run on its real Lua object"),F.Lua.ExecuteString(TEXT("assert(NestedVirtualPurchase.ready and NestedVirtualPurchase:GetParent()==NestedVirtualParent)")));
    FWowFrameDef Template; Template.Name=TEXT("TopLevelVirtualOnly"); Template.bVirtual=true;
    const auto TemplateHandle=F.Frames.CreateFrame(Template);
    TestTrue(TEXT("Top-level virtual frames remain templates without widgets"),F.Frames.GetWidgetForHandle(TemplateHandle)==nullptr);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudBankApi,"WowUnreal.Hud.BankApi",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudBankApi::RunTest(const FString&)
{
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    auto& H=Connection->PacketHandler; H.EntityManager.LocalPlayerGuid=1; H.EntityManager.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    F.Context.ConnectionManager=Connection.Get(); F.Context.EntityManager=&H.EntityManager; H.CurrentBankerGuid=1;
    TestTrue(TEXT("Stock bank IDs distinguish generic, worn-bag and bank-bag slots"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(BankButtonIDToInvSlotID(1)==40 and BankButtonIDToInvSlotID(28)==67)
        assert(BankButtonIDToInvSlotID(5,1)==68 and BankButtonIDToInvSlotID(11,1)==74)
        assert(GetInventorySlotInfo('Bag0Slot')==20 and GetInventorySlotInfo('Bag3Slot')==23)
        assert(GetInventorySlotInfo('Bag1')==68 and GetInventorySlotInfo('Bag7')==74)
        assert(GetNumBankSlots()==0 and not select(2,GetNumBankSlots()))
        assert(not PutItemInBag(68)); CloseBankFrame(); CloseBankFrame()
    )LUA")));
    TestEqual(TEXT("Repeated close is idempotent"),H.CurrentBankerGuid,uint64(0));
    auto* Player=H.EntityManager.GetLocalPlayer(); Player->SetField(PlayerField::BYTES_2,1u<<16);
    TestTrue(TEXT("Original APIs expose purchased slots from server fields"),F.Lua.ExecuteString(TEXT("assert(GetNumBankSlots()==1 and not select(2,GetNumBankSlots()))")));
    Player->SetField(PlayerField::BYTES_2,7u<<16);
    TestTrue(TEXT("The server's seventh purchased slot hides further purchases"),F.Lua.ExecuteString(TEXT("assert(GetNumBankSlots()==7 and select(2,GetNumBankSlots()))")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudEditBoxFonts,"WowUnreal.Hud.EditBoxFonts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudEditBoxFonts::RunTest(const FString&)
{
    FHudFixture F;
    if(!F.Lua.ExecuteString(TEXT("EditorFont=CreateFont('EditorFont'); EditorFont:SetFont('Fonts\\\\FRIZQT__.TTF',14); EditorFont:SetTextColor(.18,.12,.06)"))) return false;
    const FString Xml=TEXT("<Ui><EditBox name='EditorTemplate' virtual='true'><FontString inherits='EditorFont'/></EditBox><EditBox name='SingleEditor' inherits='EditorTemplate'><Size x='220' y='20'/></EditBox><EditBox name='MultiEditor' multiLine='true'><Size x='220' y='180'/><FontString inherits='EditorFont'/></EditBox></Ui>");
    const FTCHARToUTF8 Utf8(*Xml); TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("EditorFontFixture")); if(Parsed.Num()!=3) return false;
    for(const auto& Row:Parsed) F.Frames.CreateFrame(Row.FrameDef);
    const auto* Single=F.Frames.GetEditBoxWidget(F.Frames.FindFrame(TEXT("SingleEditor"))); const auto* Multi=F.Frames.GetMultiLineEditBoxWidget(F.Frames.FindFrame(TEXT("MultiEditor")));
    TestTrue(TEXT("Root FontString style survives template inheritance and styles both editor types"),Single && Multi && Single->GetWidgetStyle().TextStyle.Font.Size==11 && Multi->WidgetStyle.Font.Size==11 && F.Lua.ExecuteString(TEXT("assert(SingleEditor:GetFontObject()==EditorFont and MultiEditor:GetFontObject()==EditorFont)")));
    TestTrue(TEXT("Runtime SetFont changes native editor rendering"),F.Lua.ExecuteString(TEXT("SingleEditor:SetFont('Fonts\\\\FRIZQT__.TTF',20); MultiEditor:SetFont('Fonts\\\\FRIZQT__.TTF',16)")));
    TestTrue(TEXT("Single and multiline use WoW pixel-to-point sizing"),Single && Multi && Single->GetWidgetStyle().TextStyle.Font.Size==15 && Multi->WidgetStyle.Font.Size==12);
    TestTrue(TEXT("Parchment editor colors inherit from the original font object"),Multi && FMath::IsNearlyEqual(Multi->WidgetStyle.ColorAndOpacity.GetSpecifiedColor().R,.18f));
    TestTrue(TEXT("Stock money editors retain numeric values and treat empty text as zero"),F.Lua.ExecuteString(TEXT("SingleEditor:SetNumber(123.5); assert(SingleEditor:GetText()=='123.5' and SingleEditor:GetNumber()==123.5); SingleEditor:SetText(''); assert(SingleEditor:GetNumber()==0); MultiEditor:SetNumber(99); assert(MultiEditor:GetNumber()==99)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudTradeApi,"WowUnreal.Hud.TradeApi",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudTradeApi::RunTest(const FString&)
{
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> C(NewObject<UWowConnectionManager>());
    auto& H=C->PacketHandler; auto& E=H.EntityManager; E.LocalPlayerGuid=1;
    auto& P=E.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER); P.SetField(PlayerField::COINAGE,1000); P.SetField(UnitField::BYTES_0,0x101); P.SetField(UnitField::LEVEL,20);
    E.PromoteToTyped(2,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER); H.PlayerNameCache.Add(2,TEXT("Trader"));
    F.Context.ConnectionManager=C.Get(); F.Context.EntityManager=&E;
    auto& T=H.CurrentTrade; T.TraderGuid=2; T.bTradeOpen=true; T.PlayerMoney=75; T.TargetMoney=125; T.PlayerItems.SetNum(7); T.TargetItems.SetNum(7);
    FWowItemTemplate Item; Item.Id=2589; Item.Name=TEXT("Trade cloth"); Item.Quality=1; H.ItemTemplates.Add(Item.Id,Item);
    T.PlayerItems[0].ItemId=T.TargetItems[6].ItemId=2589; T.PlayerItems[0].Count=5; T.TargetItems[6].Count=2; T.TargetItems[6].Slot=6;
    auto& Stack=E.PromoteToTyped(101,WowTypeMask::OBJECT|WowTypeMask::ITEM); Stack.Entry=2589; Stack.SetField(ItemField::STACK_COUNT,5); P.SetField(PlayerField::PACK_SLOT_START,101); T.LocalGuids[0]=101;
    TestTrue(TEXT("Stock trade tuples, counterpart unit and item locks are available"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(UnitName('NPC')=='Trader' and GetPlayerTradeMoney()==75 and GetTargetTradeMoney()==125)
        local name,icon,count,quality,enchant=GetTradePlayerItemInfo(1)
        assert(name=='Trade cloth' and icon and count==5 and quality==-1 and enchant==nil)
        assert(select('#',GetTradePlayerItemInfo(1))==5 and select('#',GetTradeTargetItemInfo(7))==6)
        local name,icon,count,quality,usable,enchant=GetTradeTargetItemInfo(7)
        assert(name=='Trade cloth' and icon and count==2 and quality==-1 and usable and not enchant)
        assert(GetTradePlayerItemLink(1):find('item:2589:') and GetTradeTargetItemLink(7):find('item:2589:'))
        assert(GetTradeTargetItemInfo(1)==nil and GetTradePlayerItemInfo(8)==nil)
        assert(select(3,GetContainerItemInfo(0,1)))
        PickupContainerItem(0,1); assert(not CursorHasItem())
        PickupPlayerMoney(50); assert(GetCursorMoney()==50 and select(1,GetCursorInfo())=='money'); ClearCursor(); assert(GetCursorMoney()==0)
        TradeBindFixture=CreateFrame('Frame'); TradeBindFixture:RegisterEvent('TRADE_POTENTIAL_BIND_ENCHANT')
        TradeBindFixture:SetScript('OnEvent',function(self,event,bound) assert(type(bound)=='boolean'); self.bound=bound end)
    )LUA")));
    F.Events.FireEventBoolean(TEXT("TRADE_POTENTIAL_BIND_ENCHANT"),true); TestTrue(TEXT("Binding warning receives a real boolean"),F.Lua.ExecuteString(TEXT("assert(TradeBindFixture.bound==true)")));
    F.Events.FireEventBoolean(TEXT("TRADE_POTENTIAL_BIND_ENCHANT"),false); TestTrue(TEXT("A false binding warning is false in Lua"),F.Lua.ExecuteString(TEXT("assert(TradeBindFixture.bound==false)")));
    C->ResetTradeState(); TestTrue(TEXT("Closed APIs cannot show stale remote offers"),F.Lua.ExecuteString(TEXT("assert(GetTradeTargetItemInfo(7)==nil and GetTradeTargetItemLink(7)==nil and GetTargetTradeMoney()==0 and not select(3,GetContainerItemInfo(0,1)))")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudMailApi,"WowUnreal.Hud.MailApi",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudMailApi::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& Dbc=FDbcStore::Get(); if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq); if(!Dbc.IsDeferredLoaded()) Dbc.LoadDeferred(Mpq);
    TArray<uint8> Bytes; FDbcParser MailTemplates;
    if(!Mpq.ReadFile(TEXT("DBFilesClient\\MailTemplate.dbc"),Bytes) || !MailTemplates.Parse(Bytes) || !MailTemplates.GetRecordCount()) return false;
    const auto* Body=Dbc.ItemUi().MailBody(MailTemplates.GetUInt(0,0));
    TestTrue(TEXT("Body templates use the second localized string block"),Body && *Body==MailTemplates.GetString(0,18));
    const auto* Title=Dbc.ItemUi().MailSubject(MailTemplates.GetUInt(0,0));
    TestTrue(TEXT("Mail subjects use the first localized block"),Title && *Title==MailTemplates.GetString(0,1));
    TestTrue(TEXT("Original stationery and package records load"),!Dbc.ItemUi().Stationeries().IsEmpty() && !Dbc.ItemUi().Packages().IsEmpty());
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> C(NewObject<UWowConnectionManager>());
    auto& H=C->PacketHandler; auto& E=H.EntityManager; E.LocalPlayerGuid=1;
    auto& P=E.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER); P.SetField(PlayerField::COINAGE,10000); P.SetField(UnitField::BYTES_0,0x101);
    F.Context.ConnectionManager=C.Get(); F.Context.EntityManager=&E;
    H.CurrentMailboxGuid=1; H.MailComposer.Showing=true;
    FWowItemTemplate T; T.Id=2589; T.Name=TEXT("Cloth"); T.Quality=1; H.ItemTemplates.Add(T.Id,T);
    auto& Item=E.PromoteToTyped(101,WowTypeMask::OBJECT|WowTypeMask::ITEM); Item.Entry=T.Id; Item.SetField(ItemField::STACK_COUNT,3); P.SetField(PlayerField::PACK_SLOT_START,101);
    H.MailComposer.Items[3]=101;
    FWowMailMessage M; M.MessageId=77; M.SenderGuid=2; M.Subject=TEXT("A message"); M.Body=TEXT("Body"); M.Money=50; M.Stationery=41; M.DaysLeft=30;
    FWowMailAttachment A; A.AttachmentIndex=9; A.ItemGuidLow=88; A.ItemEntry=2589; A.Count=5; M.Attachments.Add(A); H.MailInbox.Add(M); H.MailInboxTotalCount=8; H.PlayerNameCache.Add(2,TEXT("Sender"));
    TestTrue(TEXT("Original mailbox tuples preserve sparse attachment slots and server counts"),F.Lua.ExecuteString(TEXT(R"LUA(
        local shown,total=GetInboxNumItems(); assert(shown==1 and total==8)
        local icon,stationery,sender,subject,money,cod,days,count,read,copied,returned,reply,gm,stack=GetInboxHeaderInfo(1)
        assert(icon and sender=='Sender' and subject=='A message' and money==50 and cod==0 and days==30 and count==1)
        assert(not read and not copied and not returned and reply and not gm and stack==5)
        local name,_,n,q,usable=GetInboxItem(1,10); assert(name=='Cloth' and n==5 and q==-1 and usable)
        assert(GetInboxItem(1,1)==nil and GetInboxItemLink(1,10):find('item:2589:'))
        assert(not InboxItemCanDelete(1)); assert(select('#',GetInboxHeaderInfo(99))==14)
        assert(GetCoinIcon(0):find('Coin_05') and GetCoinIcon(100):find('Coin_03') and GetCoinIcon(100000):find('Coin_02'))
        assert(GetCoinText(10203,' / ')=='1 Gold / 2 Silver / 3 Copper' and GetCoinText(0)=='')
        local coins=GetCoinTextureString(10203,12); assert(coins:find('GoldIcon:12:12') and coins:find('SilverIcon:12:12') and coins:find('CopperIcon:12:12'))
        MailTooltipFixture=CreateFrame('GameTooltip','MailTooltipFixture',UIParent)
        MailTooltipFixture:AddLine('Enclosed money','',1,1,1); assert(MailTooltipFixture:NumLines()==1)
        for i=1,20 do MailTooltipFixture:SetOwner(UIParent,'ANCHOR_RIGHT'); MailTooltipFixture:AddLine('Enclosed money','',1,1,1) end
        assert(MailTooltipFixture:NumLines()==1 and MailTooltipFixture:GetItem()==nil)
        local body,paper=GetInboxText(1); assert(body=='Body' and type(paper)=='string')
        local name,_,n,q=GetSendMailItem(4); assert(name=='Cloth' and n==3 and q==-1)
        assert(GetSendMailItemLink(4):find('item:2589:') and GetSendMailItem(1)==nil)
        SetSendMailMoney(100); assert(GetSendMailMoney()==100 and GetSendMailPrice()==130)
        SetSendMailMoney(0); SetSendMailCOD(42); assert(GetSendMailCOD()==42)
        assert(GetNumStationeries()>0 and GetNumPackages()>0)
        local name,texture,cost=GetPackageInfo(1); assert(name and texture and type(cost)=='number')
        SelectStationery(1); assert(GetSelectedStationeryTexture()); SelectStationery(999); assert(GetSelectedStationeryTexture()==nil)
        ClearSendMail(); assert(GetSendMailItem(4)==nil and GetSendMailCOD()==0 and GetSendMailMoney()==0)
    )LUA")));
    TestTrue(TEXT("Clearing attachments preserves the active tab"),H.MailComposer.Showing);
    H.MailInbox[0].MessageType=2; H.MailInbox[0].Subject=TEXT("2589:0:3"); H.MailInbox[0].Body=TEXT("2:100:200:1:2:3:0");
    TestTrue(TEXT("Expired-auction mail does not expose an invoice"),F.Lua.ExecuteString(TEXT("assert(select(4,GetInboxText(1))==nil and GetInboxInvoiceInfo(1)==nil)")));
    H.MailInbox[0].Subject=TEXT("2589:0:2");
    TestTrue(TEXT("Successful auction mail exposes the original invoice tuple"),F.Lua.ExecuteString(TEXT("local kind,item,who,bid,buyout,deposit,cut,delay,h,m=GetInboxInvoiceInfo(1); assert(kind=='seller' and item=='Cloth' and who=='Sender' and bid==100 and buyout==200 and deposit==1 and cut==2 and delay==3 and h==0 and m==0); assert(select(4,GetInboxText(1)))")));
    H.MailInbox[0].Attachments.Empty(); H.MailInbox[0].Money=0;
    TestTrue(TEXT("Empty mail can be deleted"),F.Lua.ExecuteString(TEXT("assert(InboxItemCanDelete(1)); CloseMail()")));
    TestTrue(TEXT("Close clears mailbox identity"),!H.CurrentMailboxGuid && !H.MailComposer.Showing);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudMerchantApi,"WowUnreal.Hud.MerchantApi",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudMerchantApi::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& Dbc=FDbcStore::Get(); if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq); if(!Dbc.IsDeferredLoaded()) Dbc.LoadDeferred(Mpq);
    const auto& Ui=Dbc.ItemUi();
    TestTrue(TEXT("Original item class and subclass names load"),Ui.ClassName(2) && *Ui.ClassName(2)==TEXT("Weapon") && Ui.SubclassName(2,0) && !Ui.SubclassName(2,0)->IsEmpty());
    TArray<uint8> Bytes; FDbcParser Costs,Quality;
    if(!Mpq.ReadFile(TEXT("DBFilesClient\\DurabilityCosts.dbc"),Bytes) || !Costs.Parse(Bytes)) return false;
    if(!Mpq.ReadFile(TEXT("DBFilesClient\\DurabilityQuality.dbc"),Bytes) || !Quality.Parse(Bytes)) return false;
    uint32 Multiplier=0; float Modifier=0;
    for(uint32 I=0;I<Costs.GetRecordCount();++I) if(Costs.GetUInt(I,0)==20) Multiplier=Costs.GetUInt(I,8); // Weapon subclass 7.
    for(uint32 I=0;I<Quality.GetRecordCount();++I) if(Quality.GetUInt(I,0)==4) Modifier=Quality.GetFloat(I,1);
    const uint32 Expected=FMath::Max(1u,uint32(float(uint32(7*Multiplier*double(Modifier)))*.95f));
    TestEqual(TEXT("Repair price follows server integer and float rounding"),Ui.RepairCost(20,1,2,7,7,.95f),Expected);
    TestEqual(TEXT("Undamaged items cost nothing"),Ui.RepairCost(20,1,2,7,0),0u);
    FHudFixture F; F.Frames.SetMpqManager(&Mpq); FWowAssetCache Cache; F.Frames.SetAssetCache(&Cache);
    TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    auto& H=Connection->PacketHandler; auto& E=H.EntityManager; E.LocalPlayerGuid=1;
    auto& P=E.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    P.SetField(UnitField::BYTES_0,0x101); P.SetField(UnitField::LEVEL,20); P.SetField(UnitField::HEALTH,100);
    auto& N=E.PromoteToTyped(2,WowTypeMask::OBJECT|WowTypeMask::UNIT); N.SetField(UnitField::NPC_FLAGS,WowNpcFlags::VENDOR|WowNpcFlags::REPAIR);
    F.Context.ConnectionManager=Connection.Get(); F.Context.EntityManager=&E;
    FWowItemTemplate T; T.Id=117; T.Name=TEXT("Vendor sword"); T.Class=2; T.Subclass=7; T.Quality=1; T.ItemLevel=20; T.MaxStack=20;
    H.ItemTemplates.Add(117,T); H.Merchant.Guid=2; H.Merchant.Open=true;
    FWowVendorItem V; V.Slot=7; V.ItemId=117; V.Price=25; V.MaxCount=MAX_uint32; V.BuyCount=1; H.Merchant.Items.Add(V);
    const auto Item=[&](uint64 Guid,int32 Slot,int32 Count,uint32 Damage)
    {
        auto& I=E.PromoteToTyped(Guid,WowTypeMask::OBJECT|WowTypeMask::ITEM); I.Entry=117; I.SetField(ItemField::STACK_COUNT,Count); I.SetField(ItemField::MAX_DURABILITY,100); I.SetField(ItemField::DURABILITY,100-Damage);
        if(Slot>=0) { P.SetField(PlayerField::INV_SLOT_HEAD+Slot*2,uint32(Guid)); P.SetField(PlayerField::INV_SLOT_HEAD+Slot*2+1,0); }
    };
    Item(101,15,1,7); Item(102,23,3,0); Item(103,39,5,50); Item(104,74,2,50); Item(105,75,4,50); Item(106,-1,100,50);
    P.SetField(PlayerField::BUYBACK_PRICE_START,20); P.SetField(PlayerField::BUYBACK_PRICE_START+1,40);
    P.SetField(PlayerField::BUYBACK_TIME_START,200); P.SetField(PlayerField::BUYBACK_TIME_START+1,100);
    TestTrue(TEXT("Usable inventory excludes bank, buyback and detached cached items"),E.GetInventoryItems().Num()==2 && E.GetInventoryItems(true).Num()==3);
    TestTrue(TEXT("Original merchant tuples, buyback ordering and real item counts"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(GetMerchantNumItems()==1 and CanMerchantRepair() and not CanGuildBankRepair())
        local name,icon,price,bundle,stock,usable,extended=GetMerchantItemInfo(1)
        assert(name=='Vendor sword' and icon and price==25 and bundle==1 and stock==-1 and usable and not extended)
        assert(GetMerchantItemMaxStack(1)==20 and GetMerchantItemLink(1):find('item:117:'))
        assert(GetItemCount(117)==4 and GetItemCount('Vendor sword',true)==9)
        assert(GetNumBuybackItems()==2)
        local n,_,p,c=GetBuybackItemInfo(1); assert(n=='Vendor sword' and p==40 and c==4)
        n,_,p,c=GetBuybackItemInfo(2); assert(p==20 and c==2)
        assert(GetBuybackItemLink(1):find('item:117:'))
    )LUA")));
    uint32 All=Ui.RepairCost(20,1,2,7,7);
    TestTrue(TEXT("Repair all sums carried inventory only"),F.Lua.ExecuteString(FString::Printf(TEXT("local cost,needed=GetRepairAllCost(); assert(cost==%u and needed)"),All)));
    H.HasWeaponProficiency=true; H.WeaponProficiency=0;
    TestFalse(TEXT("Unlearned weapon proficiency is unusable"),WowLuaApi::CanUseItem(&F.Context,T));
    H.WeaponProficiency=1u<<7; TestTrue(TEXT("Known proficiency restores usability"),WowLuaApi::CanUseItem(&F.Context,T));
    TestTrue(TEXT("Ordinary items do not open a refund confirmation"),F.Lua.ExecuteString(TEXT("assert(GetContainerItemPurchaseInfo(0,1)==nil and GetContainerItemPurchaseItem(0,1,1)==nil)")));
    auto* Refundable=E.FindItem(102); Refundable->SetField(ItemField::FLAGS,0x1000);
    FWowItemRefundInfo Refund; Refund.Money=50; Refund.Honor=10; Refund.Items[3]=117; Refund.Counts[3]=2; Refund.PurchasePlayedTime=990;
    H.ItemRefunds.Add(102,Refund); H.TotalPlayedTime=1000; H.PlayedTimeReceived=FPlatformTime::Seconds();
    TestTrue(TEXT("Refund metadata has the original tuple and sparse currency rows"),F.Lua.ExecuteString(TEXT(R"LUA(
        local money,honor,arena,count,left=GetContainerItemPurchaseInfo(0,1)
        assert(money==50 and honor==10 and arena==0 and count==1 and left>=7188 and left<=7190)
        local icon,n,link=GetContainerItemPurchaseItem(0,1,1); assert(icon and n==2 and link:find('item:117:'))
    )LUA")));
    Refundable->SetField(ItemField::ENCHANTMENT_1_1,1);
    TestTrue(TEXT("Permanent enchantments prevent a refundable quote"),F.Lua.ExecuteString(TEXT("assert(GetContainerItemPurchaseInfo(0,1)==nil)")));
    Refundable->SetField(ItemField::FLAGS,0); Refundable->SetField(ItemField::ENCHANTMENT_1_1,0);
    T.RequiredLevel=21; TestFalse(TEXT("Level requirement remains authoritative"),WowLuaApi::CanUseItem(&F.Context,T)); T.RequiredLevel=0;
    T.AllowableClass=2; TestFalse(TEXT("Wrong class is unusable"),WowLuaApi::CanUseItem(&F.Context,T)); T.AllowableClass=MAX_uint32;
    T.RequiredSpell=123; TestFalse(TEXT("Required learned spell is checked"),WowLuaApi::CanUseItem(&F.Context,T)); H.KnownSpells.Add(123); TestTrue(TEXT("Known required spell is accepted"),WowLuaApi::CanUseItem(&F.Context,T));
    Connection->PickupMerchantCursor(1); FWowUiCursorRenderer Cursor(F.Frames,F.Context); F.Frames.UpdateMouseHover(200,200); Cursor.Tick(F.Lua.GetState());
    H.ItemTemplates[117].InventoryType=13;
    TestTrue(TEXT("Equipment highlighting accepts matching slots only"),F.Lua.ExecuteString(TEXT("assert(CursorCanGoInSlot(16) and CursorCanGoInSlot(17) and not CursorCanGoInSlot(1))")));
    TestTrue(TEXT("Merchant pickup renders a visible icon and retains source identity"),F.Context.SoftwareCursorVisible && Connection->GetCursorMerchantItemId()==117 && F.Frames.GetNamedTexture(TEXT("IdleWowCursorIcon")));
    const auto* CursorImage=F.Frames.GetNamedTexture(TEXT("IdleWowCursorIcon"));
    TestTrue(TEXT("An initially empty cursor texture becomes an opaque drawable brush"),CursorImage && CursorImage->GetRenderOpacity()==1 && CursorImage->GetBrush().DrawAs==ESlateBrushDrawType::Image && CursorImage->GetBrush().GetResourceObject());
    TestTrue(TEXT("Unpurchased goods cannot replace action-bar entries"),!Connection->PlaceCursorIntoActionSlot(0) && Connection->HasCursorMerchantPayload());
    Connection->CloseMerchant(); Cursor.Tick(F.Lua.GetState());
    TestTrue(TEXT("Closing clears vendor selection and its visible icon"),!Connection->HasCursorPayload() && !F.Context.SoftwareCursorVisible);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudItemComparison,"WowUnreal.Hud.ItemComparison",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudItemComparison::RunTest(const FString&)
{
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    auto& Entities=Connection->PacketHandler.EntityManager; Entities.LocalPlayerGuid=1;
    auto& Player=Entities.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    F.Context.ConnectionManager=Connection.Get(); F.Context.EntityManager=&Entities;
    auto& Templates=Connection->PacketHandler.ItemTemplates;
    const auto AddTemplate=[&](uint32 Id,uint32 Type,const FString& Name,uint32 Armor,int32 Strength)
    {
        FWowItemTemplate T; T.Id=Id; T.Name=Name; T.InventoryType=Type; T.Armor=Armor; T.Stats.Add({4,Strength}); T.Quality=2;
        Templates.Add(Id,T);
    };
    AddTemplate(1001,13,TEXT("Incoming Sword"),0,4); Templates[1001].DamageMin=8; Templates[1001].DamageMax=12; Templates[1001].Delay=1000;
    AddTemplate(1002,13,TEXT("Main Sword"),0,7); Templates[1002].DamageMin=2; Templates[1002].DamageMax=4; Templates[1002].Delay=2000;
    AddTemplate(1003,14,TEXT("Offhand Shield"),20,2);
    AddTemplate(1004,11,TEXT("Incoming Ring"),0,10); AddTemplate(1005,11,TEXT("First Ring"),0,2); AddTemplate(1006,11,TEXT("Second Ring"),0,3);
    AddTemplate(1007,18,TEXT("Bag"),0,0);
    const auto Equip=[&](int32 Slot,uint32 Id)
    {
        const uint64 Guid=0x4000000000000000ull|Id;
        auto& Item=Entities.PromoteToTyped(Guid,WowTypeMask::OBJECT|WowTypeMask::ITEM); Item.Entry=Id; Item.SetField(ItemField::STACK_COUNT,1);
        Player.SetField(PlayerField::INV_SLOT_HEAD+Slot*2,uint32(Guid&0xffffffffull)); Player.SetField(PlayerField::INV_SLOT_HEAD+Slot*2+1,uint32(Guid>>32));
    };
    Equip(15,1002); Equip(16,1003); Equip(10,1005); Equip(11,1006);
    TestTrue(TEXT("Original tooltip signatures, weapon priority and signed stat APIs"),F.Lua.ExecuteString(TEXT(R"LUA(
        Incoming=CreateFrame('GameTooltip','CompareIncoming',UIParent)
        Compare=CreateFrame('GameTooltip','CompareEquipped',UIParent)
        Incoming:SetOwner(UIParent,'ANCHOR_RIGHT'); Incoming:SetMinimumWidth(220)
        Incoming:SetHyperlink('item:1001'); local name,link=Incoming:GetItem()
        assert(name=='Incoming Sword' and link:find('item:1001:'))
        assert(Incoming:GetAnchorType()=='ANCHOR_RIGHT' and Incoming:GetMinimumWidth()==220)
        Incoming:SetAnchorType('ANCHOR_LEFT',25,4); assert(Incoming:GetAnchorType()=='ANCHOR_LEFT')
        assert(Compare:SetHyperlinkCompareItem(link,1,false,Incoming) and Compare:GetItem()=='Offhand Shield')
        assert(Compare:SetHyperlinkCompareItem(link,2,false,Incoming) and Compare:GetItem()=='Main Sword')
        local stat=GetItemStats(link); assert(stat.ITEM_MOD_STRENGTH_SHORT==4 and stat.ITEM_MOD_DAMAGE_PER_SECOND_SHORT==10)
        local supplied={keep=1}; assert(GetItemStatDelta(link,'item:1002',supplied)==supplied and supplied.keep==1)
        assert(supplied.ITEM_MOD_STRENGTH_SHORT==-3 and supplied.ITEM_MOD_DAMAGE_PER_SECOND_SHORT==8.5)
        assert(GetItemStats('item:1003').RESISTANCE0_NAME==20)
    )LUA")));
    const auto* Tip=F.Frames.GetTooltipState(F.Frames.FindFrame(TEXT("CompareEquipped")));
    TestTrue(TEXT("Compare keeps equipped context and header before the item title"),Tip && Tip->ItemId==1002 && Tip->Lines.Num()>4 && Tip->Lines[0].Text==TEXT("Currently Equipped") && Tip->Lines[1].Text==TEXT("Main Sword"));
    if(Tip)
    {
        bool Gain=false,Loss=false;
        for(const auto& Line:Tip->Lines) { if(Line.Text.StartsWith(TEXT("+8.5"))) Gain=Line.Color.G==1 && Line.Color.R==0; if(Line.Text.StartsWith(TEXT("-3"))) Loss=Line.Color.R==1 && Line.Color.G<.2f; }
        TestTrue(TEXT("Incoming minus equipped renders gains green and losses red"),Gain && Loss);
    }
    const auto* Left3=F.Frames.GetNamedFontString(TEXT("CompareEquippedTextLeft3")); const auto* Right3=F.Frames.GetNamedFontString(TEXT("CompareEquippedTextRight3"));
    const auto* Left4=F.Frames.GetNamedFontString(TEXT("CompareEquippedTextLeft4"));
    TestTrue(TEXT("Mixed rows render damage and speed together before DPS"),Left3 && Right3 && Left4 && Left3->GetText().ToString()==TEXT("2 - 4 Damage") && Right3->GetText().ToString()==TEXT("Speed 2.00") && Left4->GetText().ToString().Contains(TEXT("1.5 damage per second")));
    TestTrue(TEXT("Ring slots, unsupported indices and stale contexts follow stock behavior"),F.Lua.ExecuteString(TEXT(R"LUA(
        Incoming:SetHyperlink('item:1004'); local _,link=Incoming:GetItem()
        assert(Compare:SetHyperlinkCompareItem(link,1) and Compare:GetItem()=='First Ring')
        assert(Compare:SetHyperlinkCompareItem(link,2) and Compare:GetItem()=='Second Ring')
        assert(not Compare:SetHyperlinkCompareItem(link,3) and not Compare:IsShown() and Compare:GetItem()==nil)
        assert(not Compare:SetHyperlinkCompareItem('item:1007',1))
        Incoming:SetText('Different content'); assert(Incoming:GetItem()==nil)
        Incoming:AddDoubleLine('left','right'); Incoming:AppendText(' more')
    )LUA")));
    Tip=F.Frames.GetTooltipState(F.Frames.FindFrame(TEXT("CompareIncoming")));
    TestTrue(TEXT("AppendText follows the last inserted row"),Tip && Tip->DoubleLines.Last().LeftText==TEXT("left more"));
    // An offhand item also compares against a two-handed weapon when offhand is empty.
    Player.SetField(PlayerField::INV_SLOT_HEAD+16*2,0); Player.SetField(PlayerField::INV_SLOT_HEAD+16*2+1,0); Templates[1002].InventoryType=17;
    TestTrue(TEXT("Offhand comparison falls back to the equipped two-hander"),F.Lua.ExecuteString(TEXT("assert(Compare:SetHyperlinkCompareItem('item:1003',1) and Compare:GetItem()=='Main Sword'); assert(not Compare:SetHyperlinkCompareItem('item:1003',2))")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudDressUpApi,"WowUnreal.Hud.DressUpPreviewApi",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudDressUpApi::RunTest(const FString&)
{
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>());
    auto& Entities=Connection->PacketHandler.EntityManager; Entities.LocalPlayerGuid=1;
    auto& Player=Entities.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    Player.SetField(PlayerField::INV_SLOT_HEAD+15*2,123);
    F.Context.ConnectionManager=Connection.Get(); F.Context.EntityManager=&Entities;
    FWowItemTemplate Chest; Chest.Id=2370; Chest.InventoryType=5; Connection->PacketHandler.ItemTemplates.Add(Chest.Id,Chest);
    FWowItemTemplate Food; Food.Id=117; Connection->PacketHandler.ItemTemplates.Add(Food.Id,Food);
    uint64 Unit=0; float Facing=0,Scale=0; FVector Position=FVector::ZeroVector; TArray<uint32> Tried; TArray<bool> Dressed;
    F.Frames.OnUnitModel=[&](int64,uint64 Guid){Unit=Guid;}; F.Frames.OnModelFacing=[&](int64,float Value){Facing=Value;};
    F.Frames.OnModelScale=[&](int64,float Value){Scale=Value;}; F.Frames.OnModelPosition=[&](int64,const FVector& Value){Position=Value;};
    F.Frames.OnModelTryOn=[&](int64,uint32 Id,uint32 Type){ Tried.Add(Id); TestEqual(TEXT("Preview receives real inventory type"),Type,5u); };
    F.Frames.OnModelDress=[&](int64,bool Value){Dressed.Add(Value);};
    const char* Xml="<Ui><DressUpModel name='PreviewModel' parent='UIParent' scale='2'><Size x='316' y='331'/><Anchors><Anchor point='CENTER'/></Anchors></DressUpModel></Ui>";
    TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Xml),FCStringAnsi::Strlen(Xml));
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("DressUpScaleFixture")); if(!TestEqual(TEXT("Model XML parsed"),Parsed.Num(),1)) return false;
    const int64 H=F.Frames.CreateFrame(Parsed[0].FrameDef); F.Events.CreateFrameObject(H,TEXT("PreviewModel"));
    TestEqual(TEXT("XML model scale does not stretch the UI frame"),F.Frames.GetFrameDef(H)->Scale,1.f);
    TestEqual(TEXT("XML retains the independent model scale"),F.Frames.GetFrameDef(H)->ModelScale,2.f);
    TestTrue(TEXT("Stock model setters, getters, try-on, reset and refresh reach the renderer"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(PreviewModel:GetScale()==1 and PreviewModel:GetModelScale()==2)
        PreviewModel:SetUnit('player'); PreviewModel:SetRotation(.61); assert(math.abs(PreviewModel:GetFacing()-.61)<.001)
        PreviewModel:SetFacing(1.5); PreviewModel:SetModelScale(1.25); PreviewModel:SetPosition(.2,-.3,.4)
        assert(PreviewModel:GetFacing()==1.5 and PreviewModel:GetModelScale()==1.25)
        local x,y,z=PreviewModel:GetPosition(); assert(math.abs(x-.2)<.001 and math.abs(y+.3)<.001 and math.abs(z-.4)<.001)
        assert(IsDressableItem('item:2370') and not IsDressableItem('item:117'))
        PreviewModel:TryOn('item:2370'); PreviewModel:TryOn('item:117'); PreviewModel:Undress(); PreviewModel:Dress(); PreviewModel:RefreshUnit()
    )LUA")));
    TestEqual(TEXT("Refresh retains the source unit"),Unit,uint64(1)); TestEqual(TEXT("Facing request"),Facing,1.5f); TestEqual(TEXT("Scale request"),Scale,1.25f);
    TestTrue(TEXT("Position request"),Position.Equals(FVector(.2,-.3,.4),.001)); TestEqual(TEXT("Only wearable item is tried"),Tried.Num(),1);
    TestTrue(TEXT("Undress and reset have distinct requests"),Dressed.Num()==2 && !Dressed[0] && Dressed[1]);
    TestEqual(TEXT("Preview never mutates authoritative equipment"),Player.GetField(PlayerField::INV_SLOT_HEAD+15*2),123u);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudMacroSequence,"WowUnreal.Hud.MacroSequences",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudMacroSequence::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& Dbc=FDbcStore::Get(); if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq); if(!Dbc.IsDeferredLoaded()) Dbc.LoadDeferred(Mpq);
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> C(NewObject<UWowConnectionManager>()); auto& H=C->PacketHandler; F.Context.ConnectionManager=C.Get(); F.Context.EntityManager=&H.EntityManager;
    H.EntityManager.LocalPlayerGuid=1; H.EntityManager.PromoteToTyped(1,WowTypeMask::PLAYER|WowTypeMask::UNIT); H.KnownSpells.Add(78); H.KnownSpells.Add(6673);
    TestTrue(TEXT("Prepare original FrameXML helpers and event listener"),F.Lua.ExecuteString(TEXT(R"LUA(
        strlower=string.lower; strtrim=function(s) return s:match('^%s*(.-)%s*$') end; strfind=string.find; strmatch=string.match; gsub=string.gsub; random=math.random
        MacroChanges=CreateFrame('Frame'); for _,event in ipairs({'ACTIONBAR_UPDATE_STATE','ACTIONBAR_SLOT_CHANGED','ACTIONBAR_UPDATE_COOLDOWN','UNIT_SPELLCAST_CHANNEL_START','UNIT_SPELLCAST_SENT','UNIT_SPELLCAST_SUCCEEDED','UNIT_SPELLCAST_FAILED'}) do MacroChanges:RegisterEvent(event) end
        MacroChanges:SetScript('OnEvent',function(self,event,...) if event=='UNIT_SPELLCAST_CHANNEL_START' then ChannelEvent={...} end end)
        Requests={}; QAShift=false; IsShiftKeyDown=function() return QAShift end
        CastSpellByName=function(name,target) local spell=_IdleWowResolveMacroSpell(name); if spell then table.insert(Requests,{spell,target}) end end
    )LUA")));
    TArray<uint8> Bytes; if(!TestTrue(TEXT("Read actual 12340 ChatFrame source from MPQ"),Mpq.ReadFile(TEXT("Interface\\FrameXML\\ChatFrame.lua"),Bytes))) return false;
    Bytes.Add(0); const FString Source=UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData()));
    const int32 Begin=Source.Find(TEXT("local CastSequenceManager;")),End=Source.Find(TEXT("-- Slash commands that are protected"));
    const int32 ItemBegin=Source.Find(TEXT("function SecureCmdItemParse(item)")),ItemEnd=Source.Find(TEXT("SecureCmdList[\"STARTATTACK\"]"));
    if(!TestTrue(TEXT("Original sequence/random and item parser sections exist"),Begin>=0 && End>Begin && ItemBegin>=0 && ItemEnd>ItemBegin)) return false;
    const FString Stock=Source.Mid(Begin,End-Begin)+TEXT("\n")+Source.Mid(ItemBegin,ItemEnd-ItemBegin)+TEXT("\n_QAExecuteSequence=ExecuteCastSequence; _QAExecuteRandom=ExecuteCastRandom; _QASequenceTick=function(seconds) CastSequenceManager_OnUpdate(CastSequenceManager,seconds) end");
    if(!TestTrue(TEXT("Load original sequence/random implementation without replacing its logic"),F.Lua.ExecuteString(Stock))) return false;
    const auto Request=[&](uint32 Spell,uint8 Counter){H.LocalCastRequests.Add(Counter,Spell); FWowMacros::CastSent(F.Lua.GetState(),Spell,Counter);};
    TestTrue(TEXT("First stock sequence request"),F.Lua.ExecuteString(TEXT("_QAExecuteSequence('Heroic Strike,Battle Shout','player'); assert(#Requests==1 and Requests[1][1]==78)")));
    Request(78,10);
    TestTrue(TEXT("Stock pending entry prevents repeat dispatch"),F.Lua.ExecuteString(TEXT("_QAExecuteSequence('Heroic Strike,Battle Shout','player'); assert(#Requests==1 and QueryCastSequence('Heroic Strike,Battle Shout')==1)")));
    FWowMacros::CastResult(F.Lua.GetState(),78,9,true); FWowMacros::CastResult(F.Lua.GetState(),6673,10,true);
    TestTrue(TEXT("Unrelated results carry no local spell name and cannot advance"),F.Lua.ExecuteString(TEXT("assert(QueryCastSequence('Heroic Strike,Battle Shout')==1)")));
    FWowMacros::CastResult(F.Lua.GetState(),78,10,false);
    TestTrue(TEXT("Rejected step remains retryable"),F.Lua.ExecuteString(TEXT("_QAExecuteSequence('Heroic Strike,Battle Shout','player'); assert(#Requests==2 and QueryCastSequence('Heroic Strike,Battle Shout')==1)")));
    Request(78,11); FWowMacros::CastResult(F.Lua.GetState(),78,11,true);
    TestTrue(TEXT("Stock tuple is index,item,spell and macro icon follows it"),F.Lua.ExecuteString(TEXT("local i,item,spell=QueryCastSequence('Heroic Strike,Battle Shout'); assert(i==2 and item==nil and spell=='battle shout'); local macro=CreateMacro('Sequence',1,'/castsequence Heroic Strike,Battle Shout',false); assert(select(3,GetMacroSpell(macro))==6673); _QAExecuteSequence('Heroic Strike,Battle Shout','player')")));
    Request(6673,12); FWowMacros::CastResult(F.Lua.GetState(),6673,12,true);
    TestTrue(TEXT("Stock sequence cycles and null stays on its step"),F.Lua.ExecuteString(TEXT("assert(QueryCastSequence('Heroic Strike,Battle Shout')==1); _QAExecuteSequence('Heroic Strike,null','player')")));
    Request(78,13); FWowMacros::CastResult(F.Lua.GetState(),78,13,true);
    TestTrue(TEXT("Null does not dispatch a cast"),F.Lua.ExecuteString(TEXT("local count=#Requests; assert(QueryCastSequence('Heroic Strike,null')==2); _QAExecuteSequence('Heroic Strike,null','player'); assert(#Requests==count); _QAExecuteSequence('reset=target Heroic Strike,Battle Shout','player')")));
    Request(78,14); FWowMacros::CastResult(F.Lua.GetState(),78,14,true); F.Events.FireEvent(TEXT("PLAYER_TARGET_CHANGED"));
    TestTrue(TEXT("Original target reset event"),F.Lua.ExecuteString(TEXT("assert(QueryCastSequence('reset=target Heroic Strike,Battle Shout')==1); _QAExecuteSequence('reset=3 Heroic Strike,Battle Shout','player')")));
    Request(78,15); FWowMacros::CastResult(F.Lua.GetState(),78,15,true);
    TestTrue(TEXT("Original timeout update"),F.Lua.ExecuteString(TEXT("assert(QueryCastSequence('reset=3 Heroic Strike,Battle Shout')==2); _QASequenceTick(4); assert(QueryCastSequence('reset=3 Heroic Strike,Battle Shout')==1); _QAExecuteSequence('reset=combat Heroic Strike,Battle Shout','player')")));
    Request(78,16); FWowMacros::CastResult(F.Lua.GetState(),78,16,true); F.Events.FireEvent(TEXT("PLAYER_REGEN_ENABLED"));
    TestTrue(TEXT("Original combat reset and modifier reset"),F.Lua.ExecuteString(TEXT("assert(QueryCastSequence('reset=combat Heroic Strike,Battle Shout')==1); _QAExecuteSequence('reset=shift Heroic Strike,Battle Shout','player')")));
    Request(78,17); FWowMacros::CastResult(F.Lua.GetState(),78,17,true);
    TestTrue(TEXT("Modifier queries restart the stock entry"),F.Lua.ExecuteString(TEXT("assert(QueryCastSequence('reset=shift Heroic Strike,Battle Shout')==2); QAShift=true; assert(QueryCastSequence('reset=shift Heroic Strike,Battle Shout')==1); QAShift=false; FirstRandom=_QAExecuteRandom('Heroic Strike,Battle Shout'); assert(_QAExecuteRandom('Heroic Strike,Battle Shout')==FirstRandom)")));
    TestTrue(TEXT("Random choice remains stable until an acknowledged success"),F.Lua.ExecuteString(TEXT("local picked=_QAExecuteRandom('Heroic Strike'); assert(picked=='Heroic Strike' and _QAExecuteRandom('Heroic Strike')==picked)")));
    Request(78,18); FWowMacros::CastResult(F.Lua.GetState(),78,18,false);
    TestTrue(TEXT("Failure retains the original random choice"),F.Lua.ExecuteString(TEXT("assert(_QAExecuteRandom('Heroic Strike')=='Heroic Strike')")));
    Request(78,19); FWowMacros::CastResult(F.Lua.GetState(),78,19,true);
    F.Frames.CurrentMouseButton=TEXT("RightButton"); H.Casts.Add(1,{740,20,FPlatformTime::Seconds(),FPlatformTime::Seconds()+5,true}); WowLuaApi::NotifyCastState(F.Lua.GetState(),1,740,3);
    TestTrue(TEXT("Additional conditions and typed channel events"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(ChannelEvent and ChannelEvent[1]=='player' and ChannelEvent[2]==GetSpellInfo(740) and ChannelEvent[5]==740 and type(ChannelEvent[5])=='number')
        assert(SecureCmdOptionParse('[button:2] Right; Left')=='Right' and SecureCmdOptionParse('[bar:1,spec:1] Active; Other')=='Active')
        assert(SecureCmdOptionParse('[noflyable,noequipped:Shields,nocursor] Clear')=='Clear')
        assert(SecureCmdOptionParse('[channeling:'..GetSpellInfo(740)..'] Channel')=='Channel' and SecureCmdOptionParse('[vehicleui] Vehicle; Normal')=='Normal')
        assert(select('#',UnitChannelInfo('player'))==8 and UnitCastingInfo('player')==nil)
    )LUA")));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudCombatFamilies,"WowUnreal.Hud.CombatFamilies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudCombatFamilies::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& Dbc=FDbcStore::Get(); if(!Dbc.IsLoaded()) Dbc.LoadAll(Mpq); if(!Dbc.IsDeferredLoaded()) Dbc.LoadDeferred(Mpq);
    const FString Renew=Dbc.Spells().Description(139,20); TestTrue(TEXT("Named description variables resolve before numeric formulas"),Renew.Contains(TEXT("45")) && !Renew.Contains(TEXT("$<")));
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> C(NewObject<UWowConnectionManager>()); auto& H=C->PacketHandler;
    F.Context.ConnectionManager=C.Get(); F.Context.EntityManager=&H.EntityManager; H.EntityManager.LocalPlayerGuid=1;
    H.EntityManager.PromoteToTyped(1,WowTypeMask::PLAYER|WowTypeMask::UNIT).SetField(UnitField::HEALTH,100);
    const uint64 Victim=0xf130000005000001ULL; H.EntityManager.PromoteToTyped(Victim,WowTypeMask::UNIT).SetField(UnitField::HEALTH,100);
    H.PlayerNameCache.Add(1,TEXT("Main")); H.CreatureNameCache.Add(5,TEXT("Wolf"));
    H.OnResolveCombatFlags.BindLambda([&](uint64 Id){return WowLuaApi::CombatLogFlags(&F.Context,Id);});
    H.OnCombatLog.BindLambda([&](const FWowCombatEntry& E){FWowCombatLogUi::Dispatch(F.Lua.GetState(),E);});
    TestTrue(TEXT("Install typed event listener"),F.Lua.ExecuteString(TEXT(R"LUA(
        Rows={}; Listener=CreateFrame('Frame'); Listener:RegisterEvent('COMBAT_LOG_EVENT_UNFILTERED'); Listener:SetScript('OnEvent',function(self,event,...) table.insert(Rows,{...}) end)
        SPELL_FAILED_MOVING='Localized moving failure'
    )LUA")));
    TArray<uint8> P; const auto U32=[&](uint32 V){P.Append(reinterpret_cast<const uint8*>(&V),4);}; const auto U64=[&](uint64 V){P.Append(reinterpret_cast<const uint8*>(&V),8);}; const auto Float=[&](float V){P.Append(reinterpret_cast<const uint8*>(&V),4);};
    const auto Guid=[&](uint64 V){uint8 Mask=0; TArray<uint8> Bytes; for(int32 I=0;I<8;++I) if(uint8 B=V>>(8*I)) {Mask|=1<<I; Bytes.Add(B);} P.Add(Mask); P.Append(Bytes);};
    const auto Check=[&](const TCHAR* Name,const TCHAR* Lua){return TestTrue(Name,F.Lua.ExecuteString(Lua));};
    Guid(Victim); Guid(1); U32(139); U32(2); U32(3); U32(20); U32(0); U32(2); U32(3); U32(4); P.Add(1); U32(8); U32(40); U32(5); U32(2); P.Add(0);
    auto Truncated=P; Truncated.Pop(); H.HandlePacket(0x24E,Truncated); TestEqual(TEXT("Partial multi-effect periodic packet publishes no rows"),H.CombatHistory.Num(),0);
    H.HandlePacket(0x24E,P);
    Check(TEXT("Periodic damage and heal suffixes retain exact order/types"),TEXT("assert(#Rows==2 and Rows[1][2]=='SPELL_PERIODIC_DAMAGE' and Rows[1][12]==20 and Rows[1][15]==4 and Rows[1][17]==3 and Rows[1][18]==true); assert(Rows[2][2]=='SPELL_PERIODIC_HEAL' and Rows[2][12]==40 and Rows[2][13]==5 and Rows[2][14]==2 and Rows[2][15]==false)"));
    P.Reset(); Guid(1); Guid(1); U32(78); U32(1); U32(150); H.HandlePacket(0x151,P);
    Check(TEXT("Rage uses client power display units"),TEXT("assert(Rows[3][2]=='SPELL_ENERGIZE' and Rows[3][12]==15 and Rows[3][13]==1)"));
    P.Reset(); Guid(1); U32(1766); U32(2); U32(68); U32(1); Guid(Victim); U32(133); U32(8); U32(1); Guid(Victim); U32(23); U32(0); Float(.5f);
    Truncated=P; Truncated.Pop(); H.HandlePacket(0x24C,Truncated); TestEqual(TEXT("Partial later execute effect does not publish an earlier interrupt"),H.CombatHistory.Num(),3);
    H.HandlePacket(0x24C,P);
    Check(TEXT("Interrupt carries the interrupted spell and leech carries integral gain"),TEXT("assert(Rows[4][2]=='SPELL_INTERRUPT' and Rows[4][12]==133 and type(Rows[4][13])=='string' and Rows[4][14]==4); assert(Rows[5][2]=='SPELL_LEECH' and Rows[5][12]==23 and Rows[5][13]==0 and Rows[5][14]==11)"));
    P.Reset(); Guid(Victim); Guid(1); U32(527); P.Add(0); U32(1); U32(139); P.Add(0); H.HandlePacket(0x27B,P); H.HandlePacket(0x333,P); H.HandlePacket(0x14F,P);
    Check(TEXT("Dispel/steal/break use different spell prefixes and original aura suffix"),TEXT("assert(Rows[6][2]=='SPELL_DISPEL' and Rows[6][9]==527 and Rows[6][12]==139 and Rows[6][15]=='BUFF'); assert(Rows[7][2]=='SPELL_STOLEN'); assert(Rows[8][2]=='SPELL_AURA_BROKEN_SPELL' and Rows[8][9]==139 and Rows[8][12]==527 and Rows[8][15]=='BUFF')"));
    P.Reset(); U64(1); U64(Victim); U32(527); U32(139); H.HandlePacket(0x262,P);
    P.Reset(); U32(78); U64(1); P.Add(0); U32(1); U64(Victim); P.Add(10); H.HandlePacket(0x24B,P);
    Check(TEXT("Failed dispels and shield misses have correct typed suffixes"),TEXT("assert(Rows[9][2]=='SPELL_DISPEL_FAILED' and Rows[9][12]==139); assert(Rows[10][2]=='DAMAGE_SHIELD_MISSED' and Rows[10][12]=='ABSORB' and Rows[10][13]==0)"));
    P.Reset(); U64(1); U64(Victim); U32(78); U32(25); U32(3); U32(1); H.HandlePacket(0x24F,P);
    P.Reset(); U64(1); U64(Victim); H.HandlePacket(0x1F5,P); U32(78); H.HandlePacket(0x32F,P);
    Check(TEXT("Shield, party kill and instant-kill preserve full GUIDs"),TEXT("assert(Rows[11][2]=='DAMAGE_SHIELD' and Rows[11][12]==25 and Rows[11][13]==3); assert(Rows[12][2]=='PARTY_KILL' and Rows[13][2]=='SPELL_INSTAKILL'); assert(Rows[13][6]=='0xF130000005000001')"));
    P.Reset(); Guid(1); Guid(1); P.Add(7); U32(133); U32(0); U32(2000); U32(2); Guid(Victim); H.HandlePacket(WowOpcode::SMSG_SPELL_START,P);
    Check(TEXT("Cast start has no fabricated destination"),TEXT("assert(Rows[14][2]=='SPELL_CAST_START' and Rows[14][6]==nil)"));
    P.Reset(); Guid(1); Guid(1); P.Add(8); U32(78); U32(0); U32(123); P.Add(1); U64(Victim); P.Add(1); U64(Victim); P.Add(11); P.Add(2); U32(2); Guid(Victim);
    Truncated=P; Truncated.Pop(); H.HandlePacket(WowOpcode::SMSG_SPELL_GO,Truncated); TestEqual(TEXT("Truncated target block does not emit success/miss"),H.CombatHistory.Num(),14);
    H.HandlePacket(WowOpcode::SMSG_SPELL_GO,P);
    Check(TEXT("Spell-go hit/miss arrays contain full, not packed, GUIDs"),TEXT("assert(Rows[15][2]=='SPELL_CAST_SUCCESS' and Rows[15][6]=='0xF130000005000001'); assert(Rows[16][2]=='SPELL_MISSED' and Rows[16][12]=='REFLECT')"));
    P.Reset(); P.Add(8); U32(78); P.Add(51); H.HandlePacket(0x130,P);
    Check(TEXT("Local cast rejection localizes the original failure token"),TEXT("assert(Rows[17][2]=='SPELL_CAST_FAILED' and Rows[17][12]=='Localized moving failure')"));
    Check(TEXT("Spell filters, reverse indices and unfiltered traversal match original API"),TEXT("CombatLogResetFilter(); CombatLogAddFilter(nil,nil,nil,133); assert(CombatLogGetNumEntries()==1 and CombatLogGetNumEntries(0)==1); assert(CombatLogSetCurrentEntry(1)==1 and select(2,CombatLogGetCurrentEntry())=='SPELL_CAST_START'); assert(CombatLogSetCurrentEntry(-1,true)==1 and select(2,CombatLogGetCurrentEntry())=='SPELL_MISSED'); assert(CombatLogAdvanceEntry(1,true) and select(2,CombatLogGetCurrentEntry())=='SPELL_CAST_FAILED'); assert(not CombatLogAdvanceEntry(1,true)); CombatLogResetFilter(); CombatLogAddFilter(nil,nil,nil,GetSpellInfo(133)); assert(CombatLogGetNumEntries()==1); assert(not pcall(CombatLogAddFilter,nil,1,nil)); CombatLogResetFilter()"));
    const uint32 OldFlags=H.CombatHistory.Find(1)->DestinationFlags;
    H.EntityManager.Find(Victim)->SetField(UnitField::SUMMONEDBY,1);
    TestTrue(TEXT("Historical affiliation is captured at receipt"),H.CombatHistory.Find(1)->FlagsCaptured && H.CombatHistory.Find(1)->DestinationFlags==OldFlags && OldFlags!=WowLuaApi::CombatLogFlags(&F.Context,Victim));
    H.CombatHistory.Reset(); Check(TEXT("Reset listener for aura transactions"),TEXT("Rows={}"));
    const auto Aura=[&](uint8 Charges){P.Reset(); Guid(1); P.Add(0); U32(7386); P.Add(0xA8); P.Add(20); P.Add(Charges); U32(30000); U32(30000);};
    Aura(1); H.HandlePacket(WowOpcode::SMSG_AURA_UPDATE,P); Aura(3); H.HandlePacket(WowOpcode::SMSG_AURA_UPDATE,P); Aura(2); H.HandlePacket(WowOpcode::SMSG_AURA_UPDATE,P);
    Check(TEXT("Aura application and stack changes expose correct names and counts"),TEXT("assert(#Rows==3 and Rows[1][2]=='SPELL_AURA_APPLIED' and Rows[1][12]=='DEBUFF'); assert(Rows[2][2]=='SPELL_AURA_APPLIED_DOSE' and Rows[2][13]==3); assert(Rows[3][2]=='SPELL_AURA_REMOVED_DOSE' and Rows[3][13]==2)"));
    P.Reset(); Guid(1); P.Add(0); U32(0); P.Add(1); U32(139); P.Add(0x20); // complete removal followed by a partial second aura
    H.HandlePacket(WowOpcode::SMSG_AURA_UPDATE_ALL,P);
    TestTrue(TEXT("Partial all-aura snapshot preserves previous entries and history"),H.EntityManager.Find(1)->Auras[0].SpellId==7386 && H.CombatHistory.Num()==3);
    P.Reset(); Guid(1); P.Add(0); U32(0); H.HandlePacket(WowOpcode::SMSG_AURA_UPDATE,P);
    Check(TEXT("Aura removal references the old caster/spell"),TEXT("assert(Rows[4][2]=='SPELL_AURA_REMOVED' and Rows[4][9]==7386 and Rows[4][12]=='DEBUFF')"));
    P.Reset(); Guid(Victim); Guid(1); Guid(0); U32(30); U32(133); H.HandlePacket(0x32,P);
    P.Reset(); Guid(Victim); Guid(1); Guid(0); U32(uint32(-20)); U32(133); H.HandlePacket(0x32,P);
    Check(TEXT("Destructible-building damage and healing have distinct suffixes"),TEXT("assert(Rows[5][2]=='SPELL_BUILDING_DAMAGE' and Rows[5][12]==30 and Rows[5][14]==4); assert(Rows[6][2]=='SPELL_BUILDING_HEAL' and Rows[6][12]==20 and Rows[6][15]==false)"));
    auto* Dying=H.EntityManager.Find(Victim); Dying->Entry=5; H.CreatureTypes.Add(5,13);
    P.Reset(); U32(1); P.Add(0); Guid(Victim); P.Add(1); U32(1u<<UnitField::HEALTH); U32(0); H.HandlePacket(WowOpcode::SMSG_UPDATE_OBJECT,P);
    Check(TEXT("Gas-cloud death uses the original dissipation event"),TEXT("assert(Rows[7][2]=='UNIT_DISSIPATES')"));
    H.CreatureTypes.Remove(5); Dying->SetField(UnitField::HEALTH,100); Dying->SetField(UnitField::CREATED_BY_SPELL,2484); H.HandlePacket(WowOpcode::SMSG_UPDATE_OBJECT,P);
    Check(TEXT("SummonProperties totem slots identify destroyed units"),TEXT("assert(Rows[8][2]=='UNIT_DESTROYED')"));
    uint32 Enchant=0; for(uint32 I=1;I<5000 && !Enchant;++I) if(const auto* E=Dbc.ItemUi().Enchant(I); E && !E->Name.IsEmpty() && !(E->Flags&2)) Enchant=I;
    TestTrue(TEXT("Client contains visible enchantment metadata"),Enchant!=0); FWowItemTemplate Template; Template.Name=TEXT("Shield"); H.ItemTemplates.Add(2211,Template);
    P.Reset(); Guid(1); Guid(0); U32(2211); U32(Enchant); H.HandlePacket(0x1D7,P);
    Check(TEXT("Enchantment removal uses names/item fields without a spell prefix"),TEXT("assert(Rows[9][2]=='ENCHANT_REMOVED' and type(Rows[9][9])=='string' and Rows[9][10]==2211 and Rows[9][11]=='Shield')"));
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudXmlTextureDefaults,"WowUnreal.Hud.XmlTextureDefaults",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudXmlTextureDefaults::RunTest(const FString&)
{
    FHudFixture F;
    const char* Xml=R"XML(<Ui><Texture name="UnanchoredTextureTemplate" virtual="true"><Size x="32" y="16"/></Texture>
      <Frame name="DefaultTextureOwner"><Size x="172" y="96"/><Anchors><Anchor point="CENTER"/></Anchors><Layers><Layer>
       <Texture name="$parentFill" inherits="UnanchoredTextureTemplate"/>
       <Texture name="$parentAnchored" inherits="UnanchoredTextureTemplate"><Anchors><Anchor point="BOTTOMRIGHT"/></Anchors></Texture>
      </Layer></Layers></Frame></Ui>)XML";
    TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Xml),FCStringAnsi::Strlen(Xml));
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("XmlTextureDefaults"));
    for(const auto& D:Parsed) if(D.Type==FWowXmlDirective::EType::Texture) F.Frames.LoadTextureDefinition(D.TextureDef,D.TextureParent); else F.Frames.CreateFrame(D.FrameDef);
    F.Frames.RefreshLayout();
    const auto* Fill=Cast<UCanvasPanelSlot>(F.Frames.GetNamedTexture(TEXT("DefaultTextureOwnerFill"))->Slot);
    const auto* Anchored=Cast<UCanvasPanelSlot>(F.Frames.GetNamedTexture(TEXT("DefaultTextureOwnerAnchored"))->Slot);
    TestTrue(TEXT("XML PostLoad defaults stretch even explicitly sized unanchored textures"),Fill && Fill->GetAnchors().Maximum==FVector2D(1,1));
    TestTrue(TEXT("Explicit child anchors override the default of an inherited template"),Anchored && Anchored->GetAnchors().Maximum==FVector2D(0,0) && Anchored->GetSize()==FVector2D(32,16));
    TestTrue(TEXT("Lua-created regions still require their own anchors"),F.Lua.ExecuteString(TEXT("LocalTexture=DefaultTextureOwner:CreateTexture('LocalTexture'); LocalTexture:SetSize(32,16); assert(not LocalTexture:IsVisible() and LocalTexture:GetNumPoints()==0)")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudFrameCharacterInput,"WowUnreal.Hud.FrameCharacterInput",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudFrameCharacterInput::RunTest(const FString&)
{
    FHudFixture F;
    TestTrue(TEXT("Create keyboard-enabled character handlers"),F.Lua.ExecuteString(TEXT(R"LUA(
        CharLow=CreateFrame('Frame','CharLow',UIParent); CharLow:SetSize(100,100); CharLow:SetPoint('CENTER'); CharLow:EnableKeyboard(true)
        CharLow:SetScript('OnChar',function(self,text) assert(type(text)=='string'); self.receivedChars=(self.receivedChars or '')..text end)
        CharHigh=CreateFrame('Frame','CharHigh',UIParent); CharHigh:SetSize(100,100); CharHigh:SetPoint('CENTER'); CharHigh:SetFrameStrata('DIALOG'); CharHigh:EnableKeyboard(true)
        CharHigh:SetScript('OnChar',function(self,text) assert(type(text)=='string'); self.receivedChars=(self.receivedChars or '')..text end); CharHigh:Hide()
    )LUA")));
    TestTrue(TEXT("Visible keyboard frames receive native digits and Unicode"),F.Frames.DispatchCharacter('3') && F.Frames.DispatchCharacter(TCHAR(0x416)));
    TestFalse(TEXT("Control characters remain key events"),F.Frames.DispatchCharacter(TCHAR(10)));
    TestTrue(TEXT("Hidden frames cannot intercept characters"),F.Lua.ExecuteString(TEXT("assert(CharLow.receivedChars=='3Ж' and CharHigh.receivedChars==nil); CharHigh:Show()")));
    TestTrue(TEXT("The top keyboard frame captures characters exclusively"),F.Frames.DispatchCharacter('7') && F.Lua.ExecuteString(TEXT("assert(CharHigh.receivedChars=='7' and CharLow.receivedChars=='3Ж'); CharHigh:EnableKeyboard(false); CharLow:Hide()")));
    TestFalse(TEXT("No character handler consumes input when disabled or hidden"),F.Frames.DispatchCharacter('1'));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudTextureAnimations,"WowUnreal.Hud.TextureAnimations",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudTextureAnimations::RunTest(const FString&)
{
    FHudFixture F;
    const char* Xml=R"XML(<Ui><Texture name="SparkleTemplate" virtual="true"><Size x="13" y="13"/>
      <Animations><AnimationGroup name="$parentMotion" parentKey="motion"><Translation name="$parentTranslate" parentKey="translate" duration="1"/></AnimationGroup></Animations></Texture>
      <Frame name="AnimationOwner"><Size x="200" y="200"/><Anchors><Anchor point="CENTER"/></Anchors>
       <Layers><Layer><Texture name="$parentGlow" parentKey="glow"><Size x="50" y="50"/><Anchors><Anchor point="CENTER"/></Anchors><Color a="0"/>
        <Animations><AnimationGroup name="$parentPulse" parentKey="pulse"><Alpha duration="1" change="1"/><Scripts><OnFinished>self:GetParent():Hide(); self.finished=true;</OnFinished></Scripts></AnimationGroup></Animations>
       </Texture></Layer></Layers></Frame></Ui>)XML";
    TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Xml),FCStringAnsi::Strlen(Xml));
    const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("TextureAnimations"));
    for(const auto& D:Parsed) if(D.Type==FWowXmlDirective::EType::Texture) F.Frames.LoadTextureDefinition(D.TextureDef,D.TextureParent); else F.Frames.CreateFrame(D.FrameDef);
    TestTrue(TEXT("Animation groups retain canonical parents and XML keys"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(AnimationOwner.glow.pulse:GetParent()==AnimationOwner.glow)
        assert(AnimationOwnerGlowPulse==AnimationOwner.glow.pulse)
        AnimationOwner.glow:Show(); AnimationOwner.glow.pulse:Play()
        Particle=AnimationOwner:CreateTexture('Particle','OVERLAY','SparkleTemplate'); Particle:SetPoint('CENTER',AnimationOwner,'CENTER',10,20)
        assert(Particle.motion.translate:GetParent()==Particle.motion)
        Particle.motion.translate:SetOffset(40,60); Particle.motion.translate:Play()
        AnimationButton=CreateFrame('Button','AnimationButton',AnimationOwner); AnimationButton:SetSize(40,40); AnimationButton:SetPoint('CENTER')
        AnimationButton:SetScript('OnEnter',function(self) self.enters=(self.enters or 0)+1 end)
        AnimationButton:SetScript('OnLeave',function(self) self.leaves=(self.leaves or 0)+1 end)
    )LUA")));
    F.Frames.RefreshLayout(); const auto R=F.Frames.GetLayoutRect(F.Frames.FindFrame(TEXT("AnimationButton"))); const float S=F.Frames.GetScreenScale();
    F.Frames.UpdateMouseHover((R.X+R.Z*.5f)*S,(R.Y+R.W*.5f)*S);
    F.Events.TickOnUpdate(.5f);
    F.Frames.UpdateMouseHover((R.X+R.Z*.5f)*S,(R.Y+R.W*.5f)*S);
    TestTrue(TEXT("Alpha and translation progress using elapsed time"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(math.abs(AnimationOwner.glow:GetAlpha()-.5)<.001 and Particle.motion:IsPlaying())
        assert(AnimationButton.enters==1 and not AnimationButton.leaves)
        local _,_,_,x,y=Particle:GetPoint(); assert(math.abs(x-30)<.001 and math.abs(y-50)<.001)
    )LUA")));
    F.Events.TickOnUpdate(.6f);
    TestTrue(TEXT("Completion restores base transform and invokes the texture handler"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(not AnimationOwner.glow:IsShown() and AnimationOwner.glow.pulse.finished)
        assert(not Particle.motion:IsPlaying()); local _,_,_,x,y=Particle:GetPoint(); assert(x==10 and y==20)
        local g=Particle:CreateAnimationGroup(); local a=g:CreateAnimation('Translation'); a:SetOffset(10,0); a:SetDuration(1)
        g:SetScript('OnStop',function(self) self.stopped=true end); g:Play(); g:Pause(); assert(g:IsPaused()); g:Play(); g:Stop(); assert(g.stopped and g:IsStopped()); g:Play(); Particle:StopAnimating(); assert(g:IsStopped() and select(2,Particle:GetAnimationGroups())==g)
    )LUA")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudGlyphSockets,"WowUnreal.Hud.GlyphSockets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudGlyphSockets::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& D=FDbcStore::Get(); if(!D.IsLoaded()) D.LoadAll(Mpq); if(!D.IsDeferredLoaded()) D.LoadDeferred(Mpq);
    const auto* Use=D.Spells().GetById(58403); if(!TestNotNull(TEXT("Warrior glyph item use spell exists"),Use)) return false;
    uint32 Id=0; for(int32 I=0;I<3;++I) if(Use->Effect[I]==74) Id=Use->EffectMiscValue[I];
    const auto* Glyph=D.Glyph(Id); if(!TestNotNull(TEXT("Use spell resolves real glyph metadata"),Glyph)) return false;
    TestTrue(TEXT("Glyph formulas resolve after spell tokens"),!D.Spells().Description(Glyph->Spell,20).Contains(TEXT("$")));
    TestTrue(TEXT("Canonical major and minor slot metadata is loaded"),D.GlyphSlot(21) && D.GlyphSlot(21)->Type==0 && D.GlyphSlot(22) && D.GlyphSlot(22)->Type==1 && Glyph->Type==0);
    FHudFixture F; TStrongObjectPtr<UWowConnectionManager> Connection(NewObject<UWowConnectionManager>()); auto& H=Connection->PacketHandler; auto& E=H.EntityManager; E.LocalPlayerGuid=1;
    auto& P=E.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER); P.SetField(UnitField::BYTES_0,(1u<<8)|1u); P.SetField(UnitField::LEVEL,20); P.SetField(PlayerField::GLYPHS_ENABLED,3);
    for(int32 I=0;I<6;++I) P.SetField(PlayerField::GLYPH_SLOTS_1+I,21+I);
    H.TalentGroups.SetNum(2); H.HasTalentInfo=true; H.TalentGroups[0].Glyphs[0]=Id;
    F.Context.ConnectionManager=Connection.Get(); F.Context.EntityManager=&E;
    TestTrue(TEXT("Socket tuples use authoritative group glyphs and player unlock bits"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(GetNumGlyphSockets()==6)
        local enabled,kind,spell,icon=GetGlyphSocketInfo(1); assert(enabled==1 and kind==1 and spell and icon)
        local enabled2,kind2,spell2=GetGlyphSocketInfo(2); assert(enabled2==1 and kind2==2 and spell2==nil)
        local enabled3,kind3=GetGlyphSocketInfo(3); assert(enabled3==nil and kind3==2)
        assert(select(3,GetGlyphSocketInfo(1,2))==nil and GetGlyphLink(1):find('glyph:21:') and GetGlyphLink(2)=='')
        assert(GetGlyphSocketInfo(0)==nil and GetGlyphSocketInfo(7)==nil and not SpellIsTargeting() and not GlyphMatchesSocket(1))
        assert(not PlaceGlyphInSocket(1)); SpellStopTargeting(); RemoveGlyphFromSocket(1)
        GlyphTip=CreateFrame('GameTooltip','GlyphTip',UIParent); GlyphTip:SetOwner(UIParent,'ANCHOR_RIGHT'); GlyphTip:SetGlyph(1)
    )LUA")));
    const auto* Tip=F.Frames.GetTooltipState(F.Frames.FindFrame(TEXT("GlyphTip")));
    TestTrue(TEXT("Glyph tooltip uses the actual glyph spell"),Tip && Tip->SpellId==Glyph->Spell && Tip->Lines.Num()>=3);
    TestEqual(TEXT("Removal requests wait for the server rather than clearing cache"),H.TalentGroups[0].Glyphs[0],uint16(Id));
    TestTrue(TEXT("Locked glyph slots have a complete multiline tooltip"),F.Lua.ExecuteString(TEXT("GlyphTip:SetGlyph(4)")));
    F.Frames.RefreshLayout(); const auto Rect=F.Frames.GetLayoutRect(F.Frames.FindFrame(TEXT("GlyphTip")));
    const auto* Last=F.Frames.GetNamedFontString(TEXT("GlyphTipTextLeft3"));
    const auto* Slot=Last?Cast<UCanvasPanelSlot>(Last->Slot):nullptr;
    TestTrue(TEXT("The final wrapped line fits inside the tooltip border"),Last && Slot && Slot->GetPosition().Y+Last->GetDesiredSize().Y<=(Rect.W-4)*F.Frames.GetUIScale());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudTextureStyles,"WowUnreal.Hud.TextureStyles",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudTextureStyles::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    FHudFixture F; F.Frames.SetMpqManager(&Mpq); FWowAssetCache Cache; F.Frames.SetAssetCache(&Cache);
    TestTrue(TEXT("Stretched and naturally sized texture fixtures use public APIs"),F.Lua.ExecuteString(TEXT(R"LUA(
        StretchOwner=CreateFrame('Frame','StretchOwner',UIParent); StretchOwner:SetSize(37,37); StretchOwner:SetPoint('CENTER')
        StretchIcon=StretchOwner:CreateTexture('StretchIcon'); StretchIcon:SetAllPoints(StretchOwner)
        NaturalIconFixture=StretchOwner:CreateTexture('NaturalIconFixture'); NaturalIconFixture:SetPoint('CENTER')
    )LUA")));
    F.Frames.RefreshLayout(); const auto* Stretch=F.Frames.GetNamedTexture(TEXT("StretchIcon"));
    const auto* StretchSlot=Stretch?Cast<UCanvasPanelSlot>(Stretch->Slot):nullptr;
    if(!TestNotNull(TEXT("Stretch region has a canvas slot"),StretchSlot)) return false;
    const auto Offsets=StretchSlot->GetOffsets(); const auto Anchors=StretchSlot->GetAnchors();
    TestTrue(TEXT("Changing an image does not replace edge offsets with its file dimensions"),F.Lua.ExecuteString(TEXT(R"LUA(
        StretchIcon:SetTexture('Interface\\Icons\\Ability_Kick'); NaturalIconFixture:SetTexture('Interface\\Icons\\Ability_Kick')
    )LUA")));
    TestTrue(TEXT("Stretched slot margins and anchor fractions survive texture assignment"),StretchSlot->GetOffsets()==Offsets && StretchSlot->GetAnchors()==Anchors);
    F.Frames.RefreshLayout();
    TestTrue(TEXT("Natural images still take their file size and stretched images keep their owner size"),F.Lua.ExecuteString(TEXT("assert(StretchIcon:GetWidth()==37 and StretchIcon:GetHeight()==37 and NaturalIconFixture:GetWidth()==64 and NaturalIconFixture:GetHeight()==64)")));
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
