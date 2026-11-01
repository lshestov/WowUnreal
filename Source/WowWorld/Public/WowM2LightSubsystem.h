#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WowM2LightSubsystem.generated.h"

class FMpqManager;
class FWowAssetCache;
class USceneComponent;
class UPointLightComponent;
struct FM2Data;

/** Bounded local-light rendering shared by streamed ADT and WMO M2 instances. */
UCLASS()
class WOWWORLD_API UWowM2LightSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    uint32 RegisterModel(USceneComponent* Anchor,TSharedPtr<const FM2Data> Model,const FTransform& Local=FTransform::Identity);
    void QueueWmoModel(USceneComponent* Anchor,const FString& Path,const FTransform& Local,FMpqManager* Mpq,FWowAssetCache* Cache);
    void RemoveModel(uint32 Id);
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override {RETURN_QUICK_DECLARE_CYCLE_STAT(UWowM2LightSubsystem,STATGROUP_Tickables);}
    virtual void Deinitialize() override;
    int32 GetSourceCount() const {return Sources.Num();}
    int32 GetActiveCount() const {return Active.Num();}
    int32 GetPendingCount() const {return PendingCount;}
    double GetTickMilliseconds() const {return TickMilliseconds;}
    double GetMaxLoadMilliseconds() const {return MaxLoadMilliseconds;}
private:
    struct FSource
    {
        TWeakObjectPtr<USceneComponent> Anchor;
        TSharedPtr<const FM2Data> Model;
        FTransform Local;
        float Started=0;
    };
    struct FPending
    {
        TWeakObjectPtr<USceneComponent> Anchor;
        FString Path;
        FTransform Local;
        FVector Position=FVector::ZeroVector;
        FMpqManager* Mpq=nullptr;
        FWowAssetCache* Cache=nullptr;
    };
    struct FActive {TWeakObjectPtr<UPointLightComponent> Light; uint32 Source=0; int32 Index=0;};
    TMap<uint32,FSource> Sources;
    TMap<FIntPoint,TArray<FPending>> Pending;
    int32 PendingCount=0;
    TSet<FString> UnlitPaths;
    TMap<uint64,FActive> Active;
    uint32 NextId=1;
    float SelectionTimer=0;
    float PruneTimer=0;
    double TickMilliseconds=0,MaxLoadMilliseconds=0;
    void ProcessPending(const FVector& Focus);
};
