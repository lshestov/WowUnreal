#include "Misc/AutomationTest.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/SpellVisualDbc.h"
#include "Formats/Dbc/SpellVisualKitDbc.h"
#include "Formats/Dbc/SpellVisualEffectNameDbc.h"
#include "Mpq/MpqManager.h"
#include "WowPacketHandler.h"
#include "WowSpellMissile.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
TArray<uint8> LaunchPacket(uint64 Explicit,const TArray<uint64>& Hits,const TArray<FWowSpellMissTarget>& Misses)
{
    TArray<uint8> P;
    const auto U32=[&](uint32 V){P.Append(reinterpret_cast<const uint8*>(&V),4);};
    const auto U64=[&](uint64 V){P.Append(reinterpret_cast<const uint8*>(&V),8);};
    const auto Guid=[&](uint64 V){uint8 Mask=0; TArray<uint8> B; for(int32 I=0;I<8;++I) if(uint8 X=V>>(8*I)) {Mask|=1<<I; B.Add(X);} P.Add(Mask); P.Append(B);};
    Guid(0x4000000000000001ULL); Guid(1); P.Add(9); U32(133); U32(0); U32(0xfffffff0u);
    P.Add(Hits.Num()); for(uint64 V:Hits) U64(V);
    P.Add(Misses.Num()); for(const auto& M:Misses) {U64(M.Guid); P.Add(M.Reason); if(M.Reason==11) P.Add(M.ReflectReason);}
    U32(Explicit?2:0); if(Explicit) Guid(Explicit);
    return P;
}
uint32 VisualFloat(float Value)
{
    uint32 Bits=0; FMemory::Memcpy(&Bits,&Value,sizeof(Bits)); return Bits;
}
TArray<uint8> VisualTable(const TArray<uint32>& Columns,const TArray<uint8>& Strings={0})
{
    TArray<uint8> Bytes;
    const uint32 Header[]={0x43424457,1,uint32(Columns.Num()),uint32(Columns.Num()*4),uint32(Strings.Num())};
    Bytes.Append(reinterpret_cast<const uint8*>(Header),sizeof(Header));
    Bytes.Append(reinterpret_cast<const uint8*>(Columns.GetData()),Columns.Num()*4);
    Bytes.Append(Strings); return Bytes;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellLaunchTargets,"WowUnreal.Gameplay.SpellLaunchTargets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FSpellLaunchTargets::RunTest(const FString&)
{
    FWowPacketHandler H; int32 Events=0; FWowSpellLaunch Last;
    H.OnSpellLaunch.AddLambda([&](const FWowSpellLaunch& E){++Events; Last=E;});
    const auto Packet=LaunchPacket(4,{2,3},{{5,2,0},{6,11,1}});
    H.HandlePacket(WowOpcode::SMSG_SPELL_GO,Packet);
    TestEqual(TEXT("A complete server launch publishes once"),Events,1);
    TestTrue(TEXT("Object, caster, spell, counter and wrapping server timestamp survive"),Last.CastObjectGuid==0x4000000000000001ULL && Last.CasterGuid==1 && Last.SpellId==133 && Last.Counter==9 && Last.ServerTimeMs==0xfffffff0u);
    TestTrue(TEXT("Explicit target has priority without losing other hits or reflected misses"),Last.PrimaryTarget()==4 && Last.HitTargets==TArray<uint64>{2,3} && Last.MissedTargets.Num()==2 && Last.MissedTargets[1].Guid==6 && Last.MissedTargets[1].Reason==11 && Last.MissedTargets[1].ReflectReason==1);
    auto& Caster=H.EntityManager.PromoteToTyped(1,WowTypeMask::OBJECT|WowTypeMask::UNIT); Caster.SetField(UnitField::TARGET,99); Caster.SetField(UnitField::TARGET+1,0);
    TestEqual(TEXT("Changing the selected unit cannot change the launch's target"),Last.PrimaryTarget(),uint64(4));
    for(const int32 Cut:{1,2,8,16}) {auto Short=Packet; Short.SetNum(Packet.Num()-Cut); H.HandlePacket(WowOpcode::SMSG_SPELL_GO,Short);}
    TestEqual(TEXT("Partial hit/miss/explicit-target data never publishes a launch"),Events,1);
    H.HandlePacket(WowOpcode::SMSG_SPELL_GO,LaunchPacket(0,{7,8},{}));
    TestEqual(TEXT("Implicit unit spells use their first server hit"),Last.PrimaryTarget(),uint64(7));
    H.HandlePacket(WowOpcode::SMSG_SPELL_GO,LaunchPacket(0,{},{{9,1,0}}));
    TestEqual(TEXT("A missed projectile retains its actual target"),Last.PrimaryTarget(),uint64(9));
    H.HandlePacket(WowOpcode::SMSG_SPELL_GO,LaunchPacket(0,{},{}));
    TestEqual(TEXT("No target stays absent instead of inheriting the selection"),Last.PrimaryTarget(),uint64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellFlightTiming,"WowUnreal.Gameplay.SpellFlightTiming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FSpellFlightTiming::RunTest(const FString&)
{
    TestEqual(TEXT("Close targets use the server's five-yard floor"),AWowSpellMissile::TravelMilliseconds(4,1.5f,24),uint32(208));
    TestEqual(TEXT("Caster combat reach is subtracted before integer milliseconds"),AWowSpellMissile::TravelMilliseconds(30,1.5f,24),uint32(1187));
    TestEqual(TEXT("A faster DBC projectile has a shorter duration"),AWowSpellMissile::TravelMilliseconds(30,1.5f,48),uint32(593));
    TestEqual(TEXT("Instant spells do not invent a one-second projectile"),AWowSpellMissile::TravelMilliseconds(30,1.5f,0),uint32(0));
    TestEqual(TEXT("Invalid numeric data cannot retain an immortal actor"),AWowSpellMissile::TravelMilliseconds(std::numeric_limits<float>::quiet_NaN(),0,24),uint32(0));
    TestEqual(TEXT("Renderer lifetime is bounded for extreme data"),AWowSpellMissile::TravelMilliseconds(10000,0,1),uint32(60000));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellVisualLayouts,"WowUnreal.Gameplay.SpellVisualLayouts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FSpellVisualLayouts::RunTest(const FString&)
{
    // Build-12340 wire layouts: SpellVisual=32 columns, SpellVisualKit=38,
    // SpellVisualEffectName=7. Distinct values catch shifted identifiers.
    TArray<uint32> V; V.Init(0,32); V[0]=73;
    for(int32 I=1;I<26;++I) V[I]=100+I;
    V[17]=uint32(-7); V[19]=uint32(-3);
    const float Offsets[]={1.25f,-2.5f,3.75f,-4.5f,5.25f,-6.75f};
    for(int32 I=0;I<6;++I) V[26+I]=VisualFloat(Offsets[I]);
    FDbcParser VP; FSpellVisualDbc Visuals;
    if(!TestTrue(TEXT("Parse the exact Wrath visual layout"),VP.Parse(VisualTable(V)) && Visuals.Load(VP))) return false;
    const auto* Visual=Visuals.GetById(73); if(!Visual) return false;
    TestTrue(TEXT("Sound, flags and impact kits precede the attachment rather than the float offsets"),Visual->MissileSound==111 && Visual->AnimEventSoundID==112 && Visual->Flags==113 && Visual->CasterImpactKit==114 && Visual->TargetImpactKit==115 && Visual->MissileAttachment==116);
    TestTrue(TEXT("Ground-follow values retain their signed integer representation"),Visual->MissileFollowGroundHeight==-7 && Visual->MissileFollowGroundDropSpeed==118 && Visual->MissileFollowGroundApproach==-3);
    TestTrue(TEXT("All six cast/impact offsets come from the final six columns"),Visual->MissileCastOffsetX==1.25f && Visual->MissileCastOffsetY==-2.5f && Visual->MissileCastOffsetZ==3.75f && Visual->MissileImpactOffsetX==-4.5f && Visual->MissileImpactOffsetY==5.25f && Visual->MissileImpactOffsetZ==-6.75f);
    TestTrue(TEXT("Area and targeting kits are independent of the offsets"),Visual->MissileTargetingKit==122 && Visual->InstantAreaKit==123 && Visual->ImpactAreaKit==124 && Visual->PersistentAreaKit==125);

    TArray<uint32> K; K.SetNum(38); K[0]=91; for(int32 I=1;I<38;++I) K[I]=1000+I; K[1]=uint32(-1);
    FDbcParser KP; FSpellVisualKitDbc Kits;
    if(!TestTrue(TEXT("Parse the exact Wrath kit layout"),KP.Parse(VisualTable(K)) && Kits.Load(KP))) return false;
    const auto* Kit=Kits.GetById(91); if(!Kit) return false;
    TestTrue(TEXT("Animation sentinels remain signed and head/chest/base have no invented AnimKit column"),Kit->StartAnimID==-1 && Kit->AnimID==1002 && Kit->HeadEffectID==1003 && Kit->ChestEffectID==1004 && Kit->BaseEffectID==1005);
    TestTrue(TEXT("Hand, breath and weapon slots retain their own effect IDs"),Kit->LeftHandEffectID==1006 && Kit->RightHandEffectID==1007 && Kit->BreathEffectID==1008 && Kit->LeftWeaponEffectID==1009 && Kit->RightWeaponEffectID==1010);
    TestTrue(TEXT("Special/world effects do not masquerade as kit sound and shake"),Kit->SpecialEffectIDs[2]==1013 && Kit->WorldEffectID==1014 && Kit->SoundID==1015 && Kit->ShakeID==1016 && Kit->Flags==1037);

    const char Text[]="\0Friendly effect\0Spells\\Test.MDX\0";
    TArray<uint8> Strings; Strings.Append(reinterpret_cast<const uint8*>(Text),sizeof(Text));
    TArray<uint32> E={19,1,17,VisualFloat(2),VisualFloat(1.5f),VisualFloat(.5f),VisualFloat(3)};
    FDbcParser EP; FSpellVisualEffectNameDbc Effects;
    if(!TestTrue(TEXT("Parse effect scale bounds and strings"),EP.Parse(VisualTable(E,Strings)) && Effects.Load(EP))) return false;
    const auto* Effect=Effects.GetById(19); if(!Effect) return false;
    TestTrue(TEXT("Model extension normalization and scale limits preserve the original effect name"),Effect->Name==TEXT("Friendly effect") && Effect->FilePath==TEXT("Spells\\Test.m2") && Effect->Scale==1.5f && Effect->MinAllowedScale==.5f && Effect->MaxAllowedScale==3);
    V.SetNum(31); K.SetNum(37); E.SetNum(6);
    VP.Parse(VisualTable(V)); KP.Parse(VisualTable(K)); EP.Parse(VisualTable(E));
    TestFalse(TEXT("Reject another visual schema rather than reading beyond its row"),Visuals.Load(VP));
    TestFalse(TEXT("Reject another kit schema"),Kits.Load(KP));
    TestFalse(TEXT("Reject an incomplete effect schema"),Effects.Load(EP));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellVisualMpq,"WowUnreal.Gameplay.SpellVisualMpqChain",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FSpellVisualMpq::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    auto& D=FDbcStore::Get(); if(!D.IsLoaded()) D.LoadAll(Mpq); if(!D.IsDeferredLoaded()) D.LoadDeferred(Mpq);
    TSet<uint32> CheckedKits,CheckedEffects;
    for(const uint32 SpellId:{133u,116u,1459u,585u,2061u,139u,17u})
    {
        const auto* Spell=D.Spells().GetById(SpellId);
        if(!TestNotNull(FString::Printf(TEXT("Real spell %u exists"),SpellId),Spell)) continue;
        const auto* Visual=D.SpellVisuals().GetById(Spell->SpellVisual[0]);
        if(!TestNotNull(FString::Printf(TEXT("Real spell %u resolves its visual"),SpellId),Visual)) continue;
        for(const uint32 KitId:{Visual->PrecastKit,Visual->CastKit,Visual->ImpactKit,Visual->StateKit,Visual->StateDoneKit,Visual->ChannelKit,Visual->CasterImpactKit,Visual->TargetImpactKit})
        {
            if(!KitId || CheckedKits.Contains(KitId)) continue; CheckedKits.Add(KitId);
            const auto* Kit=D.SpellVisualKits().GetById(KitId);
            if(!TestNotNull(FString::Printf(TEXT("Visual kit %u resolves"),KitId),Kit)) continue;
            if(Kit->SoundID) TestNotNull(FString::Printf(TEXT("Kit %u resolves its real sound"),KitId),D.SoundEntries().GetById(Kit->SoundID));
            for(const uint32 EffectId:{Kit->HeadEffectID,Kit->ChestEffectID,Kit->BaseEffectID,Kit->LeftHandEffectID,Kit->RightHandEffectID,Kit->BreathEffectID,Kit->LeftWeaponEffectID,Kit->RightWeaponEffectID})
            {
                if(!EffectId || CheckedEffects.Contains(EffectId)) continue; CheckedEffects.Add(EffectId);
                const auto* Effect=D.SpellVisualEffectNames().GetById(EffectId);
                if(!TestNotNull(FString::Printf(TEXT("Effect %u resolves"),EffectId),Effect)) continue;
                TestTrue(FString::Printf(TEXT("Effect %u resolves an actual MPQ model: %s"),EffectId,*Effect->FilePath),!Effect->FilePath.IsEmpty() && Mpq.FileExists(Effect->FilePath));
                TestTrue(TEXT("Effect scale is finite"),FMath::IsFinite(Effect->Scale));
            }
        }
        if(Visual->HasMissile && Visual->MissileModel>0)
        {
            const auto* Missile=D.SpellVisualEffectNames().GetById(Visual->MissileModel);
            TestTrue(TEXT("The original projectile model exists in MPQ"),Missile && Mpq.FileExists(Missile->FilePath));
        }
    }
    TestTrue(TEXT("Fire, frost, healing, shields and buffs exercise a real effect chain"),CheckedKits.Num()>=10 && CheckedEffects.Num()>=10);
    return true;
}
#endif
