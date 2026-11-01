#include "Misc/AutomationTest.h"
#include "WowRuntimeStaticMesh.h"
#include "WowRuntimeMeshMaterials.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowObjectBuffers,"WowUnreal.Rendering.ObjectBuffersMatchEngineBuilder",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowObjectBuffers::RunTest(const FString&)
{
    FMeshDescription Mesh; FStaticMeshAttributes A(Mesh); A.Register(); const auto Group=Mesh.CreatePolygonGroup();
    TArray<FVertexInstanceID> Triangle;
    for(int32 I=0;I<3;++I)
    {
        const auto V=Mesh.CreateVertex();
        A.GetVertexPositions()[V]=FVector3f(I==1?100:0,I==2?100:0,10);
        const auto VI=Mesh.CreateVertexInstance(V); Triangle.Add(VI);
        A.GetVertexInstanceNormals()[VI]=FVector3f(0,0,1); A.GetVertexInstanceTangents()[VI]=FVector3f(1,0,0); A.GetVertexInstanceBinormalSigns()[VI]=1;
        A.GetVertexInstanceUVs().Set(VI,0,FVector2f(I*.25f,.75f)); A.GetVertexInstanceColors()[VI]=FVector4f(.1f+I*.2f,.5f,1,.25f);
    }
    Mesh.CreatePolygon(Group,Triangle);
    auto* Original=NewObject<UStaticMesh>(); Original->bAllowCPUAccess=true; Original->CreateBodySetup();
    FWowRuntimeMeshMaterials::AssignSingle(Original,Mesh,UMaterial::GetDefaultMaterial(MD_Surface));
    UStaticMesh::FBuildMeshDescriptionsParams P; P.bBuildSimpleCollision=false; P.bFastBuild=true;
    Original->BuildFromMeshDescriptions(TArray<const FMeshDescription*>{&Mesh},P);
    auto Prepared=FWowRuntimeStaticMesh::Prepare(Mesh,true,true);
    const auto& Old=Original->GetRenderData()->LODResources[0]; const auto& New=Prepared->LODResources[0];
    TestTrue(TEXT("WMO/M2 retain actual section shadow flags, not just component flags"),New.Sections.Num()==1 && New.Sections[0].bCastShadow);
    TestEqual(TEXT("Original material section indices survive"),New.Sections[0].MaterialIndex,Old.Sections[0].MaterialIndex);
    for(int32 I=0;I<3;++I)
    {
        TestEqual(TEXT("Prepared authored colours match Unreal's previous mesh builder byte-for-byte"),New.VertexBuffers.ColorVertexBuffer.VertexColor(I),Old.VertexBuffers.ColorVertexBuffer.VertexColor(I));
        TestTrue(TEXT("Original object positions and UVs survive"),New.VertexBuffers.PositionVertexBuffer.VertexPosition(I)==Old.VertexBuffers.PositionVertexBuffer.VertexPosition(I) && New.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I,0)==Old.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I,0));
    }
    return true;
}
#endif
