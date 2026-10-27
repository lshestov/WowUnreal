#pragma once
#include "CoreMinimal.h"

class FMpqManager;
class FWowAssetCache;
class UStaticMesh;
class UPointLightComponent;
class USceneComponent;
struct FAdtWmoPlacement;
struct FWmoGroupData;
struct FWmoRootData;

/**
 * Renders WMO (World Map Object) buildings.
 * Uses UStaticMesh with Nanite when supported.
 */
class WOWWORLD_API FWowWmoRenderer
{
public:
    /** Spawn a WMO instance at the given placement. Returns the parent actor. */
    static AActor* SpawnWmo(UWorld* World, const FString& WmoPath, const FAdtWmoPlacement& Placement,
                            FMpqManager* Mpq, FWowAssetCache* Cache,AActor* Existing=nullptr,
                            const FWmoRootData* ParsedRoot=nullptr,int32 FirstGroup=0,int32 GroupLimit=MAX_int32);

    /** Query the number of groups in a WMO root file without spawning anything. Returns 0 on failure. */
    static uint32 GetWmoGroupCount(const FString& WmoPath, FMpqManager* Mpq);
    static TArray<int32> GroupLoadOrder(const FWmoRootData& Root,const FVector& ModelPosition);
    static UPointLightComponent* AddInteriorFill(USceneComponent* Parent,const FWmoGroupData& Group);

private:
    /** Construct group filename from root WMO path and group index */
    static FString GetGroupPath(const FString& RootPath, int32 GroupIndex);

    /** Create a UStaticMesh from WMO group geometry with Nanite support */
    static UStaticMesh* CreateStaticMeshFromWmoGroup(const FWmoGroupData& GroupData,
        const FWmoRootData& RootData, const FString& WmoPath, int32 GroupIdx,
        FMpqManager* Mpq, FWowAssetCache* Cache);
};
