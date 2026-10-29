#include "Formats/M2Parser.h"
DEFINE_LOG_CATEGORY_STATIC(LogM2, Log, All);

// ── Raw on-disk structures ──────────────────────────────────────────────────

#pragma pack(push, 1)

struct FM2Header
{
    uint32 Magic;               // 'MD20' = 0x3032444D
    uint32 Version;             // 264 for WotLK
    uint32 NameLength;
    uint32 NameOffset;
    uint32 GlobalFlags;
    uint32 nGlobalSequences;
    uint32 ofsGlobalSequences;
    uint32 nAnimations;
    uint32 ofsAnimations;
    uint32 nAnimationLookup;
    uint32 ofsAnimationLookup;
    uint32 nBones;
    uint32 ofsBones;
    uint32 nKeyBoneLookup;
    uint32 ofsKeyBoneLookup;
    uint32 nVertices;
    uint32 ofsVertices;
    uint32 nViews;              // Always 0 in WotLK; skins are in .skin files
    uint32 nColors;
    uint32 ofsColors;
    uint32 nTextures;
    uint32 ofsTextures;
    uint32 nTransparency;
    uint32 ofsTransparency;
    uint32 nTexAnims;
    uint32 ofsTexAnims;
    uint32 nTexReplace;
    uint32 ofsTexReplace;
    uint32 nRenderFlags;
    uint32 ofsRenderFlags;
    uint32 nBoneLookup;
    uint32 ofsBoneLookup;
    uint32 nTexLookup;
    uint32 ofsTexLookup;
    uint32 nTexUnits;
    uint32 ofsTexUnits;
    uint32 nTransLookup;
    uint32 ofsTransLookup;
    uint32 nTexAnimLookup;
    uint32 ofsTexAnimLookup;
    float  BoundingBox[6];      // min xyz, max xyz
    float  BoundingSphereRadius;
    float  CollisionBox[6];
    float  CollisionSphereRadius;
    uint32 nBoundingTriangles;
    uint32 ofsBoundingTriangles;
    uint32 nBoundingVertices;
    uint32 ofsBoundingVertices;
    uint32 nBoundingNormals;
    uint32 ofsBoundingNormals;
    uint32 nAttachments;
    uint32 ofsAttachments;
    uint32 nAttachmentLookup;
    uint32 ofsAttachmentLookup;
    uint32 nEvents;
    uint32 ofsEvents;
    uint32 nLights;
    uint32 ofsLights;
    uint32 nCameras;
    uint32 ofsCameras;
    uint32 nCameraLookup;
    uint32 ofsCameraLookup;
    uint32 nRibbonEmitters;
    uint32 ofsRibbonEmitters;
    uint32 nParticleEmitters;
    uint32 ofsParticleEmitters;
};

struct FM2VertexRaw
{
    float  Pos[3];
    uint8  BoneWeights[4];
    uint8  BoneIndices[4];
    float  Normal[3];
    float  TexCoords[2];
    float  TexCoords2[2];
};

struct FM2TextureEntry
{
    uint32 Type;
    uint32 Flags;
    uint32 NameLength;
    uint32 NameOffset;
};

struct FM2RenderFlagEntry
{
    uint16 Flags;
    uint16 BlendMode;
};

// ── Skin file structures ────────────────────────────────────────────────────

struct FM2SkinHeader
{
    uint32 Magic;               // 'SKIN' = 0x4E494B53
    uint32 nIndices;
    uint32 ofsIndices;
    uint32 nTriangles;
    uint32 ofsTriangles;
    uint32 nProperties;
    uint32 ofsProperties;
    uint32 nSubmeshes;
    uint32 ofsSubmeshes;
    uint32 nTextureUnits;
    uint32 ofsTextureUnits;
    uint32 LOD;
};

struct FM2SkinSubmesh
{
    uint16 ID;
    uint16 Level;
    uint16 StartVertex;
    uint16 nVertices;
    uint16 StartTriangle;
    uint16 nTriangles;
    uint16 nBones;
    uint16 StartBones;
    uint16 BoneInfluences;
    uint16 RootBone;
    float  CenterMass[3];
    float  CenterBoundingBox[3];
    float  Radius;
};

struct FM2SkinTextureUnit
{
    uint8  Flags;
    int8   PriorityPlane;
    int16  ShaderId;
    uint16 SkinSectionIndex;
    uint16 GeosetIndex;
    int16  ColorIndex;
    uint16 RenderFlagsIndex;
    uint16 TexUnitNumber;
    uint16 OpCount;
    uint16 TextureComboIndex;
    uint16 TexCoordComboIndex;
    uint16 TransparencyComboIndex;
    uint16 TexAnimComboIndex;
};

struct FM2BoneRaw
{
	int32  KeyBoneId;
	uint32 Flags;
	int16  ParentBone;
	uint16 SubmeshId;
	uint16 Unk[2];
	// Animation blocks follow (translation, rotation, scale) — we skip for now
	// Each is 20 bytes (type, globalSeq, nTimestamps, ofsTimestamps, nValues, ofsValues)
	uint8 TransBlock[20];
	uint8 RotBlock[20];
	uint8 ScaleBlock[20];
	float PivotPoint[3];
};

struct FM2AnimSequenceRaw
{
	uint16 AnimationId;
	uint16 SubAnimationId;
	uint32 Length;
	float  MoveSpeed;
	uint32 Flags;
	int16  Probability;
	uint16 Padding;
	uint32 MinRepetitions;
	uint32 MaxRepetitions;
	uint32 BlendTime;
	float  BoundsMin[3];
	float  BoundsMax[3];
	float  BoundRadius;
	int16  NextAnimation;
	uint16 AliasNext;
};

struct FM2AttachmentRaw
{
    uint32 Id;
    uint32 Bone;
    float Position[3];
    uint8 EnabledBlock[20]; // AnimBlock — ignored
};

struct FM2CameraRaw
{
    int32 Type;
    float Fov,FarClip,NearClip;
    uint8 PositionTrack[20];
    float Position[3];
    uint8 TargetTrack[20];
    float Target[3];
    uint8 RollTrack[20];
};
static_assert(sizeof(FM2CameraRaw)==100);

struct FM2ArrayRaw {uint32 Count,Offset;};
struct FM2ParticleTrackRaw {uint16 Interpolation; int16 GlobalSequence; FM2ArrayRaw Times,Values;};
struct FM2ParticleCurveRaw {FM2ArrayRaw Times,Values;};
struct FM2ParticleEmitterRaw
{
    uint32 Id,Flags; float Position[3]; int16 Bone; uint16 Texture;
    FM2ArrayRaw GeometryModel,RecursionModel;
    uint8 BlendMode,EmitterType; uint16 ColorIndex,Unknown; int16 Priority; uint16 Rows,Cols;
    FM2ParticleTrackRaw Speed,Variation,Latitude,Longitude,Gravity,Life;
    float LifeVariation; FM2ParticleTrackRaw Rate; float RateVariation;
    FM2ParticleTrackRaw Width,Length,ZSource;
    FM2ParticleCurveRaw Color,Alpha,Scale; float ScaleVariation[2];
    FM2ParticleCurveRaw HeadCell,TailCell;
    float TailLength,TwinkleFps,TwinkleOnOff,TwinkleRange[2],InitialVelocity,Drag,InitialSpin,InitialSpinVariation,Spin,SpinVariation;
    float Tumble[6],Wind[3],WindTime,Follow[4];
    FM2ArrayRaw Spline; FM2ParticleTrackRaw Visibility;
};
static_assert(sizeof(FM2ParticleTrackRaw)==20 && sizeof(FM2ParticleCurveRaw)==16);
static_assert(sizeof(FM2ParticleEmitterRaw)==476);
static_assert(STRUCT_OFFSET(FM2ParticleEmitterRaw,Speed)==52 && STRUCT_OFFSET(FM2ParticleEmitterRaw,Color)==260 && STRUCT_OFFSET(FM2ParticleEmitterRaw,Visibility)==456);

#pragma pack(pop)

// ── Helpers ─────────────────────────────────────────────────────────────────

template<typename T>
static bool SafeRead(const uint8* Base, int32 TotalSize, uint32 Offset, uint32 Count, const T*& OutPtr)
{
    const uint64 End = static_cast<uint64>(Offset) + static_cast<uint64>(Count) * sizeof(T);
    if (End > static_cast<uint64>(TotalSize))
    {
        OutPtr = nullptr;
        return false;
    }
    OutPtr = reinterpret_cast<const T*>(Base + Offset);
    return true;
}

template<typename T> static bool ReadParticleScalarTrack(const uint8* Base,int32 Size,const FM2ParticleTrackRaw& Src,FM2ParticleFloatTrack& Out,int64& Budget)
{
    if(Src.Interpolation>1 || Src.Times.Count!=Src.Values.Count || Src.Times.Count>256) return false;
    const FM2ArrayRaw *Times=nullptr,*Values=nullptr;
    if(!SafeRead(Base,Size,Src.Times.Offset,Src.Times.Count,Times) || !SafeRead(Base,Size,Src.Values.Offset,Src.Values.Count,Values)) return false;
    const int64 HeaderCost=int64(Src.Times.Count)*sizeof(TM2ParticleCurve<float>); if(HeaderCost>Budget) return false; Budget-=HeaderCost;
    FM2ParticleFloatTrack Parsed; Parsed.Interpolation=Src.Interpolation; Parsed.GlobalSequence=Src.GlobalSequence; Parsed.Sequences.SetNum(Src.Times.Count);
    for(uint32 I=0;I<Src.Times.Count;++I)
    {
        if(Values[I].Count>4096 || (Times[I].Count!=Values[I].Count && !(Times[I].Count==0 && Values[I].Count==1))) return false;
        const int64 Cost=int64(Values[I].Count)*(sizeof(float)+sizeof(float)); if(Cost>Budget) return false; Budget-=Cost;
        const uint32* Timestamps=nullptr; const T* Keys=nullptr;
        if(!SafeRead(Base,Size,Times[I].Offset,Times[I].Count,Timestamps) || !SafeRead(Base,Size,Values[I].Offset,Values[I].Count,Keys)) return false;
        auto& Sequence=Parsed.Sequences[I];
        for(uint32 J=0;J<Values[I].Count;++J)
        {
            const float Time=Times[I].Count?float(Timestamps[J]):0,Value=float(Keys[J]);
            if(!FMath::IsFinite(Value) || (J && Time<Sequence.Times.Last())) return false;
            Sequence.Times.Add(Time); Sequence.Values.Add(Value);
        }
    }
    Out=MoveTemp(Parsed); return true;
}
static FString M2String(const uint8* Base,int32 Size,uint32 Offset,uint32 Count)
{
    const ANSICHAR* Chars=nullptr;
    if(!Count || Count>1024*1024 || !SafeRead(Base,Size,Offset,Count,Chars)) return FString();
    uint32 Length=0; while(Length<Count && Chars[Length]) ++Length;
    const FUTF8ToTCHAR Text(Chars,Length); return FString(Text.Length(),Text.Get());
}
template<typename T> static bool ParticleValueFinite(const T& Value) {return !Value.ContainsNaN();}
template<> bool ParticleValueFinite(const float& Value) {return FMath::IsFinite(Value);}
template<typename Raw,typename Value,typename Convert> static bool ReadParticleCurve(const uint8* Base,int32 Size,const FM2ParticleCurveRaw& Src,TM2ParticleCurve<Value>& Out,int64& Budget,Convert MakeValue)
{
    if(Src.Times.Count!=Src.Values.Count || Src.Times.Count>4096) return false;
    const int64 Cost=int64(Src.Values.Count)*(sizeof(Value)+sizeof(float)); if(Cost>Budget) return false; Budget-=Cost;
    const int16* Times=nullptr; const Raw* Values=nullptr;
    if(!SafeRead(Base,Size,Src.Times.Offset,Src.Times.Count,Times) || !SafeRead(Base,Size,Src.Values.Offset,Src.Values.Count,Values)) return false;
    TM2ParticleCurve<Value> Parsed;
    for(uint32 I=0;I<Src.Values.Count;++I)
    {
        const float Time=FMath::Clamp(float(Times[I])/32767.f,0.f,1.f); const auto Key=MakeValue(Values[I]);
        if(!ParticleValueFinite(Key) || (I && Time<Parsed.Times.Last())) return false;
        Parsed.Times.Add(Time); Parsed.Values.Add(Key);
    }
    Out=MoveTemp(Parsed); return true;
}

// ── Implementation ──────────────────────────────────────────────────────────

FM2Data FM2Parser::Parse(const TArray<uint8>& InData, const TArray<uint8>& SkinData)
{
    FM2Data Result;

    // ── Validate M2 header ──────────────────────────────────────────────
    if (InData.Num() < static_cast<int32>(sizeof(FM2Header)))
    {
        UE_LOG(LogM2, Error, TEXT("M2 data too small (%d bytes)"), InData.Num());
        return Result;
    }

    const uint8* M2Base = InData.GetData();
    const int32  M2Size = InData.Num();
    const FM2Header& Header = *reinterpret_cast<const FM2Header*>(M2Base);

    // Magic check: 'MD20' stored little-endian as 0x3032444D
    if (Header.Magic != 0x3032444D)
    {
        UE_LOG(LogM2, Error, TEXT("Bad M2 magic: 0x%08X"), Header.Magic);
        return Result;
    }

    UE_LOG(LogM2, Verbose, TEXT("M2 version=%u, verts=%u, textures=%u, bones=%u"),
        Header.Version, Header.nVertices, Header.nTextures, Header.nBones);

    // ── Read model name ─────────────────────────────────────────────────
    const FString ModelName=M2String(M2Base,M2Size,Header.NameOffset,Header.NameLength);

    // ── Bounding box ────────────────────────────────────────────────────
    // WoW coordinate system: Z-up, Y-north. Convert to UE: X-forward, Z-up.
    // WoW (X,Y,Z) -> UE (Y, X, Z)  — or just store as-is and let the
    // mesh builder handle coordinate conversion.  We store raw here.
    Result.BoundingBox = FBox(
        FVector(Header.BoundingBox[0], Header.BoundingBox[1], Header.BoundingBox[2]),
        FVector(Header.BoundingBox[3], Header.BoundingBox[4], Header.BoundingBox[5])
    );
    Result.BoundingSphereRadius = Header.BoundingSphereRadius;
    Result.NumBones = Header.nBones;

    // Portraits use the model's authored camera at the beginning of Stand.
    // Camera spline values are offsets from the two pivots, not world positions.
    const auto CameraOffset=[&](const uint8* Track)
    {
        uint32 Count=0,Offset=0; FMemory::Memcpy(&Count,Track+12,4); FMemory::Memcpy(&Offset,Track+16,4);
        const uint32* Arrays=nullptr; const float* Key=nullptr;
        if(!Count || Count>4096 || !SafeRead(M2Base,M2Size,Offset,Count*2,Arrays) || !Arrays[0] ||
            !SafeRead(M2Base,M2Size,Arrays[1],9,Key)) return FVector::ZeroVector;
        const FVector Value(Key[0],Key[1],Key[2]); return Value.ContainsNaN()?FVector::ZeroVector:Value;
    };
    const FM2CameraRaw* Cameras=nullptr;
    if(Header.nCameras<=64 && SafeRead(M2Base,M2Size,Header.ofsCameras,Header.nCameras,Cameras))
        for(uint32 I=0;I<Header.nCameras;++I)
        {
            const auto& Raw=Cameras[I]; FM2Data::FCamera View;
            View.Type=Raw.Type; View.Fov=Raw.Fov;
            View.Position=FVector(Raw.Position[0],Raw.Position[1],Raw.Position[2])+CameraOffset(Raw.PositionTrack);
            View.Target=FVector(Raw.Target[0],Raw.Target[1],Raw.Target[2])+CameraOffset(Raw.TargetTrack);
            if(FMath::IsFinite(View.Fov) && View.Fov>0 && View.Fov<PI && !View.Position.ContainsNaN() &&
                !View.Target.ContainsNaN() && FVector::DistSquared(View.Position,View.Target)>SMALL_NUMBER) Result.Cameras.Add(View);
        }

    // Dedicated simplified collision data, not the visible leaves/cloth mesh.
    const float* CollisionPositions = nullptr;
    const uint16* CollisionIndices = nullptr;
    if (Header.nBoundingVertices <= 1000000 && Header.nBoundingTriangles <= 3000000 &&
        SafeRead(M2Base, M2Size, Header.ofsBoundingVertices, Header.nBoundingVertices * 3, CollisionPositions) &&
        SafeRead(M2Base, M2Size, Header.ofsBoundingTriangles, Header.nBoundingTriangles, CollisionIndices))
    {
        for (uint32 I = 0; I < Header.nBoundingVertices; ++I)
            Result.CollisionVertices.Add(FVector(CollisionPositions[I * 3], CollisionPositions[I * 3 + 1], CollisionPositions[I * 3 + 2]));
        if (Header.nBoundingTriangles) Result.CollisionIndices.Append(CollisionIndices, Header.nBoundingTriangles);
    }

    // ── Read vertices ───────────────────────────────────────────────────
    const FM2VertexRaw* RawVerts = nullptr;
    if (!SafeRead(M2Base, M2Size, Header.ofsVertices, Header.nVertices, RawVerts))
    {
        UE_LOG(LogM2, Error, TEXT("Vertex data out of bounds"));
        return Result;
    }

    Result.Vertices.SetNum(Header.nVertices);
    for (uint32 i = 0; i < Header.nVertices; i++)
    {
        const FM2VertexRaw& Src = RawVerts[i];
        FM2Vertex& Dst = Result.Vertices[i];
        Dst.Position = FVector(Src.Pos[0], Src.Pos[1], Src.Pos[2]);
        Dst.Normal   = FVector(Src.Normal[0], Src.Normal[1], Src.Normal[2]);
        Dst.TexCoord = FVector2D(Src.TexCoords[0], Src.TexCoords[1]);
        Dst.TexCoord2 = FVector2D(Src.TexCoords2[0], Src.TexCoords2[1]);
        FMemory::Memcpy(Dst.BoneWeights, Src.BoneWeights, 4);
        FMemory::Memcpy(Dst.BoneIndices, Src.BoneIndices, 4);
    }

    // ── Read bones ───────────────────────────────────────────────────────
    const FM2BoneRaw* RawBones = nullptr;
    if (Header.nBones > 0 && SafeRead(M2Base, M2Size, Header.ofsBones, Header.nBones, RawBones))
    {
        Result.Bones.SetNum(Header.nBones);
        for (uint32 i = 0; i < Header.nBones; i++)
        {
            const FM2BoneRaw& Src = RawBones[i];
            FM2Bone& Dst = Result.Bones[i];
            Dst.KeyBoneId = Src.KeyBoneId;
            Dst.Flags = Src.Flags;
            Dst.ParentBone = Src.ParentBone;
            Dst.PivotPoint = FVector(Src.PivotPoint[0], Src.PivotPoint[1], Src.PivotPoint[2]);
        }
        UE_LOG(LogM2, Verbose, TEXT("  Parsed %d bones"), Result.Bones.Num());
    }

    // ── Read animation sequences ────────────────────────────────────────
    const FM2AnimSequenceRaw* RawAnims = nullptr;
    if (Header.nAnimations > 0 && SafeRead(M2Base, M2Size, Header.ofsAnimations, Header.nAnimations, RawAnims))
    {
        Result.Animations.SetNum(Header.nAnimations);
        for (uint32 i = 0; i < Header.nAnimations; i++)
        {
            const FM2AnimSequenceRaw& Src = RawAnims[i];
            FM2AnimSequence& Dst = Result.Animations[i];
            Dst.AnimationId = Src.AnimationId;
            Dst.SubAnimationId = Src.SubAnimationId;
            Dst.Length = Src.Length;
            Dst.MoveSpeed = Src.MoveSpeed;
            Dst.Flags = Src.Flags;
            Dst.NextAnimation = Src.NextAnimation;
            Dst.AliasNext = Src.AliasNext;
        }
        UE_LOG(LogM2, Verbose, TEXT("  Parsed %d animation sequences"), Result.Animations.Num());
    }

    // ── Parse animation keyframes ─────────────────────────────────────────
    // Helper function to unpack quaternions from int16 values
    auto UnpackQuaternion = [](int16 x, int16 y, int16 z, int16 w) -> FQuat
    {
        return FQuat(
            (x < 0 ? (x + 32768) : (x - 32767)) / 32767.0f,
            (y < 0 ? (y + 32768) : (y - 32767)) / 32767.0f,
            (z < 0 ? (z + 32768) : (z - 32767)) / 32767.0f,
            (w < 0 ? (w + 32768) : (w - 32767)) / 32767.0f
        );
    };

    // Structure for animation block header (20 bytes)
    struct FAnimBlock
    {
        uint16 InterpolationType;
        uint16 GlobalSequence;
        uint32 nTimestampArrays;
        uint32 ofsTimestampArrays;
        uint32 nValueArrays;
        uint32 ofsValueArrays;
    };

    // Sub-array header (8 bytes)
    struct FSubArrayHeader
    {
        uint32 nEntries;
        uint32 ofsEntries;
    };

    if (Header.nAnimations > 0 && Header.nBones > 0 && RawBones != nullptr)
    {
        // Parse keyframes for all animations that have embedded data
        Result.AnimationTracks.Reserve(Header.nAnimations);

        for (uint32 animIdx = 0; animIdx < Header.nAnimations; animIdx++)
        {
            const FM2AnimSequenceRaw& AnimSeq = RawAnims[animIdx];

            FM2AnimationData AnimData;
            AnimData.AnimationId = AnimSeq.AnimationId;
            AnimData.SubAnimationId = AnimSeq.SubAnimationId;
            AnimData.Duration = AnimSeq.Length;
            // Use M2 flag (0x20) for looping, with fallback for essential animations
            AnimData.bIsLooping = (AnimSeq.Flags & 0x20) != 0 ||
                AnimSeq.AnimationId == 0 || AnimSeq.AnimationId == 4 || AnimSeq.AnimationId == 5; // Stand, Walk, Run
            AnimData.MoveSpeed = AnimSeq.MoveSpeed;
            AnimData.BoneTracks.SetNum(Header.nBones);

            bool bHasAnyKeyframes = false;

            // Process each bone's animation data
            for (uint32 boneIdx = 0; boneIdx < Header.nBones; boneIdx++)
            {
                const FM2BoneRaw& Bone = RawBones[boneIdx];
                FM2BoneTrack& Track = AnimData.BoneTracks[boneIdx];

                // Parse translation block
                const FAnimBlock* TransBlock = reinterpret_cast<const FAnimBlock*>(Bone.TransBlock);
                if (TransBlock->nTimestampArrays > animIdx && TransBlock->nValueArrays > animIdx)
                {
                    const FSubArrayHeader* TimestampHeaders = nullptr;
                    const FSubArrayHeader* ValueHeaders = nullptr;

                    if (SafeRead(M2Base, M2Size, TransBlock->ofsTimestampArrays, TransBlock->nTimestampArrays, TimestampHeaders) &&
                        SafeRead(M2Base, M2Size, TransBlock->ofsValueArrays, TransBlock->nValueArrays, ValueHeaders))
                    {
                        const FSubArrayHeader& TimestampHeader = TimestampHeaders[animIdx];
                        const FSubArrayHeader& ValueHeader = ValueHeaders[animIdx];

                        if (TimestampHeader.nEntries > 0 && ValueHeader.nEntries > 0)
                        {
                            const uint32* Timestamps = nullptr;
                            const float* Values = nullptr; // 3 floats per entry (FVector)

                            if (SafeRead(M2Base, M2Size, TimestampHeader.ofsEntries, TimestampHeader.nEntries, Timestamps) &&
                                SafeRead(M2Base, M2Size, ValueHeader.ofsEntries, ValueHeader.nEntries * 3, Values))
                            {
                                Track.TransTimestamps.SetNum(TimestampHeader.nEntries);
                                Track.TransValues.SetNum(ValueHeader.nEntries);

                                FMemory::Memcpy(Track.TransTimestamps.GetData(), Timestamps, TimestampHeader.nEntries * sizeof(uint32));

                                for (uint32 i = 0; i < ValueHeader.nEntries; i++)
                                {
                                    Track.TransValues[i] = FVector(Values[i * 3], Values[i * 3 + 1], Values[i * 3 + 2]);
                                }
                                bHasAnyKeyframes = true;
                            }
                        }
                    }
                }

                // Parse rotation block
                const FAnimBlock* RotBlock = reinterpret_cast<const FAnimBlock*>(Bone.RotBlock);
                if (RotBlock->nTimestampArrays > animIdx && RotBlock->nValueArrays > animIdx)
                {
                    const FSubArrayHeader* TimestampHeaders = nullptr;
                    const FSubArrayHeader* ValueHeaders = nullptr;

                    if (SafeRead(M2Base, M2Size, RotBlock->ofsTimestampArrays, RotBlock->nTimestampArrays, TimestampHeaders) &&
                        SafeRead(M2Base, M2Size, RotBlock->ofsValueArrays, RotBlock->nValueArrays, ValueHeaders))
                    {
                        const FSubArrayHeader& TimestampHeader = TimestampHeaders[animIdx];
                        const FSubArrayHeader& ValueHeader = ValueHeaders[animIdx];

                        if (TimestampHeader.nEntries > 0 && ValueHeader.nEntries > 0)
                        {
                            const uint32* Timestamps = nullptr;
                            const int16* Values = nullptr; // 4 int16 per entry (packed quaternion)

                            if (SafeRead(M2Base, M2Size, TimestampHeader.ofsEntries, TimestampHeader.nEntries, Timestamps) &&
                                SafeRead(M2Base, M2Size, ValueHeader.ofsEntries, ValueHeader.nEntries * 4, Values))
                            {
                                Track.RotTimestamps.SetNum(TimestampHeader.nEntries);
                                Track.RotValues.SetNum(ValueHeader.nEntries);

                                FMemory::Memcpy(Track.RotTimestamps.GetData(), Timestamps, TimestampHeader.nEntries * sizeof(uint32));

                                for (uint32 i = 0; i < ValueHeader.nEntries; i++)
                                {
                                    int16 x = Values[i * 4];
                                    int16 y = Values[i * 4 + 1];
                                    int16 z = Values[i * 4 + 2];
                                    int16 w = Values[i * 4 + 3];
                                    Track.RotValues[i] = UnpackQuaternion(x, y, z, w);
                                }
                                bHasAnyKeyframes = true;
                            }
                        }
                    }
                }

                // Parse scale block
                const FAnimBlock* ScaleBlock = reinterpret_cast<const FAnimBlock*>(Bone.ScaleBlock);
                if (ScaleBlock->nTimestampArrays > animIdx && ScaleBlock->nValueArrays > animIdx)
                {
                    const FSubArrayHeader* TimestampHeaders = nullptr;
                    const FSubArrayHeader* ValueHeaders = nullptr;

                    if (SafeRead(M2Base, M2Size, ScaleBlock->ofsTimestampArrays, ScaleBlock->nTimestampArrays, TimestampHeaders) &&
                        SafeRead(M2Base, M2Size, ScaleBlock->ofsValueArrays, ScaleBlock->nValueArrays, ValueHeaders))
                    {
                        const FSubArrayHeader& TimestampHeader = TimestampHeaders[animIdx];
                        const FSubArrayHeader& ValueHeader = ValueHeaders[animIdx];

                        if (TimestampHeader.nEntries > 0 && ValueHeader.nEntries > 0)
                        {
                            const uint32* Timestamps = nullptr;
                            const float* Values = nullptr; // 3 floats per entry (FVector)

                            if (SafeRead(M2Base, M2Size, TimestampHeader.ofsEntries, TimestampHeader.nEntries, Timestamps) &&
                                SafeRead(M2Base, M2Size, ValueHeader.ofsEntries, ValueHeader.nEntries * 3, Values))
                            {
                                Track.ScaleTimestamps.SetNum(TimestampHeader.nEntries);
                                Track.ScaleValues.SetNum(ValueHeader.nEntries);

                                FMemory::Memcpy(Track.ScaleTimestamps.GetData(), Timestamps, TimestampHeader.nEntries * sizeof(uint32));

                                for (uint32 i = 0; i < ValueHeader.nEntries; i++)
                                {
                                    Track.ScaleValues[i] = FVector(Values[i * 3], Values[i * 3 + 1], Values[i * 3 + 2]);
                                }
                                bHasAnyKeyframes = true;
                            }
                        }
                    }
                }
            }

            if (bHasAnyKeyframes)
            {
                Result.AnimationTracks.Add(MoveTemp(AnimData));
            }
        }

        UE_LOG(LogM2, Verbose, TEXT("  Parsed %d animation tracks with keyframe data"), Result.AnimationTracks.Num());
    }

    // ── Read textures ───────────────────────────────────────────────────
    const FM2TextureEntry* TexEntries = nullptr;
    if (Header.nTextures > 0 && SafeRead(M2Base, M2Size, Header.ofsTextures, Header.nTextures, TexEntries))
    {
        Result.TexturePaths.SetNum(Header.nTextures);
        Result.TextureTypes.SetNum(Header.nTextures);
        for (uint32 i = 0; i < Header.nTextures; i++)
        {
            const FM2TextureEntry& Tex = TexEntries[i];
            Result.TextureTypes[i] = Tex.Type;
            if (Tex.Type == 0 && Tex.NameLength > 1)
            {
                // Type 0 = filename-based texture
                FString Path=M2String(M2Base,M2Size,Tex.NameOffset,Tex.NameLength);
                // Normalise path separators
                Path.ReplaceInline(TEXT("\\"), TEXT("/"));
                Result.TexturePaths[i] = Path;
            }
            else
            {
                // Type != 0 means a replaceable texture (skin, hair, etc.)
                // Store empty string; the renderer resolves these by type at runtime
                Result.TexturePaths[i] = FString();
            }
        }
    }

    // ── Read render flags ───────────────────────────────────────────────
    struct FRenderFlag { uint16 Flags; uint16 BlendMode; };
    TArray<FRenderFlag> RenderFlags;
    const FM2RenderFlagEntry* RFEntries = nullptr;
    if (Header.nRenderFlags > 0 && SafeRead(M2Base, M2Size, Header.ofsRenderFlags, Header.nRenderFlags, RFEntries))
    {
        RenderFlags.SetNum(Header.nRenderFlags);
        for (uint32 i = 0; i < Header.nRenderFlags; i++)
        {
            RenderFlags[i].Flags     = RFEntries[i].Flags;
            RenderFlags[i].BlendMode = RFEntries[i].BlendMode;
        }
    }

    // ── Read texture lookup table ───────────────────────────────────────
    TArray<uint16> TexLookup;
    const uint16* TexLookupPtr = nullptr;
    if (Header.nTexLookup > 0 && SafeRead(M2Base, M2Size, Header.ofsTexLookup, Header.nTexLookup, TexLookupPtr))
    {
        TexLookup.SetNum(Header.nTexLookup);
        FMemory::Memcpy(TexLookup.GetData(), TexLookupPtr, Header.nTexLookup * sizeof(uint16));
    }

    // Emitter-only effects have no geometry and legitimately have no skin.
    if(Header.nVertices>0)
    {
    // ── Parse skin file ─────────────────────────────────────────────────
    if (SkinData.Num() < static_cast<int32>(sizeof(FM2SkinHeader)))
    {
        UE_LOG(LogM2, Error, TEXT("Skin data too small (%d bytes)"), SkinData.Num());
        return Result;
    }

    const uint8* SkinBase = SkinData.GetData();
    const int32  SkinSize = SkinData.Num();
    const FM2SkinHeader& Skin = *reinterpret_cast<const FM2SkinHeader*>(SkinBase);

    if (Skin.Magic != 0x4E494B53) // 'SKIN' in LE
    {
        UE_LOG(LogM2, Error, TEXT("Bad skin magic: 0x%08X"), Skin.Magic);
        return Result;
    }

    // ── Skin vertex indices (lookup into M2 vertex array) ───────────────
    const uint16* SkinVertexIndices = nullptr;
    if (!SafeRead(SkinBase, SkinSize, Skin.ofsIndices, Skin.nIndices, SkinVertexIndices))
    {
        UE_LOG(LogM2, Error, TEXT("Skin vertex indices out of bounds"));
        return Result;
    }

    // ── Skin triangle indices (index into SkinVertexIndices) ────────────
    const uint16* SkinTriangles = nullptr;
    if (!SafeRead(SkinBase, SkinSize, Skin.ofsTriangles, Skin.nTriangles, SkinTriangles))
    {
        UE_LOG(LogM2, Error, TEXT("Skin triangles out of bounds"));
        return Result;
    }

    // ── Submeshes ───────────────────────────────────────────────────────
    const FM2SkinSubmesh* SkinSubmeshes = nullptr;
    if (!SafeRead(SkinBase, SkinSize, Skin.ofsSubmeshes, Skin.nSubmeshes, SkinSubmeshes))
    {
        UE_LOG(LogM2, Error, TEXT("Skin submeshes out of bounds"));
        return Result;
    }

    // ── Texture units ───────────────────────────────────────────────────
    const FM2SkinTextureUnit* SkinTexUnits = nullptr;
    if (!SafeRead(SkinBase, SkinSize, Skin.ofsTextureUnits, Skin.nTextureUnits, SkinTexUnits))
    {
        UE_LOG(LogM2, Error, TEXT("Skin texture units out of bounds"));
        return Result;
    }

    // ── Build final index buffer ────────────────────────────────────────
    // The skin's triangle indices are indices into SkinVertexIndices[],
    // which are themselves indices into the M2's vertex array.
    // We resolve the double-indirection and emit direct vertex indices.
    Result.Indices.SetNum(Skin.nTriangles);
    for (uint32 i = 0; i < Skin.nTriangles; i++)
    {
        uint16 LocalIdx = SkinTriangles[i];
        if (LocalIdx < Skin.nIndices)
        {
            Result.Indices[i] = SkinVertexIndices[LocalIdx];
        }
        else
        {
            UE_LOG(LogM2, Warning, TEXT("Triangle index %u out of range (max %u)"), LocalIdx, Skin.nIndices);
            Result.Indices[i] = 0;
        }
    }

    // ── Build render passes from texture units ──────────────────────────
    Result.RenderPasses.SetNum(Skin.nTextureUnits);
    for (uint32 i = 0; i < Skin.nTextureUnits; i++)
    {
        const FM2SkinTextureUnit& TU = SkinTexUnits[i];
        FM2RenderPass& Pass = Result.RenderPasses[i];

        Pass.SubmeshIndex = TU.SkinSectionIndex;

        // Resolve texture index through the texture lookup table
        if (TU.TextureComboIndex < static_cast<uint16>(TexLookup.Num()))
        {
            Pass.TextureIndex = TexLookup[TU.TextureComboIndex];
        }
        else
        {
            Pass.TextureIndex = TU.TextureComboIndex;
        }

        // Resolve render flags
        if (TU.RenderFlagsIndex < static_cast<uint16>(RenderFlags.Num()))
        {
            Pass.Flags     = RenderFlags[TU.RenderFlagsIndex].Flags;
            Pass.BlendMode = RenderFlags[TU.RenderFlagsIndex].BlendMode;
        }
    }

    }

    // ── Parse attachments ────────────────────────────────────────────────
    const FM2AttachmentRaw* RawAttachments = nullptr;
    if (Header.nAttachments > 0 && SafeRead(M2Base, M2Size, Header.ofsAttachments, Header.nAttachments, RawAttachments))
    {
        Result.Attachments.SetNum(Header.nAttachments);
        for (uint32 i = 0; i < Header.nAttachments; i++)
        {
            const FM2AttachmentRaw& Src = RawAttachments[i];
            FM2Attachment& Dst = Result.Attachments[i];
            Dst.Id = Src.Id;
            Dst.Bone = static_cast<int32>(Src.Bone);
            Dst.Position = FVector(Src.Position[0], Src.Position[1], Src.Position[2]);
        }
        UE_LOG(LogM2, Verbose, TEXT("  Parsed %d attachments"), Result.Attachments.Num());
    }

    // ── Parse attachment lookup ─────────────────────────────────────────
    const int16* RawAttachLookup = nullptr;
    if (Header.nAttachmentLookup > 0 && SafeRead(M2Base, M2Size, Header.ofsAttachmentLookup, Header.nAttachmentLookup, RawAttachLookup))
    {
        Result.AttachmentLookup.SetNum(Header.nAttachmentLookup);
        FMemory::Memcpy(Result.AttachmentLookup.GetData(), RawAttachLookup, Header.nAttachmentLookup * sizeof(int16));
    }

    // ── Parse build-12340 particle emitters (476-byte records) ───────────
    const uint32* GlobalDurations=nullptr;
    if(Header.nGlobalSequences<=4096 && SafeRead(M2Base,M2Size,Header.ofsGlobalSequences,Header.nGlobalSequences,GlobalDurations))
        Result.GlobalSequenceDurations.Append(GlobalDurations,Header.nGlobalSequences);
    const FM2ParticleEmitterRaw* RawParticleEmitters = nullptr;
    int64 ParticleBudget=8*1024*1024;
    if (Header.Version==264 && Header.nParticleEmitters>0 && Header.nParticleEmitters<=256 && SafeRead(M2Base, M2Size, Header.ofsParticleEmitters, Header.nParticleEmitters, RawParticleEmitters))
    {
        Result.ParticleEmitters.SetNum(Header.nParticleEmitters);
        for (uint32 i = 0; i < Header.nParticleEmitters; i++)
        {
            const FM2ParticleEmitterRaw& Src = RawParticleEmitters[i];
            FM2ParticleEmitter& Dst = Result.ParticleEmitters[i];

            Dst.Id = Src.Id;
            Dst.Flags = Src.Flags;
            Dst.Position = FVector(Src.Position[0], Src.Position[1], Src.Position[2]);
            Dst.Bone = Src.Bone;
            Dst.Texture = Src.Texture;
            Dst.BlendMode=Src.BlendMode; Dst.BlendingType=FString::FromInt(Src.BlendMode);
            Dst.EmitterType = Src.EmitterType;
            Dst.Rows=Src.Rows; Dst.Cols=Src.Cols; Dst.Priority=Src.Priority; Dst.ColorIndex=Src.ColorIndex;
            const auto Track=[&](const FM2ParticleTrackRaw& From,FM2ParticleFloatTrack& To){ReadParticleScalarTrack<float>(M2Base,M2Size,From,To,ParticleBudget);};
            Track(Src.Speed,Dst.SpeedTrack); Track(Src.Variation,Dst.VariationTrack); Track(Src.Latitude,Dst.LatitudeTrack); Track(Src.Longitude,Dst.LongitudeTrack);
            Track(Src.Gravity,Dst.GravityTrack); Track(Src.Life,Dst.LifeTrack); Track(Src.Rate,Dst.RateTrack); Track(Src.Width,Dst.WidthTrack); Track(Src.Length,Dst.LengthTrack); Track(Src.ZSource,Dst.ZSourceTrack);
            ReadParticleScalarTrack<uint8>(M2Base,M2Size,Src.Visibility,Dst.VisibilityTrack,ParticleBudget);
            ReadParticleCurve<FVector3f>(M2Base,M2Size,Src.Color,Dst.ColorCurve,ParticleBudget,[](const FVector3f& V){return V/255.f;});
            ReadParticleCurve<int16>(M2Base,M2Size,Src.Alpha,Dst.AlphaCurve,ParticleBudget,[](int16 V){return FMath::Clamp(float(V)/32767.f,0.f,1.f);});
            ReadParticleCurve<FVector2f>(M2Base,M2Size,Src.Scale,Dst.ScaleCurve,ParticleBudget,[](const FVector2f& V){return V;});
            ReadParticleCurve<uint16>(M2Base,M2Size,Src.HeadCell,Dst.HeadCellCurve,ParticleBudget,[](uint16 V){return float(V);});
            ReadParticleCurve<uint16>(M2Base,M2Size,Src.TailCell,Dst.TailCellCurve,ParticleBudget,[](uint16 V){return float(V);});
            const auto Finite=[](float V){return FMath::IsFinite(V)?V:0.f;};
            Dst.ScaleVariance=FVector2f(Finite(Src.ScaleVariation[0]),Finite(Src.ScaleVariation[1])); Dst.Wind=FVector3f(Finite(Src.Wind[0]),Finite(Src.Wind[1]),Finite(Src.Wind[2]));
            Dst.LifeVariation=Finite(Src.LifeVariation); Dst.RateVariation=Finite(Src.RateVariation); Dst.Drag=Finite(Src.Drag);
            Dst.InitialSpin=Finite(Src.InitialSpin); Dst.InitialSpinVariation=Finite(Src.InitialSpinVariation); Dst.Spin=Finite(Src.Spin); Dst.SpinVariation=Finite(Src.SpinVariation); Dst.TailLengthUnits=Finite(Src.TailLength);
            // Keep the old approximation's public fields derived from authored
            // values until the general particle renderer replaces that path.
            const auto Color=[&](float Age,uint32* Out){const auto V=Dst.ColorCurve.Sample(Age,FVector3f(1)); Out[0]=FMath::Clamp(FMath::RoundToInt(V.X*255),0,255); Out[1]=FMath::Clamp(FMath::RoundToInt(V.Y*255),0,255); Out[2]=FMath::Clamp(FMath::RoundToInt(V.Z*255),0,255);};
            Color(0,Dst.ColorStart); Color(.5f,Dst.ColorMiddle); Color(1,Dst.ColorEnd);
            Dst.AlphaStart=FMath::RoundToInt(Dst.AlphaCurve.Sample(0,1.f)*255); Dst.AlphaMiddle=FMath::RoundToInt(Dst.AlphaCurve.Sample(.5f,1.f)*255); Dst.AlphaEnd=FMath::RoundToInt(Dst.AlphaCurve.Sample(1,1.f)*255);
            Dst.ScaleStart=Dst.ScaleCurve.Sample(0,FVector2f(1)).X; Dst.ScaleMiddle=Dst.ScaleCurve.Sample(.5f,FVector2f(1)).X; Dst.ScaleEnd=Dst.ScaleCurve.Sample(1,FVector2f(1)).X; Dst.ScaleVariation=Dst.ScaleVariance.X;
            Dst.HeadLifeStart=Dst.HeadLifeEnd=Dst.LifeTrack.Sample(0,0,Result.GlobalSequenceDurations,1)*1000;
            Dst.TailLifeStart=Dst.TailLifeEnd=Dst.HeadLifeStart; Dst.MiddleTime=.5f;
            Dst.EmissionRate=Dst.RateTrack.Sample(0,0,Result.GlobalSequenceDurations); Dst.EmissionAreaWidth=Dst.WidthTrack.Sample(0,0,Result.GlobalSequenceDurations); Dst.EmissionAreaLength=Dst.LengthTrack.Sample(0,0,Result.GlobalSequenceDurations); Dst.Gravity=Dst.GravityTrack.Sample(0,0,Result.GlobalSequenceDurations);
        }
        UE_LOG(LogM2, Verbose, TEXT("  Parsed %d particle emitters"), Result.ParticleEmitters.Num());
    }

    // ── Parse submeshes from skin file ──────────────────────────────────
    if (SkinData.Num() >= static_cast<int32>(sizeof(FM2SkinHeader)))
    {
        const FM2SkinHeader* SkinHeader = reinterpret_cast<const FM2SkinHeader*>(SkinData.GetData());
        const FM2SkinSubmesh* RawSubmeshes = nullptr;
        if (SkinHeader->nSubmeshes > 0 && SafeRead(SkinData.GetData(), SkinData.Num(), SkinHeader->ofsSubmeshes, SkinHeader->nSubmeshes, RawSubmeshes))
        {
            Result.Submeshes.SetNum(SkinHeader->nSubmeshes);
            for (uint32 i = 0; i < SkinHeader->nSubmeshes; i++)
            {
                const FM2SkinSubmesh& Src = RawSubmeshes[i];
                FM2Submesh& Dst = Result.Submeshes[i];
                Dst.Id = Src.ID;
                Dst.StartVertex = Src.StartVertex;
                Dst.NumVertices = Src.nVertices;
                Dst.StartTriangle = Src.StartTriangle;
                Dst.NumTriangles = Src.nTriangles;
                Dst.CenterMass = FVector(Src.CenterMass[0], Src.CenterMass[1], Src.CenterMass[2]);
                Dst.Radius = Src.Radius;
            }
            UE_LOG(LogM2, Verbose, TEXT("  Parsed %d submeshes from skin"), Result.Submeshes.Num());
        }
    }

    Result.bIsValid = true;
    UE_LOG(LogM2, Verbose, TEXT("M2 '%s' parsed: %d verts, %d indices, %d passes, %d textures, %d attachments, %d submeshes, %d particle emitters"),
        *ModelName, Result.Vertices.Num(), Result.Indices.Num(),
        Result.RenderPasses.Num(), Result.TexturePaths.Num(),
        Result.Attachments.Num(), Result.Submeshes.Num(), Result.ParticleEmitters.Num());
    return Result;
}
