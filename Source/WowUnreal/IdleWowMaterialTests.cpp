#include "Misc/AutomationTest.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WowRuntimeMeshMaterials.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowRuntimeMeshSections, "WowUnreal.Rendering.RuntimeMeshMaterialSections",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowRuntimeMeshSections::RunTest(const FString& Parameters)
{
    FMeshDescription Description;
    FStaticMeshAttributes Attributes(Description);
    Attributes.Register();
    auto Positions = Attributes.GetVertexPositions();
    for (int32 Section = 0; Section < 2; ++Section)
    {
        const FPolygonGroupID Group = Description.CreatePolygonGroup();
        TArray<FVertexInstanceID> Triangle;
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            const FVertexID Vertex = Description.CreateVertex();
            Positions[Vertex] = FVector3f(Section * 200.0f + (Corner == 1 ? 100.0f : 0.0f), Corner == 2 ? 100.0f : 0.0f, 0.0f);
            Triangle.Add(Description.CreateVertexInstance(Vertex));
        }
        Description.CreatePolygon(Group, Triangle);
    }
    UStaticMesh* Mesh = NewObject<UStaticMesh>();
    TArray<UMaterialInterface*> Materials;
    for (int32 Section = 0; Section < 2; ++Section)
    {
        auto* Material = UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), Mesh);
        Materials.Add(Material);
    }
    FWowRuntimeMeshMaterials::Assign(Mesh, Description, Materials);
    for (const FPolygonGroupID Group : Description.PolygonGroups().GetElementIDs())
    {
        TestFalse(TEXT("Explicit slot survives cooked builds; editor inference is insufficient"),
            Attributes.GetPolygonGroupMaterialSlotNames()[Group].IsNone());
    }
    UStaticMesh::FBuildMeshDescriptionsParams Build;
    Build.bFastBuild = true;
    Build.bBuildSimpleCollision = false;
    TArray<const FMeshDescription*> Lods = {&Description};
    Mesh->BuildFromMeshDescriptions(Lods, Build);
    const FStaticMeshRenderData* Render = Mesh->GetRenderData();
    if (!TestNotNull(TEXT("Built render data"), Render)) return false;
    const auto& Sections = Render->LODResources[0].Sections;
    TestEqual(TEXT("Both batches survive building"), Sections.Num(), 2);
    for (int32 Index = 0; Index < Sections.Num(); ++Index)
    {
        AddInfo(FString::Printf(TEXT("Section %d: material index %d, slot name %s"), Index,
            Sections[Index].MaterialIndex, *Mesh->GetStaticMaterials()[Index].MaterialSlotName.ToString()));
        TestEqual(TEXT("Render section uses its own material slot"), Sections[Index].MaterialIndex, Index);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdleWowCookedWorldMaterials, "WowUnreal.Rendering.CookedWorldMaterialAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIdleWowCookedWorldMaterials::RunTest(const FString& Parameters)
{
    struct FExpected { const TCHAR* Name; EBlendMode Blend; EMaterialShadingModel Shading; };
    const FExpected Expected[] = {
        {TEXT("M_WowSky"), BLEND_Opaque, MSM_Unlit},
        {TEXT("M_WowCloud"), BLEND_Translucent, MSM_Unlit},
        {TEXT("M_WowCelestialDisc"), BLEND_Additive, MSM_Unlit},
        {TEXT("M_WowDistantTerrain"), BLEND_Opaque, MSM_DefaultLit},
        {TEXT("M_WowWater"), BLEND_Masked, MSM_DefaultLit},
        {TEXT("M_WowLava"), BLEND_Opaque, MSM_DefaultLit},
        {TEXT("M_WowSlime"), BLEND_Opaque, MSM_DefaultLit},
    };
    for (const FExpected& Item : Expected)
    {
        const FString Path = FString(TEXT("/Game/Materials/")) + Item.Name;
        UMaterial* Material = LoadObject<UMaterial>(nullptr, *Path);
        if (!TestNotNull(*Path, Material)) continue;
        TestTrue(TEXT("Correct cooked blend mode; no default opaque sky or water"), Material->BlendMode == Item.Blend);
        TestTrue(TEXT("Correct lighting model"), Material->GetShadingModels().HasShadingModel(Item.Shading));
    }
    UMaterial* Sky = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_WowSky"));
    UMaterial* Distant = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_WowDistantTerrain"));
    FLinearColor Color;
    if (Sky) TestTrue(TEXT("Sky exposes the gradient parameter used by the manager"),
        Sky->GetVectorParameterValue(FMaterialParameterInfo(TEXT("SkyTopColor")), Color));
    if (Distant) TestTrue(TEXT("Distant terrain exposes its real color parameter"),
        Distant->GetVectorParameterValue(FMaterialParameterInfo(TEXT("BaseColor")), Color));
    return true;
}
#endif
