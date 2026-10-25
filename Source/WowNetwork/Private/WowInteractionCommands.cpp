#include "WowConnectionManager.h"

namespace
{
template<typename T> void Append(TArray<uint8>& Data, T Value)
{
    Data.Append(reinterpret_cast<const uint8*>(&Value),sizeof(Value));
}
}

void UWowConnectionManager::SendGossipSelect(uint64 Guid, uint32 Menu, uint32 Option)
{
    if (GetState()!=EWowSessionState::WorldInGame) return;
    TArray<uint8> Data; Append(Data,Guid); Append(Data,Menu); Append(Data,Option);
    SendRawPacket(WowOpcode::CMSG_GOSSIP_SELECT_OPTION,Data);
}

void UWowConnectionManager::SendQuestQuery(uint64 Guid, uint32 Quest)
{
    if (GetState()!=EWowSessionState::WorldInGame) return;
    TArray<uint8> Data; Append(Data,Guid); Append(Data,Quest); Data.Add(0);
    SendRawPacket(WowOpcode::CMSG_QUESTGIVER_QUERY_QUEST,Data);
}

void UWowConnectionManager::RequestQuestTemplate(uint32 Id)
{
    if(GetState()!=EWowSessionState::WorldInGame || !Id || PacketHandler.QuestTemplates.Contains(Id) || PacketHandler.RequestedQuestTemplates.Contains(Id)) return;
    PacketHandler.RequestedQuestTemplates.Add(Id); TArray<uint8> Data; Append(Data,Id); SendRawPacket(WowOpcode::CMSG_QUEST_QUERY,Data);
}

TArray<uint8> UWowConnectionManager::BuildQuestAcceptPacket(uint64 Guid,uint32 Quest)
{
    TArray<uint8> Data; Append(Data,Guid); Append(Data,Quest); Append(Data,uint32(0)); return Data;
}

void UWowConnectionManager::SendQuestComplete(uint64 Guid, uint32 Quest)
{
    if (GetState()!=EWowSessionState::WorldInGame) return;
    TArray<uint8> Data; Append(Data,Guid); Append(Data,Quest);
    SendRawPacket(WowOpcode::CMSG_QUESTGIVER_COMPLETE_QUEST,Data);
}

void UWowConnectionManager::SendLootMoney()
{
    if (PacketHandler.Loot.Guid && PacketHandler.Loot.Gold) SendRawPacket(WowOpcode::CMSG_LOOT_MONEY);
}

void UWowConnectionManager::SendQuestAbandon(uint32 Quest)
{
    if (GetState()!=EWowSessionState::WorldInGame) return;
    const auto* Player=PacketHandler.EntityManager.GetLocalPlayer();
    if (!Player) return;
    for (uint8 Slot=0; Slot<25; ++Slot)
        if (Player->GetField(PlayerField::BASE+0x000A+Slot*5)==Quest)
        {
            SendRawPacket(0x0194,{Slot}); return;
        }
}

void UWowConnectionManager::RequestItemTemplate(uint32 Id)
{
    if (!Id || PacketHandler.ItemTemplates.Contains(Id) || RequestedItems.Contains(Id) || GetState()!=EWowSessionState::WorldInGame) return;
    RequestedItems.Add(Id);
    TArray<uint8> Data; Append(Data,Id); SendRawPacket(WowOpcode::CMSG_ITEM_QUERY_SINGLE,Data);
}

int32 UWowConnectionManager::GetBonusActionOffset() const
{
    const auto* Player=PacketHandler.EntityManager.GetLocalPlayer();
    const uint8 Form=Player?Player->GetFieldByte(UnitField::BYTES_2,3):0;
    switch (Form)
    {
    case 1: case 17: return 1;
    case 18: return 2;
    case 5: case 8: case 19: return 3;
    case 31: return 4;
    default: return 0;
    }
}

int32 UWowConnectionManager::GetVisibleActionSlot(int32 ButtonIndex) const
{
    if (ButtonIndex<0 || ButtonIndex>=12) return INDEX_NONE;
    const int32 Bonus=GetBonusActionOffset();
    return ButtonIndex+(Bonus>0?((Bonus+5)*12):0);
}
