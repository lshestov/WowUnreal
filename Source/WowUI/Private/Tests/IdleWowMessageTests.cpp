#include "Misc/AutomationTest.h"
#include "WowFrameManager.h"
#include "WowFrameXmlParser.h"
#include "WowEventSystem.h"
#include "WowLuaVM.h"
#include "WowTextBlock.h"
#include "../LuaApi/LuaApiRegistry.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_DEV_AUTOMATION_TESTS && __has_include("lua.h")
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudTimedMessages,"WowUnreal.Hud.TimedMessages",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudTimedMessages::RunTest(const FString&)
{
    FWowLuaVM Lua; Lua.Initialize(); FWowFrameManager Frames; FWowEventSystem Events; FWowLuaContext Context;
    TStrongObjectPtr<UCanvasPanel> Canvas(NewObject<UCanvasPanel>()); Context.FrameManager=&Frames; Context.EventSystem=&Events;
    WowLuaApi::SetContext(Lua.GetState(),&Context); Events.SetLuaVM(&Lua); Events.SetFrameManager(&Frames); Frames.SetEventSystem(&Events); Frames.Initialize(Canvas.Get());
    Events.CreateFrameObject(Frames.FindFrame(TEXT("UIParent")),TEXT("UIParent"));
    const FString Xml=TEXT("<Ui><MessageFrame name='NoticeTemplate' virtual='true' displayDuration='2' fadeDuration='2' insertMode='TOP'><Size x='400' y='100'/><FontString inherits='NoticeFont' justifyH='CENTER'/></MessageFrame><MessageFrame name='Notice' inherits='NoticeTemplate' parent='UIParent'><Anchors><Anchor point='CENTER'/></Anchors></MessageFrame></Ui>");
    const FTCHARToUTF8 Utf8(*Xml); TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length()); const auto Definitions=FWowFrameXmlParser::ParseXml(Bytes,TEXT("TimedMessages")); if(!TestEqual(TEXT("Message template and instance parse"),Definitions.Num(),2)) return false;
    Frames.CreateFrame(Definitions[0].FrameDef); const int64 Notice=Frames.CreateFrame(Definitions[1].FrameDef); Frames.RefreshLayout();
    TestTrue(TEXT("Stock message XML settings survive template inheritance"),Lua.ExecuteString(TEXT("assert(Notice:GetFading() and Notice:GetTimeVisible()==2 and Notice:GetFadeDuration()==2 and Notice:GetInsertMode()=='TOP'); Notice:AddMessage('first',1,1,0); Notice:AddMessage('second',1,0,0)")));
    auto& State=Frames.MessageFrames[Notice]; const double At=State.Lines.Last().At;
    if(!TestTrue(TEXT("The message canvas has room for two visible rows"),State.Widgets.Num()>=2)) {const auto R=Frames.GetLayoutRect(Notice); AddError(FString::Printf(TEXT("Notice layout %.1f %.1f %.1f %.1f, definition %.1f %.1f"),R.X,R.Y,R.Z,R.W,Frames.GetFrameDef(Notice)->Width,Frames.GetFrameDef(Notice)->Height)); WowLuaApi::ClearContext(Lua.GetState()); Lua.Shutdown(); return false;}
    const auto* Label=Cast<UWowTextBlock>(State.Widgets[0].Get());
    TestTrue(TEXT("Message text keeps its XML font and centered alignment"),Frames.GetFrameDef(Notice)->MessageFont==TEXT("NoticeFont") && Label && Label->GetHorizontalJustification()==ETextJustify::Center);
    const auto* Top=Cast<UCanvasPanelSlot>(State.Widgets[0]->Slot); const auto* Below=Cast<UCanvasPanelSlot>(State.Widgets[1]->Slot);
    TestTrue(TEXT("TOP insertion puts the newest message above the previous one"),Top && Below && Top->GetPosition().Y==0 && Below->GetPosition().Y>0);
    Frames.TickMessages(At+1); TestEqual(TEXT("Messages remain opaque for their visible duration"),State.Widgets[0]->GetRenderOpacity(),1.f);
    Frames.TickMessages(At+3); TestTrue(TEXT("Fade duration produces intermediate opacity"),FMath::IsNearlyEqual(State.Widgets[0]->GetRenderOpacity(),.5f,.01f));
    Frames.TickMessages(At+4.1); TestTrue(TEXT("Expired transient messages are removed and their widgets hidden"),State.Lines.IsEmpty() && State.Widgets[0]->GetVisibility()==ESlateVisibility::Collapsed);
    TestTrue(TEXT("Duration and fading APIs accept normal WoW numeric/boolean arguments"),Lua.ExecuteString(TEXT("Notice:SetTimeVisible(1); Notice:SetFadeDuration(0); Notice:SetFading(false); Notice:AddMessage('persistent'); assert(not Notice:GetFading() and Notice:GetTimeVisible()==1 and Notice:GetFadeDuration()==0)")));
    Frames.TickMessages(FPlatformTime::Seconds()+100); TestEqual(TEXT("Disabling fading preserves messages"),State.Lines.Num(),1);
    TestTrue(TEXT("Create a fading scrolling history"),Lua.ExecuteString(TEXT("History=CreateFrame('ScrollingMessageFrame','History',UIParent); History:SetSize(400,100); History:SetPoint('CENTER'); History:SetTimeVisible(1); History:SetFadeDuration(1); History:SetFading(true); History:AddMessage('older'); History:AddMessage('newer')")));
    const int64 History=Frames.FindFrame(TEXT("History")); auto& Chat=Frames.MessageFrames[History]; const double ChatAt=Chat.Lines.Last().At;
    Frames.TickMessages(ChatAt+5); TestTrue(TEXT("Faded chat keeps its scrollable history"),Chat.Lines.Num()==2 && Chat.Widgets[0]->GetRenderOpacity()==0);
    Frames.ScrollMessages(History,1,true); Frames.TickMessages(ChatAt+5); TestEqual(TEXT("Scrolling back reveals old messages"),Chat.Widgets[0]->GetRenderOpacity(),1.f);
    TestTrue(TEXT("Clearing history resets the scroll offset for future messages"),Lua.ExecuteString(TEXT("History:Clear(); History:AddMessage('after clear'); assert(History:GetNumMessages()==1)")) && Chat.Offset==0);
    WowLuaApi::ClearContext(Lua.GetState()); Lua.Shutdown(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudMessageReflow,"WowUnreal.Hud.MessageResizeAndWrapping",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FHudMessageReflow::RunTest(const FString&)
{
    FWowFrameManager Frames;
    TStrongObjectPtr<UCanvasPanel> Canvas(NewObject<UCanvasPanel>());
    Frames.Initialize(Canvas.Get());
    FWowFrameDef Def; Def.Name=TEXT("ResizeChat"); Def.Parent=TEXT("UIParent");
    Def.Type=EWowFrameType::ScrollingMessageFrame; Def.Width=440; Def.Height=180;
    Def.Anchors.Add({EWowAnchorPoint::BOTTOMLEFT,TEXT("UIParent"),EWowAnchorPoint::BOTTOMLEFT,10,40});
    const int64 Handle=Frames.CreateFrame(Def); Frames.RefreshLayout();
    Frames.AddMessage(Handle,TEXT("Older message remains immediately above the next wrapped message."),FLinearColor::White);
    Frames.AddMessage(Handle,TEXT("Meet me at Northshire Abbey near the front door. Bring |cffffff00a friend|r and wait by the entrance before entering the building together."),FLinearColor::White);
    auto& State=Frames.MessageFrames[Handle];
    if(!TestTrue(TEXT("Two chat rows were created"),State.Widgets.Num()>=2)) return false;
    auto* Newest=Cast<UCanvasPanelSlot>(State.Widgets[0]->Slot);
    auto* Older=Cast<UCanvasPanelSlot>(State.Widgets[1]->Slot);
    const float WideHeight=Newest->GetSize().Y;
    Frames.SetFrameSize(Handle,180,220); Frames.RefreshLayout();
    Frames.TickMessages(FPlatformTime::Seconds());
    TestEqual(TEXT("Resizing with fading disabled reflows existing history without a new message"),float(Newest->GetSize().X),180*Frames.GetUIScale());
    TestTrue(TEXT("Narrow rich text takes more lines"),Newest->GetSize().Y>WideHeight);
    TestTrue(TEXT("Newest message stays attached to the bottom after resizing"),FMath::IsNearlyEqual(Newest->GetPosition().Y+Newest->GetSize().Y,220*Frames.GetUIScale(),.1));
    TestTrue(TEXT("Wrapped chat rows do not overlap"),FMath::IsNearlyEqual(Older->GetPosition().Y+Older->GetSize().Y,Newest->GetPosition().Y,.1));
    Frames.SetFrameScale(Handle,.75f); Frames.RefreshLayout(); Frames.TickMessages(FPlatformTime::Seconds());
    TestEqual(TEXT("Frame scale is inherited once, not multiplied into local row width"),float(Newest->GetSize().X),180*Frames.GetUIScale());
    TestTrue(TEXT("Scaled chat keeps its local bottom edge"),FMath::IsNearlyEqual(Newest->GetPosition().Y+Newest->GetSize().Y,220*Frames.GetUIScale(),.1));
    const float Dpi=Frames.GetScreenScale()/Frames.GetUIScale();
    const auto Root=Canvas->TakeWidget(); Root->SlatePrepass(Dpi);
    for(const auto& Row:State.Widgets) if(Row.IsValid() && Row->IsVisible())
    {
        const auto* Slot=Cast<UCanvasPanelSlot>(Row->Slot);
        const float TextHeight=CastChecked<UWowTextBlock>(Row.Get())->MeasureRenderedText(Dpi).Y;
        TestTrue(TEXT("Actual Slate text fits the row after its parent's render transform"),Slot && Slot->GetSize().Y+.1f>=TextHeight);
    }
    Frames.SetFrameSize(Handle,440,180); Frames.RefreshLayout(); Frames.TickMessages(FPlatformTime::Seconds());
    TestTrue(TEXT("Expanding the frame removes cached narrow wrapping"),Newest->GetSize().Y<WideHeight+1);
    TestEqual(TEXT("Reflow preserves message history"),State.Lines.Num(),2);
    return true;
}
#endif
