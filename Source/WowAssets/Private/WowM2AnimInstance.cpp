#include "WowM2AnimInstance.h"
#include "WowSkeletalMeshBuilder.h"
#include "Formats/M2Types.h"
#include "Coord/WowCoordinate.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

namespace
{
template<typename T, typename Interpolate>
T SampleKeys(const TArray<uint32>& Times, const TArray<T>& Values, double TimeMs,
    const T& Default, Interpolate Lerp)
{
    const int32 Count = FMath::Min(Times.Num(), Values.Num());
    if (Count == 0) return Default;
    if (Count == 1 || TimeMs <= Times[0]) return Values[0];
    if (TimeMs >= Times[Count - 1]) return Values[Count - 1];
    int32 Low = 0, High = Count - 1;
    while (High - Low > 1)
    {
        const int32 Mid = (Low + High) / 2;
        if (Times[Mid] <= TimeMs) Low = Mid; else High = Mid;
    }
    const double Span = double(Times[High]) - Times[Low];
    return Lerp(Values[Low], Values[High], Span > 0 ? (TimeMs - Times[Low]) / Span : 0.0);
}

// Sampling and UObject access happen on the game thread. Workers only consume
// this frame's copied local transforms, never the controller or M2 cache.
struct FWowM2AnimProxy : FAnimInstanceProxy
{
    explicit FWowM2AnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
    TArray<FTransform> LocalPose;
    virtual void PreEvaluateAnimation(UAnimInstance* Instance) override
    {
        FAnimInstanceProxy::PreEvaluateAnimation(Instance);
        CastChecked<UWowM2AnimInstance>(Instance)->SampleLocalPose(LocalPose);
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose();
        for (const FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
        {
            const int32 MeshBone = Output.Pose.GetBoneContainer().MakeMeshPoseIndex(Bone).GetInt();
            if (LocalPose.IsValidIndex(MeshBone)) Output.Pose[Bone] = LocalPose[MeshBone];
        }
        return true;
    }
};
}

void UWowM2AnimInstance::InitializeModel(TSharedPtr<const FM2Data> InModel)
{
    Model = MoveTemp(InModel);
    TrackIndex = INDEX_NONE;
    ReferencePose.Reset();
    PortraitStandPose.Reset();
    ModelBoneIndices.Reset();
    BlendFromPose.Reset();
    StopM2SpellAnimation(true);
    SpellTorsoMask.Reset();
    const auto* Mesh = GetSkelMeshComponent() ? GetSkelMeshComponent()->GetSkeletalMeshAsset() : nullptr;
    if (!Mesh || !Model) return;
    const auto& Ref = Mesh->GetRefSkeleton();
    ReferencePose = Ref.GetRefBonePose();
    ModelBoneIndices.Init(INDEX_NONE, ReferencePose.Num());
    for (int32 Bone = 0; Bone < Model->Bones.Num(); ++Bone)
    {
        const int32 MeshBone = Ref.FindBoneIndex(FWowSkeletalMeshBuilder::GetBoneName(Bone));
        if (ModelBoneIndices.IsValidIndex(MeshBone)) ModelBoneIndices[MeshBone] = Bone;
    }
    // The original unit renderer selects torso keybone 4, then head keybone 6.
    int32 Torso=Model->Bones.IndexOfByPredicate([](const auto& B){return B.KeyBoneId==4;});
    if(Torso==INDEX_NONE) Torso=Model->Bones.IndexOfByPredicate([](const auto& B){return B.KeyBoneId==6;});
    SpellTorsoMask.Init(false,ReferencePose.Num());
    if(Torso!=INDEX_NONE) for(int32 Bone=0;Bone<ModelBoneIndices.Num();++Bone)
    {
        int32 Parent=ModelBoneIndices[Bone];
        for(int32 Depth=0;Depth<Model->Bones.Num() && Model->Bones.IsValidIndex(Parent);++Depth)
        {
            if(Parent==Torso) {SpellTorsoMask[Bone]=true; break;}
            Parent=Model->Bones[Parent].ParentBone;
        }
    }
    if (!PlayM2Animation(0, true))
    {
        for (const auto& Clip : Model->AnimationTracks)
            if (Clip.bIsLooping && PlayM2Animation(Clip.AnimationId, true)) break;
    }
}

int32 UWowM2AnimInstance::FindClip(int32 AnimationId) const
{
    return Model?Model->AnimationTracks.IndexOfByPredicate([AnimationId](const FM2AnimationData& Track)
    {
        return Track.AnimationId == AnimationId && Track.Duration > 0 && !Track.BoneTracks.IsEmpty();
    }):INDEX_NONE;
}

bool UWowM2AnimInstance::PlayM2Animation(int32 AnimationId, bool bLooping)
{
    const int32 Next=FindClip(AnimationId);
    if (Next == INDEX_NONE) return false;
    if (Next == TrackIndex && bLoop && bLooping) return true;
    TArray<FTransform> Previous;
    if (TrackIndex != INDEX_NONE) SampleLocalPose(Previous);
    BlendFromPose = MoveTemp(Previous);
    BlendElapsed = 0;
    TrackIndex = Next;
    PlaybackTime = 0;
    PlaybackRate = 1;
    bLoop = bLooping;
    return true;
}

int32 UWowM2AnimInstance::GetM2AnimationId() const
{
    return Model && Model->AnimationTracks.IsValidIndex(TrackIndex) ? Model->AnimationTracks[TrackIndex].AnimationId : INDEX_NONE;
}

float UWowM2AnimInstance::GetM2Duration() const
{
    return Model && Model->AnimationTracks.IsValidIndex(TrackIndex) ? Model->AnimationTracks[TrackIndex].Duration * 0.001f : 0;
}

void UWowM2AnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    const float Duration = GetM2Duration();
    if (Duration > 0)
    {
        PlaybackTime += FMath::Max(0.0f, DeltaSeconds) * PlaybackRate;
        PlaybackTime = bLoop ? FMath::Fmod(PlaybackTime, Duration) : FMath::Min(PlaybackTime, Duration);
    }
    BlendElapsed += FMath::Max(0.0f, DeltaSeconds);
    if (BlendElapsed >= BlendDuration) BlendFromPose.Reset();
    UpdateSpellAnimation(FMath::Max(0.f,DeltaSeconds));
}

bool UWowM2AnimInstance::PlayM2SpellAnimation(int32 IntroId,int32 AnimationId,bool bLooping,bool bRestart)
{
    int32 Intro=IntroId>0?FindClip(IntroId):INDEX_NONE;
    const int32 Main=AnimationId>0?FindClip(AnimationId):INDEX_NONE;
    if(Intro==Main) Intro=INDEX_NONE;
    const int32 First=Intro!=INDEX_NONE?Intro:Main;
    if(First==INDEX_NONE) return false;
    if(!bRestart && !bSpellFadingOut && First==SpellTrackIndex && Intro==INDEX_NONE)
    {
        bSpellLoop=bLooping; return true;
    }
    if(SpellTrackIndex==INDEX_NONE)
    {
        const int32 Base=GetM2AnimationId(); SpellLowerBodyWeight=(Base==0 || Base==25 || GetSpellTorsoBoneCount()==0)?1.f:0.f;
    }
    TArray<FTransform> Previous; SampleLocalPose(Previous); SpellBlendFromPose=MoveTemp(Previous);
    SpellBlendElapsed=SpellFadeElapsed=SpellTime=0;
    SpellTrackIndex=First; SpellNextTrackIndex=Intro!=INDEX_NONE?Main:INDEX_NONE;
    bSpellIntro=Intro!=INDEX_NONE; bSpellNextLoop=bLooping;
    bSpellLoop=!bSpellIntro && bLooping; bSpellFadingOut=false;
    return true;
}

void UWowM2AnimInstance::StopM2SpellAnimation(bool bImmediate)
{
    SpellNextTrackIndex=INDEX_NONE; bSpellIntro=false;
    if(bImmediate)
    {
        SpellTrackIndex=INDEX_NONE; SpellBlendFromPose.Reset();
        bSpellFadingOut=false; bSpellLoop=false; SpellTime=0;
    }
    else if(SpellTrackIndex!=INDEX_NONE && !bSpellFadingOut)
    {
        bSpellFadingOut=true; SpellFadeElapsed=0;
    }
}

int32 UWowM2AnimInstance::GetM2SpellAnimationId() const
{
    return Model && Model->AnimationTracks.IsValidIndex(SpellTrackIndex)?Model->AnimationTracks[SpellTrackIndex].AnimationId:INDEX_NONE;
}

float UWowM2AnimInstance::GetM2SpellDuration() const
{
    return Model && Model->AnimationTracks.IsValidIndex(SpellTrackIndex)?Model->AnimationTracks[SpellTrackIndex].Duration*.001f:0;
}

int32 UWowM2AnimInstance::GetSpellTorsoBoneCount() const
{
    int32 Count=0; for(bool Bone:SpellTorsoMask) Count+=Bone; return Count;
}

void UWowM2AnimInstance::UpdateSpellAnimation(float Delta)
{
    if(SpellTrackIndex==INDEX_NONE) return;
    const int32 Base=GetM2AnimationId();
    SpellLowerBodyWeight=FMath::FInterpConstantTo(SpellLowerBodyWeight,(Base==0 || Base==25 || GetSpellTorsoBoneCount()==0)?1.f:0.f,Delta,1/BlendDuration);
    SpellBlendElapsed+=Delta;
    if(SpellBlendElapsed>=BlendDuration) SpellBlendFromPose.Reset();
    if(!bSpellFadingOut)
    {
        float Time=SpellTime+Delta;
        if(bSpellIntro && SpellNextTrackIndex!=INDEX_NONE && Time>=GetM2SpellDuration())
        {
            TArray<FTransform> IntroEnd; SampleClip(SpellTrackIndex,GetM2SpellDuration(),IntroEnd);
            Time-=GetM2SpellDuration(); SpellTrackIndex=SpellNextTrackIndex; SpellNextTrackIndex=INDEX_NONE;
            bSpellIntro=false; bSpellLoop=bSpellNextLoop;
            SpellBlendFromPose=MoveTemp(IntroEnd); SpellBlendElapsed=Time;
            if(SpellBlendElapsed>=BlendDuration) SpellBlendFromPose.Reset();
        }
        const float Duration=GetM2SpellDuration();
        if(Duration<=0) {StopM2SpellAnimation(true); return;}
        SpellTime=bSpellLoop?FMath::Fmod(Time,Duration):FMath::Min(Time,Duration);
        if(!bSpellLoop && Time>=Duration) {bSpellFadingOut=true; SpellFadeElapsed=Time-Duration;}
    }
    else SpellFadeElapsed+=Delta;
    if(bSpellFadingOut && SpellFadeElapsed>=BlendDuration) StopM2SpellAnimation(true);
}

void UWowM2AnimInstance::SetM2MovementSpeed(float Speed, float ModelScale)
{
    const float ClipSpeed = Model && Model->AnimationTracks.IsValidIndex(TrackIndex) ? Model->AnimationTracks[TrackIndex].MoveSpeed : 0;
    PlaybackRate = Speed > 0 && ClipSpeed > UE_SMALL_NUMBER && ModelScale > UE_SMALL_NUMBER
        ? FMath::Clamp(Speed / (ClipSpeed * ModelScale), 0.1f, 4.0f) : 1.0f;
}

void UWowM2AnimInstance::SampleClip(int32 Clip,float Time,TArray<FTransform>& OutPose) const
{
    OutPose = ReferencePose;
    if (!Model || !Model->AnimationTracks.IsValidIndex(Clip)) return;
    const auto& Animation = Model->AnimationTracks[Clip];
    const double TimeMs = double(Time) * 1000;
    const auto VectorLerp = [](const FVector& A, const FVector& B, double Alpha) { return FMath::Lerp(A, B, Alpha); };
    for (int32 Bone = 0; Bone < OutPose.Num(); ++Bone)
    {
        const int32 ModelBone = ModelBoneIndices[Bone];
        if (Animation.BoneTracks.IsValidIndex(ModelBone))
        {
            const auto& Track = Animation.BoneTracks[ModelBone];
            const FVector T = SampleKeys(Track.TransTimestamps, Track.TransValues, TimeMs, FVector::ZeroVector, VectorLerp);
            const FVector S = SampleKeys(Track.ScaleTimestamps, Track.ScaleValues, TimeMs, FVector::OneVector, VectorLerp);
            const FQuat R = SampleKeys(Track.RotTimestamps, Track.RotValues, TimeMs, FQuat::Identity,
                [](const FQuat& A, const FQuat& B, double Alpha) { return FQuat::Slerp(A, B, Alpha).GetNormalized(); });
            // Reflect the M2 X/Y basis, matching the mesh and reference skeleton.
            OutPose[Bone].AddToTranslation(FVector(T.Y, T.X, T.Z) * FWowCoordinate::SCALE);
            OutPose[Bone].SetRotation(FQuat(R.Y, R.X, R.Z, -R.W).GetNormalized());
            OutPose[Bone].SetScale3D(FVector(S.Y, S.X, S.Z));
        }
    }
}

const TArray<FTransform>& UWowM2AnimInstance::GetPortraitStandPose()
{
    if (PortraitStandPose.IsEmpty()) SampleClip(FindClip(0), 0.f, PortraitStandPose);
    return PortraitStandPose;
}

void UWowM2AnimInstance::SampleLocalPose(TArray<FTransform>& OutPose) const
{
    SampleClip(TrackIndex,PlaybackTime,OutPose);
    for(int32 Bone=0;Bone<OutPose.Num();++Bone) if(BlendFromPose.IsValidIndex(Bone))
    {
        const FTransform Target=OutPose[Bone];
        OutPose[Bone].Blend(BlendFromPose[Bone],Target,FMath::Clamp(BlendElapsed/BlendDuration,0.f,1.f));
    }
    if(SpellTrackIndex==INDEX_NONE) return;
    TArray<FTransform> SpellPose; SampleClip(SpellTrackIndex,SpellTime,SpellPose);
    const float Weight=bSpellFadingOut?1-FMath::Clamp(SpellFadeElapsed/BlendDuration,0.f,1.f):1;
    for(int32 Bone=0;Bone<OutPose.Num();++Bone)
    {
        const float BoneWeight=Weight*((SpellTorsoMask.IsValidIndex(Bone) && SpellTorsoMask[Bone])?1.f:SpellLowerBodyWeight);
        if(BoneWeight<=0) continue;
        if(SpellBlendFromPose.IsValidIndex(Bone))
        {
            const FTransform Target=SpellPose[Bone];
            SpellPose[Bone].Blend(SpellBlendFromPose[Bone],Target,FMath::Clamp(SpellBlendElapsed/BlendDuration,0.f,1.f));
        }
        const FTransform BasePose=OutPose[Bone];
        OutPose[Bone].Blend(BasePose,SpellPose[Bone],BoneWeight);
    }
}

FAnimInstanceProxy* UWowM2AnimInstance::CreateAnimInstanceProxy() { return new FWowM2AnimProxy(this); }
void UWowM2AnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) { delete InProxy; }
