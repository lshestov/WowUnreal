#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "WowM2AnimInstance.generated.h"

struct FM2Data;

/** Samples immutable M2 keys at runtime; no editor animation controller or cooked sequence is needed. */
UCLASS(Transient)
class WOWASSETS_API UWowM2AnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    void InitializeModel(TSharedPtr<const FM2Data> InModel);
    bool PlayM2Animation(int32 AnimationId, bool bLooping);
    int32 GetM2AnimationId() const;
    float GetM2Duration() const;
    float GetM2Time() const { return PlaybackTime; }
    void SetM2MovementSpeed(float Speed, float ModelScale);
    float GetM2PlayRate() const { return PlaybackRate; }
    void SampleLocalPose(TArray<FTransform>& OutPose) const;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
    TSharedPtr<const FM2Data> Model;
    TArray<FTransform> ReferencePose;
    TArray<int32> ModelBoneIndices;
    TArray<FTransform> BlendFromPose;
    int32 TrackIndex = INDEX_NONE;
    float PlaybackTime = 0;
    float PlaybackRate = 1;
    float BlendElapsed = 0;
    bool bLoop = true;
    static constexpr float BlendDuration = 0.12f;
};
