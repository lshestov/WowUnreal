#include "Misc/AutomationTest.h"
#include "WowPacketHandler.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/M2Types.h"
#include "WowDoodadManager.h"
#include "WowM2EmitterComponent.h"
#include "Mpq/MpqManager.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
void Word(TArray<uint8>& P,uint32 V) {P.Append(reinterpret_cast<const uint8*>(&V),4);}
void Wide(TArray<uint8>& P,uint64 V) {P.Append(reinterpret_cast<const uint8*>(&V),8);}
void Float(TArray<uint8>& P,float V) {P.Append(reinterpret_cast<const uint8*>(&V),4);}
TArray<uint8> GroundGo()
{
    TArray<uint8> P={1,1,1,1,37}; Word(P,10); Word(P,0); Word(P,0xfffffff0); P.Add(2); Wide(P,2); Wide(P,3); P.Add(0);
    Word(P,0x60); P.Add(0); Float(P,1); Float(P,2); Float(P,3); P.Add(0); Float(P,4); Float(P,5); Float(P,6); return P;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellGroundTargetPacket,"WowUnreal.Gameplay.SpellGroundTargets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FSpellGroundTargetPacket::RunTest(const FString&)
{
    TestTrue(TEXT("Spell visual attachment enums resolve separately from raw M2 IDs"),FSpellVisualDbcEntry::EffectAttachment(1)==34 && FSpellVisualDbcEntry::EffectAttachment(3)==21 && FSpellVisualDbcEntry::EffectAttachment(1,true)==1 && FSpellVisualDbcEntry::EffectAttachment(MAX_uint32)==MAX_uint32);
    FWowPacketHandler H; int32 Count=0; FWowSpellLaunch Last;
    H.OnSpellLaunch.AddLambda([&](const FWowSpellLaunch& L){Last=L; ++Count;}); const auto P=GroundGo();
    H.HandlePacket(WowOpcode::SMSG_SPELL_GO,P);
    TestTrue(TEXT("Ground source, destination, every hit and cast identity survive the packet"),Count==1 && Last.HasSource && Last.HasDestination && Last.Source==FVector(1,2,3) && Last.Destination==FVector(4,5,6) && Last.HitTargets==TArray<uint64>{2,3} && Last.Counter==37 && Last.ReceivedAt>0);
    for(int32 Cut=1;Cut<=25;++Cut) {auto Short=P; Short.SetNum(P.Num()-Cut); H.HandlePacket(WowOpcode::SMSG_SPELL_GO,Short);}
    TestEqual(TEXT("A partial ground target cannot release or impact an existing cast"),Count,1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellDelayChannelIdentity,"WowUnreal.Gameplay.SpellDelayChannelIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FSpellDelayChannelIdentity::RunTest(const FString&)
{
    FWowPacketHandler H; const double Now=FPlatformTime::Seconds(); H.Casts.Add(1,{133,37,Now,Now+3,false});
    uint8 Kind=0,Counter=0; H.OnCastState.BindLambda([&](uint64,uint32,uint8 K,uint8 C){Kind=K; Counter=C;});
    TArray<uint8> P={1,1}; Word(P,500); H.HandlePacket(0x1e2,P);
    TestTrue(TEXT("Server pushback extends the matching cast, with the delayed UI event and original counter"),H.Casts[1].Ends==Now+3.5 && Kind==9 && Counter==37);
    auto Short=P; Short.Pop(); H.HandlePacket(0x1e2,Short); TestEqual(TEXT("Partial pushback cannot change the deadline"),H.Casts[1].Ends,Now+3.5);
    H.Casts[1].Spell=10; H.HandlePacket(WowOpcode::SMSG_SPELL_GO,GroundGo());
    P={1,1}; Word(P,10); Word(P,8000); H.HandlePacket(0x139,P);
    TestTrue(TEXT("Channel start following GO retains the release counter after the precast is removed"),H.Casts[1].Channel && H.Casts[1].Spell==10 && H.Casts[1].Counter==37);
    const double End=H.Casts[1].Ends; P={1,1}; Word(P,500); H.HandlePacket(0x1e2,P); TestEqual(TEXT("Cast pushback cannot extend a channel"),H.Casts[1].Ends,End);
    P={1,1}; Word(P,0); H.HandlePacket(0x13a,P); TestTrue(TEXT("Zero channel update removes the channel"),!H.Casts.Contains(1));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellDynamicObjectPosition,"WowUnreal.Gameplay.SpellDynamicObjectPosition",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FSpellDynamicObjectPosition::RunTest(const FString&)
{
    FWowPacketHandler H; TArray<uint8> P; Word(P,2);
    const auto Guid=[&](uint64 V){uint8 Mask=0; TArray<uint8> Bytes; for(int32 I=0;I<8;++I) if(uint8 B=V>>(I*8)) {Mask|=1<<I; Bytes.Add(B);} P.Add(Mask); P.Append(Bytes);};
    for(uint64 Id:{0xF100000000000001ULL,0xF100000000000002ULL})
    {
        P.Add(2); Guid(Id); P.Add(6); const uint16 Flags=0x150; P.Append(reinterpret_cast<const uint8*>(&Flags),2);
        P.Add(0); for(float V:{10.f,20.f,30.f,10.f,20.f,30.f,.5f,0.f}) Float(P,V); Word(P,Id&0xffffffff);
        P.Add(1); Word(P,0xfff); Wide(P,Id); Word(P,65); Word(P,0); Word(P,0); Float(P,1); Wide(P,17); Word(P,0); Word(P,10); Float(P,8); Word(P,1234);
    }
    H.HandlePacket(WowOpcode::SMSG_UPDATE_OBJECT,P);
    for(uint64 Id:{0xF100000000000001ULL,0xF100000000000002ULL})
    {
        const auto* E=H.EntityManager.FindDynamicObject(Id);
        TestTrue(TEXT("Dynamic object POSITION takes precedence over STATIONARY_POSITION without shifting the next block"),E && E->GetCasterGuid()==17 && E->GetSpellId()==10 && E->GetRadius()==8 && E->Movement.Position==FVector(10,20,30));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellPhaseModelCoverage,"WowUnreal.Gameplay.SpellPhaseModelCoverage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FSpellPhaseModelCoverage::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false; FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& D=FDbcStore::Get(); if(!D.IsLoaded()) D.LoadAll(Mpq); if(!D.IsDeferredLoaded()) D.LoadDeferred(Mpq);
    TestTrue(TEXT("Original area procedure model resolves from its DBC"),!D.SpellVisualKits().AreaModel(0).IsEmpty() && Mpq.FileExists(D.SpellVisualKits().AreaModel(0)));
    TSet<uint32> Checked; int32 Geometry=0,Particles=0,Tails=0;
    const auto Check=[&](uint32 Id)
    {
        if(!Id || Checked.Contains(Id)) return; Checked.Add(Id);
        const auto* Effect=D.SpellVisualEffectNames().GetById(Id); if(!TestNotNull(TEXT("Effect definition exists"),Effect)) return;
        const auto Model=FWowDoodadManager::GetOrParseM2(Effect->FilePath,&Mpq);
        if(!TestTrue(FString::Printf(TEXT("Original phase model parses: %s"),*Effect->FilePath),Model && Model->bIsValid)) return;
        Geometry+=!Model->Vertices.IsEmpty(); Particles+=!Model->ParticleEmitters.IsEmpty();
        for(const auto& E:Model->ParticleEmitters) Tails+=(E.Flags&0x40000)!=0;
        const float Duration=UWowM2EmitterComponent::OneShotSeconds(*Model); TestTrue(TEXT("Every one-shot has a finite bounded authored duration"),Duration>=.05f && Duration<=30 && FMath::IsFinite(Duration));
        AddInfo(FString::Printf(TEXT("PhaseModel effect=%u path=%s verts=%d emitters=%d duration=%.3f"),Id,*Effect->FilePath,Model->Vertices.Num(),Model->ParticleEmitters.Num(),Duration));
    };
    for(uint32 Id:{133u,116u,585u,2061u,139u,17u,1459u,5143u,10u,1449u})
    {
        const auto* Spell=D.Spells().GetById(Id); const auto* V=Spell?D.SpellVisuals().GetById(Spell->SpellVisual[0]):nullptr; if(!TestNotNull(TEXT("Representative spell has a visual"),V)) continue;
        for(uint32 KitId:{V->PrecastKit,V->CastKit,V->ImpactKit,V->StateKit,V->StateDoneKit,V->ChannelKit,V->InstantAreaKit,V->ImpactAreaKit,V->PersistentAreaKit})
            if(const auto* K=D.SpellVisualKits().GetById(KitId)) for(uint32 E:{K->HeadEffectID,K->ChestEffectID,K->BaseEffectID,K->LeftHandEffectID,K->RightHandEffectID,K->BreathEffectID,K->WorldEffectID}) Check(E);
        if(V->HasMissile && V->MissileModel>0) Check(V->MissileModel);
    }
    TestTrue(TEXT("Coverage includes authored geometry, particle-only effects and tails"),Geometry>=3 && Particles>=10 && Tails>=1 && Checked.Num()>=15);
    return true;
}
#endif
