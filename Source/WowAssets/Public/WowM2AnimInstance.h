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
    bool PlayM2SpellAnimation(int32 IntroId, int32 AnimationId, bool bLooping, bool bRestart = true);
    void StopM2SpellAnimation(bool bImmediate = false);
    int32 GetM2SpellAnimationId() const;
    float GetM2SpellTime() const { return SpellTime; }
    float GetM2SpellDuration() const;
    bool IsM2SpellIntro() const { return bSpellIntro; }
    bool IsM2SpellLooping() const { return bSpellLoop && !bSpellFadingOut; }
    bool HasM2SpellAnimation() const { return SpellTrackIndex != INDEX_NONE; }
    int32 GetSpellTorsoBoneCount() const;
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
    int32 SpellTrackIndex = INDEX_NONE, SpellNextTrackIndex = INDEX_NONE;
    float SpellTime = 0, SpellBlendElapsed = 0, SpellFadeElapsed = 0, SpellLowerBodyWeight = 0;
    bool bSpellLoop = false, bSpellNextLoop = false, bSpellIntro = false, bSpellFadingOut = false;
    TArray<bool> SpellTorsoMask;
    TArray<FTransform> SpellBlendFromPose;
    int32 FindClip(int32 AnimationId) const;
    void SampleClip(int32 Clip, float Time, TArray<FTransform>& OutPose) const;
    void UpdateSpellAnimation(float DeltaSeconds);
    static constexpr float BlendDuration = 0.12f;
};
