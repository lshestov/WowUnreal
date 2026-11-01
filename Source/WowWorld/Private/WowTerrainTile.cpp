#include "WowTerrainTile.h"
#include "WowStreamingProfile.h"
#include "UObject/StrongObjectPtr.h"
#include "WowRuntimeMeshMaterials.h"
#include "WowRuntimeStaticMesh.h"
#include "WowAssetCache.h"
#include "WowTerrainMeshBuilder.h"
#include "WowTerrainMaterial.h"
#include "WowTerrainBatch.h"
#include "WowTerrainGeometry.h"
#include "Formats/AdtTypes.h"
#include "Coord/WowCoordinate.h"
#include "WowDoodadManager.h"
#include "WowWmoRenderer.h"
#include "WowWaterRenderer.h"
#include "VT/RuntimeVirtualTexture.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

DEFINE_LOG_CATEGORY_STATIC(LogTerrainTile, Log, All);

AWowTerrainTile::AWowTerrainTile()
{
    PrimaryActorTick.bCanEverTick = false;
    RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(RootScene);
}

UMaterialInterface* AWowTerrainTile::GetDefaultTerrainMaterial() const
{
    static TStrongObjectPtr<UMaterial> DefaultMat;
    if (!DefaultMat)
    {
        DefaultMat.Reset(LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial")));
    }
    return DefaultMat.Get();
}

void AWowTerrainTile::BuildFromAdtData(const FAdtData& Data, int32 TX, int32 TY, FMpqManager* Mpq, FWowAssetCache* Cache, TSet<uint32>* SpawnedWmoIds,TSharedPtr<FWowTerrainGeometry> Prepared,const FString& CacheKey)
{
    FWowStreamingProfile Profile(TEXT("terrainBuild"));
    TileCoord = FIntPoint(TX, TY);
    if (!Data.bIsValid) return;

    FVector TileCenter = FWowCoordinate::TileToWorld(TX, TY);
    SetActorLocation(TileCenter);
    RootScene->SetMobility(EComponentMobility::Static);

    UE_LOG(LogTerrainTile, Log, TEXT("Tile %d,%d at %s: %d textures, %d doodads, %d WMOs"),
        TX, TY, *TileCenter.ToString(), Data.TexturePaths.Num(), Data.DoodadPlacements.Num(), Data.WmoPlacements.Num());

    UMaterialInterface* DefaultMat = GetDefaultTerrainMaterial();

    auto Geometry=Prepared?MoveTemp(Prepared):MakeShared<FWowTerrainGeometry>(Data,TX,TY);
    UStaticMesh* SM=Cache && !CacheKey.IsEmpty()?Cache->FindMesh(CacheKey):nullptr;
    MeshReused=SM!=nullptr; if(!SM) SM=NewObject<UStaticMesh>();
    const auto& CollisionVertices=Geometry->CollisionVertices;
    const auto& CollisionIndices=Geometry->CollisionIndices;
    TArray<UMaterialInterface*> SectionMaterials;
    if(!MeshReused)
    {
        auto* AlphaAtlas=FWowTerrainMaterial::CreateAlphaAtlas(Data,&Geometry->AlphaPixels);
        for(int32 Representative:Geometry->Representatives)
        {
            auto* Material=FWowTerrainMaterial::CreateBatchMaterial(Data.Chunks[Representative],Data,AlphaAtlas,Mpq,Cache,SM);
            SectionMaterials.Add(Material?static_cast<UMaterialInterface*>(Material):DefaultMat);
        }
        if(Geometry->Chunks>0)
        {
            FWowRuntimeStaticMesh::Initialize(SM,MoveTemp(Geometry->RenderData),SectionMaterials);
            if(Cache && !CacheKey.IsEmpty()) Cache->CacheMesh(CacheKey,SM);
        }
    }
    else for(const auto& Material:SM->GetStaticMaterials()) SectionMaterials.Add(Material.MaterialInterface);

    if(Geometry->Chunks>0)
    {
        // ProceduralMeshComponent cooks triangle collision in packaged games;
        // transient UStaticMesh imported collision is only reliable in Editor.
        auto* GroundCollision = NewObject<UProceduralMeshComponent>(this, TEXT("TerrainCollision"));
        GroundCollision->SetupAttachment(RootScene);
        GroundCollision->bUseAsyncCooking = true;
        GroundCollision->bUseComplexAsSimpleCollision = true;
        GroundCollision->SetCollisionObjectType(ECC_WorldStatic);
        GroundCollision->SetCollisionResponseToAllChannels(ECR_Block);
        GroundCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        GroundCollision->SetVisibility(false);
        GroundCollision->SetHiddenInGame(true);
        GroundCollision->CreateMeshSection(0, CollisionVertices, CollisionIndices,
            TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), true);
        GroundCollision->RegisterComponent();

        // Create UStaticMeshComponent and set materials on the component
        UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this, TEXT("TerrainMesh"));
        MeshComp->SetStaticMesh(SM);
        MeshComp->SetupAttachment(RootScene);
        MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
        for (int32 s = 0; s < SectionMaterials.Num(); ++s)
        {
            MeshComp->SetMaterial(s, SectionMaterials[s]);
        }
        MeshComp->RegisterComponent();
        MeshComp->RecreatePhysicsState();
        MeshComp->SetCastShadow(false);
        MeshComp->SetCollisionObjectType(ECC_WorldStatic);

        ChunkMeshes.Add(MeshComp);
        UE_LOG(LogTerrainTile, Log, TEXT("Tile %d,%d: %d sections, %d textured, %d/%d have heights, %d skipped"),
            TX, TY, SectionMaterials.Num(), Geometry->TexturedChunks, Geometry->HeightChunks, Geometry->Chunks, 256-Geometry->Chunks);
    }

    // Spawn water meshes from MH2O data
    {
        TArray<UStaticMeshComponent*> WaterComps = FWowWaterRenderer::CreateWaterMeshes(this, Data, TX, TY);
        for (UStaticMeshComponent* WC : WaterComps)
        {
            WaterMeshes.Add(WC);
        }

        // Check for ocean liquid type and spawn ocean plane if found
        float OceanHeight = 0.0f;
        bool bHasOcean = false;
        for (int32 i = 0; i < 256; i++)
        {
            for (const FMH2OInstance& Layer : Data.WaterChunks[i].Layers)
            {
                if (Layer.GetLiquidCategory() == 1) // ocean
                {
                    OceanHeight = Layer.MinHeight;
                    bHasOcean = true;
                    break;
                }
            }
            if (bHasOcean) break;
        }
        if (bHasOcean)
        {
            UStaticMeshComponent* OceanComp = FWowWaterRenderer::CreateOceanPlane(this, OceanHeight, TX, TY);
            if (OceanComp)
            {
                WaterMeshes.Add(OceanComp);
            }
        }
    }

    // Preserve every placement, but build only one model group per streaming job.
    DoodadPlacements=Data.DoodadPlacements; DoodadPaths=Data.DoodadPaths;
    bUsesInstancedDoodads=!DoodadPlacements.IsEmpty();
    TMap<FString,TArray<FAdtDoodadPlacement>> ByModel;
    for(const auto& P:DoodadPlacements) if(DoodadPaths.IsValidIndex(P.NameIndex) && !DoodadPaths[P.NameIndex].IsEmpty()) ByModel.FindOrAdd(DoodadPaths[P.NameIndex]).Add(P);
    for(auto& Pair:ByModel) PendingDoodads.Add(MoveTemp(Pair.Value));

    // Store WMO placement data for distance-based streaming (doodads are now instanced)
    WmoPlacements = Data.WmoPlacements;
    WmoPaths = Data.WmoPaths;
    CachedMpq = Mpq;
    CachedCache = Cache;

    // Store area IDs per chunk for zone detection
    ChunkAreaIds.SetNum(256);
    for (int32 i = 0; i < 256; ++i)
    {
        ChunkAreaIds[i] = Data.Chunks[i].AreaId;
    }

    UE_LOG(LogTerrainTile, Log, TEXT("Tile %d,%d: %d instanced doodad HISMCs, %d WMO placements for streaming"),
        TX, TY, InstancedDoodads.Num(), WmoPlacements.Num());
}

void AWowTerrainTile::ApplyRuntimeVirtualTexture(URuntimeVirtualTexture* RVT)
{
    if (!RVT) return;

    // Configure all terrain chunk meshes to render into the RVT
    for (UStaticMeshComponent* Mesh : ChunkMeshes)
    {
        if (Mesh)
        {
            Mesh->RuntimeVirtualTextures.AddUnique(RVT);
            Mesh->VirtualTextureLodBias = 0;
            Mesh->VirtualTextureCullMips = 0;
            Mesh->VirtualTextureMinCoverage = 1;
            Mesh->VirtualTextureRenderPassType = ERuntimeVirtualTextureMainPassType::Always;
            Mesh->MarkRenderStateDirty();
        }
    }

    UE_LOG(LogTerrainTile, Log, TEXT("Tile %d,%d: Applied RVT to %d chunk meshes"),
        TileCoord.X, TileCoord.Y, ChunkMeshes.Num());
}

bool AWowTerrainTile::StreamNextDoodad()
{
    if(PendingDoodads.IsEmpty() || !CachedMpq || !CachedCache) return false;
    auto Placements=MoveTemp(PendingDoodads.Last()); PendingDoodads.Pop(EAllowShrinking::No);
    for(auto* Component:FWowDoodadManager::SpawnDoodadsInstanced(this,Placements,DoodadPaths,CachedMpq,CachedCache)) InstancedDoodads.Add(Component);
    return true;
}
