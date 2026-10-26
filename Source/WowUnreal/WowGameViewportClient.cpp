#include "WowGameViewportClient.h"
#include "WowGameplayController.h"
#include "WowUIManager.h"
#include "WowFrameManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

bool UWowGameViewportClient::InputChar(FViewport* InViewport,int32 ControllerId,TCHAR Character)
{
    auto* Instance=GetGameInstance(); auto* Ui=Instance?Instance->GetSubsystem<UWowUIManager>():nullptr;
    const auto* Controller=GetWorld()?Cast<AWowGameplayController>(GetWorld()->GetFirstPlayerController()):nullptr;
    if(Controller && !Controller->CanAcceptNativeCharacter()) return true;
    if(Ui && Ui->GetFrameManager() && Ui->GetFrameManager()->DispatchCharacter(Character)) return true;
    return Super::InputChar(InViewport,ControllerId,Character);
}
