#include "Misc/AutomationTest.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowLuaVM.h"
#include "../LuaApi/LuaApiRegistry.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS && __has_include("lua.h")
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudChatControls, "WowUnreal.Hud.ChatControls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHudChatControls::RunTest(const FString&)
{
    FWowLuaVM Lua;
    Lua.Initialize();
    FWowFrameManager Frames;
    FWowEventSystem Events;
    FWowLuaContext Context;
    TStrongObjectPtr<UCanvasPanel> Root(NewObject<UCanvasPanel>());
    Context.FrameManager = &Frames;
    Context.EventSystem = &Events;
    WowLuaApi::SetContext(Lua.GetState(), &Context);
    Events.SetLuaVM(&Lua);
    Events.SetFrameManager(&Frames);
    Frames.SetEventSystem(&Events);
    Frames.Initialize(Root.Get());
    Events.CreateFrameObject(Frames.FindFrame(TEXT("UIParent")), TEXT("UIParent"));

    TestTrue(TEXT("A chat frame has an adjacent child button and multiple pages of messages"), Lua.ExecuteString(TEXT(R"LUA(
        History=CreateFrame('ScrollingMessageFrame','History',UIParent)
        History:SetSize(200,90); History:SetPoint('CENTER')
        Controls=CreateFrame('Button','Controls',History)
        Controls:SetSize(32,32); Controls:SetPoint('RIGHT',History,'LEFT',-4,0)
        History:SetFading(false)
        for i=1,24 do History:AddMessage('History row '..i) end
    )LUA")));
    Frames.RefreshLayout();
    Frames.TickMessages(FPlatformTime::Seconds());
    const int64 Handle = Frames.FindFrame(TEXT("History"));
    auto& State = Frames.MessageFrames.FindOrAdd(Handle);
    auto* History = Cast<UCanvasPanel>(Frames.GetWidgetForHandle(Handle));
    TestTrue(TEXT("The parent permits stock controls outside the chat rectangle"),
        History && History->GetClipping() == EWidgetClipping::Inherit);
    TestTrue(TEXT("Only message content has viewport clipping"),
        State.Content.IsValid() && State.Content->GetClipping() == EWidgetClipping::ClipToBounds);
    TestTrue(TEXT("Message rows are inside the clipped content"),
        !State.Widgets.IsEmpty() && State.Widgets[0].IsValid() && State.Widgets[0]->GetParent() == State.Content.Get());
    const auto ButtonRect = Frames.GetLayoutRect(Frames.FindFrame(TEXT("Controls")));
    const auto HistoryRect = Frames.GetLayoutRect(Handle);
    TestTrue(TEXT("The actual control remains outside the left edge"),
        ButtonRect.X + ButtonRect.Z < HistoryRect.X && Frames.IsFrameDrawn(Frames.FindFrame(TEXT("Controls"))));
    TestTrue(TEXT("Scroll status reflects the displayed history"), Lua.ExecuteString(TEXT(R"LUA(
        assert(History:AtBottom()==1 and History:AtTop()==nil and History:GetCurrentScroll()==0)
        assert(History:GetNumLinesDisplayed()>0 and History:GetNumLinesDisplayed()<24)
        History:ScrollUp()
        assert(History:AtBottom()==nil and History:GetCurrentScroll()==1)
        History:AddMessage('New arrival while reading older messages')
        assert(History:GetCurrentScroll()==2 and History:AtBottom()==nil)
        History:ScrollDown()
        assert(History:GetCurrentScroll()==1)
        History:ScrollToTop()
        assert(History:AtTop()==1 and History:AtBottom()==nil)
        local top=History:GetCurrentScroll()
        History:ScrollUp()
        assert(History:GetCurrentScroll()==top)
        History:ScrollToBottom()
        assert(History:AtBottom()==1 and History:AtTop()==nil and History:GetCurrentScroll()==0)
        History:Clear()
        assert(History:GetNumMessages()==0 and History:GetNumLinesDisplayed()==0 and History:AtBottom()==1)
    )LUA")));
    WowLuaApi::ClearContext(Lua.GetState());
    Lua.Shutdown();
    return true;
}
#endif
