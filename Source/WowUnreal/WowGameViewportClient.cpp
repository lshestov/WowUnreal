#include "WowGameViewportClient.h"
#include "WowGameplayController.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Widgets/SNullWidget.h"

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
