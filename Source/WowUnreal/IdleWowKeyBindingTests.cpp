#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerInput.h"
#include "Components/InputComponent.h"
#include "Components/CanvasPanel.h"
#include "WowGameplayController.h"
#include "WowUIManager.h"
#include "WowKeyBindings.h"
#include "WowConnectionManager.h"
#include "Net/WowWorldSocket.h"
#include "WowOpcodes.h"
#include "../WowUI/Private/LuaApi/LuaApiRegistry.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowNativeBindingsTest,"WowUnreal.Input.NativeKeyBindings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowNativeBindingsTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Input test world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
    TStrongObjectPtr<UWowUIManager> UI(NewObject<UWowUIManager>(Instance.Get()));
    auto* Controller=World->SpawnActor<AWowGameplayController>();
    Controller->UIManager=UI.Get();
    Controller->PlayerInput=NewObject<UPlayerInput>(Controller);
    Controller->SetupInputComponent();
    Controller->ConnectionManager=NewObject<UWowConnectionManager>(Controller);
    auto& Connection=*Controller->ConnectionManager;
    // Data-only VM plus real APIs: this fixture never saves shared preferences.
    UI->LuaVM=MakeUnique<FWowLuaVM>(); UI->LuaVM->Initialize(false);
    WowLuaApi::RegisterAll(UI->LuaVM->GetState());
    UI->FrameManager=MakeUnique<FWowFrameManager>(); UI->EventSystem=MakeUnique<FWowEventSystem>();
    UI->UIContext=new FWowLuaContext();
    UI->UIContext->ConnectionManager=&Connection; UI->UIContext->EntityManager=&Connection.PacketHandler.EntityManager;
    UI->UIContext->FrameManager=UI->FrameManager.Get(); UI->UIContext->EventSystem=UI->EventSystem.Get();
    UI->UIContext->KeyDown=[Controller](const FKey& Key){return Controller->PressedBindingKeys.Contains(Key) || (!Key.IsModifierKey() && Controller->IsInputKeyDown(Key));};
    WowLuaApi::SetContext(UI->LuaVM->GetState(),UI->UIContext);
    UI->EventSystem->SetLuaVM(UI->LuaVM.Get()); UI->EventSystem->SetFrameManager(UI->FrameManager.Get());
    UI->FrameManager->SetEventSystem(UI->EventSystem.Get());
    TStrongObjectPtr<UCanvasPanel> Canvas(NewObject<UCanvasPanel>()); UI->FrameManager->Initialize(Canvas.Get());
    UI->EventSystem->CreateFrameObject(UI->FrameManager->FindFrame(TEXT("UIParent")),TEXT("UIParent"));
    auto& Lua=*UI->LuaVM;
    const auto Check=[&](const TCHAR* Label,const TCHAR* Code){return TestTrue(Label,Lua.ExecuteString(Code,TEXT("=NativeBindingsTest")));};
    const auto Input=[&](const FKey& Key,EInputEvent Event=IE_Pressed,float Value=1.f)
    {
        Controller->InputKey(FInputKeyEventArgs::CreateSimulated(Key,Event,Value,1));
        Controller->ProcessPlayerInput(1.f/60,false);
    };
    const auto Tap=[&](const FKey& Key){Input(Key);Input(Key,IE_Released,0);};
    Check(TEXT("Create an observable UI command"),TEXT("BookToggles=0; BOOKTYPE_SPELL='spell'; function ToggleSpellBook(book) assert(book=='spell'); BookToggles=BookToggles+1 end"));
    Tap(EKeys::P);
    Check(TEXT("The default native P binding executes exactly once"),TEXT("assert(BookToggles==1); assert(SetBinding('CTRL-F8','TOGGLESPELLBOOK'))"));
    Input(EKeys::LeftControl); Input(EKeys::F8); Input(EKeys::F8,IE_Repeat);
    Input(EKeys::LeftControl,IE_Released,0); Input(EKeys::F8,IE_Released,0);
    Check(TEXT("Routed Ctrl chord executes once and survives modifier-first release"),TEXT("assert(BookToggles==2); assert(SetBinding('ALT-CTRL-SHIFT-F9','TOGGLESPELLBOOK'))"));
    Input(EKeys::RightAlt); Input(EKeys::RightControl); Input(EKeys::RightShift); Tap(EKeys::F9);
    Input(EKeys::RightShift,IE_Released,0); Input(EKeys::RightControl,IE_Released,0); Input(EKeys::RightAlt,IE_Released,0);
    Check(TEXT("All three modifiers use the original canonical chord order"),TEXT("assert(BookToggles==3); assert(SetBinding('CTRL-P'))"));
    Input(EKeys::LeftControl); Tap(EKeys::P); Input(EKeys::LeftControl,IE_Released,0);
    Check(TEXT("An explicitly unbound chord does not execute its base key"),TEXT("assert(BookToggles==3); assert(SetBinding('BUTTON4','TOGGLESPELLBOOK')); assert(SetBinding('MOUSEWHEELUP','TOGGLESPELLBOOK'))"));
    Tap(EKeys::ThumbMouseButton); Input(EKeys::MouseScrollUp); Input(EKeys::MouseScrollUp);
    Input(EKeys::MouseWheelAxis,IE_Axis,1);
    Check(TEXT("Side button and successive wheel ticks execute supported bindings"),TEXT("assert(BookToggles==6)"));
    TestEqual(TEXT("A bound mouse wheel does not also reach camera zoom"),Controller->GetInputAnalogKeyState(EKeys::MouseWheelAxis),0.f);
    TestEqual(TEXT("Second side button uses the stock API name"),FWowKeyBindings::KeyName(EKeys::ThumbMouseButton2),FString(TEXT("BUTTON5")));
    TestEqual(TEXT("OEM punctuation keeps its assignable WoW key name"),FWowKeyBindings::KeyName(EKeys::Semicolon),FString(TEXT(";")));
    Check(TEXT("Create a UI mouse capture with bound primary buttons"),TEXT(R"LUA(
        MouseUps=0; MouseClicks=0
        MouseCapture=CreateFrame('Button','MouseCapture',UIParent)
        MouseCapture:SetScript('OnMouseDown',function(self,button) self.pressed=button end)
        MouseCapture:SetScript('OnMouseUp',function(self,button) assert(self.pressed==button); self.pressed=nil; MouseUps=MouseUps+1 end)
        MouseCapture:SetScript('OnClick',function() MouseClicks=MouseClicks+1 end)
        SetBinding('BUTTON1','TOGGLESPELLBOOK'); SetBinding('BUTTON2','TOGGLESPELLBOOK')
    )LUA"));
    const int64 MouseCapture=UI->FrameManager->FindFrame(TEXT("MouseCapture"));
    for(const bool Right:{false,true})
    {
        const FKey Button=Right?EKeys::RightMouseButton:EKeys::LeftMouseButton;
        // Seed the already-recorded UI down without requiring a viewport in
        // this unit fixture; the release uses the real native controller path.
        Controller->APlayerController::InputKey(FInputKeyEventArgs::CreateSimulated(Button,IE_Pressed,1.f));
        Controller->PlayerInput->ProcessInputStack({},1.f/60,false);
        UI->FrameManager->DispatchMouseDown(MouseCapture,Right?TEXT("RightButton"):TEXT("LeftButton"));
        auto& Captured=Right?Controller->PressedRightMouseFrameHandle:Controller->PressedLeftMouseFrameHandle;
        Captured=MouseCapture;
        Controller->GameplayProbePointer=FVector2D(-1,-1);
        Input(Button,IE_Released,0);
        TestEqual(TEXT("Leaving a UI frame releases its original primary-button capture"),Captured,int64(-1));
    }
    Check(TEXT("UI-origin releases neither click the frame nor execute its world binding"),TEXT("assert(MouseUps==2 and MouseClicks==0 and BookToggles==6); SetBinding('BUTTON1'); SetBinding('BUTTON2'); MouseCapture:Hide()"));
    Controller->GameplayProbePointer.Reset();
    Check(TEXT("Bind two physical keys to the same movement action"),TEXT("SetBinding('F6','MOVEFORWARD'); SetBinding('F7','MOVEFORWARD')"));
    Input(EKeys::F6); Input(EKeys::F7); Input(EKeys::F6,IE_Released,0);
    TestTrue(TEXT("Releasing one alias cannot release another held movement key"),Controller->IsInputKeyDown(EKeys::W));
    Check(TEXT("Open a keyboard capture while a remapped movement key is held"),TEXT("BindingCapture=CreateFrame('Frame','BindingCapture',UIParent); BindingCapture:EnableKeyboard(true); BindingCapture:SetScript('OnKeyDown',function() end); BindingCapture:Show(); SetBinding('F7','MOVEBACKWARD')"));
    Tap(EKeys::W);
    TestTrue(TEXT("Key-up from captured text cannot release another held movement alias"),Controller->IsInputKeyDown(EKeys::W));
    Input(EKeys::F7,IE_Released,0);
    TestFalse(TEXT("Release reaches the original movement action after focus and binding changes"),Controller->IsInputKeyDown(EKeys::W));
    Input(EKeys::F7); Input(EKeys::F7,IE_Released,0);
    TestFalse(TEXT("Keyboard capture prevents new movement bindings"),Controller->IsInputKeyDown(EKeys::S));
    Check(TEXT("Dismiss the synthetic capture"),TEXT("BindingCapture:Hide()"));
    Input(EKeys::F6); Controller->FlushPressedKeys();
    TestTrue(TEXT("Focus-loss flush clears physical and remapped input together"),!Controller->IsInputKeyDown(EKeys::W) && Controller->HeldBindingKeys.IsEmpty() && Controller->PressedBindingKeys.IsEmpty());
    Check(TEXT("A wheel can pulse a movement binding without latching it"),TEXT("SetBinding('MOUSEWHEELDOWN','MOVEFORWARD')"));
    Input(EKeys::MouseScrollDown);
    Controller->ProcessPlayerInput(1.f/60,false);
    TestTrue(TEXT("The wheel's virtual movement key is released after one input frame"),!Controller->IsInputKeyDown(EKeys::W) && Controller->PulsedBindingKeys.IsEmpty());

    // Real native key 2 -> stance bar button 2 -> spell 78 -> wire packet.
    // A queue-only socket never calls Connect and has no socket/thread.
    Connection.State=EWowSessionState::WorldInGame; Connection.TargetGuid=2;
    Connection.WorldSocket=MakeShared<FWowWorldSocket,ESPMode::ThreadSafe>();
    auto& Handler=Connection.PacketHandler; Handler.EntityManager.LocalPlayerGuid=1;
    auto& Player=Handler.EntityManager.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT|WowTypeMask::PLAYER);
    Player.SetField(UnitField::BYTES_0,3|(1<<8)|(1<<24)); Player.SetField(UnitField::BYTES_2,17u<<24);
    Player.SetField(UnitField::HEALTH,70); Player.SetField(UnitField::POWER2,200);
    Handler.ActionButtons.SetNumZeroed(144); Handler.ActionButtons[73]=78;
    uint32 SentSpell=0; Connection.OnLocalCastSent.BindLambda([&](uint32 Spell,uint8){SentSpell=Spell;});
    Tap(EKeys::Two);
    TArray<uint8> Packet;
    if(TestTrue(TEXT("Native action 2 submits the Heroic Strike request"),Connection.WorldSocket->SendQueue.Dequeue(Packet) && Packet.Num()>=16))
    {
        uint32 Opcode=0; FMemory::Memcpy(&Opcode,Packet.GetData()+2,4);
        FPacketReader Reader(Packet.GetData()+6,Packet.Num()-6); Reader.ReadU8(); const uint32 Spell=Reader.ReadU32(); Reader.ReadU8();
        TestTrue(TEXT("Heroic Strike keeps its actual opcode, spell and unit target"),Opcode==WowOpcode::CMSG_CAST_SPELL && Spell==78 && SentSpell==78 && Reader.ReadU32()==2 && Reader.ReadPackedGuid()==2);
        TestTrue(TEXT("One key activation creates one cast request"),Connection.WorldSocket->SendQueue.IsEmpty());
    }
    Connection.State=EWowSessionState::Disconnected; Controller->FlushPressedKeys(); UI->Deinitialize();
    Controller->Destroy(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
