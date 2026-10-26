#include "WowConnectionManager.h"
#include "Net/WowWorldSocket.h"

bool UWowConnectionManager::SendBankItem(uint8 Bag,uint8 Slot,bool Deposit)
{
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !PacketHandler.CurrentBankerGuid || !ResolveCursorSource(Bag,Slot)) return false;
    WorldSocket->SendPacket(Deposit?0x0283:0x0282,{Bag,Slot}); return true;
}
bool UWowConnectionManager::SendBuyBankSlot()
{
    const auto* Player=PacketHandler.EntityManager.GetLocalPlayer(); const uint64 Guid=PacketHandler.CurrentBankerGuid;
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !Guid || !Player || Player->GetBankBagSlotCount()>=7) return false;
    TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Guid),sizeof(Guid)); WorldSocket->SendPacket(0x01B9,Data); return true;
}
void UWowConnectionManager::CloseBank()
{
    PacketHandler.CurrentBankerGuid=0; RefreshItemCursor();
}
