#include "Misc/AutomationTest.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/Dbc/SpellVisualDbc.h"
#include "Formats/Dbc/SpellVisualKitDbc.h"
#include "Formats/Dbc/SpellVisualEffectNameDbc.h"
#include "Mpq/MpqManager.h"
#include "WowPacketHandler.h"
#include "WowSpellMissile.h"
#include "WowDoodadManager.h"
#include "Formats/M2Parser.h"
#include "Formats/M2Types.h"
#include "Formats/BlpParser.h"
#include "Materials/Material.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FParticleFixture
{
    TArray<uint8> Bytes;
    uint32 SpeedKeys=0;
    template<typename T> uint32 Append(const T* Values,int32 Count) {while(Bytes.Num()%4) Bytes.Add(0); const uint32 At=Bytes.Num(); Bytes.Append(reinterpret_cast<const uint8*>(Values),Count*sizeof(T)); return At;}
    template<typename T> void Put(uint32 At,T Value) {FMemory::Memcpy(Bytes.GetData()+At,&Value,sizeof(T));}
    void Track(uint32 At,const TArray<float>& Values,int16 Global=-1)
    {
        const uint32 Times[]={0,1000}; const uint32 TimeOffset=Append(Times,2),ValueOffset=Append(Values.GetData(),Values.Num());
        const uint32 TimeArray[]={2,TimeOffset},ValueArray[]={uint32(Values.Num()),ValueOffset};
        const uint32 TimeArrays=Append(TimeArray,2),ValueArrays=Append(ValueArray,2);
        Put<uint16>(At,1); Put<int16>(At+2,Global); Put<uint32>(At+4,1); Put(At+8,TimeArrays); Put<uint32>(At+12,1); Put(At+16,ValueArrays);
        if(At==304+52) SpeedKeys=ValueOffset;
    }
    template<typename T> void Curve(uint32 At,const TArray<T>& Values)
    {
        const int16 Times[]={0,16384,32767}; const uint32 TimeOffset=Append(Times,Values.Num()),ValueOffset=Append(Values.GetData(),Values.Num());
        Put<uint32>(At,Values.Num()); Put(At+4,TimeOffset); Put<uint32>(At+8,Values.Num()); Put(At+12,ValueOffset);
    }
    FParticleFixture()
    {
        Bytes.SetNumZeroed(304+476*2); Put<uint32>(0,0x3032444d); Put<uint32>(4,264); Put<uint32>(296,2); Put<uint32>(300,304);
        const uint32 Duration=1000,DurationAt=Append(&Duration,1); Put<uint32>(20,1); Put(24,DurationAt);
        const uint32 P=304; Put<uint32>(P,101); Put<uint32>(P+4,0x20429); Put<float>(P+8,1); Put<float>(P+12,2); Put<float>(P+16,3);
        Put<int16>(P+20,-1); Put<uint16>(P+22,1); Put<uint8>(P+40,4); Put<uint8>(P+41,1); Put<uint16>(P+42,17); Put<int16>(P+46,-2); Put<uint16>(P+48,4); Put<uint16>(P+50,8);
        Track(P+52,{2,4},0); Track(P+132,{3,6}); Track(P+152,{1,2}); Track(P+176,{10,20}); Track(P+200,{.1f,.2f}); Track(P+220,{.3f,.4f});
        Curve<FVector3f>(P+260,{FVector3f(255,0,0),FVector3f(0,255,0),FVector3f(0,0,255)});
        Curve<int16>(P+276,{32767,16384,0}); Curve<FVector2f>(P+292,{FVector2f(.2f,.3f),FVector2f(.4f,.5f),FVector2f(.8f,.9f)});
        Curve<uint16>(P+316,{0,4,15}); Put<float>(P+308,.125f); Put<float>(P+312,.25f); Put<float>(P+348,.75f); Put<float>(P+372,.5f); Put<float>(P+376,.2f);
        const uint32 Q=P+476; Put<uint32>(Q,202); Put<uint8>(Q+40,2); Put<uint8>(Q+41,2); Put<uint16>(Q+22,2);
    }
};
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM2ParticleLayout,"WowUnreal.Gameplay.M2ParticleLayout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FM2ParticleLayout::RunTest(const FString&)
{
    FParticleFixture F; const auto Data=FM2Parser::Parse(F.Bytes,{});
    if(!TestTrue(TEXT("Emitter-only MD20 parses two real-size records"),Data.bIsValid && Data.ParticleEmitters.Num()==2 && Data.Vertices.IsEmpty())) return false;
    const auto& P=Data.ParticleEmitters[0]; const auto& Q=Data.ParticleEmitters[1];
    if(!TestTrue(TEXT("All lifetime curves are present before sampling"),P.ColorCurve.Values.Num()==3 && P.AlphaCurve.Values.Num()==3 && P.ScaleCurve.Values.Num()==3 && P.HeadCellCurve.Values.Num()==3)) return false;
    TestTrue(TEXT("Blend, emitter, atlas, texture and signed fields use their actual offsets"),P.Id==101 && P.Flags==0x20429 && P.Position==FVector(1,2,3) && P.Bone==-1 && P.Texture==1 && P.BlendMode==4 && P.EmitterType==1 && P.Rows==4 && P.Cols==8 && P.ColorIndex==17 && P.Priority==-2 && Q.Id==202 && Q.BlendMode==2 && Q.EmitterType==2 && Q.Texture==2);
    TestEqual(TEXT("Global scalar tracks wrap in their authored duration"),P.SpeedTrack.Sample(0,1500,Data.GlobalSequenceDurations),3.f);
    TestEqual(TEXT("Non-global scalar tracks clamp independently"),P.RateTrack.Sample(0,1500,Data.GlobalSequenceDurations),20.f);
    TestTrue(TEXT("Colour, alpha and non-uniform size retain their lifetime keys"),P.ColorCurve.Values[1].Equals(FVector3f(0,1,0)) && FMath::IsNearlyEqual(P.AlphaCurve.Sample(1,1.f),0.f) && P.ScaleCurve.Values[2].Equals(FVector2f(.8f,.9f)));
    TestTrue(TEXT("Atlas cells, scale variance, drag and spin are distinct fields"),P.HeadCellCurve.Values.Last()==15 && P.ScaleVariance.Equals(FVector2f(.125f,.25f)) && P.Drag==.5f && P.InitialSpin==.2f && P.TailLengthUnits==.75f);
    TestTrue(TEXT("The compatibility view comes from authored values, not orange defaults"),P.ColorEnd[0]==0 && P.ColorEnd[2]==255 && P.AlphaEnd==0 && P.HeadLifeStart==1000 && P.EmissionRate==10 && P.Gravity==3);
    auto Bad=F; Bad.Put<uint32>(304+260+12,0xfffffff0u); const auto BadCurve=FM2Parser::Parse(Bad.Bytes,{});
    TestTrue(TEXT("Out-of-range lifetime arrays are rejected without corrupting other emitters"),BadCurve.bIsValid && BadCurve.ParticleEmitters[0].ColorCurve.Values.IsEmpty() && BadCurve.ParticleEmitters[1].Id==202);
    Bad=F; Bad.Put<float>(Bad.SpeedKeys,std::numeric_limits<float>::quiet_NaN());
    TestTrue(TEXT("A nonfinite scalar track is not published"),FM2Parser::Parse(Bad.Bytes,{}).ParticleEmitters[0].SpeedTrack.Sequences.IsEmpty());
    Bad=F; Bad.Put<uint32>(296,257); TestTrue(TEXT("Emitter allocation count is bounded"),FM2Parser::Parse(Bad.Bytes,{}).ParticleEmitters.IsEmpty());
    Bad=F; Bad.Put<uint32>(300,0xfffffff0u); TestTrue(TEXT("Record offset arithmetic cannot wrap"),FM2Parser::Parse(Bad.Bytes,{}).ParticleEmitters.IsEmpty());
    Bad=F; Bad.Put<uint32>(8,8); Bad.Put<uint32>(12,0xfffffffcu); TestTrue(TEXT("Model string offset arithmetic cannot wrap or scan past its bounds"),FM2Parser::Parse(Bad.Bytes,{}).bIsValid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM2AuthoredSpriteAssets,"WowUnreal.Gameplay.M2AuthoredSpriteAssets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FM2AuthoredSpriteAssets::RunTest(const FString&)
{
    auto* Alpha=LoadObject<UMaterial>(nullptr,TEXT("/Game/Materials/M_WowParticleAlpha"));
    auto* Add=LoadObject<UMaterial>(nullptr,TEXT("/Game/Materials/M_WowParticleAdd"));
    if(!TestTrue(TEXT("Particle parents retain the authored alpha/add blend modes"),Alpha && Add && Alpha->BlendMode==BLEND_Translucent && Add->BlendMode==BLEND_Additive && Alpha->MaterialDomain==MD_Surface && Add->MaterialDomain==MD_Surface)) return false;
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    const auto Character=FWowDoodadManager::GetOrParseM2(TEXT("Character\\Human\\Male\\HumanMale.m2"),&Mpq);
    if(!TestTrue(TEXT("The actual player model supplies both spell hand attachments"),Character && Character->bIsValid)) return false;
    for(uint32 Id:{21u,22u})
    {
        const auto* Attachment=Character->Attachments.FindByPredicate([&](const auto& A){return A.Id==Id;});
        TestTrue(TEXT("Spell hand attachment references a real animated bone"),Attachment && Character->Bones.IsValidIndex(Attachment->Bone));
    }
    for(const TCHAR* Name:{TEXT("Spells\\Fire_Precast_Hand.m2"),TEXT("Spells\\Fire_Cast_Hand.m2")})
    {
        const auto Model=FWowDoodadManager::GetOrParseM2(Name,&Mpq);
        if(!TestTrue(TEXT("Original hand effect parses as an emitter-only model"),Model && Model->bIsValid && Model->Vertices.IsEmpty())) return false;
        int32 Supported=0;
        for(const auto& E:Model->ParticleEmitters)
        {
            if((E.EmitterType!=1 && E.EmitterType!=2) || !(E.Flags&0x20000) || E.BlendMode<2 || E.BlendMode>4 || !Model->TexturePaths.IsValidIndex(E.Texture)) continue;
            ++Supported; TArray<uint8> Bytes;
            const bool Read=Mpq.ReadFile(Model->TexturePaths[E.Texture],Bytes);
            TestTrue(TEXT("Every supported head sprite reads its original BLP texture"),Read && FBlpParser::Parse(Bytes).bIsValid);
        }
        TestTrue(TEXT("Each fire phase provides multiple supported head emitters"),Supported>=3);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM2ParticleMpq,"WowUnreal.Gameplay.M2ParticleMpq",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FM2ParticleMpq::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false;
    const auto Model=FWowDoodadManager::GetOrParseM2(TEXT("Spells\\Fire_Cast_Hand.m2"),&Mpq);
    if(!TestTrue(TEXT("An original emitter-only spell model loads without an invented skin"),Model && Model->bIsValid && Model->Vertices.IsEmpty() && Model->ParticleEmitters.Num()==3)) return false;
    const auto& P=Model->ParticleEmitters[0];
    TestTrue(TEXT("Real fire uses its actual blend mode, emitter type and texture"),P.BlendMode==4 && P.EmitterType==1 && P.Texture==0 && Model->TexturePaths.Num()==3);
    TestTrue(TEXT("Real fire colour keys are normalized from its authored RGB values"),P.ColorCurve.Values.Num()==3 && P.ColorCurve.Values[0].Equals(FVector3f(1,234/255.f,0),.0001f) && P.ColorCurve.Values.Last().Equals(FVector3f(1,0,0),.0001f));
    TestTrue(TEXT("Real fire alpha fades to zero and grows in two dimensions"),P.AlphaCurve.Values.Num()==3 && P.AlphaCurve.Values[0]==1 && P.AlphaCurve.Values.Last()==0 && P.ScaleCurve.Values.Num()==3 && FMath::IsNearlyEqual(P.ScaleCurve.Values[0].X,.225f,.0001f) && P.ScaleCurve.Values.Last().X>.67f);
    TestTrue(TEXT("Actual life and rate tracks are finite and nonempty"),!P.LifeTrack.Sequences.IsEmpty() && !P.RateTrack.Sequences.IsEmpty() && FMath::IsFinite(P.LifeTrack.Sample(0,0,Model->GlobalSequenceDurations)));
    for(const auto& Texture:Model->TexturePaths) TestTrue(TEXT("Every particle texture resolves in the user's MPQs"),!Texture.IsEmpty() && Mpq.FileExists(Texture));
    return true;
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
