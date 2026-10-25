#include "WowConnectionManager.h"
#include "Net/WowWorldSocket.h"

namespace
{
template<typename T> void AppendMove(TArray<uint8>& Data,T Value)
{
    Data.Append(reinterpret_cast<const uint8*>(&Value),sizeof(Value));
}
void PackedMoveGuid(TArray<uint8>& Data,uint64 Guid)
{
    const int32 MaskPosition=Data.Add(0);
    for (int32 I=0; I<8; ++I)
        if (const uint8 Byte=uint8(Guid>>(I*8))) { Data[MaskPosition]|=uint8(1<<I); Data.Add(Byte); }
}
}

TArray<uint8> UWowConnectionManager::BuildMovementPacket(uint64 Guid,const FWowMovementInfo& Move,uint32 TimeMilliseconds)
{
    TArray<uint8> Data;
    PackedMoveGuid(Data,Guid);
    AppendMove(Data,Move.MoveFlags); AppendMove(Data,Move.MoveFlags2); AppendMove(Data,TimeMilliseconds);
    AppendMove(Data,float(Move.Position.X)); AppendMove(Data,float(Move.Position.Y)); AppendMove(Data,float(Move.Position.Z)); AppendMove(Data,Move.Orientation);
    if (Move.MoveFlags&0x00000200)
    {
        PackedMoveGuid(Data,Move.TransportGuid);
        AppendMove(Data,float(Move.TransportOffset.X)); AppendMove(Data,float(Move.TransportOffset.Y)); AppendMove(Data,float(Move.TransportOffset.Z));
        AppendMove(Data,Move.TransportOrientation); AppendMove(Data,uint32(0)); AppendMove(Data,uint8(0));
        if (Move.MoveFlags2&0x0200) AppendMove(Data,uint32(0));
    }
    if ((Move.MoveFlags&(0x00200000|0x02000000)) || (Move.MoveFlags2&0x0020)) AppendMove(Data,Move.Pitch);
    AppendMove(Data,Move.FallTime);
    if (Move.MoveFlags&0x00001000)
    {
        AppendMove(Data,Move.JumpZSpeed); AppendMove(Data,Move.JumpSinAngle); AppendMove(Data,Move.JumpCosAngle); AppendMove(Data,Move.JumpXYSpeed);
    }
    if (Move.MoveFlags&0x04000000) AppendMove(Data,0.f);
    return Data;
}

void UWowConnectionManager::SendMovementInfo(int32 Opcode,const FWowMovementInfo& Movement)
{
    if (!WorldSocket.IsValid() || State!=EWowSessionState::WorldInGame) return;
    if (auto* Local=PacketHandler.EntityManager.GetLocalPlayer())
    {
        Local->Movement.Position=Movement.Position; Local->Movement.Orientation=Movement.Orientation; Local->Movement.MoveFlags=Movement.MoveFlags;
    }
    // The protocol clock is milliseconds, independent of the CPU cycle counter.
    const uint32 Time=uint32(uint64(FPlatformTime::Seconds()*1000.0));
    WorldSocket->SendPacket(uint32(Opcode),BuildMovementPacket(PacketHandler.EntityManager.LocalPlayerGuid,Movement,Time));
}
