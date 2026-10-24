#include "WowRuntimeMeshMaterials.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"

void FWowRuntimeMeshMaterials::Assign(UStaticMesh* Mesh, FMeshDescription& Description,
    const TArray<UMaterialInterface*>& Materials)
{
    FStaticMeshAttributes Attributes(Description);
    auto SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
    auto& Slots = Mesh->GetStaticMaterials();
    Slots.Reset();
    int32 MaterialIndex = 0;
    for (const FPolygonGroupID Group : Description.PolygonGroups().GetElementIDs())
    {
        const FName SlotName(*FString::Printf(TEXT("WowMaterial_%d"), MaterialIndex));
        SlotNames[Group] = SlotName;
        UMaterialInterface* Material = Materials.IsValidIndex(MaterialIndex) ? Materials[MaterialIndex] : nullptr;
        FStaticMaterial Binding(Material, SlotName);
#if WITH_EDITORONLY_DATA
        Binding.ImportedMaterialSlotName = SlotName;
#endif
        Slots.Add(Binding);
        Slots.Last().UVChannelData.bInitialized = true;
        ++MaterialIndex;
    }
}

void FWowRuntimeMeshMaterials::AssignSingle(UStaticMesh* Mesh, FMeshDescription& Description, UMaterialInterface* Material)
{
    Assign(Mesh, Description, TArray<UMaterialInterface*>{Material});
}
