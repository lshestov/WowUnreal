#include "WowPacketHandler.h"
#include "Formats/Dbc/DbcStore.h"

namespace
{
using V=FWowCombatValue;
bool Guid(FPacketReader& R,uint64& Out)
{
    if(!R.CanRead(1)) return false;
    const uint8 Mask=R.ReadU8(); int32 Bytes=0; for(int32 I=0;I<8;++I) if(Mask&(1<<I)) ++Bytes;
    if(!R.CanRead(Bytes)) return false;
    Out=0; for(int32 I=0;I<8;++I) if(Mask&(1<<I)) Out|=uint64(R.ReadU8())<<(8*I);
    return true;
}
}
void FWowPacketHandler::RecordCombat(FWowCombatEntry Entry)
{
    const auto Copy=CombatHistory.Append(MoveTemp(Entry));
    OnCombatLog.ExecuteIfBound(Copy);
}
void FWowPacketHandler::HandleAttackerStateUpdate(FPacketReader& R)
{
    if(!R.CanRead(4)) return;
    const uint32 Hit=R.ReadU32(); FWowCombatEntry E;
    if(!Guid(R,E.Source) || !Guid(R,E.Destination) || !R.CanRead(9)) return;
    const uint32 Damage=R.ReadU32(),Overkill=R.ReadU32(); const uint8 Count=R.ReadU8();
    if(!Count || Count>2 || !R.CanRead(Count*12)) return;
    uint32 School=0,Absorb=0,Resist=0,Blocked=0;
    for(uint8 I=0;I<Count;++I) { School|=R.ReadU32(); R.ReadFloat(); R.ReadU32(); }
    if(Hit&0x60) { if(!R.CanRead(Count*4)) return; for(uint8 I=0;I<Count;++I) Absorb+=R.ReadU32(); }
    if(Hit&0x180) { if(!R.CanRead(Count*4)) return; for(uint8 I=0;I<Count;++I) Resist+=R.ReadU32(); }
    if(!R.CanRead(9)) return;
    const uint8 State=R.ReadU8(); R.ReadU32(); R.ReadU32();
    if(Hit&0x2000) { if(!R.CanRead(4)) return; Blocked=R.ReadU32(); }
    if(Hit&0x800000) { if(!R.CanRead(4)) return; R.ReadU32(); }
    if(Hit&1) { if(!R.CanRead(48)) return; R.Pos+=48; }
    FString Miss;
    if(Hit&0x10) Miss=TEXT("MISS");
    else if(State==2) Miss=TEXT("DODGE"); else if(State==3) Miss=TEXT("PARRY");
    else if(State==6) Miss=TEXT("EVADE"); else if(State==7) Miss=TEXT("IMMUNE"); else if(State==8) Miss=TEXT("DEFLECT");
    else if(!Damage && Absorb) Miss=TEXT("ABSORB"); else if(!Damage && Resist) Miss=TEXT("RESIST"); else if(!Damage && Blocked) Miss=TEXT("BLOCK");
    if(!Miss.IsEmpty()) { E.Event=TEXT("SWING_MISSED"); E.Values={V::S(Miss),V::N(Absorb+Resist+Blocked)}; }
    else { E.Event=TEXT("SWING_DAMAGE"); E.Values={V::N(Damage),V::N(Overkill),V::N(School?School:1),V::N(Resist),V::N(Blocked),V::N(Absorb),V::B((Hit&0x200)!=0),V::B((Hit&0x10000)!=0),V::B((Hit&0x20000)!=0)}; }
    const uint64 Source=E.Source,Destination=E.Destination;
    RecordCombat(MoveTemp(E));
    OnAttackerStateUpdate.Broadcast(Source,Destination,Hit,Damage);
}
void FWowPacketHandler::HandleSpellDamageLog(FPacketReader& R)
{
    FWowCombatEntry E;
    if(!Guid(R,E.Destination) || !Guid(R,E.Source) || !R.CanRead(32)) return;
    E.Spell=R.ReadU32(); const uint32 Damage=R.ReadU32(),Overkill=R.ReadU32(); E.School=R.ReadU8();
    const uint32 Absorb=R.ReadU32(),Resist=R.ReadU32(); R.ReadU8(); R.ReadU8(); const uint32 Blocked=R.ReadU32(),Hit=R.ReadU32(); R.ReadU8();
    E.Event=TEXT("SPELL_DAMAGE"); E.Values={V::N(Damage),V::N(Overkill),V::N(E.School),V::N(Resist),V::N(Blocked),V::N(Absorb),V::B((Hit&2)!=0),V::B(false),V::B(false)};
    RecordCombat(MoveTemp(E));
}
void FWowPacketHandler::HandleSpellHealLog(FPacketReader& R)
{
    FWowCombatEntry E;
    if(!Guid(R,E.Destination) || !Guid(R,E.Source) || !R.CanRead(18)) return;
    E.Spell=R.ReadU32(); const uint32 Amount=R.ReadU32(),Overheal=R.ReadU32(),Absorb=R.ReadU32(); const bool Critical=R.ReadU8()!=0; R.ReadU8();
    if(const auto* Spell=FDbcStore::Get().Spells().GetById(E.Spell)) E.School=Spell->SchoolMask;
    E.Event=TEXT("SPELL_HEAL"); E.Values={V::N(Amount),V::N(Overheal),V::N(Absorb),V::B(Critical)};
    RecordCombat(MoveTemp(E));
}
