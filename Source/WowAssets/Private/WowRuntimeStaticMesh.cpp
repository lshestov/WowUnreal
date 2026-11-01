#include "WowRuntimeStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "Components.h"
#include "StaticMeshResources.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"

TUniquePtr<FStaticMeshRenderData> FWowRuntimeStaticMesh::Prepare(const FMeshDescription& Description,bool VertexColors,bool CastShadow)
{
    auto Data=MakeUnique<FStaticMeshRenderData>(); Data->AllocateLODResources(1);
    auto& Lod=Data->LODResources[0]; FStaticMeshConstAttributes Attributes(Description);
    const auto Positions=Attributes.GetVertexPositions(); const auto Normals=Attributes.GetVertexInstanceNormals();
    const auto Tangents=Attributes.GetVertexInstanceTangents(); const auto Signs=Attributes.GetVertexInstanceBinormalSigns(); const auto UVs=Attributes.GetVertexInstanceUVs();
    const auto Colors=Attributes.GetVertexInstanceColors();
    TArray<FStaticMeshBuildVertex> Vertices; Vertices.Reserve(Description.VertexInstances().Num());
    TMap<FVertexInstanceID,uint32> Lookup; Lookup.Reserve(Description.VertexInstances().Num()); FBox Bounds(ForceInit);
    for(const auto Instance:Description.VertexInstances().GetElementIDs())
    {
        FStaticMeshBuildVertex V{}; V.Position=Positions[Description.GetVertexInstanceVertex(Instance)];
        V.TangentX=Tangents[Instance]; V.TangentZ=Normals[Instance]; V.TangentY=FVector3f::CrossProduct(V.TangentZ,V.TangentX)*Signs[Instance]; V.Color=FColor::White;
        if(VertexColors){const auto C=Colors[Instance]; V.Color=FLinearColor(C.X,C.Y,C.Z,C.W).ToFColor(true);}
        for(int32 UV=0;UV<UVs.GetNumChannels();++UV) V.UVs[UV]=UVs.Get(Instance,UV);
        Bounds+=FVector(V.Position); Lookup.Add(Instance,Vertices.Add(V));
    }
    TArray<uint32> Indices; Indices.Reserve(Description.Triangles().Num()*3); int32 Material=0;
    for(const auto Group:Description.PolygonGroups().GetElementIDs())
    {
        FStaticMeshSection Section; Section.MaterialIndex=Material++; Section.FirstIndex=Indices.Num(); Section.MinVertexIndex=MAX_uint32; Section.MaxVertexIndex=0;
        Section.bEnableCollision=false; Section.bCastShadow=CastShadow;
        for(const auto Triangle:Description.GetPolygonGroupTriangles(Group)) for(const auto Instance:Description.GetTriangleVertexInstances(Triangle))
        {
            const uint32 Index=Lookup.FindChecked(Instance); Indices.Add(Index);
            Section.MinVertexIndex=FMath::Min(Section.MinVertexIndex,Index); Section.MaxVertexIndex=FMath::Max(Section.MaxVertexIndex,Index);
        }
        Section.NumTriangles=(Indices.Num()-Section.FirstIndex)/3; if(!Section.NumTriangles) Section.MinVertexIndex=0;
        Lod.Sections.Add(Section);
    }
    Lod.VertexBuffers.PositionVertexBuffer.Init(Vertices,false);
    Lod.VertexBuffers.StaticMeshVertexBuffer.Init(Vertices,UVs.GetNumChannels(),false);
    Lod.IndexBuffer.SetIndices(Indices,EIndexBufferStride::AutoDetect);
    if(VertexColors) Lod.VertexBuffers.ColorVertexBuffer.Init(Vertices);
    Lod.MaxDeviation=0; Lod.bBuffersInlined=true; Lod.bIsOptionalLOD=false; Lod.bHasColorVertexData=VertexColors;
    Data->Bounds=FBoxSphereBounds(Bounds); Lod.SourceMeshBounds=Data->Bounds;
    Data->ScreenSize[0].Default=1; Data->NumInlinedLODs=1; Data->CurrentFirstLODIdx=0;
    return Data;
}
TUniquePtr<FStaticMeshRenderData> FWowRuntimeStaticMesh::PrepareSurface(const TArray<FVector>& Positions,const TArray<FVector>& Normals,const TArray<FVector2D>& UVs,const TArray<int32>& SourceIndices,const TArray<FVector4f>* LiquidColors)
{
    auto Data=MakeUnique<FStaticMeshRenderData>(); Data->AllocateLODResources(1); auto& Lod=Data->LODResources[0];
    TArray<FStaticMeshBuildVertex> Vertices; Vertices.SetNum(Positions.Num()); FBox Bounds(ForceInit);
    for(int32 I=0;I<Positions.Num();++I)
    {
        auto& V=Vertices[I]; V.Position=FVector3f(Positions[I]); Bounds+=FVector(V.Position);
        V.TangentZ=Normals.IsValidIndex(I)?FVector3f(Normals[I]):FVector3f(0,0,1); V.TangentZ.Normalize();
        V.TangentX=FVector3f::CrossProduct(V.TangentZ,FVector3f(0,1,0)); if(V.TangentX.SizeSquared()<.001f) V.TangentX=FVector3f::CrossProduct(V.TangentZ,FVector3f(1,0,0)); V.TangentX.Normalize();
        V.TangentY=FVector3f::CrossProduct(V.TangentZ,V.TangentX); V.UVs[0]=UVs.IsValidIndex(I)?FVector2f(UVs[I]):FVector2f::ZeroVector; V.Color=FColor::White;
        if(LiquidColors)
        {
            V.TangentX=FVector3f(1,0,0); V.TangentY=FVector3f(0,1,0);
            const auto C=LiquidColors->IsValidIndex(I)?(*LiquidColors)[I]:FVector4f(1,1,1,.7f);
            V.Color=FLinearColor(C.X,C.Y,C.Z,C.W).ToFColor(true);
        }
    }
    TArray<uint32> Indices; Indices.Reserve(SourceIndices.Num()); for(int32 Index:SourceIndices) Indices.Add(Index);
    FStaticMeshSection Section; Section.NumTriangles=Indices.Num()/3; Section.MaxVertexIndex=FMath::Max(0,Vertices.Num()-1); Section.bCastShadow=false; Lod.Sections.Add(Section);
    Lod.VertexBuffers.PositionVertexBuffer.Init(Vertices,false); Lod.VertexBuffers.StaticMeshVertexBuffer.Init(Vertices,1,false); Lod.IndexBuffer.SetIndices(Indices,EIndexBufferStride::AutoDetect);
    if(LiquidColors) Lod.VertexBuffers.ColorVertexBuffer.Init(Vertices);
    Lod.MaxDeviation=0; Lod.bBuffersInlined=true; Lod.bIsOptionalLOD=false; Lod.bHasColorVertexData=LiquidColors!=nullptr;
    Data->Bounds=FBoxSphereBounds(Bounds); Lod.SourceMeshBounds=Data->Bounds; Data->ScreenSize[0].Default=1; Data->NumInlinedLODs=1; Data->CurrentFirstLODIdx=0;
    return Data;
}
void FWowRuntimeStaticMesh::Initialize(UStaticMesh* Mesh,TUniquePtr<FStaticMeshRenderData> Data,const TArray<UMaterialInterface*>& Materials)
{
    check(IsInGameThread()); check(Mesh && Data);
    auto& Slots=Mesh->GetStaticMaterials(); Slots.Reset();
    for(int32 I=0;I<Materials.Num();++I)
    {
        const FName Name(*FString::Printf(TEXT("WowMaterial_%d"),I)); FStaticMaterial Binding(Materials[I],Name);
#if WITH_EDITORONLY_DATA
        Binding.ImportedMaterialSlotName=Name;
#endif
        Binding.UVChannelData.bInitialized=true; Slots.Add(Binding);
    }
    Mesh->bAllowCPUAccess=false; Mesh->NeverStream=true;
    Mesh->CreateBodySetup(); Mesh->GetBodySetup()->bNeverNeedsCookedCollisionData=true;
    Mesh->SetRenderData(MoveTemp(Data)); Mesh->CalculateExtendedBounds(); Mesh->InitResources();
}
