#include "WowConnectionManager.h"

uint32 UWowConnectionManager::GetCursorItemCount() const
{
    const auto* Item=GetCursorItem(); return Item?(CursorPayload.SplitCount?CursorPayload.SplitCount:uint32(FMath::Max(1,Item->GetStackCount()))):0;
}
#include "WowUpdateFields.h"

const FWowItemEntity* UWowConnectionManager::ResolveCursorSource(uint8 Bag,uint8 Slot) const
{
    const auto& Entities=PacketHandler.EntityManager; const auto* Player=Entities.GetLocalPlayer(); if(!Player) return nullptr;
    uint64 Guid=0;
    if(Bag==255 && Slot<74) Guid=Player->GetField64(PlayerField::INV_SLOT_HEAD+uint16(Slot)*2);
    else
    {
        const uint64 ContainerGuid=Bag>=19 && Bag<=22?Player->GetBagGuid(Bag-18):Bag>=67 && Bag<=73?Player->GetBankBagGuid(Bag-67):0;
        if(const auto* Container=Entities.FindContainer(ContainerGuid); Container && Slot<Container->GetNumSlots()) Guid=Container->GetItemGuidAtSlot(Slot);
    }
    return Entities.FindItem(Guid);
}

const FWowItemEntity* UWowConnectionManager::GetCursorItem() const
{
    if(CursorPayload.Type!=ECursorPayloadType::Item || !CursorPayload.SourceItemGuid) return nullptr;
    const auto* Item=ResolveCursorSource(CursorPayload.SourceBag,CursorPayload.SourceSlot);
    return Item && Item->Guid==CursorPayload.SourceItemGuid && Item->Entry==uint32(CursorPayload.PrimaryId)
        && Item->GetStackCount()>0 && (!CursorPayload.SplitCount || CursorPayload.SplitCount<uint32(Item->GetStackCount()))?Item:nullptr;
}

void UWowConnectionManager::RefreshItemCursor()
{
    if(CursorPayload.Type==ECursorPayloadType::Item && !GetCursorItem()) ClearCursorPayload();
    if(CursorPayload.Type==ECursorPayloadType::Merchant && !HasCursorMerchantPayload()) ClearCursorPayload();
}
