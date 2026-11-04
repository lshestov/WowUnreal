#pragma once
#include <algorithm>
#include <cmath>

/** Wrath 12340 world effects policy, independent of streaming and physics. */
namespace WowRenderPolicy
{
inline float ParticleDensity(float Value)
{return std::isfinite(Value)?std::clamp(Value,.1f,1.f):1.f;}
inline float EnvironmentDetail(float Value)
{return std::isfinite(Value)?std::clamp(Value,.5f,1.5f):1.f;}

// MapDoodadDef::UpdateBounds uses the longest axis of the transformed AABB.
inline int DoodadFadeLevel(float SizeWow)
{
    constexpr float Sizes[]={1,4,15,100};
    for(int Level=0;Level<4;++Level) if(SizeWow<=Sizes[Level]) return Level;
    return 4;
}
inline float DoodadCullDistanceCm(int Level,float Detail)
{
    constexpr float Distances[]={30,100,200,750,1250};
    Level=std::clamp(Level,0,4);
    // ScaleFadeDist deliberately leaves the smallest/largest bands fixed.
    return Distances[Level]*100.f*((Level>0 && Level<4)?EnvironmentDetail(Detail):1.f);
}

inline int EmitCount(double& Accumulator,float Rate,float Seconds,float Density,int Available)
{
    Accumulator+=std::clamp(Rate,0.f,1000.f)*std::clamp(Seconds,0.f,.1f)*ParticleDensity(Density);
    const int Count=std::min({int(std::floor(Accumulator)),32,std::max(0,Available)});
    Accumulator=std::fmod(Accumulator,1.);
    return Count;
}
}
