#if WITH_DEV_AUTOMATION_TESTS && __has_include("lua.h")
#include "Misc/AutomationTest.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowLuaVM.h"
#include "WowFrameXmlParser.h"
#include "WowSavedVariables.h"
#include "../LuaApi/LuaApiRegistry.h"
#include "Components/CanvasPanel.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"

namespace
{
struct FPlacedFixture
{
    FWowLuaVM Lua;
    FWowFrameManager Frames;
    FWowEventSystem Events;
    FWowLuaContext Context;
    TStrongObjectPtr<UCanvasPanel> Canvas{NewObject<UCanvasPanel>()};
    FPlacedFixture()
    {
        Lua.Initialize(); Context.FrameManager=&Frames; Context.EventSystem=&Events;
        WowLuaApi::SetContext(Lua.GetState(),&Context);
        Events.SetLuaVM(&Lua); Events.SetFrameManager(&Frames); Frames.SetEventSystem(&Events); Frames.Initialize(Canvas.Get());
        Events.CreateFrameObject(Frames.FindFrame(TEXT("UIParent")),TEXT("UIParent"));
    }
    ~FPlacedFixture() { WowLuaApi::ClearContext(Lua.GetState()); Lua.Shutdown(); }
    bool Create()
    {
        return Lua.ExecuteString(TEXT(R"LUA(
            Window=CreateFrame('Frame','PlacedWindow',UIParent)
            Window:SetSize(160,80); Window:SetPoint('TOPLEFT',UIParent,'TOPLEFT',80,-60)
            Window:SetMovable(true); Window:SetResizable(true); Window:EnableMouse(true)
            Window:RegisterForDrag('LeftButton')
            Window:SetScript('OnDragStart',function(s) s:StartMoving() end)
            Window:SetScript('OnDragStop',function(s) s:StopMovingOrSizing() end)
            Child=CreateFrame('Frame','PlacedChild',Window); Child:SetScale(.5)
            Child:SetSize(20,10); Child:SetPoint('TOPLEFT',Window,'TOPLEFT',5,-5)
            Child:SetMovable(true); Child:SetResizable(true)
        )LUA"));
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUserPlacedApi,"WowUnreal.UI.UserPlaced.ApiAndMovement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FUserPlacedApi::RunTest(const FString&)
{
    FPlacedFixture F;
    if(!TestTrue(TEXT("Create a draggable frame through Lua"),F.Create())) return false;
    TestTrue(TEXT("12340 flags, validation and StringToBOOL conversions"),F.Lua.ExecuteString(TEXT(R"LUA(
        assert(Window:IsUserPlaced()==nil and Window:GetDontSavePosition()==nil)
        assert(not pcall(function() UIParent:SetUserPlaced(true) end))
        Window:SetUserPlaced(); assert(Window:IsUserPlaced()==1)
        Window:SetUserPlaced(nil); assert(Window:IsUserPlaced()==nil)
        for _,v in ipairs({false,0,.5,'false','0','OFF','DiSaBlEd'}) do Window:SetUserPlaced(v); assert(Window:IsUserPlaced()==nil) end
        for _,v in ipairs({true,1,-1,'yes','on','enabled',{},''}) do Window:SetUserPlaced(v); assert(Window:IsUserPlaced()==1) end
        Window:SetDontSavePosition(); assert(Window:GetDontSavePosition()==1 and Window:IsUserPlaced()==1)
        Window:SetDontSavePosition(false); Window:SetUserPlaced(false)
    )LUA")));
    const int64 Handle=F.Frames.FindFrame(TEXT("PlacedWindow"));
    const float S=F.Frames.GetScreenScale();
    F.Frames.UpdateMouseHover(160*S,100*S);
    TestTrue(TEXT("Normal drag script starts movement"),F.Frames.DispatchDragStart(Handle,TEXT("LeftButton")));
    TestTrue(TEXT("StartMoving marks the frame immediately"),F.Lua.ExecuteString(TEXT("assert(Window:IsUserPlaced()==1 and Window:GetNumPoints()==1)")));
    F.Frames.UpdateMouseHover(220*S,140*S); F.Frames.DispatchDragStop(Handle,TEXT("LeftButton"));
    auto R=F.Frames.GetLayoutRect(Handle);
    TestTrue(TEXT("Pointer delta produces exact logical coordinates"),R.Equals(FVector4(140,100,160,80),.01));
    TestTrue(TEXT("Stop uses matching nearest screen anchors"),F.Lua.ExecuteString(TEXT("local p,r,q,x,y=Window:GetPoint(); assert(p=='TOPLEFT' and r==UIParent and q==p and math.abs(x-140)<.01 and math.abs(y+100)<.01)")));
    TestTrue(TEXT("Disabling persistence does not move the window"),F.Lua.ExecuteString(TEXT("Window:SetUserPlaced(false); assert(Window:IsUserPlaced()==nil)")) && R.Equals(F.Frames.GetLayoutRect(Handle),.01));
    F.Frames.StartFrameMovement(Handle); F.Frames.SetFrameVisible(Handle,false);
    F.Frames.UpdateMouseHover(500*S,500*S); F.Frames.SetFrameVisible(Handle,true);
    TestTrue(TEXT("Hide stops movement before subsequent pointer input"),R.Equals(F.Frames.GetLayoutRect(Handle),.01));
    TestTrue(TEXT("StartSizing also marks user placement"),F.Lua.ExecuteString(TEXT("Window:SetUserPlaced(false); Window:StartSizing('BOTTOMRIGHT'); assert(Window:IsUserPlaced()==1)")));
    F.Frames.UpdateMouseHover(520*S,510*S); F.Frames.StopFrameMovement(Handle);
    R=F.Frames.GetLayoutRect(Handle);
    TestTrue(TEXT("Sizing keeps top-left and updates logical dimensions"),R.Equals(FVector4(140,100,180,90),.01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUserPlacedPersistence,"WowUnreal.UI.UserPlaced.CharacterPersistence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FUserPlacedPersistence::RunTest(const FString&)
{
    const FString OriginalCharacterDirectory=FWowSavedVariables::GetCharacterDirectory();
    const FString Root=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Automation"),FGuid::NewGuid().ToString());
    const FString A=Root/TEXT("A.txt"),B=Root/TEXT("B.txt");
    {
        FPlacedFixture F; if(!F.Create()) return false;
        F.Frames.LoadUserPlacedFrames(A);
        TestTrue(TEXT("Set explicit placement, size, frame level and exclusion cases"),F.Lua.ExecuteString(TEXT(R"LUA(
            Window:ClearAllPoints(); Window:SetPoint('TOPRIGHT',UIParent,'TOPRIGHT',-60,-100)
            Window:SetSize(180,90); Window:SetFrameLevel(7); Window:SetUserPlaced(true)
            Child:SetUserPlaced(true)
            Excluded=CreateFrame('Frame','ExcludedWindow',UIParent); Excluded:SetSize(10,10); Excluded:SetPoint('CENTER'); Excluded:SetMovable(true); Excluded:SetUserPlaced(true); Excluded:SetDontSavePosition(true)
            Anonymous=CreateFrame('Frame',nil,UIParent); Anonymous:SetSize(10,10); Anonymous:SetPoint('CENTER'); Anonymous:SetMovable(true); Anonymous:SetUserPlaced(true)
            Fixed=CreateFrame('Frame','FixedWindow',UIParent); Fixed:SetSize(10,10); Fixed:SetPoint('CENTER'); Fixed:SetMovable(true); Fixed:SetUserPlaced(true); Fixed:SetMovable(false)
            Unanchored=CreateFrame('Frame','UnanchoredWindow',UIParent); Unanchored:SetSize(10,10); Unanchored:SetPoint('CENTER'); Unanchored:SetMovable(true); Unanchored:SetUserPlaced(true)
            local oldLeft=Unanchored:GetLeft(); Unanchored:ClearAllPoints()
        )LUA")));
        // Save only this fixture, without flushing unrelated registered addons
        // into the account identity left by another automation test.
        F.Frames.SaveUserPlacedFrames();
        FString Text; TestTrue(TEXT("Layout file was written"),FFileHelper::LoadFileToString(Text,*A));
        TestTrue(TEXT("Only named movable/resizable placements enter the original plain-text format"),Text.Contains(TEXT("Frame: PlacedWindow\nFrameLevel: 7\nAnchor: TOPRIGHT\nX: -60\nY: -100\nW: 180\nH: 90")) && !Text.Contains(TEXT("ExcludedWindow")) && !Text.Contains(TEXT("__WowAnonymous")) && !Text.Contains(TEXT("FixedWindow")) && !Text.Contains(TEXT("UnanchoredWindow")));
    }
    {
        FPlacedFixture F; if(!F.Create()) return false;
        F.Frames.LoadUserPlacedFrames(B);
        TestTrue(TEXT("A different character has no placement"),F.Lua.ExecuteString(TEXT("assert(Window:IsUserPlaced()==nil and Window:GetWidth()==160)")));
        F.Frames.LoadUserPlacedFrames(A);
        TestTrue(TEXT("A fresh VM restores root anchor, dimensions, level and flag"),F.Lua.ExecuteString(TEXT("local p,r,q,x,y=Window:GetPoint(); assert(p=='TOPRIGHT' and r==UIParent and q==p and x==-60 and y==-100 and Window:GetWidth()==180 and Window:GetHeight()==90 and Window:GetFrameLevel()==7 and Window:IsUserPlaced()==1)")));
        TestTrue(TEXT("Named children also restore against the screen while retaining inherited scale"),F.Lua.ExecuteString(TEXT("local p,r=Child:GetPoint(); assert(r==UIParent and Child:IsUserPlaced()==1 and Child:GetScale()==.5 and Child:GetWidth()==20 and Child:GetHeight()==10)")));
        TestTrue(TEXT("Reset opts out without changing current placement"),F.Lua.ExecuteString(TEXT("Window:SetUserPlaced(false)")));
        F.Frames.SaveUserPlacedFrames();
    }
    {
        FPlacedFixture F; if(!F.Create()) return false;
        F.Frames.LoadUserPlacedFrames(A);
        TestTrue(TEXT("After saving a reset a fresh VM keeps addon defaults"),F.Lua.ExecuteString(TEXT("local p,r,q,x,y=Window:GetPoint(); assert(p=='TOPLEFT' and x==80 and y==-60 and Window:GetWidth()==160 and Window:IsUserPlaced()==nil)")));
        // XML dontSavePosition must survive inheritance as well as Lua flags.
        const char* Xml="<Ui><Frame name='NoSaveTemplate' virtual='true' movable='true' dontSavePosition='true'/><Frame name='NoSaveInstance' inherits='NoSaveTemplate'/><Frame name='SaveOverride' inherits='NoSaveTemplate' dontSavePosition='false'/></Ui>";
        TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Xml),FCStringAnsi::Strlen(Xml));
        const auto Parsed=FWowFrameXmlParser::ParseXml(Bytes,TEXT("UserPlacedFixture"));
        for(const auto& D:Parsed) F.Frames.CreateFrame(D.FrameDef);
        const auto* Def=F.Frames.GetFrameDef(F.Frames.FindFrame(TEXT("NoSaveInstance")));
        TestTrue(TEXT("XML persistence opt-out is inherited"),Def && Def->bDontSavePosition);
        Def=F.Frames.GetFrameDef(F.Frames.FindFrame(TEXT("SaveOverride")));
        TestTrue(TEXT("An explicit XML false overrides the template opt-out"),Def && !Def->bDontSavePosition);
        // The original loader restores positions for movable frames, dimensions
        // for resizable frames, independently, even if both were saved together.
        FFileHelper::SaveStringToFile(TEXT("Version: 1\nFrame: PlacedWindow\nAnchor: TOPRIGHT\nX: -20\nY: -30\nW: 222\nH: 111\n"),*A);
        F.Lua.ExecuteString(TEXT("Window:SetMovable(false)")); F.Frames.LoadUserPlacedFrames(A);
        TestTrue(TEXT("Resizable-only restoration leaves existing anchors"),F.Lua.ExecuteString(TEXT("local p=Window:GetPoint(); assert(p=='TOPLEFT' and Window:GetWidth()==222 and Window:GetHeight()==111 and Window:IsUserPlaced()==1)")));
        F.Lua.ExecuteString(TEXT("Window:SetMovable(true); Window:SetResizable(false); Window:SetSize(160,80)")); F.Frames.LoadUserPlacedFrames(A);
        TestTrue(TEXT("Movable-only restoration leaves addon dimensions"),F.Lua.ExecuteString(TEXT("local p=Window:GetPoint(); assert(p=='TOPRIGHT' and Window:GetWidth()==160 and Window:GetHeight()==80)")));
    }
    IFileManager::Get().Delete(*A); IFileManager::Get().Delete(*B); IFileManager::Get().DeleteDirectory(*Root);
    FWowSavedVariables::SetIdentity(TEXT("LayoutAutomation"),TEXT("RealmOne"),TEXT("First")); const FString First=FWowSavedVariables::GetCharacterDirectory();
    FWowSavedVariables::SetIdentity(TEXT("LayoutAutomation"),TEXT("RealmOne"),TEXT("Second")); const FString Second=FWowSavedVariables::GetCharacterDirectory();
    FWowSavedVariables::SetIdentity(TEXT("LayoutAutomation"),TEXT("RealmTwo"),TEXT("First")); const FString OtherRealm=FWowSavedVariables::GetCharacterDirectory();
    FWowSavedVariables::SetIdentity(TEXT("OtherLayoutAutomation"),TEXT("RealmOne"),TEXT("First")); const FString OtherAccount=FWowSavedVariables::GetCharacterDirectory();
    TestTrue(TEXT("Account, realm and character each partition local placement"),First!=Second && First!=OtherRealm && First!=OtherAccount);
    const FString OriginalRealmDirectory=FPaths::GetPath(OriginalCharacterDirectory);
    FWowSavedVariables::SetIdentity(FPaths::GetCleanFilename(FPaths::GetPath(OriginalRealmDirectory)),FPaths::GetCleanFilename(OriginalRealmDirectory),FPaths::GetCleanFilename(OriginalCharacterDirectory));
    return true;
}
#endif
