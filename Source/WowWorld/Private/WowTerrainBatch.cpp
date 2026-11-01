#include "WowTerrainBatch.h"
#include "Formats/AdtTypes.h"

FWowTerrainBatch::FWowTerrainBatch(const FAdtData& Data)
{
    SectionForChunk.Init(INDEX_NONE,256); TMap<FString,int32> Sections;
    for(int32 I=0;I<256;++I)
    {
        const auto& C=Data.Chunks[I]; if(C.Holes==0xffff) continue;
        FString Key;
        for(int32 Layer=0;Layer<4;++Layer)
            Key+=FString::Printf(TEXT("%d/"),C.TextureIndices.IsValidIndex(Layer)?C.TextureIndices[Layer]:INDEX_NONE);
        int32* Existing=Sections.Find(Key);
        if(!Existing) {const int32 Section=Representatives.Add(I); Sections.Add(Key,Section); Existing=Sections.Find(Key);}
        SectionForChunk[I]=*Existing;
    }
}
TArray<FColor> FWowTerrainBatch::AlphaPixels(const FAdtData& Data)
{
    TArray<FColor> Pixels; Pixels.Init(FColor(0,0,0,255),AtlasPixels*AtlasPixels);
    for(const auto& Chunk:Data.Chunks)
    {
        if(Chunk.IndexX<0 || Chunk.IndexX>=Grid || Chunk.IndexY<0 || Chunk.IndexY>=Grid) continue;
        for(int32 Y=0;Y<ChunkPixels;++Y) for(int32 X=0;X<ChunkPixels;++X)
        {
            auto& Pixel=Pixels[(Chunk.IndexY*ChunkPixels+Y)*AtlasPixels+Chunk.IndexX*ChunkPixels+X];
            const int32 Source=Y*ChunkPixels+X;
            Pixel.R=Chunk.AlphaMaps.IsValidIndex(0) && Chunk.AlphaMaps[0].IsValidIndex(Source)?Chunk.AlphaMaps[0][Source]:0;
            Pixel.G=Chunk.AlphaMaps.IsValidIndex(1) && Chunk.AlphaMaps[1].IsValidIndex(Source)?Chunk.AlphaMaps[1][Source]:0;
            Pixel.B=Chunk.AlphaMaps.IsValidIndex(2) && Chunk.AlphaMaps[2].IsValidIndex(Source)?Chunk.AlphaMaps[2][Source]:0;
        }
    }
    return Pixels;
}
FVector2f FWowTerrainBatch::AlphaUV(const FVector2f& Local,const FVector2f& Chunk)
{
    return (Chunk*float(ChunkPixels)+FVector2f(FMath::Clamp(Local.X,.5f/ChunkPixels,1.f-.5f/ChunkPixels),FMath::Clamp(Local.Y,.5f/ChunkPixels,1.f-.5f/ChunkPixels))*float(ChunkPixels))/float(AtlasPixels);
}
