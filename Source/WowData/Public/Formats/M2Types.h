#pragma once
#include "CoreMinimal.h"

struct WOWDATA_API FM2Vertex
{
    FVector Position;
    FVector Normal;
    FVector2D TexCoord;
    FVector2D TexCoord2;
    uint8 BoneWeights[4] = {};
    uint8 BoneIndices[4] = {};
};

struct WOWDATA_API FM2RenderPass
{
    uint16 SubmeshIndex = 0;
    uint16 TextureIndex = 0;
    uint16 BlendMode = 0;
    uint16 Flags = 0;
    int16 ColorIndex=-1,AlphaIndex=-1,TransformIndex=-1;
    uint16 TextureCount=1,SecondaryTextureIndex=0,TexCoord=0,SecondaryTexCoord=0;
    int16 SecondaryTransformIndex=-1;
    uint8 Combiner=1,SecondaryCombiner=1;
    int8 PriorityPlane=0;
};

struct WOWDATA_API FM2Bone
{
	int32 KeyBoneId = -1;
	uint32 Flags = 0;
	int16 ParentBone = -1;
	FVector PivotPoint = FVector::ZeroVector;
};

struct WOWDATA_API FM2AnimSequence
{
	uint16 AnimationId = 0;
	uint16 SubAnimationId = 0;
	uint32 Length = 0; // duration in ms
	float MoveSpeed = 0.0f;
	uint32 Flags = 0;
	uint32 Frequency = 0; // authored variation weight in the original 15-bit random range
	int16 NextAnimation = -1;
	uint16 AliasNext = 0;
	bool IsLooping() const { return (Flags & 0x20) != 0; }
};

// Animation keyframe for a single bone
struct WOWDATA_API FM2AnimKeyframe
{
    uint32 Time = 0; // ms
    FVector Translation = FVector::ZeroVector;
    FQuat Rotation = FQuat::Identity;
    FVector Scale = FVector::OneVector;
};

// Animation track for one bone across one animation sequence
struct WOWDATA_API FM2BoneTrack
{
    int16 TranslationGlobal=-1,RotationGlobal=-1,ScaleGlobal=-1;
    TArray<uint32> TransTimestamps;
    TArray<FVector> TransValues;
    TArray<uint32> RotTimestamps;
    TArray<FQuat> RotValues;
    TArray<uint32> ScaleTimestamps;
    TArray<FVector> ScaleValues;

    bool HasTranslation() const { return TransTimestamps.Num() > 0; }
    bool HasRotation() const { return RotTimestamps.Num() > 0; }
    bool HasScale() const { return ScaleTimestamps.Num() > 0; }
    bool IsEmpty() const { return !HasTranslation() && !HasRotation() && !HasScale(); }
};

// All animation tracks for all bones for one animation sequence
struct WOWDATA_API FM2AnimationData
{
    uint16 AnimationId = 0;
    uint16 SubAnimationId = 0;
    uint32 Duration = 0; // ms
    bool bIsLooping = false;
    float MoveSpeed = 0.0f;
    TArray<FM2BoneTrack> BoneTracks; // one per bone
};

struct WOWDATA_API FM2Attachment
{
    uint32 Id = 0;           // Attachment point ID (1=right palm, 2=left palm, 5=right shoulder, 11=helmet, etc.)
    int32 Bone = -1;         // Bone index this attachment is parented to
    FVector Position = FVector::ZeroVector; // Position relative to the bone
};

template<typename T> struct TM2ParticleCurve
{
    TArray<float> Times;
    TArray<T> Values;
    SIZE_T GetAllocatedSize() const {return Times.GetAllocatedSize()+Values.GetAllocatedSize();}
    T Sample(float Time,const T& Default,bool Step=false) const
    {
        const int32 N=FMath::Min(Times.Num(),Values.Num());
        if(!N || !FMath::IsFinite(Time)) return Default;
        if(N==1 || Time<=Times[0]) return Values[0];
        if(Time>=Times[N-1]) return Values[N-1];
        int32 Low=0,High=N-1;
        while(High-Low>1) {const int32 Mid=(Low+High)/2; if(Times[Mid]<=Time) Low=Mid; else High=Mid;}
        if(Step) return Values[Low];
        const float Range=Times[High]-Times[Low];
        return Range>SMALL_NUMBER?FMath::Lerp(Values[Low],Values[High],(Time-Times[Low])/Range):Values[High];
    }
};

struct WOWDATA_API FM2ParticleFloatTrack
{
    uint16 Interpolation=0;
    int16 GlobalSequence=-1;
    TArray<TM2ParticleCurve<float>> Sequences;
    SIZE_T GetAllocatedSize() const {SIZE_T N=Sequences.GetAllocatedSize(); for(const auto& S:Sequences) N+=S.GetAllocatedSize(); return N;}
    float Sample(int32 Animation,float TimeMs,const TArray<uint32>& GlobalDurations,float Default=0) const;
};

template<typename T> struct TM2AnimatedTrack
{
    uint16 Interpolation=0; int16 GlobalSequence=-1;
    TArray<TM2ParticleCurve<T>> Sequences;
    SIZE_T GetAllocatedSize() const {SIZE_T N=Sequences.GetAllocatedSize(); for(const auto& S:Sequences) N+=S.GetAllocatedSize(); return N;}
    T Sample(float TimeMs,const TArray<uint32>& Durations,const T& Default,int32 Animation=0) const
    {
        if(Sequences.IsEmpty()) return Default;
        if(Durations.IsValidIndex(GlobalSequence) && Durations[GlobalSequence]) TimeMs=FMath::Fmod(TimeMs,float(Durations[GlobalSequence]));
        if(GlobalSequence>=0 || !Sequences.IsValidIndex(Animation)) Animation=0;
        return Sequences[Animation].Sample(TimeMs,Default,Interpolation==0);
    }
};
struct FM2ModelColor {TM2AnimatedTrack<FVector3f> Color; FM2ParticleFloatTrack Alpha;};
struct WOWDATA_API FM2Light
{
    uint16 Type=1;
    int16 Bone=-1;
    FVector Position=FVector::ZeroVector;
    TM2AnimatedTrack<FVector3f> AmbientColor,DiffuseColor;
    FM2ParticleFloatTrack AmbientIntensity,DiffuseIntensity,AttenuationStart,AttenuationEnd,Visibility;
};
struct FM2LightBoneTracks
{
    TM2AnimatedTrack<FVector3f> Translation,Scale;
    TM2AnimatedTrack<FQuat4f> Rotation;
};
struct FM2TextureTransform {TM2AnimatedTrack<FVector3f> Translation,Scale; TM2AnimatedTrack<FQuat4f> Rotation;};
struct FM2Ribbon
{
    int16 Bone=-1; uint16 Texture=0,BlendMode=2,Rows=1,Cols=1;
    FVector Position=FVector::ZeroVector;
    float EdgesPerSecond=10,EdgeLifetime=1,Gravity=0;
    TM2AnimatedTrack<FVector3f> Color;
    FM2ParticleFloatTrack Alpha,Above,Below,Slot,Visibility;
};

struct WOWDATA_API FM2ParticleEmitter
{
    uint32 Id = 0;                          // Particle emitter ID
    uint32 Flags = 0;                       // Emitter flags
    FVector Position = FVector::ZeroVector; // Position relative to bone
    int16 Bone = -1;                        // Bone index this emitter is attached to
    uint16 Texture = 0;                     // Texture index for particles
    FString BlendingType;                   // Blending mode for particles
    uint8 EmitterType = 0;                  // 1=plane, 2=sphere, 3=spline
    uint8 ParticleType = 0;                 // 0=regular, 1=chunky, 2=both
    uint8 HeadorTail = 0;                   // 0=head, 1=tail, 2=both
    int16 TailLength = 0;                   // Length of particle tail
    float MiddleTime = 0.0f;                // Time when particles are in middle phase
    uint32 ColorStart[3] = {255, 255, 255}; // RGB start color
    uint32 ColorMiddle[3] = {255, 255, 255}; // RGB middle color
    uint32 ColorEnd[3] = {255, 255, 255};   // RGB end color
    uint8 AlphaStart = 255;                 // Start alpha
    uint8 AlphaMiddle = 255;                // Middle alpha
    uint8 AlphaEnd = 255;                   // End alpha
    float ScaleStart = 1.0f;                // Start scale
    float ScaleMiddle = 1.0f;               // Middle scale
    float ScaleEnd = 1.0f;                  // End scale
    float ScaleVariation = 0.0f;            // Scale randomization
    float HeadLifeStart = 1000.0f;          // Head lifetime start (ms)
    float HeadLifeEnd = 1000.0f;            // Head lifetime end (ms)
    float HeadLifeRepeat = 0.0f;            // Head lifetime repeat rate
    float HeadDecay = 0.0f;                 // Head decay rate
    float TailLifeStart = 1000.0f;          // Tail lifetime start (ms)
    float TailLifeEnd = 1000.0f;            // Tail lifetime end (ms)
    float TailLifeRepeat = 0.0f;            // Tail lifetime repeat rate
    float TailDecay = 0.0f;                 // Tail decay rate
    float EmissionRate = 10.0f;             // Particles per second
    float EmissionAreaLength = 0.0f;        // Length of emission area
    float EmissionAreaWidth = 0.0f;         // Width of emission area
    float Gravity = -9.8f;                  // Gravity affecting particles
    // Actual build-12340 fields. Scalar tracks use milliseconds; lifetime
    // curves use normalized age and preserve their authored key values.
    uint8 BlendMode=0;
    uint16 Rows=1,Cols=1,ColorIndex=0;
    int16 Priority=0;
    FM2ParticleFloatTrack SpeedTrack,VariationTrack,LatitudeTrack,LongitudeTrack,GravityTrack,LifeTrack,RateTrack,WidthTrack,LengthTrack,ZSourceTrack,VisibilityTrack;
    TM2ParticleCurve<FVector3f> ColorCurve;
    TM2ParticleCurve<float> AlphaCurve,HeadCellCurve,TailCellCurve;
    TM2ParticleCurve<FVector2f> ScaleCurve;
    FVector2f ScaleVariance=FVector2f::ZeroVector;
    FVector3f Wind=FVector3f::ZeroVector;
    float LifeVariation=0,RateVariation=0,Drag=0,InitialSpin=0,InitialSpinVariation=0,Spin=0,SpinVariation=0,TailLengthUnits=0;

    // Helper functions
    bool IsFireEmitter() const;             // Check if this is a fire emitter
    bool IsSmokeEmitter() const;            // Check if this is a smoke emitter
    bool IsSparkleEmitter() const;          // Check if this is an ambient sparkle emitter
};

struct WOWDATA_API FM2Submesh
{
    uint16 Id = 0;           // Geoset ID (used for equipment geoset swaps)
    uint16 StartVertex = 0;
    uint16 NumVertices = 0;
    uint16 StartTriangle = 0;
    uint16 NumTriangles = 0;
    FVector CenterMass = FVector::ZeroVector;
    float Radius = 0.0f;
};

struct WOWDATA_API FM2Data
{
    // Camera keys are M2SplineKey<T> triples even for linear interpolation.
    template<typename T> struct TSplineTrack
    {
        uint16 Interpolation=0; int16 GlobalSequence=-1;
        struct FKey {T Value{},In{},Out{};};
        struct FSequence {TArray<uint32> Times;TArray<FKey> Keys;};
        TArray<FSequence> Sequences;
        SIZE_T GetAllocatedSize() const {SIZE_T N=Sequences.GetAllocatedSize();for(const auto& S:Sequences) N+=S.Times.GetAllocatedSize()+S.Keys.GetAllocatedSize();return N;}
        T Sample(float Time,const TArray<uint32>& Durations,const T& Default,int32 Animation=0) const
        {
            if(Sequences.IsEmpty() || !FMath::IsFinite(Time)) return Default;
            if(Durations.IsValidIndex(GlobalSequence) && Durations[GlobalSequence]) Time=FMath::Fmod(Time,float(Durations[GlobalSequence]));
            if(GlobalSequence>=0 || !Sequences.IsValidIndex(Animation)) Animation=0;
            const auto& S=Sequences[Animation];const int32 N=FMath::Min(S.Times.Num(),S.Keys.Num());
            if(!N) return Default;if(N==1 || Time<=S.Times[0]) return S.Keys[0].Value;
            if(Time>=S.Times[N-1]) return S.Keys[N-1].Value;
            int32 A=0,B=N-1;while(B-A>1){const int32 M=(A+B)/2;if(S.Times[M]<=Time) A=M;else B=M;}
            const float T0=(Time-S.Times[A])/FMath::Max(1.f,float(S.Times[B]-S.Times[A]));
            const auto& K=S.Keys[A];const auto& L=S.Keys[B];
            if(!Interpolation) return K.Value;
            if(Interpolation==1) return FMath::Lerp(K.Value,L.Value,T0);
            const float U=1-T0,TT=T0*T0;
            if(Interpolation==2) return K.Value*(2*TT*T0-3*TT+1)+K.Out*(TT*T0-2*TT+T0)+L.Value*(-2*TT*T0+3*TT)+L.In*(TT*T0-TT);
            return K.Value*(U*U*U)+K.Out*(3*U*U*T0)+L.In*(3*U*TT)+L.Value*(TT*T0);
        }
    };
    struct FCamera
    {
        int32 Type=-1;float Fov=0;
        FVector Position=FVector::ZeroVector,Target=FVector::ZeroVector;
        float NearClip=0,FarClip=0;
        FVector PositionPivot=FVector::ZeroVector,TargetPivot=FVector::ZeroVector;
        TSplineTrack<FVector> PositionTrack,TargetTrack;TSplineTrack<float> RollTrack;
    };
    TArray<FCamera> Cameras;
    struct FEvent {FString Token;uint32 Data=0,Bone=0;FVector Position=FVector::ZeroVector;int16 GlobalSequence=-1;TArray<TArray<uint32>> Times;};
    TArray<FEvent> Events;
    TArray<FVector> CollisionVertices;
    TArray<uint16> CollisionIndices;
    TArray<FM2Vertex> Vertices;
    TArray<uint16> Indices;
    TArray<FString> TexturePaths;
    TArray<uint32> TextureTypes;  // M2 texture type per index (0=filename, 1=skin, 6=hair, etc.)
    TArray<uint32> TextureFlags;
    TArray<FM2RenderPass> RenderPasses;
    FBox BoundingBox = FBox(ForceInit);
    float BoundingSphereRadius = 0.0f;
    TArray<FM2Bone> Bones;
    TArray<FM2AnimSequence> Animations;
    TArray<FM2AnimationData> AnimationTracks; // parsed keyframe data per animation
    TArray<FM2Attachment> Attachments;
    TArray<FM2ParticleEmitter> ParticleEmitters; // particle emitter data
    TArray<uint32> GlobalSequenceDurations;
    TArray<FM2ModelColor> Colors;
    TArray<FM2ParticleFloatTrack> TextureWeights;
    TArray<FM2TextureTransform> TextureTransforms;
    TArray<FM2Ribbon> Ribbons;
    TArray<FM2Light> Lights;
    TArray<FM2LightBoneTracks> LightBoneTracks;
    TArray<FM2Submesh> Submeshes;          // geoset info from skin file
    TArray<int16> AttachmentLookup;        // attachment ID → index in Attachments
    uint32 NumBones = 0;
    bool bIsValid = false;

    bool HasBones() const { return Bones.Num() > 0; }
    bool HasAnimations() const { return Animations.Num() > 0; }
    bool HasAnimationData() const { return AnimationTracks.Num() > 0; }
    SIZE_T GetAllocatedSize() const;
};
