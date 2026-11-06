#include "Misc/AutomationTest.h"
#include "Formats/M2Parser.h"
#include "Formats/M2Types.h"
#include "Mpq/MpqManager.h"
#include "WowM2AnimInstance.h"
#include "WowAnimationController.h"
#include "WowSkeletalMeshBuilder.h"
#include "WowPlayerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowAnimationVariationWeights,"WowUnreal.Animation.VariationWeights",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowAnimationVariationWeights::RunTest(const FString&)
{
    // Four original 64-byte sequence records; links deliberately skip another
    // attack family. Runtime tracks have a different order from source records.
    TArray<uint8> Bytes;Bytes.SetNumZeroed(560);
    TArray<uint8> Skin;Skin.SetNumZeroed(48);const uint32 SkinMagic=0x4E494B53;
    FMemory::Memcpy(Skin.GetData(),&SkinMagic,4);
    const auto U32=[&](int32 At,uint32 Value){FMemory::Memcpy(Bytes.GetData()+At,&Value,4);};
    const auto U16=[&](int32 At,uint16 Value){FMemory::Memcpy(Bytes.GetData()+At,&Value,2);};
    U32(0,0x3032444D);U32(4,264);U32(28,4);U32(32,304);
    for(int32 I=0;I<4;++I){U16(304+I*64,19);U32(308+I*64,1000);U16(364+I*64,0xffff);}
    U32(320,8192);U16(364,2);
    U16(368,17);U32(384,65536); // full-width frequency, not a signed int16
    U16(434,1);U32(448,24576);U16(492,3);
    U16(498,2); // zero-frequency variant must not be selected
    auto Data=FM2Parser::Parse(Bytes,Skin);
    if(!TestEqual(TEXT("All source sequence records are parsed"),Data.Animations.Num(),4)) return false;
    TestEqual(TEXT("Source frequency is preserved"),Data.Animations[0].Frequency,8192u);
    TestEqual(TEXT("Frequency retains all 32 source bits"),Data.Animations[1].Frequency,65536u);
    TestEqual(TEXT("Source variation link is preserved"),Data.Animations[0].NextAnimation,int16(2));
    for(uint16 Sub:{1,9,0,2})
    {
        FM2AnimationData Clip;Clip.AnimationId=19;Clip.SubAnimationId=Sub;Clip.Duration=1000;Clip.BoneTracks.SetNum(1);
        Data.AnimationTracks.Add(MoveTemp(Clip));
    }
    int32 Counts[4]={};
    for(uint32 Roll=0;Roll<32768;++Roll)
    {
        const int32 Chosen=UWowM2AnimInstance::SelectM2Variation(Data,19,Roll);
        if(Chosen>=0 && Chosen<4) ++Counts[Chosen];else {AddError(TEXT("Variation selection returned an invalid runtime track"));return false;}
    }
    TestEqual(TEXT("First variation keeps its authored 25% weight"),Counts[2],8192);
    TestEqual(TEXT("Linked second variation keeps its authored 75% weight"),Counts[0],24576);
    TestEqual(TEXT("Unlinked same-ID tracks are not picked"),Counts[1],0);
    TestEqual(TEXT("Zero-frequency variants are not picked"),Counts[3],0);
    Data.AnimationTracks[0].BoneTracks.Reset();
    TestEqual(TEXT("Missing selected clip falls back to a playable first variation"),UWowM2AnimInstance::SelectM2Variation(Data,19,20000),2);
    Data.Animations[0].NextAnimation=1;
    TestEqual(TEXT("A malformed link cannot switch weapon families"),UWowM2AnimInstance::SelectM2Variation(Data,19,20000),2);
    Data.Animations[0].NextAnimation=2;Data.Animations[0].Frequency=Data.Animations[2].Frequency=0;Data.Animations[2].NextAnimation=0;
    TestEqual(TEXT("A cyclic zero-weight chain terminates at a playable fallback"),UWowM2AnimInstance::SelectM2Variation(Data,19,20000),2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowAttackVariations,"WowUnreal.Animation.RepeatedAttackVariants",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowAttackVariations::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto Data=MakeShared<FM2Data>();Data->Bones.SetNum(1);Data->Bones[0].ParentBone=-1;
    Data->Vertices.SetNum(3);Data->Vertices[1].Position=FVector(1,0,0);Data->Vertices[2].Position=FVector(0,0,1);
    for(auto& V:Data->Vertices){V.BoneWeights[0]=255;V.Normal=FVector(0,1,0);}
    Data->Indices={0,1,2};Data->BoundingBox=FBox(FVector::ZeroVector,FVector(1,1,1));
    for(int32 I=0;I<3;++I)
    {
        FM2AnimSequence S;S.AnimationId=I==2?0:19;S.SubAnimationId=I==1?1:0;
        S.Frequency=I==0?8192:24576;S.NextAnimation=I==0?1:-1;Data->Animations.Add(S);
        FM2AnimationData Clip;Clip.AnimationId=S.AnimationId;Clip.SubAnimationId=S.SubAnimationId;
        Clip.Duration=I==0?500:900;Clip.BoneTracks.SetNum(1);
        Clip.BoneTracks[0].TransTimestamps={0};Clip.BoneTracks[0].TransValues={FVector(I+1,0,0)};
        Data->AnimationTracks.Add(MoveTemp(Clip));
    }
    auto* Skeleton=FWowSkeletalMeshBuilder::CreateSkeleton(*Data,TEXT("AttackVariationTest"));
    auto* Mesh=FWowSkeletalMeshBuilder::CreateSkeletalMesh(*Data,Skeleton,TEXT("AttackVariationTest"),nullptr,nullptr,nullptr,nullptr,true);
    auto* Player=World->SpawnActor<AWowPlayerCharacter>();Player->GetMesh()->SetSkeletalMesh(Mesh);
    auto* Controller=NewObject<UWowAnimationController>(Player);Controller->InitializeRuntime(Player->GetMesh(),Data);
    auto* Instance=Controller->GetRuntimeInstance();
    if(TestNotNull(TEXT("Melee variation uses the live M2 animation instance"),Instance))
    {
        Instance->SetM2VariationSeed(1234);
        TSet<int32> Seen;TSet<int32> PosePositions;
        for(int32 Swing=0;Swing<32;++Swing)
        {
            TestTrue(TEXT("The equipped weapon attack starts"),Controller->PlayAttackAnimation(EWowAnimId::Attack2HL));
            TestEqual(TEXT("Variation keeps the weapon animation family"),Instance->GetM2AnimationId(),19);
            TestEqual(TEXT("Every new swing starts at its first sample"),Instance->GetM2Time(),0.f);
            const int32 Variation=Instance->GetM2VariationId();Seen.Add(Variation);
            TestTrue(TEXT("One-shot duration follows the chosen variation"),FMath::IsNearlyEqual(Instance->GetM2Duration(),Variation==0?.5f:.9f));
            Instance->NativeUpdateAnimation(.2f);
            TArray<FTransform> Pose;Instance->SampleLocalPose(Pose);
            if(Pose.Num()) PosePositions.Add(FMath::RoundToInt(Pose[0].GetTranslation().Y));
        }
        TestEqual(TEXT("Repeated swings select both authored alternatives"),Seen.Num(),2);
        TestEqual(TEXT("Alternatives change actual sampled bone transforms"),PosePositions.Num(),2);
        Data->Animations[0].Frequency=0;Data->Animations[1].Frequency=32768;
        TestTrue(TEXT("A fully weighted alternative remains available for one-shot playback"),Instance->PlayM2Animation(19,false));
        TestEqual(TEXT("One-shot playback follows the alternative's full authored weight"),Instance->GetM2VariationId(),1);
        TestTrue(TEXT("The same family can enter looping playback"),Instance->PlayM2Animation(19,true));
        TestEqual(TEXT("A loop starts its base clip even when all weight favors a one-shot alternative"),Instance->GetM2VariationId(),0);
        Instance->NativeUpdateAnimation(.17f);
        const int32 LoopVariation=Instance->GetM2VariationId();const float LoopTime=Instance->GetM2Time();
        for(int32 I=0;I<10;++I) Instance->PlayM2Animation(19,true);
        TestEqual(TEXT("Polling a looping state preserves its chosen variation"),Instance->GetM2VariationId(),LoopVariation);
        TestEqual(TEXT("Polling a looping state preserves its playback clock"),Instance->GetM2Time(),LoopTime);
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowOriginalAttackVariants,"WowUnreal.Animation.OriginalDwarfAttackVariants",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowOriginalAttackVariants::RunTest(const FString&)
{
    FString Path;if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq;if(!Mpq.Initialize(Path)) return false;
    TArray<uint8> Bytes,Skin;const FString Base=TEXT("Character\\Dwarf\\Male\\DwarfMale");
    if(!TestTrue(TEXT("User's original dwarf model and skin are readable"),Mpq.ReadFile(Base+TEXT(".m2"),Bytes) && Mpq.ReadFile(Base+TEXT("00.skin"),Skin))) return false;
    const auto Data=MakeShared<FM2Data>(FM2Parser::Parse(Bytes,Skin));
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Skeleton=FWowSkeletalMeshBuilder::CreateSkeleton(*Data,TEXT("DwarfAttackVariationTest"));
    auto* Mesh=FWowSkeletalMeshBuilder::CreateSkeletalMesh(*Data,Skeleton,TEXT("DwarfAttackVariationTest"),nullptr,nullptr,nullptr,nullptr,true);
    auto* Player=World->SpawnActor<AWowPlayerCharacter>();Player->GetMesh()->SetSkeletalMesh(Mesh);
    auto* Controller=NewObject<UWowAnimationController>(Player);Controller->InitializeRuntime(Player->GetMesh(),Data);
    auto* Instance=Controller->GetRuntimeInstance();
    if(TestNotNull(TEXT("Original dwarf has a runtime skeletal animation instance"),Instance))
    {
        for(const EWowAnimId Attack:{EWowAnimId::Attack1H,EWowAnimId::Attack2H})
        {
            Instance->SetM2VariationSeed(1234);
            TMap<int32,TArray<FTransform>> Poses;
            for(int32 Swing=0;Swing<64;++Swing)
            {
                TestTrue(TEXT("Repeated original dwarf weapon swing starts"),Controller->PlayAttackAnimation(Attack));
                TestEqual(TEXT("Original attack preserves the equipped weapon family"),Instance->GetM2AnimationId(),int32(Attack));
                TestEqual(TEXT("Original attack restarts at time zero"),Instance->GetM2Time(),0.f);
                Instance->NativeUpdateAnimation(.2f);
                const int32 Variation=Instance->GetM2VariationId();
                if(!Poses.Contains(Variation)) Instance->SampleLocalPose(Poses.Add(Variation));
            }
            TestTrue(TEXT("Repeated original dwarf swings choose multiple authored variations"),Poses.Num()>1);
            const TArray<FTransform>* First=nullptr;bool Different=false;
            for(const auto& Pair:Poses)
            {
                if(!First){First=&Pair.Value;continue;}
                for(int32 Bone=0;Bone<FMath::Min(First->Num(),Pair.Value.Num());++Bone)
                    if(!(*First)[Bone].Equals(Pair.Value[Bone],.01)){Different=true;break;}
            }
            TestTrue(TEXT("Original dwarf alternatives produce different bone poses at the same swing time"),Different);
            AddInfo(FString::Printf(TEXT("Original DwarfMale AnimId=%d produced %d distinct swing variants in 64 seeded attacks"),int32(Attack),Poses.Num()));
        }
    }
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
