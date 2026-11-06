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
    int32 GetM2VariationId() const;
    /** Original frequency/variationNext selection; the roll is in [0,32767]. */
    static int32 SelectM2Variation(const FM2Data& Data,int32 AnimationId,uint32 Roll);
    /** Reproducible playback for captures and automation, independent of other actors. */
    void SetM2VariationSeed(int32 Seed) { VariationRandom.Initialize(Seed); }
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
    /** Authored portrait cameras frame the first Stand pose, which can differ from the bind pose. */
    const TArray<FTransform>& GetPortraitStandPose();
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
    TSharedPtr<const FM2Data> Model;
    TArray<FTransform> ReferencePose;
    TArray<FTransform> PortraitStandPose;
    TArray<int32> ModelBoneIndices;
    TArray<FTransform> BlendFromPose;
    int32 TrackIndex = INDEX_NONE;
    float PlaybackTime = 0;
    float PlaybackRate = 1;
    float BlendElapsed = 0;
    bool bLoop = true;
    FRandomStream VariationRandom;
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
