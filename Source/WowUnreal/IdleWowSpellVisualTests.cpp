#include "Misc/AutomationTest.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/SpellVisualDbc.h"
#include "Formats/Dbc/SpellVisualKitDbc.h"
#include "Formats/Dbc/SpellVisualEffectNameDbc.h"
#include "Mpq/MpqManager.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
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
