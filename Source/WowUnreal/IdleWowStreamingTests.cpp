#include "Misc/AutomationTest.h"
#include "WowTerrainGeometry.h"
#include "WowTerrainMeshBuilder.h"
#include "Formats/AdtTypes.h"
#include "StaticMeshAttributes.h"
#include "Async/Async.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowWorkerTerrain,"WowUnreal.World.WorkerTerrainGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowWorkerTerrain::RunTest(const FString&)
{
    auto Data=MakeShared<FAdtData>(); Data->bIsValid=true;
    for(int32 I=0;I<256;++I)
    {
        auto& C=Data->Chunks[I]; C.IndexX=I%16; C.IndexY=I/16; C.WorldZ=30+I*.1f; C.TextureIndices={I%4,4,5};
        for(int32 V=0;V<145;++V){C.Heights[V]=FMath::Sin(float(V)*.07f); C.Normals[V]=FVector(.1,1,.2).GetSafeNormal();}
    }
    Data->Chunks[0].Holes=0x1021; Data->Chunks[255].Holes=0xffff;
    auto Future=Async(EAsyncExecution::ThreadPool,[Data](){return MakeShared<FWowTerrainGeometry>(*Data,32,48);});
    auto Geometry=Future.Get(); FStaticMeshAttributes Attributes(Geometry->Mesh);
    const auto Positions=Attributes.GetVertexPositions(); const auto UVs=Attributes.GetVertexInstanceUVs();
    TestEqual(TEXT("All visible chunks survive worker preparation"),Geometry->Chunks,255);
    int32 Vertex=0,Triangles=0;
    for(int32 I=0;I<256;++I)
    {
        const auto Original=FTerrainMeshBuilder::BuildChunkMesh(Data->Chunks[I],32,48); if(Original.Indices.IsEmpty()) continue;
        Triangles+=Original.Indices.Num()/3;
        for(int32 V=0;V<Original.Vertices.Num();++V,++Vertex)
        {
            if(Positions[FVertexID(Vertex)]!=FVector3f(Original.Vertices[V]) || UVs.Get(FVertexInstanceID(Vertex),0)!=FVector2f(Original.UVs[V]) || UVs.Get(FVertexInstanceID(Vertex),1)!=FVector2f(Original.TiledUVs[V]) || UVs.Get(FVertexInstanceID(Vertex),2)!=FVector2f(Data->Chunks[I].IndexX,Data->Chunks[I].IndexY))
            {AddError(TEXT("Worker changed terrain position or alpha/tiled/cell UVs")); return false;}
        }
    }
    TestEqual(TEXT("Original triangle/hole topology survives the handoff"),Geometry->Mesh.Triangles().Num(),Triangles);
    TestEqual(TEXT("Independent runtime collision has identical triangle count"),Geometry->CollisionIndices.Num(),Triangles*3);
    auto Lod=Async(EAsyncExecution::ThreadPool,[Data](){return MakeShared<FWowTerrainGeometry>(*Data,32,48,true);}).Get();
    TestTrue(TEXT("LOD worker does not allocate full-detail alpha textures or collision"),Lod->AlphaPixels.IsEmpty() && Lod->CollisionVertices.IsEmpty() && Lod->Chunks==255);
    return true;
}
#endif
