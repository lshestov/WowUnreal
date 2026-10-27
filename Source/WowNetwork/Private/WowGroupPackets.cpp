#include "WowPacketHandler.h"
#include "Formats/Dbc/DbcStore.h"

namespace
{
bool Text(FPacketReader& R,FString& Out,int32 Limit=63)
{
    int32 N=0; while(N<R.Remaining() && R.Data[R.Pos+N]) ++N;
    if(N==R.Remaining() || N>Limit) return false; Out=R.ReadCString(); return true;
}
bool Packed(FPacketReader& R,uint64& Guid)
{
    if(!R.CanRead(1)) return false; const uint8 Mask=R.ReadU8(); int32 Count=0;
    for(int32 I=0;I<8;++I) Count+=(Mask>>I)&1;
    if(!R.CanRead(Count)) return false; Guid=0;
    for(int32 I=0;I<8;++I) if(Mask&(1<<I)) Guid|=uint64(R.ReadU8())<<(I*8); return true;
}
void Byte(FWowEntity& E,uint16 Field,int32 Offset,uint8 Value)
{
    E.SetField(Field,(E.GetField(Field)&~(255u<<(Offset*8)))|(uint32(Value)<<(Offset*8)));
}
}

FWowEntity* FWowPacketHandler::FindUiUnit(uint64 Guid)
{
    if(auto* E=EntityManager.Find(Guid)) return E;
    if(auto* State=RemoteGroupMembers.Find(Guid); State && GroupInfo.FindMember(Guid)) return &State->Player;
    for(auto& Pair:RemoteGroupMembers) if(GroupInfo.FindMember(Pair.Key) && Guid && Pair.Value.Pet.Guid==Guid) return &Pair.Value.Pet;
    return nullptr;
}

void FWowPacketHandler::CacheGroupUnit(uint64 Guid)
{
    const auto* E=EntityManager.Find(Guid); if(!E || !E->IsUnit()) return;
    if(GroupInfo.FindMember(Guid))
    {
        auto& State=RemoteGroupMembers.FindOrAdd(Guid); State.Player=FWowPlayerEntity(*E);
        State.Map=CurrentMapId; State.HasPosition=true; ++State.Revision; ++RemoteGroupRevision;
    }
    else for(auto& Pair:RemoteGroupMembers) if(Pair.Value.Pet.Guid==Guid)
    {
        Pair.Value.Pet=FWowUnitEntity(*E); ++Pair.Value.Revision; ++RemoteGroupRevision; break;
    }
}

void FWowPacketHandler::ResetGroupState()
{
    const bool Icons=RaidTargets.HasAnyIcons(),Ready=ReadyCheck.bActive;
    GroupInfo.Clear(); RemoteGroupMembers.Reset(); PendingGroupInviter.Empty(); RaidTargets.Clear(); ReadyCheck.Clear(); ++GroupRevision; ++RemoteGroupRevision;
    if(!LootRolls.IsEmpty()) {LootRolls.Reset(); ++LootRollRevision;}
    if(!MasterLootCandidates.IsEmpty()) {MasterLootCandidates.Reset(); MasterLootGuid=0; ++MasterLootRevision;}
    OnGroupUpdated.Broadcast(); if(Icons) OnRaidTargetsUpdated.Broadcast(RaidTargets); if(Ready) OnReadyCheckUpdated.Broadcast(ReadyCheck);
}
void FWowPacketHandler::HandleGroupDestroyed(FPacketReader& R) {if(!R.Remaining()) ResetGroupState();}
void FWowPacketHandler::HandleGroupLeader(FPacketReader& R)
{
    FString Name; if(!Text(R,Name) || R.Remaining()) return; uint64 Leader=0;
    for(const auto& Member:GroupInfo.Members) if(Member.Name==Name) Leader=Member.Guid;
    if(PlayerNameCache.FindRef(EntityManager.LocalPlayerGuid)==Name) Leader=EntityManager.LocalPlayerGuid;
    if(!Leader) return; GroupInfo.LeaderGuid=Leader; ++GroupRevision; OnGroupUpdated.Broadcast();
}

void FWowPacketHandler::HandleGroupList(FPacketReader& R)
{
    if(!R.CanRead(4)) return; FWowGroupInfo Incoming;
    Incoming.RawGroupType=R.ReadU8(); Incoming.GroupType=(Incoming.RawGroupType&WowGroupType::RAID)?1:0;
    Incoming.SelfSubgroup=R.ReadU8(); Incoming.SelfFlags=R.ReadU8(); Incoming.SelfRoles=R.ReadU8(); if(Incoming.SelfSubgroup>7) return;
    if(Incoming.RawGroupType&WowGroupType::LFG)
    {
        if(!R.CanRead(5)) return; Incoming.LfgState=R.ReadU8(); Incoming.LfgDungeon=R.ReadU32();
    }
    if(!R.CanRead(16)) return; Incoming.GroupGuid=R.ReadU64(); R.ReadU32(); const uint32 Count=R.ReadU32();
    if(Count>39 || (!Incoming.IsRaidGroup() && Count>4)) return;
    TSet<uint64> Guids;
    for(uint32 I=0;I<Count;++I)
    {
        FWowGroupMember Member;
        if(!Text(R,Member.Name) || Member.Name.IsEmpty() || !R.CanRead(12)) return;
        Member.Guid=R.ReadU64(); Member.Status=R.ReadU8(); Member.Group=R.ReadU8(); Member.Flags=R.ReadU8(); Member.Roles=R.ReadU8();
        if(!Member.Guid || (Member.Guid>>48) || Member.Guid==EntityManager.LocalPlayerGuid || Guids.Contains(Member.Guid) || Member.Group>7) return;
        Guids.Add(Member.Guid);
        if(const auto* E=EntityManager.Find(Member.Guid)) {Member.Level=uint8(E->GetLevel()); Member.Class=E->GetFieldByte(UnitField::BYTES_0,1);}
        Incoming.Members.Add(MoveTemp(Member));
    }
    if(!R.CanRead(8)) return; Incoming.LeaderGuid=R.ReadU64();
    if(Count)
    {
        if(R.Remaining()!=13) return;
        Incoming.LootMethod=R.ReadU8(); Incoming.MasterLooter=R.ReadU64(); Incoming.LootThreshold=R.ReadU8();
        Incoming.DungeonDifficulty=R.ReadU8(); Incoming.RaidDifficulty=R.ReadU8(); Incoming.RaidHeroic=R.ReadU8();
        if(Incoming.LootMethod>4 || Incoming.LootThreshold>7 || Incoming.DungeonDifficulty>3 || Incoming.RaidDifficulty>3 || Incoming.RaidHeroic>1) return;
    }
    if(R.Remaining() || (Incoming.LeaderGuid && Incoming.LeaderGuid!=EntityManager.LocalPlayerGuid && !Guids.Contains(Incoming.LeaderGuid))) return;
    if(!Incoming.LeaderGuid && !Count) {ResetGroupState(); return;}
    if(!Incoming.GroupGuid) return;
    Incoming.MemberCount=uint8(Count+1);
    const bool Changed=GroupInfo.GroupGuid!=Incoming.GroupGuid;
    const bool ClearIcons=Changed && RaidTargets.HasAnyIcons();
    const bool ClearReady=Changed && ReadyCheck.bActive;
    if(Changed) RemoteGroupMembers.Reset();
    for(auto I=RemoteGroupMembers.CreateIterator();I;++I) if(!Guids.Contains(I.Key())) I.RemoveCurrent();
    GroupInfo=MoveTemp(Incoming); PendingGroupInviter.Empty(); ++GroupRevision;
    if(Changed) {RaidTargets.Clear(); ReadyCheck.Clear();}
    TArray<uint64> Requests;
    for(const auto& Member:GroupInfo.Members)
    {
        const bool New=!RemoteGroupMembers.Contains(Member.Guid);
        auto& State=RemoteGroupMembers.FindOrAdd(Member.Guid); State.Player.Guid=Member.Guid; State.Player.TypeMask=WowTypeMask::UNIT|WowTypeMask::PLAYER;
        State.Status=(State.Status&~uint16(1))|Member.Status; State.Player.SetField(UnitField::FLAGS,State.Player.GetField(UnitField::FLAGS)|8);
        if(const auto* Identity=PlayerIdentities.Find(Member.Guid))
        {Byte(State.Player,UnitField::BYTES_0,0,Identity->Race); Byte(State.Player,UnitField::BYTES_0,1,Identity->Class); Byte(State.Player,UnitField::BYTES_0,2,Identity->Gender);}
        PlayerNameCache.Add(Member.Guid,Member.Name); CacheGroupUnit(Member.Guid);
        ++State.Revision; if(New) Requests.Add(Member.Guid);
    }
    ++RemoteGroupRevision; OnGroupUpdated.Broadcast();
    if(ClearIcons) OnRaidTargetsUpdated.Broadcast(RaidTargets); if(ClearReady) OnReadyCheckUpdated.Broadcast(ReadyCheck);
    for(uint64 Guid:Requests)
    {
        TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Guid),8);
        OnSendPacket.ExecuteIfBound(0x027F,Data); OnSendPacket.ExecuteIfBound(0x0050,Data);
    }
}

void FWowPacketHandler::HandlePartyMemberStats(FPacketReader& R) {ParsePartyMemberStats(R,false);}
void FWowPacketHandler::HandlePartyMemberStatsFull(FPacketReader& R) {if(R.CanRead(1)) {R.ReadU8(); ParsePartyMemberStats(R,true);}}
void FWowPacketHandler::ParsePartyMemberStats(FPacketReader& R,bool Full)
{
    uint64 Guid=0; if(!Packed(R,Guid) || !R.CanRead(4)) return; const uint32 Mask=R.ReadU32();
    if(!GroupInfo.FindMember(Guid) || !Mask || (Mask&~0xfffffu)) return;
    FWowRemoteGroupMember State=RemoteGroupMembers.FindRef(Guid); State.Player.Guid=Guid; State.Player.TypeMask=WowTypeMask::UNIT|WowTypeMask::PLAYER;
    if(Full) {Byte(State.Player,UnitField::BYTES_0,3,0); State.Player.Auras.Reset(); State.Pet=FWowUnitEntity(); State.PetName.Empty();}
    const auto U32=[&](uint32 Flag,FWowEntity& E,uint16 Field)->bool {if(!(Mask&Flag)) return true; if(!R.CanRead(4)) return false; E.SetField(Field,R.ReadU32()); return true;};
    const auto U16=[&](uint32 Flag,FWowEntity& E,uint16 Field)->bool {if(!(Mask&Flag)) return true; if(!R.CanRead(2)) return false; E.SetField(Field,R.ReadU16()); return true;};
    const auto Power=[&](uint32 Flag,FWowEntity& E)->bool {if(!(Mask&Flag)) return true; if(!R.CanRead(1)) return false; const uint8 Type=R.ReadU8(); if(Type>6) return false; Byte(E,UnitField::BYTES_0,3,Type); return true;};
    const auto Auras=[&](uint32 Flag,FWowEntity& E)->bool
    {
        if(!(Mask&Flag)) return true; if(!R.CanRead(8)) return false; const uint64 Slots=R.ReadU64();
        E.Auras.SetNum(64);
        for(int32 I=0;I<64;++I) if(Slots&(uint64(1)<<I))
        {
            if(!R.CanRead(5)) return false; auto& Aura=E.Auras[I]; Aura=FAuraInfo(); Aura.SpellId=R.ReadU32(); Aura.Flags=R.ReadU8();
            Aura.bActive=Aura.SpellId!=0; Aura.UpdatedAt=FPlatformTime::Seconds();
        }
        return true;
    };
    if(Mask&1) {if(!R.CanRead(2)) return; State.Status=R.ReadU16();}
    if(!U32(2,State.Player,UnitField::HEALTH) || !U32(4,State.Player,UnitField::MAXHEALTH) || !Power(8,State.Player)) return;
    const uint8 PlayerPower=State.Player.GetPowerTypeId();
    if(!U16(0x10,State.Player,UnitField::POWER1+PlayerPower) || !U16(0x20,State.Player,UnitField::MAXPOWER1+PlayerPower) || !U16(0x40,State.Player,UnitField::LEVEL)) return;
    if(Mask&0x80) {if(!R.CanRead(2)) return; State.Zone=R.ReadU16(); if(const auto* Area=FDbcStore::Get().AreaTable().GetById(State.Zone)) State.Map=Area->MapID; else State.Map=MAX_uint32;}
    if(Mask&0x100)
    {
        if(!R.CanRead(4)) return; State.Player.Movement.Position.X=int16(R.ReadU16()); State.Player.Movement.Position.Y=int16(R.ReadU16()); State.HasPosition=true;
    }
    if(!Auras(0x200,State.Player)) return;
    if(Mask&0x400)
    {
        if(!R.CanRead(8)) return; const uint64 Pet=R.ReadU64(); if(Pet!=State.Pet.Guid) {State.Pet=FWowUnitEntity(); State.PetName.Empty();} State.Pet.Guid=Pet;
    }
    if((Mask&0x800) && !Text(R,State.PetName)) return;
    if(!U16(0x1000,State.Pet,UnitField::DISPLAYID) || !U32(0x2000,State.Pet,UnitField::HEALTH) || !U32(0x4000,State.Pet,UnitField::MAXHEALTH) || !Power(0x8000,State.Pet)) return;
    const uint8 PetPower=State.Pet.GetPowerTypeId();
    if(!U16(0x10000,State.Pet,UnitField::POWER1+PetPower) || !U16(0x20000,State.Pet,UnitField::MAXPOWER1+PetPower) || !Auras(0x40000,State.Pet)) return;
    if(Mask&0x80000) {if(!R.CanRead(4)) return; State.VehicleSeat=R.ReadU32();}
    if(R.Remaining()) return;
    State.Player.SetField(UnitField::FLAGS,State.Player.GetField(UnitField::FLAGS)|8);
    Byte(State.Player,UnitField::BYTES_2,1,uint8(((State.Status&2)?1:0)|((State.Status&0x10)?4:0)));
    const uint32 StatusFlags=((State.Status&8)?0x10:0)|((State.Status&0x40)?1:0)|((State.Status&0x80)?2:0);
    State.Player.SetField(PlayerField::FLAGS,(State.Player.GetField(PlayerField::FLAGS)&~0x13u)|StatusFlags);
    Byte(State.Player,UnitField::BYTES_1,0,(State.Status&12)?WowStandState::DEAD:WowStandState::STAND);
    if(State.Pet.Guid)
    {
        State.Pet.TypeMask=WowTypeMask::UNIT; State.Pet.SetField(UnitField::FLAGS,State.Pet.GetField(UnitField::FLAGS)|8);
        State.Pet.SetField(UnitField::SUMMONEDBY,uint32(Guid)); State.Pet.SetField(UnitField::SUMMONEDBY+1,uint32(Guid>>32));
    }
    State.Player.SetField(UnitField::SUMMON,uint32(State.Pet.Guid)); State.Player.SetField(UnitField::SUMMON+1,uint32(State.Pet.Guid>>32));
    ++State.Revision; RemoteGroupMembers.Add(Guid,MoveTemp(State)); ++RemoteGroupRevision;
    if(auto* Member=GroupInfo.Members.FindByPredicate([Guid](const auto& M){return M.Guid==Guid;}))
    {const auto& Cached=RemoteGroupMembers[Guid]; Member->Status=uint8(Cached.Status); Member->Level=uint8(Cached.Player.GetLevel());}
}
