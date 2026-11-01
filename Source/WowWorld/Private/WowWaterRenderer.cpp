#include "WowWaterRenderer.h"
#include "WowRuntimeStaticMesh.h"
#include "WowRuntimeMeshMaterials.h"
#include "WowWaterMaterial.h"
#include "Formats/AdtTypes.h"
#include "Coord/WowCoordinate.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Materials/Material.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowWater, Log, All);

// ── Helper: build a UStaticMesh from vertices/indices/UVs/colors ───────────────

static UStaticMesh* BuildLiquidStaticMesh(
	const TArray<FVector>& Vertices,
	const TArray<int32>& Indices,
	const TArray<FVector2D>& UVs,
	const TArray<FVector4f>& Colors,
	UMaterial* Material)
{
    // Preserve authored positions, triangles, UVs, +X tangent and depth alpha.
    // Build runtime buffers directly; the editor mesh builder also cooked an
    // unnecessary physics body and destroyed topology on the game thread.
    TArray<FVector> Normals; Normals.Init(FVector(0,0,1),Vertices.Num());
    UStaticMesh* SM=NewObject<UStaticMesh>();
    FWowRuntimeStaticMesh::Initialize(SM,FWowRuntimeStaticMesh::PrepareSurface(Vertices,Normals,UVs,Indices,&Colors),TArray<UMaterialInterface*>{Material});

	return SM;
}

// ── Create water meshes for all MH2O layers in an ADT ──────────────────────────

TArray<UStaticMeshComponent*> FWowWaterRenderer::CreateWaterMeshes(
	AActor* Owner, const FAdtData& AdtData, int32 TileX, int32 TileY)
{
	TArray<UStaticMeshComponent*> Result;
	if (!Owner) return Result;

	int32 WaterSectionIndex = 0;

	for (int32 ChunkIdx = 0; ChunkIdx < 256; ChunkIdx++)
	{
		const FMH2OChunkData& WaterChunk = AdtData.WaterChunks[ChunkIdx];
		if (!WaterChunk.HasWater()) continue;

		for (const FMH2OInstance& Layer : WaterChunk.Layers)
		{
			if (Layer.Width == 0 || Layer.Height == 0) continue;

			int32 ChunkX = ChunkIdx % 16;
			int32 ChunkY = ChunkIdx / 16;

			float ChunkNgX = TileX * FWowCoordinate::TILE_SIZE + ChunkX * FWowCoordinate::CHUNK_SIZE;
			float ChunkNgZ = TileY * FWowCoordinate::TILE_SIZE + ChunkY * FWowCoordinate::CHUNK_SIZE;
			float SubTileSize = FWowCoordinate::CHUNK_SIZE / 8.0f;

			int32 VW = Layer.Width + 1;
			int32 VH = Layer.Height + 1;

			TArray<FVector> Vertices;
			TArray<FVector2D> UVs;
			TArray<FVector4f> Colors;
			Vertices.Reserve(VW * VH);
			UVs.Reserve(VW * VH);
			Colors.Reserve(VW * VH);

			int32 Category = Layer.GetLiquidCategory();

			for (int32 Row = 0; Row <= Layer.Height; Row++)
			{
				for (int32 Col = 0; Col <= Layer.Width; Col++)
				{
					float NgX = ChunkNgX + (Layer.XOffset + Col) * SubTileSize;
					float NgZ = ChunkNgZ + (Layer.YOffset + Row) * SubTileSize;

					int32 HeightIdx = Row * VW + Col;
					float NgY = (HeightIdx < Layer.Heights.Num()) ? Layer.Heights[HeightIdx] : Layer.MinHeight;

					FVector UEPos = FWowCoordinate::AdtToUE(NgX, NgY, NgZ);
					Vertices.Add(UEPos);
					UVs.Add(FVector2D((float)Col / (float)Layer.Width, (float)Row / (float)Layer.Height));

					// Depth-based vertex color: alpha = opacity from depth map
					// Depth 0 = shallow (more transparent), 255 = deep (more opaque)
					float DepthAlpha = 0.7f; // default medium opacity
					if (HeightIdx < Layer.Depths.Num())
					{
						// MH2O depth is 0-255: 0 = surface/shallow, 255 = deep
						float NormDepth = Layer.Depths[HeightIdx] / 255.0f;
						// Map: shallow (0) → 0.3 opacity, deep (1) → 0.9 opacity
						DepthAlpha = FMath::Lerp(0.3f, 0.9f, NormDepth);
					}

					// For lava/slime, full opacity
					if (Category == 2 || Category == 3)
					{
						DepthAlpha = 1.0f;
					}

					// RGB carries liquid tint, Alpha carries depth opacity
					FVector4f VertColor;
					switch (Category)
					{
					case 0: // water
					case 1: // ocean
						VertColor = FVector4f(0.1f, 0.3f, 0.6f, DepthAlpha);
						break;
					case 2: // magma
						VertColor = FVector4f(1.0f, 0.4f, 0.1f, 1.0f);
						break;
					case 3: // slime
						VertColor = FVector4f(0.2f, 0.7f, 0.1f, 1.0f);
						break;
					default:
						VertColor = FVector4f(0.1f, 0.3f, 0.6f, DepthAlpha);
						break;
					}
					Colors.Add(VertColor);
				}
			}

			// Build triangles, skipping sub-tiles where liquid doesn't exist
			TArray<int32> Indices;
			for (int32 Row = 0; Row < Layer.Height; Row++)
			{
				for (int32 Col = 0; Col < Layer.Width; Col++)
				{
					int32 BitX = Layer.XOffset + Col;
					int32 BitY = Layer.YOffset + Row;
					if (BitX < 8 && BitY < 8)
					{
						uint64 Bit = 1ULL << (BitY * 8 + BitX);
						if ((Layer.ExistsBitmap & Bit) == 0) continue;
					}

					int32 TL = Row * VW + Col;
					int32 TR = TL + 1;
					int32 BL = TL + VW;
					int32 BR = BL + 1;

					Indices.Add(TL);
					Indices.Add(BL);
					Indices.Add(TR);

					Indices.Add(TR);
					Indices.Add(BL);
					Indices.Add(BR);
				}
			}

			if (Indices.Num() == 0) continue;

			// Get proper material for this liquid type
			UMaterial* LiquidMat = FWowWaterMaterial::GetMaterialForCategory(Category);

			UStaticMesh* SM = BuildLiquidStaticMesh(Vertices, Indices, UVs, Colors, LiquidMat);

			FString CompName = FString::Printf(TEXT("Water_%d"), WaterSectionIndex++);
			UStaticMeshComponent* WaterMeshComp = NewObject<UStaticMeshComponent>(Owner, *CompName);
			WaterMeshComp->SetStaticMesh(SM);
			WaterMeshComp->RegisterComponent();
			WaterMeshComp->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			WaterMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			WaterMeshComp->SetCastShadow(false);

			// Note: water uses masked blend (not translucent) to avoid Metal crash

			Result.Add(WaterMeshComp);
		}
	}

	if (Result.Num() > 0)
	{
		UE_LOG(LogWowWater, Log, TEXT("Created %d water meshes for tile %d,%d"), Result.Num(), TileX, TileY);
	}

	return Result;
}

// ── Ocean Plane ────────────────────────────────────────────────────────────────

UStaticMeshComponent* FWowWaterRenderer::CreateOceanPlane(
	AActor* Owner, float OceanHeight, int32 TileX, int32 TileY)
{
	if (!Owner) return nullptr;

	// Create a large flat plane centered on this tile, extending well beyond tile edges
	// for a seamless ocean. Each tile is TILE_SIZE in WoW coordinates.
	// The ocean plane extends 2 tiles in each direction for overlap.
	float TileSize = FWowCoordinate::TILE_SIZE;
	float Extent = TileSize * 2.0f; // extend 2 tiles outward

	float CenterNgX = TileX * TileSize + TileSize * 0.5f;
	float CenterNgZ = TileY * TileSize + TileSize * 0.5f;

	// Generate a grid of vertices for the ocean plane (low poly is fine)
	const int32 GridRes = 8; // 8x8 grid
	float Step = (Extent * 2.0f) / GridRes;

	TArray<FVector> Vertices;
	TArray<FVector2D> UVs;
	TArray<FVector4f> Colors;
	Vertices.Reserve((GridRes + 1) * (GridRes + 1));
	UVs.Reserve((GridRes + 1) * (GridRes + 1));
	Colors.Reserve((GridRes + 1) * (GridRes + 1));

	for (int32 Row = 0; Row <= GridRes; Row++)
	{
		for (int32 Col = 0; Col <= GridRes; Col++)
		{
			float NgX = (CenterNgX - Extent) + Col * Step;
			float NgZ = (CenterNgZ - Extent) + Row * Step;

			FVector UEPos = FWowCoordinate::AdtToUE(NgX, OceanHeight, NgZ);
			Vertices.Add(UEPos);
			UVs.Add(FVector2D((float)Col / GridRes, (float)Row / GridRes));
			// Deep ocean: high opacity
			Colors.Add(FVector4f(0.05f, 0.15f, 0.35f, 0.85f));
		}
	}

	TArray<int32> Indices;
	int32 VW = GridRes + 1;
	for (int32 Row = 0; Row < GridRes; Row++)
	{
		for (int32 Col = 0; Col < GridRes; Col++)
		{
			int32 TL = Row * VW + Col;
			int32 TR = TL + 1;
			int32 BL = TL + VW;
			int32 BR = BL + 1;

			Indices.Add(TL);
			Indices.Add(BL);
			Indices.Add(TR);

			Indices.Add(TR);
			Indices.Add(BL);
			Indices.Add(BR);
		}
	}

	UMaterial* WaterMat = FWowWaterMaterial::GetWaterMaterial();
	UStaticMesh* SM = BuildLiquidStaticMesh(Vertices, Indices, UVs, Colors, WaterMat);

	UStaticMeshComponent* OceanComp = NewObject<UStaticMeshComponent>(Owner, TEXT("OceanPlane"));
	OceanComp->SetStaticMesh(SM);
	OceanComp->RegisterComponent();
	OceanComp->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	OceanComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OceanComp->SetCastShadow(false);
	// Note: uses masked blend (not translucent) to avoid Metal crash

	UE_LOG(LogWowWater, Log, TEXT("Created ocean plane at height %.1f for tile %d,%d"), OceanHeight, TileX, TileY);

	return OceanComp;
}
