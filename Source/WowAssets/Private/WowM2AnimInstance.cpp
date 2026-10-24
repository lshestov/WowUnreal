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
    ModelBoneIndices.Reset();
    BlendFromPose.Reset();
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
    if (!PlayM2Animation(0, true))
    {
        for (const auto& Clip : Model->AnimationTracks)
            if (Clip.bIsLooping && PlayM2Animation(Clip.AnimationId, true)) break;
    }
}

bool UWowM2AnimInstance::PlayM2Animation(int32 AnimationId, bool bLooping)
{
    if (!Model) return false;
    const int32 Next = Model->AnimationTracks.IndexOfByPredicate([AnimationId](const FM2AnimationData& Track)
    {
        return Track.AnimationId == AnimationId && Track.Duration > 0 && !Track.BoneTracks.IsEmpty();
    });
    if (Next == INDEX_NONE) return false;
    if (Next == TrackIndex && bLoop && bLooping) return true;
    TArray<FTransform> Previous;
    if (TrackIndex != INDEX_NONE) SampleLocalPose(Previous);
    BlendFromPose = MoveTemp(Previous);
    BlendElapsed = 0;
    TrackIndex = Next;
    PlaybackTime = 0;
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
    if (Duration <= 0) return;
    PlaybackTime += FMath::Max(0.0f, DeltaSeconds);
    PlaybackTime = bLoop ? FMath::Fmod(PlaybackTime, Duration) : FMath::Min(PlaybackTime, Duration);
    BlendElapsed += FMath::Max(0.0f, DeltaSeconds);
    if (BlendElapsed >= BlendDuration) BlendFromPose.Reset();
}

void UWowM2AnimInstance::SampleLocalPose(TArray<FTransform>& OutPose) const
{
    OutPose = ReferencePose;
    if (!Model || !Model->AnimationTracks.IsValidIndex(TrackIndex)) return;
    const auto& Animation = Model->AnimationTracks[TrackIndex];
    const double TimeMs = double(PlaybackTime) * 1000;
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
        if (BlendFromPose.IsValidIndex(Bone))
        {
            const FTransform Target = OutPose[Bone];
            OutPose[Bone].Blend(BlendFromPose[Bone], Target, FMath::Clamp(BlendElapsed / BlendDuration, 0.0f, 1.0f));
        }
    }
}

FAnimInstanceProxy* UWowM2AnimInstance::CreateAnimInstanceProxy() { return new FWowM2AnimProxy(this); }
void UWowM2AnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) { delete InProxy; }
