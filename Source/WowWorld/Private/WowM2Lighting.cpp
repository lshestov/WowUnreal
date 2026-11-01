#include "WowM2Lighting.h"
#include "Coord/WowCoordinate.h"

float FWowM2Lighting::Attenuation(float Distance,float Start,float End)
{
    const float D=FMath::Max(0.f,Distance);
    if(End>Start && End>0) return FMath::Clamp((End-D)/FMath::Max(.001f,End-Start),0.f,1.f);
    // CM2Light's original constant/linear/quadratic defaults (0, .7, .03).
    return 1.f/FMath::Max(1.f,.7f*D+.03f*D*D);
}

FWowM2LightSample FWowM2Lighting::Sample(const FM2Data& Model,int32 Index,float AgeMs,float GlobalMs)
{
    FWowM2LightSample Out; if(!Model.Lights.IsValidIndex(Index)) return Out;
    const auto& L=Model.Lights[Index]; const auto& Durations=Model.GlobalSequenceDurations;
    int32 Animation=Model.Animations.IndexOfByPredicate([](const auto& A){return A.AnimationId==0;}); if(Animation<0) Animation=0;
    const float Clip=Model.Animations.IsValidIndex(Animation) && Model.Animations[Animation].Length?FMath::Fmod(AgeMs,float(Model.Animations[Animation].Length)):AgeMs;
    const auto Scalar=[&](const FM2ParticleFloatTrack& T,float Default){return T.Sample(Animation,T.GlobalSequence>=0?GlobalMs:Clip,Durations,Default);};
    const auto Color=[&](const TM2AnimatedTrack<FVector3f>& T,const FM2ParticleFloatTrack& Intensity)
    {
        const auto C=T.Sample(T.GlobalSequence>=0?GlobalMs:Clip,Durations,FVector3f::ZeroVector,Animation);
        const float I=FMath::Max(0.f,Scalar(Intensity,1)); return FLinearColor(FMath::Max(0.f,C.X)*I,FMath::Max(0.f,C.Y)*I,FMath::Max(0.f,C.Z)*I);
    };
    Out.Diffuse=Color(L.DiffuseColor,L.DiffuseIntensity); Out.Ambient=Color(L.AmbientColor,L.AmbientIntensity);
    Out.Start=FMath::Max(0.f,Scalar(L.AttenuationStart,0)); Out.End=Scalar(L.AttenuationEnd,0);
    Out.Radius=Out.End>Out.Start && Out.End>0?Out.End:(FMath::Sqrt(.7f*.7f+4*.03f*100)-.7f)/(2*.03f);
    Out.Visible=Scalar(L.Visibility,1)>0 && !Out.Diffuse.IsAlmostBlack();
    TArray<int32,TInlineAllocator<32>> Chain;
    for(int32 Bone=L.Bone;Model.Bones.IsValidIndex(Bone);Bone=Model.Bones[Bone].ParentBone)
    {
        if(Chain.Contains(Bone) || Chain.Num()>=256) {Out.Visible=false; return Out;}
        Chain.Add(Bone);
    }
    FTransform Matrix=FTransform::Identity;
    for(int32 I=Chain.Num()-1;I>=0;--I)
    {
        const int32 Bone=Chain[I]; const auto& B=Model.Bones[Bone];
        FVector T=FVector::ZeroVector,S=FVector::OneVector; FQuat Q=FQuat::Identity;
        if(Model.LightBoneTracks.IsValidIndex(Bone))
        {
            const auto& Tracks=Model.LightBoneTracks[Bone];
            T=FVector(Tracks.Translation.Sample(Tracks.Translation.GlobalSequence>=0?GlobalMs:Clip,Durations,FVector3f::ZeroVector,Animation));
            S=FVector(Tracks.Scale.Sample(Tracks.Scale.GlobalSequence>=0?GlobalMs:Clip,Durations,FVector3f(1),Animation));
            Q=FQuat(Tracks.Rotation.Sample(Tracks.Rotation.GlobalSequence>=0?GlobalMs:Clip,Durations,FQuat4f::Identity,Animation));
        }
        const FVector Pivot=FWowCoordinate::ModelToUE(B.PivotPoint);
        FTransform Local(FWowCoordinate::ModelRotationToUE(Q),FVector::ZeroVector,FVector(S.Y,S.X,S.Z));
        Local.SetTranslation(Pivot+FWowCoordinate::ModelToUE(T)-Local.TransformPosition(Pivot));
        Matrix=Local*Matrix;
    }
    Out.Position=Matrix.TransformPosition(FWowCoordinate::ModelToUE(L.Position));
    Out.Direction=Matrix.TransformVectorNoScale(FVector::DownVector).GetSafeNormal();
    if(Out.Position.ContainsNaN() || !FMath::IsFinite(Out.Radius+Out.Start+Out.End+Out.Diffuse.R+Out.Diffuse.G+Out.Diffuse.B)) Out.Visible=false;
    return Out;
}
