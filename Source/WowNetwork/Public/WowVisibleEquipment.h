#pragma once

#include "WowEntity.h"

namespace WowVisibleEquipment
{
inline bool IsSlotShown(const FWowEntity& Player, uint8 Slot)
{
    constexpr uint32 HideHelm = 0x00000400;
    constexpr uint32 HideCloak = 0x00000800;
    if (Slot == PlayerField::EQUIPMENT_SLOT_HEAD) return !(Player.GetField(PlayerField::FLAGS) & HideHelm);
    if (Slot == PlayerField::EQUIPMENT_SLOT_BACK) return !(Player.GetField(PlayerField::FLAGS) & HideCloak);
    return true;
}

// Visible item entries are public for both the owner and other players.
// The private inventory GUID identifies the actual item, not its appearance.
inline uint32 GetEntry(const FWowEntity& Player, uint8 Slot)
{
    return Slot <= PlayerField::EQUIPMENT_SLOT_TABARD && IsSlotShown(Player, Slot)
        ? Player.GetField(PlayerField::VISIBLE_ITEM_BASE + Slot * 2) : 0;
}
}
