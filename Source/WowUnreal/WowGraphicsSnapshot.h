#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
class UWorld;
class APlayerController;

/** Diagnostic only: counts distance/frustum eligible instances before GPU occlusion. */
TSharedRef<FJsonObject> WowGraphicsSnapshot(UWorld* World,APlayerController* Controller);
