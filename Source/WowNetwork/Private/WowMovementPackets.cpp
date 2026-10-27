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

TArray<uint8> UWowConnectionManager::BuildTeleportAckPacket(uint64 Guid,uint32 OrderCounter,uint32 TimeMilliseconds)
{
    TArray<uint8> Data; PackedMoveGuid(Data,Guid); AppendMove(Data,OrderCounter); AppendMove(Data,TimeMilliseconds); return Data;
}
void UWowConnectionManager::SendTeleportAck(uint64 Guid,uint32 OrderCounter,uint32 TimeMilliseconds)
{
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || Guid!=PacketHandler.EntityManager.LocalPlayerGuid) return;
    WorldSocket->SendPacket(WowOpcode::MSG_MOVE_TELEPORT_ACK,BuildTeleportAckPacket(Guid,OrderCounter,TimeMilliseconds));
}

bool FWowPacketHandler::ParseMovementPayload(FPacketReader& R,FWowMovementInfo& Out,uint32& Time)
{
    FWowMovementInfo Move=Out;
    const auto ReadGuid=[&](uint64& Guid) {
        if(!R.CanRead(1)) return false; const uint8 Mask=R.ReadU8(); int32 Count=0;
        for(int32 I=0;I<8;++I) if(Mask&(1<<I)) ++Count; if(!R.CanRead(Count)) return false;
        Guid=0; for(int32 I=0;I<8;++I) if(Mask&(1<<I)) Guid|=uint64(R.ReadU8())<<(8*I); return true;
    };
    if(!R.CanRead(26)) return false;
    Move.MoveFlags=R.ReadU32(); Move.MoveFlags2=R.ReadU16(); const uint32 Stamp=R.ReadU32();
    Move.Position={R.ReadFloat(),R.ReadFloat(),R.ReadFloat()}; Move.Orientation=R.ReadFloat();
    if(Move.Position.ContainsNaN() || !FMath::IsFinite(Move.Orientation) || FMath::Abs(Move.Position.X)>20000 || FMath::Abs(Move.Position.Y)>20000) return false;
    Move.TransportGuid=0; Move.TransportOffset=FVector::ZeroVector; Move.TransportOrientation=0;
    if(Move.MoveFlags&0x200)
    {
        if(!ReadGuid(Move.TransportGuid) || !R.CanRead(21)) return false;
        Move.TransportOffset={R.ReadFloat(),R.ReadFloat(),R.ReadFloat()}; Move.TransportOrientation=R.ReadFloat(); R.ReadU32(); R.ReadU8();
        if(Move.TransportOffset.ContainsNaN() || !FMath::IsFinite(Move.TransportOrientation)) return false;
        if(Move.MoveFlags2&0x200) {if(!R.CanRead(4)) return false; R.ReadU32();}
    }
    Move.Pitch=0;
    if((Move.MoveFlags&0x02200000) || (Move.MoveFlags2&0x20)) {if(!R.CanRead(4)) return false; Move.Pitch=R.ReadFloat(); if(!FMath::IsFinite(Move.Pitch)) return false;}
    if(!R.CanRead(4)) return false; Move.FallTime=R.ReadU32();
    Move.JumpZSpeed=Move.JumpSinAngle=Move.JumpXYSpeed=0; Move.JumpCosAngle=1;
    if(Move.MoveFlags&0x1000)
    {
        if(!R.CanRead(16)) return false; Move.JumpZSpeed=R.ReadFloat(); Move.JumpSinAngle=R.ReadFloat(); Move.JumpCosAngle=R.ReadFloat(); Move.JumpXYSpeed=R.ReadFloat();
        if(!FMath::IsFinite(Move.JumpZSpeed) || !FMath::IsFinite(Move.JumpSinAngle) || !FMath::IsFinite(Move.JumpCosAngle) || !FMath::IsFinite(Move.JumpXYSpeed)) return false;
    }
    if(Move.MoveFlags&0x04000000) {if(!R.CanRead(4) || !FMath::IsFinite(R.ReadFloat())) return false;}
    if(R.Remaining()) return false;
    Move.bHasActiveSpline=false; Move.SplineWaypoints.Reset(); Out=Move; Time=Stamp; return true;
}
