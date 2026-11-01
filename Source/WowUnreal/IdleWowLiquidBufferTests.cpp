#include "Misc/AutomationTest.h"
#include "WowRuntimeStaticMesh.h"
#include "StaticMeshResources.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowLiquidBuffers,"WowUnreal.Rendering.LiquidBuffersAndDepth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FIdleWowLiquidBuffers::RunTest(const FString&)
{
    const TArray<FVector> Positions={{0,0,7},{100,0,7},{0,100,7}},Normals={{0,0,1},{0,0,1},{0,0,1}};
    const TArray<FVector2D> UVs={{0,0},{1,0},{0,1}};
    const TArray<int32> Indices={0,1,2}; const TArray<FVector4f> Colors={FVector4f(1,1,1,0),FVector4f(1,1,1,.25f),FVector4f(1,1,1,1)};
    auto Data=FWowRuntimeStaticMesh::PrepareSurface(Positions,Normals,UVs,Indices,&Colors); const auto& L=Data->LODResources[0];
    TestTrue(TEXT("Liquid keeps authored transparency rather than losing the color stream"),L.bHasColorVertexData && L.VertexBuffers.ColorVertexBuffer.GetNumVertices()==3);
    for(int32 I=0;I<3;++I)
    {
        TestTrue(TEXT("Liquid positions, UVs, original +X tangent and upward normal survive"),L.VertexBuffers.PositionVertexBuffer.VertexPosition(I)==FVector3f(Positions[I]) && L.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(I,0)==FVector2f(UVs[I]) && FVector3f(L.VertexBuffers.StaticMeshVertexBuffer.VertexTangentX(I)).Equals(FVector3f(1,0,0),.01f) && FVector3f(L.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(I)).Equals(FVector3f(0,0,1),.01f));
        const auto C=L.VertexBuffers.ColorVertexBuffer.VertexColor(I); TestTrue(TEXT("Water RGB remains white and source depth alpha is retained"),C.R==255 && C.G==255 && C.B==255 && C.A==(I==0?0:I==1?64:255));
        TestEqual(TEXT("Original triangle winding survives"),int32(L.IndexBuffer.GetIndex(I)),I);
    }
    auto Wdl=FWowRuntimeStaticMesh::PrepareSurface(Positions,Normals,UVs,Indices);
    TestFalse(TEXT("Uncoloured WDL retains its original no-colour-stream path"),Wdl->LODResources[0].bHasColorVertexData);
    return true;
}
#endif
