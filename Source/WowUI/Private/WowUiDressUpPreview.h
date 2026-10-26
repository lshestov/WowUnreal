#pragma once
#include "CoreMinimal.h"

class AActor;
class UWorld;
class FMpqManager;
class FWowAssetCache;
struct FWowLuaContext;

/** Owns an isolated UI actor. Changes never touch live meshes or inventory. */
class FWowUiDressUpPreview
{
public:
    ~FWowUiDressUpPreview();
    void SetUnit(uint64 UnitGuid);
    void TryOn(uint32 ItemId,uint32 InventoryType);
    void Dress(bool Dressed);
    void ReleaseActor();
    bool Rebuild(UWorld* World,FWowLuaContext& Context,FMpqManager* Mpq,FWowAssetCache* Cache);
    AActor* GetActor() const { return Actor.Get(); }
    static int32 SlotForType(uint32 InventoryType);
    uint64 Guid=0;
    bool Dirty=true;
private:
    bool Undressed=false;
    TMap<int32,uint32> Overrides;
    TWeakObjectPtr<AActor> Actor;
    uint32 SourceHash=0;
};
