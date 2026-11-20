#include "WowGameViewportClient.h"
#include "WowGameplayController.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Widgets/SNullWidget.h"
#if PLATFORM_WINDOWS
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Misc/CoreMisc.h"
#include "Widgets/SWindow.h"
#include "Windows/WindowsHWrapper.h"
#endif

bool UWowGameViewportClient::InputChar(FViewport* InViewport,int32 ControllerId,TCHAR Character)
{
    auto* Instance=GetGameInstance(); auto* Ui=Instance?Instance->GetSubsystem<UWowUIManager>():nullptr;
    const auto* Controller=GetWorld()?Cast<AWowGameplayController>(GetWorld()->GetFirstPlayerController()):nullptr;
    if(Controller && !Controller->CanAcceptNativeCharacter()) return true;
    if(Ui && Ui->GetFrameManager() && Ui->GetFrameManager()->DispatchCharacter(Character)) return true;
    return Super::InputChar(InViewport,ControllerId,Character);
}

TOptional<TSharedRef<SWidget>> UWowGameViewportClient::MapCursor(FViewport* InViewport, const FCursorReply& CursorReply)
{
    // Slate buttons can capture the mouse and request a hardware cursor even
    // while the controller is drawing the WoW cursor above their window.
    const auto* Controller = GetWorld() ? Cast<AWowGameplayController>(GetWorld()->GetFirstPlayerController()) : nullptr;
    if (Controller && Controller->CurrentMouseCursor == EMouseCursor::None)
        return TSharedRef<SWidget>(SNullWidget::NullWidget);

    return Super::MapCursor(InViewport, CursorReply);
}

#if PLATFORM_WINDOWS
void UWowGameViewportClient::Init(FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice)
{
    Super::Init(WorldContext, OwningGameInstance, bCreateNewAudioDevice);
    if (IsRunningGame() && FSlateApplication::IsInitialized() && !SlatePostTick.IsValid())
        SlatePostTick = FSlateApplication::Get().OnPostTick().AddUObject(this, &UWowGameViewportClient::RefreshNativeCursor);
}

void UWowGameViewportClient::BeginDestroy()
{
    if (SlatePostTick.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().OnPostTick().Remove(SlatePostTick);
    Super::BeginDestroy();
}

void UWowGameViewportClient::RefreshNativeCursor(float)
{
    const auto* GameWorld = GetWorld();
    const auto* Controller = GameWorld ? GameWorld->GetFirstPlayerController<AWowGameplayController>() : nullptr;
    if (!Controller || Controller->CurrentMouseCursor != EMouseCursor::None) return;

    // A native click or activation change can install an OS cursor after
    // Slate has mapped the widget's cursor, even while the mouse is still.
    CURSORINFO Cursor{};
    Cursor.cbSize = sizeof(Cursor);
    if (!::GetCursorInfo(&Cursor) || !(Cursor.flags & CURSOR_SHOWING) || !Cursor.hCursor) return;

    const auto SlateWindow = GetWindow();
    const auto NativeWindow = SlateWindow ? SlateWindow->GetNativeWindow() : nullptr;
    const HWND GameWindow = NativeWindow ? static_cast<HWND>(NativeWindow->GetOSWindowHandle()) : nullptr;
    if (!GameWindow || ::WindowFromPoint(Cursor.ptScreenPos) != GameWindow) return;

    GUITHREADINFO Input{};
    Input.cbSize = sizeof(Input);
    if (!::GetGUIThreadInfo(0, &Input) || (Input.hwndCapture && Input.hwndCapture != GameWindow)) return;

    const LPARAM ScreenPoint = MAKELPARAM(Cursor.ptScreenPos.x, Cursor.ptScreenPos.y);
    if (::SendMessageW(GameWindow, WM_NCHITTEST, 0, ScreenPoint) == HTCLIENT)
        ::SetCursor(nullptr);
}
#endif
