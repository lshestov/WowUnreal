#include "WowConnectionManager.h"
#include "Net/WowWorldSocket.h"

namespace
{
template<typename T> void Append(TArray<uint8>& Bytes,T Value) { const int32 Start=Bytes.AddUninitialized(sizeof(T)); FMemory::Memcpy(Bytes.GetData()+Start,&Value,sizeof(T)); }
const FWowVendorItem* VendorItem(const FWowMerchantState& Merchant,uint32 Slot,uint32 Count)
{
    if(!Merchant.Open || !Merchant.Guid || Count<1 || Count>255) return nullptr;
    for(const auto& Item:Merchant.Items) if(Item.Slot==Slot && Item.ItemId && Item.MaxCount!=0) return &Item;
    return nullptr;
}
}
void UWowConnectionManager::RequestVendorInventory(uint64 VendorGuid)
{
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !VendorGuid) return;
    if(PacketHandler.Merchant.Guid!=VendorGuid) PacketHandler.Merchant.Clear();
    TArray<uint8> Bytes; Append(Bytes,VendorGuid); WorldSocket->SendPacket(WowOpcode::CMSG_LIST_INVENTORY,Bytes);
}
bool UWowConnectionManager::SendBuyVendorSlot(uint32 VendorSlot,uint32 Count)
{
    const auto& Merchant=PacketHandler.Merchant; const auto* Item=VendorItem(Merchant,VendorSlot,Count);
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !Item) return false;
    TArray<uint8> Bytes; Append(Bytes,Merchant.Guid); Append(Bytes,Item->ItemId); Append(Bytes,VendorSlot); Append(Bytes,Count); Append<uint8>(Bytes,0);
    WorldSocket->SendPacket(WowOpcode::CMSG_BUY_ITEM,Bytes); return true;
}
bool UWowConnectionManager::SendBuyVendorSlotIntoBag(uint32 VendorSlot,uint32 Count,uint8 Bag,uint8 Slot)
{
    const auto& Merchant=PacketHandler.Merchant; const auto* Item=VendorItem(Merchant,VendorSlot,Count);
    const auto* Player=PacketHandler.EntityManager.GetLocalPlayer();
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !Item || !Player) return false;
    uint64 BagGuid=0;
    if(Bag==255 && Slot<39) BagGuid=Player->Guid;
    else if(Bag>=19 && Bag<=22) if(const auto* Container=PacketHandler.EntityManager.FindContainer(Player->GetBagGuid(Bag-18)); Container && Slot<Container->GetNumSlots()) BagGuid=Container->Guid;
    if(!BagGuid) return false;
    TArray<uint8> Bytes; Append(Bytes,Merchant.Guid); Append(Bytes,Item->ItemId); Append(Bytes,VendorSlot); Append(Bytes,BagGuid); Append(Bytes,Slot); Append(Bytes,Count);
    WorldSocket->SendPacket(WowOpcode::CMSG_BUY_ITEM_IN_SLOT,Bytes); return true;
}
bool UWowConnectionManager::SendBuybackItem(uint32 InventorySlot)
{
    const auto& Merchant=PacketHandler.Merchant;
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !Merchant.Open || !Merchant.Guid || InventorySlot<74 || InventorySlot>=86) return false;
    TArray<uint8> Bytes; Append(Bytes,Merchant.Guid); Append(Bytes,InventorySlot); WorldSocket->SendPacket(WowOpcode::CMSG_BUYBACK_ITEM,Bytes); return true;
}
bool UWowConnectionManager::SendRepairItems(uint64 ItemGuid,bool GuildFunds)
{
    const auto& Merchant=PacketHandler.Merchant;
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !Merchant.Open || !Merchant.Guid) return false;
    TArray<uint8> Bytes; Append(Bytes,Merchant.Guid); Append(Bytes,ItemGuid); Append<uint8>(Bytes,GuildFunds?1:0); WorldSocket->SendPacket(WowOpcode::CMSG_REPAIR_ITEM,Bytes); return true;
}
bool UWowConnectionManager::HasCursorMerchantPayload() const
{
    const auto& M=PacketHandler.Merchant;
    if(CursorPayload.Type!=ECursorPayloadType::Merchant || !M.Open || M.Guid!=CursorPayload.SourceVendorGuid) return false;
    for(const auto& Item:M.Items) if(Item.Slot==CursorPayload.SourceVendorSlot && Item.ItemId==uint32(CursorPayload.PrimaryId) && Item.MaxCount!=0) return true;
    return false;
}
uint32 UWowConnectionManager::GetCursorMerchantItemId() const { return HasCursorMerchantPayload()?uint32(CursorPayload.PrimaryId):0; }
void UWowConnectionManager::PickupMerchantCursor(int32 Index)
{
    const auto& M=PacketHandler.Merchant; if(!M.Open || !M.Items.IsValidIndex(Index-1) || M.Items[Index-1].MaxCount==0) return;
    SetRepairMode(false); CancelGlyphUse(); const auto& Item=M.Items[Index-1];
    CursorPayload=FCursorPayloadState(); CursorPayload.Type=ECursorPayloadType::Merchant; CursorPayload.PrimaryId=Item.ItemId;
    CursorPayload.SourceActionSlot=Index-1; CursorPayload.SourceVendorGuid=M.Guid; CursorPayload.SourceVendorSlot=Item.Slot; OnCursorChanged.Broadcast();
}
bool UWowConnectionManager::SellCursorToMerchant()
{
    if(HasCursorMerchantPayload()) { ClearCursorPayload(); return true; }
    const auto* Item=GetCursorItem(); const auto& M=PacketHandler.Merchant;
    if(!Item || !M.Open || !M.Guid || !WorldSocket || State!=EWowSessionState::WorldInGame) return false;
    if(!IsRepairMode() && (Item->GetField(ItemField::FLAGS)&0x1000) && (!PacketHandler.ItemRefunds.Contains(Item->Guid) || PacketHandler.PlayedTimeReceived<=0)) { RequestItemRefundInfo(Item->Guid); return false; }
    if(IsRepairMode()) SendRepairItems(Item->Guid);
    else if(!CursorPayload.SplitCount) SendSellItem(M.Guid,Item->Guid,0);
    else SendSellItem(M.Guid,Item->Guid,int32(CursorPayload.SplitCount));
    ClearCursorPayload(); return true;
}
void UWowConnectionManager::CloseMerchant()
{
    PacketHandler.Merchant.Clear(); RepairCursor=false;
    if(CursorPayload.Type==ECursorPayloadType::Merchant) ClearCursorPayload(); else OnCursorChanged.Broadcast();
}
void UWowConnectionManager::SetRepairMode(bool Active)
{
    const bool Next=Active && PacketHandler.Merchant.Open; if(RepairCursor==Next) return;
    RepairCursor=Next; if(Next) ClearCursorPayload(); else OnCursorChanged.Broadcast();
}
void FWowPacketHandler::HandleBuyItemResult(FPacketReader& R)
{
    if(!R.CanRead(20)) return;
    const uint64 Guid=R.ReadU64(); const uint32 Slot=R.ReadU32(),Stock=R.ReadU32(); R.ReadU32();
    if(!Merchant.Open || Merchant.Guid!=Guid) return;
    for(auto& Item:Merchant.Items) if(Item.Slot==Slot) { Item.MaxCount=Stock; ++Merchant.Revision; OnVendorUpdated.Broadcast(); break; }
}
void FWowPacketHandler::HandleBuyItemFailure(FPacketReader& R)
{
    if(!R.CanRead(13)) return;
    const uint64 Guid=R.ReadU64(); R.ReadU32(); R.Skip(R.Remaining()-1); const uint8 Error=R.ReadU8();
    if(Merchant.Open && (!Guid || Merchant.Guid==Guid)) OnVendorError.Broadcast(Error,false);
}
void FWowPacketHandler::HandleSellItemResult(FPacketReader& R)
{
    if(!R.CanRead(17)) return;
    const uint64 Guid=R.ReadU64(); R.ReadU64(); R.Skip(R.Remaining()-1); const uint8 Error=R.ReadU8();
    if(!Merchant.Open || (Guid && Merchant.Guid!=Guid)) return;
    if(Error) OnVendorError.Broadcast(Error,true); else OnVendorUpdated.Broadcast();
}

int32 FWowPacketHandler::ItemRefundSeconds(uint64 Guid) const
{
    const auto* Info=ItemRefunds.Find(Guid); if(!Info || PlayedTimeReceived<=0) return 0;
    const double Played=TotalPlayedTime+FMath::Max(0.,FPlatformTime::Seconds()-PlayedTimeReceived);
    return FMath::Clamp(int32(Info->PurchasePlayedTime+7200.-Played),0,7200);
}
void UWowConnectionManager::RequestItemRefundInfo(uint64 Guid)
{
    const auto* Item=PacketHandler.EntityManager.FindItem(Guid);
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !Item || !(Item->GetField(ItemField::FLAGS)&0x1000) || PacketHandler.RequestedItemRefunds.Contains(Guid)) return;
    PacketHandler.RequestedItemRefunds.Add(Guid);
    if(!PacketHandler.PlayedTimeRequested) { PacketHandler.PlayedTimeRequested=true; WorldSocket->SendPacket(0x01CC,{0}); }
    TArray<uint8> Data; Append(Data,Guid); WorldSocket->SendPacket(0x04B3,Data);
}
bool UWowConnectionManager::SendItemRefund(uint64 Guid)
{
    const auto* Item=PacketHandler.EntityManager.FindItem(Guid);
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !PacketHandler.Merchant.Open || !Item || !(Item->GetField(ItemField::FLAGS)&0x1000) || PacketHandler.ItemRefundSeconds(Guid)<=0) return false;
    TArray<uint8> Data; Append(Data,Guid); WorldSocket->SendPacket(0x04B4,Data); return true;
}
void FWowPacketHandler::HandleItemRefundInfo(FPacketReader& R)
{
    if(!R.CanRead(68)) return;
    const uint64 Guid=R.ReadU64(); FWowItemRefundInfo Info; Info.Money=R.ReadU32(); Info.Honor=R.ReadU32(); Info.Arena=R.ReadU32();
    for(int32 I=0;I<5;++I) { Info.Items[I]=R.ReadU32(); Info.Counts[I]=R.ReadU32(); }
    R.ReadU32(); Info.PurchasePlayedTime=R.ReadU32();
    if(!EntityManager.FindItem(Guid)) return;
    ItemRefunds.Add(Guid,Info); RequestedItemRefunds.Add(Guid); OnPlayerInventoryUpdate.Broadcast();
}
void FWowPacketHandler::HandleItemRefundResult(FPacketReader& R)
{
    if(!R.CanRead(12)) return;
    const uint64 Guid=R.ReadU64(); const uint32 Error=R.ReadU32(); if(!Error && !R.CanRead(52)) return;
    ItemRefunds.Remove(Guid); RequestedItemRefunds.Remove(Guid); OnRefundResult.Broadcast(Error); OnPlayerInventoryUpdate.Broadcast();
}
