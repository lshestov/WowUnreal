#include "Formats/M2Types.h"

SIZE_T FM2Data::GetAllocatedSize() const
{
    SIZE_T N=sizeof(*this)+Cameras.GetAllocatedSize()+CollisionVertices.GetAllocatedSize()+CollisionIndices.GetAllocatedSize()+Vertices.GetAllocatedSize()+Indices.GetAllocatedSize()+TexturePaths.GetAllocatedSize()+TextureTypes.GetAllocatedSize()+RenderPasses.GetAllocatedSize()+Bones.GetAllocatedSize()+Animations.GetAllocatedSize()+AnimationTracks.GetAllocatedSize()+Attachments.GetAllocatedSize()+ParticleEmitters.GetAllocatedSize()+GlobalSequenceDurations.GetAllocatedSize()+Colors.GetAllocatedSize()+TextureWeights.GetAllocatedSize()+TextureTransforms.GetAllocatedSize()+Ribbons.GetAllocatedSize()+Lights.GetAllocatedSize()+LightBoneTracks.GetAllocatedSize()+Submeshes.GetAllocatedSize()+AttachmentLookup.GetAllocatedSize();
    for(const auto& P:TexturePaths) N+=P.GetAllocatedSize();
    for(const auto& A:AnimationTracks) {N+=A.BoneTracks.GetAllocatedSize(); for(const auto& B:A.BoneTracks) N+=B.TransTimestamps.GetAllocatedSize()+B.TransValues.GetAllocatedSize()+B.RotTimestamps.GetAllocatedSize()+B.RotValues.GetAllocatedSize()+B.ScaleTimestamps.GetAllocatedSize()+B.ScaleValues.GetAllocatedSize();}
    for(const auto& C:Colors) N+=C.Color.GetAllocatedSize()+C.Alpha.GetAllocatedSize();
    for(const auto& W:TextureWeights) N+=W.GetAllocatedSize();
    for(const auto& T:TextureTransforms) N+=T.Translation.GetAllocatedSize()+T.Scale.GetAllocatedSize()+T.Rotation.GetAllocatedSize();
    for(const auto& R:Ribbons) N+=R.Color.GetAllocatedSize()+R.Alpha.GetAllocatedSize()+R.Above.GetAllocatedSize()+R.Below.GetAllocatedSize()+R.Slot.GetAllocatedSize()+R.Visibility.GetAllocatedSize();
    for(const auto& L:Lights) N+=L.AmbientColor.GetAllocatedSize()+L.DiffuseColor.GetAllocatedSize()+L.AmbientIntensity.GetAllocatedSize()+L.DiffuseIntensity.GetAllocatedSize()+L.AttenuationStart.GetAllocatedSize()+L.AttenuationEnd.GetAllocatedSize()+L.Visibility.GetAllocatedSize();
    for(const auto& B:LightBoneTracks) N+=B.Translation.GetAllocatedSize()+B.Scale.GetAllocatedSize()+B.Rotation.GetAllocatedSize();
    for(const auto& P:ParticleEmitters) N+=P.BlendingType.GetAllocatedSize()+P.SpeedTrack.GetAllocatedSize()+P.VariationTrack.GetAllocatedSize()+P.LatitudeTrack.GetAllocatedSize()+P.LongitudeTrack.GetAllocatedSize()+P.GravityTrack.GetAllocatedSize()+P.LifeTrack.GetAllocatedSize()+P.RateTrack.GetAllocatedSize()+P.WidthTrack.GetAllocatedSize()+P.LengthTrack.GetAllocatedSize()+P.ZSourceTrack.GetAllocatedSize()+P.VisibilityTrack.GetAllocatedSize()+P.ColorCurve.GetAllocatedSize()+P.AlphaCurve.GetAllocatedSize()+P.HeadCellCurve.GetAllocatedSize()+P.TailCellCurve.GetAllocatedSize()+P.ScaleCurve.GetAllocatedSize();
    return N;
}

float FM2ParticleFloatTrack::Sample(int32 Animation,float TimeMs,const TArray<uint32>& GlobalDurations,float Default) const
{
    if(Sequences.IsEmpty()) return Default;
    if(GlobalSequence>=0)
    {
        Animation=0;
        if(GlobalDurations.IsValidIndex(GlobalSequence) && GlobalDurations[GlobalSequence]) TimeMs=FMath::Fmod(TimeMs,float(GlobalDurations[GlobalSequence]));
    }
    if(!Sequences.IsValidIndex(Animation)) Animation=0;
    return Sequences[Animation].Sample(TimeMs,Default,Interpolation==0);
}

bool FM2ParticleEmitter::IsFireEmitter() const
{
    // Fire emitters typically have warm colors (red/orange/yellow) and high emission rates
    // Check for red/orange/yellow dominance in start color
    float RedComponent = ColorStart[0] / 255.0f;
    float GreenComponent = ColorStart[1] / 255.0f;
    float BlueComponent = ColorStart[2] / 255.0f;

    // Fire is typically red-orange: high red, moderate green, low blue
    bool IsWarmColor = (RedComponent > 0.6f && GreenComponent > 0.2f && BlueComponent < 0.5f);

    // Fire emitters also typically have higher emission rates and shorter lifespans
    bool IsFireLike = (EmissionRate > 5.0f && HeadLifeStart < 3000.0f);

    return IsWarmColor || IsFireLike;
}

bool FM2ParticleEmitter::IsSmokeEmitter() const
{
    // Smoke emitters typically have grey colors and rise upward (negative gravity or upward motion)
    // Check for greyish colors
    float RedComponent = ColorStart[0] / 255.0f;
    float GreenComponent = ColorStart[1] / 255.0f;
    float BlueComponent = ColorStart[2] / 255.0f;

    // Grey colors have similar RGB values
    float ColorVariance = FMath::Abs(RedComponent - GreenComponent) +
                         FMath::Abs(GreenComponent - BlueComponent) +
                         FMath::Abs(BlueComponent - RedComponent);

    bool IsGreyish = (ColorVariance < 0.3f && RedComponent > 0.2f && RedComponent < 0.8f);

    // Smoke typically has longer lifespan and lower emission rate
    bool IsSmokelike = (HeadLifeStart > 2000.0f && EmissionRate < 15.0f);

    // Also check if gravity is positive (particles fall down, suggesting smoke rising up initially)
    bool HasUpwardMotion = (Gravity > -5.0f);

    return IsGreyish && (IsSmokelike || HasUpwardMotion);
}

bool FM2ParticleEmitter::IsSparkleEmitter() const
{
    // Sparkle emitters typically have bright colors, low emission rates, and small scales
    float RedComponent = ColorStart[0] / 255.0f;
    float GreenComponent = ColorStart[1] / 255.0f;
    float BlueComponent = ColorStart[2] / 255.0f;

    // Bright/saturated colors
    float MaxComponent = FMath::Max3(RedComponent, GreenComponent, BlueComponent);
    bool IsBright = (MaxComponent > 0.7f);

    // Low emission rate and small scale suggests ambient sparkles
    bool IsSparklelike = (EmissionRate < 8.0f && ScaleStart < 2.0f);

    // Sparkles often have shorter lifespans but not as short as fire
    bool IsAmbientTiming = (HeadLifeStart > 500.0f && HeadLifeStart < 2500.0f);

    return IsBright && IsSparklelike && IsAmbientTiming;
}
