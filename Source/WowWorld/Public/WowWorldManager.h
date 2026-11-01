#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Mpq/MpqManager.h"
#include "WowAssetCache.h"
#include "Formats/WdtTypes.h"
#include "Formats/WdlTypes.h"
#include "Formats/AdtTypes.h"
#include "Formats/WmoTypes.h"
#include "Async/Future.h"
#include "Containers/Ticker.h"
#include "WowWorldManager.generated.h"

class AWowTerrainTile;
class URuntimeVirtualTexture;
class URuntimeVirtualTextureComponent;
struct FWowTerrainGeometry;
struct FWowPreparedWmoGroup;

struct FWowPreparedTile
{
    TSharedPtr<FAdtData> Data;
    TSharedPtr<FWowTerrainGeometry> Geometry;
};

/** Result of an async tile load (MPQ read + ADT parse on background thread) */
struct FPendingTileLoad
{
    int32 TX = 0;
    int32 TY = 0;
    uint32 Generation=0;
    bool Lod1=false;
    bool Wdl=false;
    TFuture<FWowPreparedTile> Future;
};
struct FPendingWmoLoad
{
    TWeakObjectPtr<AActor> Actor;
    FString Path;
    FAdtWmoPlacement Placement;
    TSharedPtr<FWmoRootData,ESPMode::ThreadSafe> Root;
    TFuture<TSharedPtr<FWowPreparedWmoGroup>> Future;
    FMpqManager* Mpq=nullptr;
    FWowAssetCache* Cache=nullptr;
    TArray<int32> Order;
    int32 Next=0;
};

UCLASS()
class WOWWORLD_API AWowWorldManager : public AActor
{
    friend class FIdleWowLargeWmo;
    friend class FIdleWowServerMapSelection;
    friend class FIdleWowGlobalWmo;
    friend class FIdleWowWdlWorker;
    GENERATED_BODY()
public:
    AWowWorldManager();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

    FMpqManager* GetMpqManager() const { return MpqManager.Get(); }
    FWowAssetCache* GetAssetCache() const { return AssetCache.Get(); }

    /** Load WDT/WDL and enable terrain streaming. Call after deferred startup (e.g. login scene). */
    void EnableTerrainStreaming();
    /** Select the server's Map.dbc row before scheduling the initial terrain. */
    bool SelectMap(uint32 MapId);
    void SuspendStreaming() { bStreamingSuspended=true; }
    void LoadStreamingAt(const FVector& Position) { StreamingLoadPosition=Position; bStreamingSuspended=false; LastCameraTile=FIntPoint(-1,-1); }
    void FollowStreamingActor(AActor* Actor) { StreamingFocusActor=Actor; StreamingLoadPosition.Reset(); bStreamingSuspended=false; LastCameraTile=FIntPoint(-1,-1); }

    /** Queue initial tiles around a UE world position. Returns how many were queued. */
    int32 LoadTilesAroundPosition(const FVector& UEPosition, int32 Radius = 2);

    /** Number of tiles that have been queued for the initial load */
    int32 GetInitialTilesQueued() const { return InitialTilesQueued; }

    /** Number of initial tiles that have been finalized (ready to render) */
    int32 GetInitialTilesLoaded() const { return InitialTilesLoaded; }

    /** Whether all initially-queued tiles have finished loading */
    bool IsInitialLoadComplete() const;
    /** Verification: full/mid/far source tiles missing from the rendered rings; -1 for non-terrain maps. */
    int32 GetMissingTerrainCoverage() const;
    bool IsStreamingWorkIdle() const;
    int32 GetPendingDoodadJobs() const;
    int32 GetPendingLod1Count() const {return PendingLod1Keys.Num();}
    int32 GetLod1TileCount() const {return Lod1Tiles.Num();}
    int32 GetWdlTileCount() const {return WdlTiles.Num();}
    int32 GetPreparedTileCount() const;
    int32 GetPendingWmoGroupCount() const {int32 Count=PendingObjectGroups; for(const auto& Job:PendingWmoLoads) if(Job.Actor.IsValid()) Count+=Job.Order.Num()-Job.Next; return Count;}

    /** Get the terrain RVT for mesh components to write to */
    URuntimeVirtualTexture* GetTerrainRVT() const { return TerrainRVT; }

    /** HUD info accessors */
    int32 GetLoadedTileCount() const { return LoadedTiles.Num(); }
    int32 GetPendingTileCount() const { return PendingTileKeys.Num(); }
    FIntPoint GetCameraTile() const { return LastCameraTile; }
    int32 GetActiveDoodadCount() const { return ActiveDoodadCount; }
    int32 GetActiveWmoGroupCount() const { return ActiveWmoGroupCount; }
    const FString& GetMapName() const { return MapName; }
    uint32 GetAreaAt(const FVector& Position) const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW")
    FString MapName = TEXT("Azeroth");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 LoadRadius = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 UnloadRadius = 4;

    /** Radius in tiles for WDL distant terrain (LOD 2) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 WdlRadius = 12;

    /** Distance beyond WdlRadius to unload WDL tiles */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 WdlUnloadRadius = 15;

    /** Radius in tiles for LOD 1 simplified terrain (between LoadRadius and WdlRadius) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 Lod1Radius = 5;

    /** Initial tile to load for testing (Elwynn Forest area) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Debug")
    int32 DebugTileX = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Debug")
    int32 DebugTileY = 48;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Debug")
    bool bStreamingEnabled = false;

    // ---- Object streaming settings ----
    /** Radius (UE cm) within which doodads (M2) are spawned. Default 2000m. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    float DoodadRadius = 200000.0f;

    /** Radius (UE cm) within which WMOs (buildings) are spawned. Default 3000m. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    float WmoRadius = 300000.0f;

    /** Maximum number of active doodad components. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 MaxActiveDoodads = 200;

    /** Maximum number of active WMO group components. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 MaxActiveWmoGroups = 2000;

    /** Legacy serialized field. All groups now use the bounded worker queue. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WoW|Streaming")
    int32 MaxWmoGroupsPerObject = 0;

private:
    bool bStreamingSuspended=false;
    TOptional<FVector> StreamingLoadPosition;
    TWeakObjectPtr<AActor> StreamingFocusActor;
    FVector StreamingPosition(const FVector& Camera) const { return StreamingLoadPosition.IsSet()?StreamingLoadPosition.GetValue():StreamingFocusActor.IsValid()?StreamingFocusActor->GetActorLocation():Camera; }
    TUniquePtr<FMpqManager> MpqManager;
    TUniquePtr<FWowAssetCache> AssetCache;
    TUniquePtr<FWdtData> WdtData;
    TUniquePtr<FWdlData> WdlData;

    UPROPERTY()
    TMap<int64, TObjectPtr<AWowTerrainTile>> LoadedTiles;

    /** WDL distant terrain actors (LOD 2) */
    UPROPERTY()
    TMap<int64, TObjectPtr<AActor>> WdlTiles;

    /** LOD 1 simplified terrain tiles (mid-distance) */
    UPROPERTY()
    TMap<int64, TObjectPtr<AActor>> Lod1Tiles;

    /** Runtime Virtual Texture for terrain */
    UPROPERTY()
    TObjectPtr<URuntimeVirtualTexture> TerrainRVT;

    /** RVT volume component defining the virtual texture bounds */
    UPROPERTY()
    TObjectPtr<URuntimeVirtualTextureComponent> RVTVolumeComponent;

    FIntPoint LastCameraTile = FIntPoint(-9999, -9999);

    /** Initial load tracking */
    int32 InitialTilesQueued = 0;
    int32 InitialTilesLoaded = 0;
    int32 InitialObjectTiles=0;
    TArray<FPendingWmoLoad> PendingWmoLoads;
    TArray<TFuture<TSharedPtr<FWowPreparedWmoGroup>>> RetiredWmoReads;
    UPROPERTY() TObjectPtr<AActor> GlobalWmo;
    FTSTicker::FDelegateHandle WmoTicker;
    TMap<FString,uint32> WmoGroupCounts;
    int32 PendingObjectGroups=0;
    TSet<int64> InitialTileKeys;

    /** Track spawned WMO unique IDs to avoid duplicates across tiles */
    TSet<uint32> SpawnedWmoIds;

    /** Track spawned doodad unique IDs to avoid duplicates across tiles */
    TSet<uint32> SpawnedDoodadIds;

    /** Current count of active doodad mesh components */
    int32 ActiveDoodadCount = 0;

    /** Current count of active WMO group mesh components */
    int32 ActiveWmoGroupCount = 0;

    /** Optional command-line driven screenshot path and timing for automated verification runs. */
    FString AutoScreenshotPath;
    float AutoScreenshotDelaySeconds = -1.0f;
    float AutoQuitDelaySeconds = -1.0f;
    double AutoStartWallClockSeconds = 0.0;
    bool bAutoWaitForInitialLoad = false;
    bool bAutoTimerArmed = false;
    bool bAutoScreenshotRequested = false;
    bool bAutoQuitRequested = false;
    int32 AutoLogBucket = -1;

    void LoadTile(int32 TX, int32 TY);
    void LoadTileAsync(int32 TX, int32 TY);
    void FinalizeTileLoad(int32 TX, int32 TY, TSharedPtr<FAdtData> AdtData,TSharedPtr<FWowTerrainGeometry> Prepared=nullptr);
    void StartPendingTileLoads();
    void StartPendingWmoReads();
    bool ProcessPendingLoads(bool Lod1=false);
    void PumpStreamingWork();
    bool IsCollisionReady() const;
    bool ProcessDoodadJob();
    bool ProcessWdlJob();
    void UnloadTile(int32 TX, int32 TY);
    void UpdateStreaming();
    bool UpdateObjectStreaming();
    bool UpdatePendingWmoLoads();
    bool LoadGlobalWmo(const FVector& Position);
    void UpdateZoneAudio();
    void UpdateWdlStreaming(const FIntPoint& CameraTile);
    void UpdateLod1Streaming(const FIntPoint& CameraTile);
    void SpawnWdlTile(int32 TX, int32 TY,const FAdtData* Transition=nullptr);
    void SpawnLod1Tile(int32 TX, int32 TY,const FAdtData& Data,TSharedPtr<FWowTerrainGeometry> Prepared);
    void SetupRuntimeVirtualTexture();
    void UpdateRVTBounds(const FIntPoint& CameraTile);
    bool IsTileLoaded(int32 TX, int32 TY) const;
    bool IsTilePending(int32 TX, int32 TY) const;
    static int64 TileKey(int32 TX, int32 TY) { return ((int64)TX << 32) | (int64)(uint32)TY; }

    /** Current area ID for zone audio detection */
    uint32 CurrentAudioAreaId = 0;

    /** Tiles currently being loaded on background threads */
    TArray<FPendingTileLoad> PendingLoads;
    uint32 MapGeneration=0;

    /** Track tile keys that are in-flight to avoid duplicate requests */
    TSet<int64> PendingTileKeys;
    TSet<int64> PendingLod1Keys, PendingWdlKeys;
    TArray<FIntPoint> PendingWdlTiles;
    int32 NextWorkKind=0;
    mutable bool CollisionReadyCached=false;
    bool ObjectScanPending=true;
    double NextObjectScan=0;
    FVector GetStreamingFocus() const;
};
