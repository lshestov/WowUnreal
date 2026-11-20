#pragma once

#include "CoreMinimal.h"
#include "Layout/Geometry.h"

class UWowUIManager;

namespace WowQuestItemTooltip
{
    void Show(UWowUIManager& UI, uint32 ItemId, const FGeometry& Row);
    void Hide(UWowUIManager& UI);
}
