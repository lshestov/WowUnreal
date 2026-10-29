#include "WowPacketHandler.h"
#include "Formats/Dbc/DbcStore.h"

namespace
{
using V=FWowCombatValue;
uint32 PowerMod(uint32 Power) {return Power==1 || Power==6?10:Power==4?1000:1;}
bool Guid(FPacketReader& R,uint64& Out)
{
    if(!R.CanRead(1)) return false;
    const uint8 Mask=R.ReadU8(); int32 Bytes=0; for(int32 I=0;I<8;++I) if(Mask&(1<<I)) ++Bytes;
    if(!R.CanRead(Bytes)) return false;
    Out=0; for(int32 I=0;I<8;++I) if(Mask&(1<<I)) Out|=uint64(R.ReadU8())<<(8*I);
    return true;
}
FString MissName(uint8 Id)
{
    static const TCHAR* Names[]={TEXT(""),TEXT("MISS"),TEXT("RESIST"),TEXT("DODGE"),TEXT("PARRY"),TEXT("BLOCK"),TEXT("EVADE"),TEXT("IMMUNE"),TEXT("IMMUNE"),TEXT("DEFLECT"),TEXT("ABSORB"),TEXT("REFLECT")};
    return Id<UE_ARRAY_COUNT(Names)?Names[Id]:TEXT("");
}
void ExtraSpell(FWowCombatEntry& E,uint32 Id)
{
    const auto* S=FDbcStore::Get().Spells().GetById(Id);
    E.Values.Add(V::N(Id)); E.Values.Add(V::S(S?S->SpellName:FString::FromInt(Id))); E.Values.Add(V::N(S?S->SchoolMask:0));
}
void SpellDamage(FWowCombatEntry& E,uint32 Amount,uint32 Overkill,uint32 School,uint32 Absorb,uint32 Resist,uint32 Block,bool Critical,bool Periodic,bool Split=false)
{
    const auto* S=FDbcStore::Get().Spells().GetById(E.Spell); const bool Ranged=S && (S->AttributesEx3&0x8000);
    E.School=School;
    if(!Amount && (Absorb || Resist || Block)) {E.Event=Periodic?TEXT("SPELL_PERIODIC_MISSED"):Ranged?TEXT("RANGE_MISSED"):TEXT("SPELL_MISSED"); E.Values={V::S(Absorb?TEXT("ABSORB"):Block?TEXT("BLOCK"):TEXT("RESIST")),V::N(Absorb?Absorb:Block?Block:Resist)};}
    else {E.Event=Split?TEXT("DAMAGE_SPLIT"):Periodic?TEXT("SPELL_PERIODIC_DAMAGE"):Ranged?TEXT("RANGE_DAMAGE"):TEXT("SPELL_DAMAGE"); E.Values={V::N(Amount),V::N(Overkill),V::N(School),V::N(Resist),V::N(Block),V::N(Absorb),V::B(Critical),V::B(false),V::B(false)};}
}
bool Targets(FPacketReader& R,uint64& Target)
{
    if(!R.CanRead(4)) return false; const uint32 Mask=R.ReadU32(); uint64 Ignored;
    if((Mask&0x18A02) && !Guid(R,Target)) return false;
    if((Mask&0x1010) && !Guid(R,Ignored)) return false;
    for(uint32 Flag:{0x20u,0x40u}) if(Mask&Flag) {if(!Guid(R,Ignored) || !R.CanRead(12)) return false; for(int32 I=0;I<3;++I) if(!FMath::IsFinite(R.ReadFloat())) return false;}
    if(Mask&0x2000) {int32 N=0; while(N<R.Remaining() && R.Data[R.Pos+N]) ++N; if(N==R.Remaining() || N>1024) return false; R.ReadCString();}
    return true;
}
void PowerDrain(FWowCombatEntry& E,uint32 Amount,uint32 Power,float Multiplier,bool Periodic)
{
    const uint32 Mod=PowerMod(Power); const double Extra=FMath::TruncToDouble(FMath::TruncToDouble(double(Amount)*Multiplier)/Mod);
    E.Event=Extra?(Periodic?TEXT("SPELL_PERIODIC_LEECH"):TEXT("SPELL_LEECH")):(Periodic?TEXT("SPELL_PERIODIC_DRAIN"):TEXT("SPELL_DRAIN"));
    E.Values={V::N(Amount/Mod),V::N(Power),V::N(Extra)};
}
}
void FWowPacketHandler::RecordCombat(FWowCombatEntry Entry)
{
    if(OnResolveCombatFlags.IsBound()) {Entry.SourceFlags=OnResolveCombatFlags.Execute(Entry.Source); Entry.DestinationFlags=OnResolveCombatFlags.Execute(Entry.Destination); Entry.FlagsCaptured=true;}
    const auto Copy=CombatHistory.Append(MoveTemp(Entry));
    OnCombatLog.ExecuteIfBound(Copy);
}
void FWowPacketHandler::RecordSpellCombat(FWowCombatEntry Entry)
{
    if(const auto* Spell=FDbcStore::Get().Spells().GetById(Entry.Spell))
    {if((Spell->Attributes&0x180) || Spell->SpellName.IsEmpty()) return; Entry.School=Spell->SchoolMask;}
    RecordCombat(MoveTemp(Entry));
}
void FWowPacketHandler::RecordUnitDeath(uint64 Target)
{
    Casts.Remove(Target);
    FWowCombatEntry E; E.Event=TEXT("UNIT_DIED"); E.Destination=Target;
    if(const auto* Entity=EntityManager.Find(Target))
    {
        if(CreatureTypes.FindRef(Entity->Entry)==13) E.Event=TEXT("UNIT_DISSIPATES");
        else if(const auto* Spell=FDbcStore::Get().Spells().GetById(Entity->GetField(UnitField::CREATED_BY_SPELL)); Spell && Spell->Effect[0]==28)
        {const uint32 Slot=FDbcStore::Get().SummonSlot(Spell->EffectMiscValueB[0]); if(Slot>=1 && Slot<=4) E.Event=TEXT("UNIT_DESTROYED");}
    }
    RecordCombat(MoveTemp(E));
}
void FWowPacketHandler::HandleSpellStart(FPacketReader& R)
{
    uint64 Object=0,Caster=0,Target=0;
    if(!Guid(R,Object) || !Guid(R,Caster) || !R.CanRead(13)) return;
    const uint8 Counter=R.ReadU8(); const uint32 Spell=R.ReadU32(),Flags=R.ReadU32(); const int32 Time=int32(R.ReadU32()); if(!Targets(R,Target)) return;
    if(Time>0) {const double Now=FPlatformTime::Seconds(); if(Casts.Num()>256) for(auto It=Casts.CreateIterator();It;++It) if(It.Value().Ends<Now) It.RemoveCurrent(); Casts.Add(Caster,{Spell,Counter,Now,Now+Time/1000.,false});}
    if(Time>0) {FWowCombatEntry E; E.Source=Caster; E.Spell=Spell; E.Event=TEXT("SPELL_CAST_START"); RecordSpellCombat(MoveTemp(E));}
    CastFailures.Remove(Caster);
    OnCastState.ExecuteIfBound(Caster,Spell,0,Counter); OnSpellStart.Broadcast(Caster,Spell,Flags,Time);
}
void FWowPacketHandler::HandleSpellGo(FPacketReader& R)
{
    uint64 Object=0,Caster=0,Target=0;
    if(!Guid(R,Object) || !Guid(R,Caster) || !R.CanRead(14)) return;
    const uint8 Counter=R.ReadU8(); const uint32 Spell=R.ReadU32(),Flags=R.ReadU32(); R.ReadU32(); const uint8 Hits=R.ReadU8();
    if(!R.CanRead(int32(Hits)*8+1)) return; for(uint8 I=0;I<Hits;++I) {const uint64 Hit=R.ReadU64(); if(!Target) Target=Hit;}
    const uint8 Misses=R.ReadU8(); TArray<FWowCombatEntry> Missed;
    const auto* Definition=FDbcStore::Get().Spells().GetById(Spell);
    for(uint8 I=0;I<Misses;++I)
    {
        if(!R.CanRead(9)) return; FWowCombatEntry E; E.Source=Caster; E.Destination=R.ReadU64(); E.Spell=Spell;
        const uint8 Reason=R.ReadU8(); const auto Miss=MissName(Reason); if(Miss.IsEmpty()) return;
        if(Reason==11) {if(!R.CanRead(1)) return; R.ReadU8();}
        E.Event=Definition && (Definition->AttributesEx3&0x8000)?TEXT("RANGE_MISSED"):TEXT("SPELL_MISSED"); E.Values={V::S(Miss)}; if(Reason==2 || Reason==5 || Reason==10) E.Values.Add(V::N(0)); Missed.Add(MoveTemp(E));
    }
    uint64 ExplicitTarget=0; if(!Targets(R,ExplicitTarget)) return; if(ExplicitTarget) Target=ExplicitTarget;
    const auto& Spells=FDbcStore::Get().Spells(); const bool HasCastTime=Definition && Spells.CastTime(Definition->CastingTimeIndex)>0;
    bool OpenLock=false; if(Definition) for(uint32 Effect:Definition->Effect) OpenLock|=Effect==33;
    if(!HasCastTime && !OpenLock) {FWowCombatEntry E; E.Source=Caster; E.Destination=Target; E.Spell=Spell; E.Event=TEXT("SPELL_CAST_SUCCESS"); RecordSpellCombat(MoveTemp(E));}
    for(auto& E:Missed) RecordSpellCombat(MoveTemp(E));
    const auto* Active=Casts.Find(Caster); const bool Stop=Active && Active->Spell==Spell && Active->Counter==Counter && !Active->Channel;
    if(Active && Active->Spell==Spell && Active->Counter==Counter) Casts.Remove(Caster);
    OnCastResult.ExecuteIfBound(Caster,Spell,Counter,true,0); OnCastState.ExecuteIfBound(Caster,Spell,1,Counter); OnSpellGo.Broadcast(Caster,Spell,Flags);
    if(Stop) OnCastState.ExecuteIfBound(Caster,Spell,8,Counter);
}
void FWowPacketHandler::HandleSpellFailure(FPacketReader& R)
{
    uint64 Caster=0; if(!Guid(R,Caster) || !R.CanRead(6)) return; const uint8 Counter=R.ReadU8(); const uint32 Spell=R.ReadU32(); const uint8 Reason=R.ReadU8();
    HandleCastFailure(Caster,Spell,Counter,Reason,false);
}
void FWowPacketHandler::HandleCastFailure(uint64 Caster,uint32 Spell,uint8 Counter,uint8 Reason,bool Private)
{
    // Suppress only the complementary packet for this identity, never a later
    // attempt with the same spell. Starts and local requests clear this pairing.
    if(const auto* Previous=CastFailures.Find(Caster); Previous && Previous->Spell==Spell && Previous->Counter==Counter && Previous->Private!=Private)
    {CastFailures.Remove(Caster); return;}
    if(CastFailures.Num()>=256) CastFailures.Reset();
    CastFailures.Add(Caster,{Spell,Counter,Private});
    RecordCastFailure(Caster,Spell,Reason);
    const auto* Active=Casts.Find(Caster);
    const bool DifferentCast=Active && (Active->Spell!=Spell || Active->Counter!=Counter);
    if(Active && !DifferentCast) Casts.Remove(Caster);
    OnCastResult.ExecuteIfBound(Caster,Spell,Counter,false,Reason);
    OnCastState.ExecuteIfBound(Caster,Spell,Reason==40?7:2,Counter);
    if(!DifferentCast) OnSpellFailure.Broadcast(Caster,Spell,Reason);
}
void FWowPacketHandler::RecordCastFailure(uint64 Caster,uint32 Spell,uint8 Reason)
{
    static const TCHAR* Keys[]={
#include "WowSpellFailureKeys.inl"
    };
    if(Reason && Reason!=27) {FWowCombatEntry E; E.Source=Caster; E.Spell=Spell; E.Event=TEXT("SPELL_CAST_FAILED"); E.Values={V::L(Reason<UE_ARRAY_COUNT(Keys)?Keys[Reason]:TEXT("SPELL_FAILED_ERROR"))}; RecordSpellCombat(MoveTemp(E));}
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
    if(R.Remaining()) return;
    FString Miss;
    if(Hit&0x10) Miss=TEXT("MISS");
    else if(State==2) Miss=TEXT("DODGE"); else if(State==3) Miss=TEXT("PARRY");
    else if(State==6) Miss=TEXT("EVADE"); else if(State==7) Miss=TEXT("IMMUNE"); else if(State==8) Miss=TEXT("DEFLECT");
    else if(!Damage && Absorb) Miss=TEXT("ABSORB"); else if(!Damage && Resist) Miss=TEXT("RESIST"); else if(!Damage && Blocked) Miss=TEXT("BLOCK");
    if(!Miss.IsEmpty()) { E.Event=TEXT("SWING_MISSED"); E.Values={V::S(Miss)}; if(Miss==TEXT("ABSORB") || Miss==TEXT("RESIST") || Miss==TEXT("BLOCK")) E.Values.Add(V::N(Miss==TEXT("ABSORB")?Absorb:Miss==TEXT("RESIST")?Resist:Blocked)); }
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
    const uint32 Absorb=R.ReadU32(),Resist=R.ReadU32(); const bool Periodic=R.ReadU8()!=0; R.ReadU8(); const uint32 Blocked=R.ReadU32(),Hit=R.ReadU32(); R.ReadU8();
    if(R.Remaining()) return;
    if(Hit&0x10) E.Source=E.Destination;
    SpellDamage(E,Damage,Overkill,E.School,Absorb,Resist,Blocked,(Hit&2)!=0,Periodic,(Hit&8)!=0);
    RecordSpellCombat(MoveTemp(E));
}
void FWowPacketHandler::HandleSpellHealLog(FPacketReader& R)
{
    FWowCombatEntry E;
    if(!Guid(R,E.Destination) || !Guid(R,E.Source) || !R.CanRead(18)) return;
    E.Spell=R.ReadU32(); const uint32 Amount=R.ReadU32(),Overheal=R.ReadU32(),Absorb=R.ReadU32(); const bool Critical=R.ReadU8()!=0; R.ReadU8();
    if(const auto* Spell=FDbcStore::Get().Spells().GetById(E.Spell)) E.School=Spell->SchoolMask;
    E.Event=TEXT("SPELL_HEAL"); E.Values={V::N(Amount),V::N(Overheal),V::N(Absorb),V::B(Critical)};
    if(!R.Remaining()) RecordSpellCombat(MoveTemp(E));
}

void FWowPacketHandler::HandleCombatPacket(uint32 Opcode,FPacketReader& R)
{
    FWowCombatEntry Base; TArray<FWowCombatEntry> Entries;
    const auto Push=[&](const FString& Event){Base.Event=Event; Entries.Add(Base);};
    if(Opcode==0x0139 || Opcode==0x013A)
    {
        uint64 Caster=0; if(!Guid(R,Caster) || !R.CanRead(Opcode==0x0139?8:4)) return;
        const uint32 Spell=Opcode==0x0139?R.ReadU32():(Casts.Find(Caster)?Casts[Caster].Spell:0); const uint32 Duration=R.ReadU32(); if(R.Remaining()) return;
        const auto* Old=Casts.Find(Caster); const uint8 Counter=Old?Old->Counter:0; const double Started=Old?Old->Started:FPlatformTime::Seconds();
        if(!Duration) Casts.Remove(Caster);
        else if(Spell) {const double Now=FPlatformTime::Seconds(); Casts.Add(Caster,{Spell,Counter,Started,Now+Duration/1000.,true});}
        OnCastState.ExecuteIfBound(Caster,Spell,!Duration?5:Opcode==0x139?3:4,Counter); return;
    }
    else if(Opcode==0x0032)
    {
        uint64 Owner=0; if(!Guid(R,Base.Destination) || !Guid(R,Base.Source) || !Guid(R,Owner) || !R.CanRead(8)) return;
        const int32 Damage=int32(R.ReadU32()); Base.Spell=R.ReadU32(); const auto* Spell=FDbcStore::Get().Spells().GetById(Base.Spell); const uint32 School=Spell?Spell->SchoolMask:1;
        if(Damage>0) {Base.Values={V::N(Damage),V::N(0),V::N(School),V::N(0),V::N(0),V::N(0),V::B(false),V::B(false),V::B(false)}; Push(TEXT("SPELL_BUILDING_DAMAGE"));}
        else {Base.Values={V::N(-int64(Damage)),V::N(0),V::N(0),V::B(false)}; Push(TEXT("SPELL_BUILDING_HEAL"));}
    }
    else if(Opcode==0x0130)
    {
        if(!R.CanRead(6)) return; const uint8 Counter=R.ReadU8(); const uint32 Spell=R.ReadU32(); const uint8 Reason=R.ReadU8();
        // Selected failures include additional numeric parameters for the error UI.
        HandleCastFailure(EntityManager.LocalPlayerGuid,Spell,Counter,Reason,true); return;
    }
    else if(Opcode==0x024E) // periodic aura log; every effect is validated before publishing any
    {
        if(!Guid(R,Base.Destination) || !Guid(R,Base.Source) || !R.CanRead(8)) return;
        Base.Spell=R.ReadU32(); const uint32 Count=R.ReadU32(); if(!Count || Count>64) return;
        for(uint32 I=0;I<Count;++I)
        {
            if(!R.CanRead(4)) return; const uint32 Type=R.ReadU32(); auto E=Base;
            if(Type==3 || Type==89)
            {if(!R.CanRead(21)) return; const uint32 Amount=R.ReadU32(),Over=R.ReadU32(),School=R.ReadU32(),Absorb=R.ReadU32(),Resist=R.ReadU32(); const bool Critical=R.ReadU8()!=0; SpellDamage(E,Amount,Over,School,Absorb,Resist,0,Critical,true);}
            else if(Type==8 || Type==20)
            {if(!R.CanRead(13)) return; E.Event=TEXT("SPELL_PERIODIC_HEAL"); const uint32 Amount=R.ReadU32(),Over=R.ReadU32(),Absorb=R.ReadU32(); E.Values={V::N(Amount),V::N(Over),V::N(Absorb),V::B(R.ReadU8()!=0)};}
            else if(Type==21 || Type==24 || Type==64)
            {if(!R.CanRead(Type==64?12:8)) return; const uint32 Power=R.ReadU32(),Amount=R.ReadU32(); E.Event=TEXT("SPELL_PERIODIC_ENERGIZE"); E.Values={V::N(Amount/PowerMod(Power)),V::N(Power)}; if(Type==64) {const float Multiplier=R.ReadFloat(); if(!FMath::IsFinite(Multiplier) || Multiplier<0) return; if(Power==4) continue; PowerDrain(E,Amount,Power,Multiplier,true);}}
            else return;
            Entries.Add(MoveTemp(E));
        }
    }
    else if(Opcode==0x0151)
    {
        if(!Guid(R,Base.Destination) || !Guid(R,Base.Source) || !R.CanRead(12)) return;
        Base.Spell=R.ReadU32(); const uint32 Power=R.ReadU32(),Amount=R.ReadU32(); Base.Values={V::N(Amount/PowerMod(Power)),V::N(Power)};
        const auto* Spell=FDbcStore::Get().Spells().GetById(Base.Spell); Push(Spell && (Spell->AttributesEx3&0x2000000)?TEXT("SPELL_PERIODIC_ENERGIZE"):TEXT("SPELL_ENERGIZE"));
    }
    else if(Opcode==0x024B)
    {
        if(!R.CanRead(17)) return; Base.Spell=R.ReadU32(); Base.Source=R.ReadU64(); R.ReadU8(); const uint32 Count=R.ReadU32(); if(Count>256 || R.Remaining()!=int64(Count)*9) return;
        for(uint32 I=0;I<Count;++I) {Base.Destination=R.ReadU64(); const auto Miss=MissName(R.ReadU8()); if(Miss.IsEmpty()) return; Base.Values={V::S(Miss)}; if(Miss==TEXT("ABSORB") || Miss==TEXT("BLOCK") || Miss==TEXT("RESIST")) Base.Values.Add(V::N(0)); Push(TEXT("DAMAGE_SHIELD_MISSED"));}
    }
    else if(Opcode==0x014F || Opcode==0x027B || Opcode==0x0333)
    {
        if(!Guid(R,Base.Destination) || !Guid(R,Base.Source) || !R.CanRead(9)) return;
        Base.Spell=R.ReadU32(); R.ReadU8(); const uint32 Count=R.ReadU32(); if(Count>256 || R.Remaining()!=int64(Count)*5) return;
        for(uint32 I=0;I<Count;++I)
        {
            const uint32 Removed=R.ReadU32(); const bool Harmful=R.ReadU8()!=0; Base.Values.Reset(); auto E=Base;
            if(Opcode==0x014F) {E.Spell=Removed; E.Event=Base.Spell?TEXT("SPELL_AURA_BROKEN_SPELL"):TEXT("SPELL_AURA_BROKEN"); if(Base.Spell) ExtraSpell(E,Base.Spell);}
            else {E.Event=Opcode==0x0333?TEXT("SPELL_STOLEN"):TEXT("SPELL_DISPEL"); ExtraSpell(E,Removed);}
            E.Values.Add(V::S(Harmful?TEXT("DEBUFF"):TEXT("BUFF"))); Entries.Add(MoveTemp(E));
        }
    }
    else if(Opcode==0x0262)
    {
        if(!R.CanRead(24) || (R.Remaining()-20)%4) return; Base.Source=R.ReadU64(); Base.Destination=R.ReadU64(); Base.Spell=R.ReadU32();
        while(R.CanRead(4)) {auto E=Base; E.Event=TEXT("SPELL_DISPEL_FAILED"); ExtraSpell(E,R.ReadU32()); Entries.Add(MoveTemp(E));}
    }
    else if(Opcode==0x0260 || Opcode==0x0263)
    {
        if(!R.CanRead(21)) return; Base.Source=R.ReadU64(); Base.Destination=R.ReadU64(); Base.Spell=R.ReadU32(); R.ReadU8(); Base.Values={V::S(Opcode==0x0260?TEXT("RESIST"):TEXT("IMMUNE"))}; if(Opcode==0x0260) Base.Values.Add(V::N(0)); Push(TEXT("SPELL_MISSED"));
    }
    else if(Opcode==0x024F)
    {
        if(!R.CanRead(32)) return; Base.Source=R.ReadU64(); Base.Destination=R.ReadU64(); Base.Spell=R.ReadU32(); const uint32 Amount=R.ReadU32(),Over=R.ReadU32(),School=R.ReadU32(); Base.School=School;
        Base.Values={V::N(Amount),V::N(Over),V::N(School),V::N(0),V::N(0),V::N(0),V::B(false),V::B(false),V::B(false)}; Push(TEXT("DAMAGE_SHIELD"));
    }
    else if(Opcode==0x01F5 || Opcode==0x032F)
    {
        if(!R.CanRead(Opcode==0x01F5?16:20)) return; Base.Source=R.ReadU64(); Base.Destination=R.ReadU64(); if(Opcode==0x032F) Base.Spell=R.ReadU32(); Push(Opcode==0x01F5?TEXT("PARTY_KILL"):TEXT("SPELL_INSTAKILL"));
    }
    else if(Opcode==0x01D7)
    {
        if(!Guid(R,Base.Destination) || !Guid(R,Base.Source) || !R.CanRead(8)) return; const uint32 Item=R.ReadU32(),Enchant=R.ReadU32(); const auto* Row=FDbcStore::Get().ItemUi().Enchant(Enchant); if(!Row || (Row->Flags&2)) return;
        const auto* Template=ItemTemplates.Find(Item); Base.Values={V::S(Row->Name),V::N(Item),V::S(Template?Template->Name:FString::FromInt(Item))}; Push(Base.Source?TEXT("ENCHANT_APPLIED"):TEXT("ENCHANT_REMOVED"));
    }
    else if(Opcode==0x024C)
    {
        if(!Guid(R,Base.Source) || !R.CanRead(8)) return; Base.Spell=R.ReadU32(); const uint32 Count=R.ReadU32(); if(!Count || Count>32) return;
        for(uint32 I=0;I<Count;++I)
        {
            if(!R.CanRead(8)) return; const uint32 Effect=R.ReadU32(),Targets=R.ReadU32(); if(Targets>256) return;
            for(uint32 J=0;J<Targets;++J)
            {
                auto E=Base;
                if(Effect==24 || Effect==59 || Effect==101 || Effect==157) {if(!R.CanRead(4)) return; R.ReadU32(); continue;} // trade-skill/item messages are not combat events
                if(!Guid(R,E.Destination)) return;
                switch(Effect)
                {
                case 8: case 62: {if(!R.CanRead(12)) return; const uint32 Amount=R.ReadU32(),Power=R.ReadU32(); const float Multiplier=R.ReadFloat(); if(!FMath::IsFinite(Multiplier) || Multiplier<0) return; if(Power==4) continue; const auto* Spell=FDbcStore::Get().Spells().GetById(E.Spell); PowerDrain(E,Amount,Power,Multiplier,Spell && (Spell->AttributesEx3&0x2000000)); break;}
                case 18: case 113: E.Event=TEXT("SPELL_RESURRECT"); break;
                case 19: if(!R.CanRead(4)) return; E.Event=TEXT("SPELL_EXTRA_ATTACKS"); E.Values={V::N(R.ReadU32())}; break;
                case 28: case 56: case 104: E.Event=TEXT("SPELL_SUMMON"); break;
                case 50: case 76: case 81: case 83: case 105: case 106: case 107: E.Event=TEXT("SPELL_CREATE"); break;
                case 68: if(!R.CanRead(4)) return; E.Event=TEXT("SPELL_INTERRUPT"); ExtraSpell(E,R.ReadU32()); break;
                case 111: {if(!R.CanRead(8)) return; const int32 Item=int32(R.ReadU32()),Slot=int32(R.ReadU32()); E.Event=Item==-1 && Slot==-1?TEXT("SPELL_DURABILITY_DAMAGE_ALL"):TEXT("SPELL_DURABILITY_DAMAGE"); if(E.Event==TEXT("SPELL_DURABILITY_DAMAGE")) {const auto* Template=ItemTemplates.Find(Item); E.Values={V::N(Item),V::S(Template?Template->Name:FString::FromInt(Item))};} break;}
                case 33: case 102: continue; // open-lock and dismiss-pet have no combat-log row
                default: return; // unknown record length: preserve history rather than desynchronize
                }
                Entries.Add(MoveTemp(E));
            }
        }
    }
    if(R.Remaining()) return;
    for(auto& E:Entries) {if(E.Spell) RecordSpellCombat(MoveTemp(E)); else RecordCombat(MoveTemp(E));}
}

void FWowPacketHandler::HandleAuraUpdate(FPacketReader& R) {uint64 Target=0; if(Guid(R,Target)) HandleAuraRecords(R,Target);}
void FWowPacketHandler::HandleAuraUpdateAll(FPacketReader& R) {uint64 Target=0; if(Guid(R,Target)) HandleAuraRecords(R,Target,true);}
void FWowPacketHandler::HandleAuraRecords(FPacketReader& R,uint64 TargetGuid,bool All)
{
    auto* Entity=EntityManager.Find(TargetGuid); if(!Entity) return;
    const auto Previous=Entity->Auras; auto Next=Previous; if(All) for(auto& Aura:Next) Aura=FAuraInfo();
    TSet<uint8> Slots;
    while(R.Remaining())
    {
        if(!R.CanRead(5)) return; const uint8 Slot=R.ReadU8(); const uint32 Id=R.ReadU32();
        if(!Next.IsValidIndex(Slot) || Slots.Contains(Slot)) return; Slots.Add(Slot);
        FAuraInfo Aura; Aura.SpellId=Id;
        if(Id)
        {
            if(!R.CanRead(3)) return; Aura.Flags=R.ReadU8(); Aura.Level=R.ReadU8(); Aura.Charges=R.ReadU8();
            if(Aura.Flags&0x08) Aura.CasterGuid=TargetGuid; else if(!Guid(R,Aura.CasterGuid)) return;
            if(Aura.Flags&0x20) {if(!R.CanRead(8)) return; Aura.DurationMs=R.ReadU32(); Aura.RemainingMs=R.ReadU32();}
            Aura.bActive=true; Aura.UpdatedAt=FPlatformTime::Seconds();
        }
        Next[Slot]=Aura;
    }
    Entity->Auras=MoveTemp(Next); RecordAuraChanges(TargetGuid,Previous,All); OnUnitAuraUpdate.Broadcast(TargetGuid);
}
void FWowPacketHandler::RecordAuraChanges(uint64 Target,const TArray<FAuraInfo>& Previous,bool Initial)
{
    const auto* Entity=EntityManager.Find(Target); if(!Entity || Initial) return;
    const auto Emit=[&](const FAuraInfo& Aura,const TCHAR* Event,bool Dose=false)
    {
        const auto* Spell=FDbcStore::Get().Spells().GetById(Aura.SpellId);
        if(Spell && ((Spell->Attributes&0x40) || (Spell->AttributesEx6&0x400))) return;
        FWowCombatEntry E; E.Source=Aura.CasterGuid; E.Destination=Target; E.Spell=Aura.SpellId; E.Event=Event;
        E.Values.Add(V::S(Aura.Flags&0x80?TEXT("DEBUFF"):TEXT("BUFF"))); if(Dose) E.Values.Add(V::N(Aura.Charges)); RecordSpellCombat(MoveTemp(E));
    };
    for(int32 I=0;I<Previous.Num() && I<Entity->Auras.Num();++I)
    {
        const auto& Old=Previous[I]; const auto& Current=Entity->Auras[I]; const bool Same=Old.bActive && Current.bActive && Old.SpellId==Current.SpellId && Old.CasterGuid==Current.CasterGuid;
        if(Same)
        {
            const auto* Spell=FDbcStore::Get().Spells().GetById(Current.SpellId);
            if(Current.Charges!=Old.Charges && (!Spell || Spell->StackAmount>1)) Emit(Current,Current.Charges>Old.Charges?TEXT("SPELL_AURA_APPLIED_DOSE"):TEXT("SPELL_AURA_REMOVED_DOSE"),true);
            else if(Current.UpdatedAt!=Old.UpdatedAt) Emit(Current,TEXT("SPELL_AURA_REFRESH"));
        }
        else
        {
            if(Old.bActive && Entity->GetHealth()>0) Emit(Old,TEXT("SPELL_AURA_REMOVED"));
            if(Current.bActive) Emit(Current,TEXT("SPELL_AURA_APPLIED"));
        }
    }
}
