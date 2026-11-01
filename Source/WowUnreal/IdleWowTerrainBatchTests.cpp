#include "Misc/AutomationTest.h"
#include "WowTerrainBatch.h"
#include "WowTerrainMeshBuilder.h"
#include "Formats/AdtTypes.h"
#include "Formats/AdtParser.h"
#include "Mpq/MpqManager.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowTerrainAlpha,"WowUnreal.Rendering.TerrainBatchLayersAndEdges",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowTerrainAlpha::RunTest(const FString&)
{
    auto Data=MakeUnique<FAdtData>();
    for(int32 I=0;I<256;++I)
    {
        auto& C=Data->Chunks[I]; C.IndexX=I%16; C.IndexY=I/16; C.TextureIndices={0,1,2,3};
        C.AlphaMaps.SetNum(3);
        for(int32 L=0;L<3;++L) {C.AlphaMaps[L].SetNum(4096); for(int32 P=0;P<4096;++P) C.AlphaMaps[L][P]=uint8((P*17+I*29+L*83)%256);}
    }
    Data->Chunks[1].TextureIndices={1,0,2,3}; Data->Chunks[2].Holes=0xffff;
    const FWowTerrainBatch Batch(*Data);
    TestTrue(TEXT("Only identical ordered layer sets share a section; full holes have none"),Batch.Representatives.Num()==2 && Batch.SectionForChunk[0]==Batch.SectionForChunk[3] && Batch.SectionForChunk[0]!=Batch.SectionForChunk[1] && Batch.SectionForChunk[2]==INDEX_NONE);
    const auto Atlas=FWowTerrainBatch::AlphaPixels(*Data);
    const auto Sample=[](double X,double Y,int32 Size,const auto& Pixel)
    {
        X=FMath::Clamp(X*Size-.5,0.,double(Size-1)); Y=FMath::Clamp(Y*Size-.5,0.,double(Size-1));
        const int32 IX=int32(X),IY=int32(Y); const double FX=X-IX,FY=Y-IY;
        return FMath::Lerp(FMath::Lerp(Pixel(IX,IY),Pixel(FMath::Min(IX+1,Size-1),IY),FX),FMath::Lerp(Pixel(IX,FMath::Min(IY+1,Size-1)),Pixel(FMath::Min(IX+1,Size-1),FMath::Min(IY+1,Size-1)),FX),FY);
    };
    double MaxError=0;
    for(int32 I:{0,1,15,16,127,240,255}) for(float U:{-.1f,0.f,.001f,.125f,.4321f,.999f,1.f,1.1f}) for(float V:{0.f,.003f,.25f,.876f,1.f}) for(int32 L=0;L<3;++L)
    {
        const auto& C=Data->Chunks[I]; const auto UV=FWowTerrainBatch::AlphaUV(FVector2f(U,V),FVector2f(C.IndexX,C.IndexY));
        const double Original=Sample(U,V,64,[&](int32 X,int32 Y){return double(C.AlphaMaps[L][Y*64+X]);});
        const double Packed=Sample(UV.X,UV.Y,1024,[&](int32 X,int32 Y){const auto P=Atlas[Y*1024+X]; return double(L==0?P.R:L==1?P.G:P.B);});
        MaxError=FMath::Max(MaxError,FMath::Abs(Original-Packed));
    }
    TestTrue(TEXT("Bilinear alpha matches independent old clamp sampler at edges/corners and in all RGB layers"),MaxError<.01);
    Data->Chunks[0].Holes=0x1021;
    const auto Mesh=FTerrainMeshBuilder::BuildChunkMesh(Data->Chunks[0],32,48);
    TestEqual(TEXT("Original hole topology is retained"),Mesh.Indices.Num(),(64-3*4)*12);
    TestEqual(TEXT("Original tiled UVs are not packed/rescaled"),Mesh.TiledUVs[8],FVector2D(8,0));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowTerrainBatchReal,"WowUnreal.Rendering.TerrainBatchRealAdt",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowTerrainBatchReal::RunTest(const FString&)
{
    FString Path; if(!FParse::Value(FCommandLine::Get(),TEXT("wowdata="),Path)) return false;
    FMpqManager Mpq; if(!Mpq.Initialize(Path)) return false; TArray<uint8> Bytes;
    if(!Mpq.ReadFile(TEXT("World\\Maps\\Azeroth\\Azeroth_32_48.adt"),Bytes)) return false;
    const auto Data=MakeUnique<FAdtData>(FAdtParser::Parse(Bytes,false)); if(!Data->bIsValid) return false;
    const FWowTerrainBatch Batch(*Data); const auto Atlas=FWowTerrainBatch::AlphaPixels(*Data);
    TestTrue(TEXT("Real Abbey texture sets reduce sections without replacing original textures"),Batch.Representatives.Num()>1 && Batch.Representatives.Num()<128);
    for(int32 I=0;I<256;++I)
    {
        const auto& C=Data->Chunks[I]; if(C.Holes==0xffff) continue;
        TestTrue(TEXT("Every visible chunk has a matching ordered material set"),Batch.SectionForChunk[I]>=0 && C.TextureIndices==Data->Chunks[Batch.Representatives[Batch.SectionForChunk[I]]].TextureIndices);
        for(int32 L=0;L<FMath::Min(3,C.AlphaMaps.Num());++L) for(int32 Y=0;Y<64;++Y) for(int32 X=0;X<64;++X)
        {
            const auto P=Atlas[(C.IndexY*64+Y)*1024+C.IndexX*64+X]; const uint8 Value=L==0?P.R:L==1?P.G:P.B;
            if(C.AlphaMaps[L].IsValidIndex(Y*64+X) && Value!=C.AlphaMaps[L][Y*64+X]) {AddError(TEXT("Source alpha bytes changed")); return false;}
        }
    }
    AddInfo(FString::Printf(TEXT("Abbey tile: %d original texture-set sections"),Batch.Representatives.Num())); return true;
}
#endif
