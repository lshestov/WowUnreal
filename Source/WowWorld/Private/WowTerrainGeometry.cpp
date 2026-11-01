#include "WowTerrainGeometry.h"
#include "WowTerrainBatch.h"
#include "WowTerrainMeshBuilder.h"
#include "WowStreamingProfile.h"
#include "Formats/AdtTypes.h"
#include "StaticMeshAttributes.h"
#include "WowRuntimeStaticMesh.h"
#include "StaticMeshResources.h"
#include "MeshDescription.h"

FWowTerrainGeometry::~FWowTerrainGeometry()=default;

FWowTerrainGeometry::FWowTerrainGeometry(const FAdtData& Data,int32 TX,int32 TY,bool Lod1)
{
    FWowStreamingProfile Profile(Lod1?TEXT("lod1PrepareWorker"):TEXT("terrainPrepareWorker"));
    // The temporary topology is also destroyed on this worker. Retaining it
    // until upload would move its substantial destruction cost back to the GT.
    FMeshDescription Mesh;
    TArray<int32> Sections;
    if(Lod1)
    {
        Sections.Init(INDEX_NONE,256); TMap<int32,int32> ByTexture;
        for(int32 I=0;I<256;++I)
        {
            const auto& C=Data.Chunks[I]; if(C.Holes==0xffff) continue;
            const int32 Texture=C.TextureIndices.IsEmpty()?INDEX_NONE:C.TextureIndices[0];
            int32* Section=ByTexture.Find(Texture);
            if(!Section){ByTexture.Add(Texture,Representatives.Add(I)); Section=ByTexture.Find(Texture);}
            Sections[I]=*Section;
        }
    }
    else
    {
        FWowTerrainBatch Batch(Data); Sections=MoveTemp(Batch.SectionForChunk); Representatives=MoveTemp(Batch.Representatives);
        AlphaPixels=FWowTerrainBatch::AlphaPixels(Data);
    }
    Mesh.SetNumUVChannels(Lod1?2:3); FStaticMeshAttributes Attributes(Mesh); Attributes.Register();
    auto Positions=Attributes.GetVertexPositions(); auto Normals=Attributes.GetVertexInstanceNormals();
    auto Tangents=Attributes.GetVertexInstanceTangents(); auto Signs=Attributes.GetVertexInstanceBinormalSigns(); auto UVs=Attributes.GetVertexInstanceUVs();
    TArray<FPolygonGroupID> Groups; for(int32 I:Representatives) Groups.Add(Mesh.CreatePolygonGroup());
    for(int32 I=0;I<256;++I)
    {
        const auto& C=Data.Chunks[I];
        auto G=Lod1?FTerrainMeshBuilder::BuildChunkMeshLOD1(C,TX,TY):FTerrainMeshBuilder::BuildChunkMesh(C,TX,TY);
        if(G.Vertices.IsEmpty() || G.Indices.IsEmpty() || Sections[I]<0) continue;
        ++Chunks; if(!C.TextureIndices.IsEmpty()) ++TexturedChunks;
        for(float H:C.Heights) if(H!=0){++HeightChunks; break;}
        if(!Lod1)
        {
            const int32 Base=CollisionVertices.Num(); CollisionVertices.Append(G.Vertices);
            for(int32 Index:G.Indices) CollisionIndices.Add(Base+Index);
        }
        TArray<FVertexInstanceID> Instances; Instances.SetNum(G.Vertices.Num());
        for(int32 V=0;V<G.Vertices.Num();++V)
        {
            const auto Vertex=Mesh.CreateVertex(); Positions[Vertex]=FVector3f(G.Vertices[V]);
            const auto Instance=Mesh.CreateVertexInstance(Vertex); Instances[V]=Instance;
            FVector3f N=G.Normals.IsValidIndex(V)?FVector3f(G.Normals[V]):FVector3f(0,0,1); N.Normalize(); Normals[Instance]=N;
            FVector3f T=FVector3f::CrossProduct(N,FVector3f(0,1,0)); if(T.SizeSquared()<.001f) T=FVector3f::CrossProduct(N,FVector3f(1,0,0)); T.Normalize();
            Tangents[Instance]=T; Signs[Instance]=1;
            UVs.Set(Instance,0,FVector2f(Lod1?G.TiledUVs[V]:G.UVs[V])); UVs.Set(Instance,1,FVector2f(G.TiledUVs[V]));
            if(!Lod1) UVs.Set(Instance,2,FVector2f(C.IndexX,C.IndexY));
        }
        for(int32 T=0;T<G.Indices.Num();T+=3)
        {
            TArray<FVertexInstanceID,TInlineAllocator<3>> Triangle={Instances[G.Indices[T]],Instances[G.Indices[T+1]],Instances[G.Indices[T+2]]};
            Mesh.CreatePolygon(Groups[Sections[I]],Triangle);
        }
    }
    RenderData=FWowRuntimeStaticMesh::Prepare(Mesh);
}
