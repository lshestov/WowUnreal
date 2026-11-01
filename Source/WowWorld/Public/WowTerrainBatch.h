#pragma once
#include "CoreMinimal.h"
struct FAdtData;

/** Pure ADT batching data. Layers are never baked/resampled into a colour texture. */
struct WOWWORLD_API FWowTerrainBatch
{
    static constexpr int32 ChunkPixels=64;
    static constexpr int32 Grid=16;
    static constexpr int32 AtlasPixels=ChunkPixels*Grid;
    TArray<int32> SectionForChunk, Representatives;
    explicit FWowTerrainBatch(const FAdtData& Data);
    static TArray<FColor> AlphaPixels(const FAdtData& Data);
    /** Equivalent to a clamped 64x64 bilinear sampler, without atlas-edge bleed. */
    static FVector2f AlphaUV(const FVector2f& Local,const FVector2f& Chunk);
};
