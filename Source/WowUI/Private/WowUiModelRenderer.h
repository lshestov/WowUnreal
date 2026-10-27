#pragma once
#include "CoreMinimal.h"

class FWowFrameManager;
struct FWowLuaContext;
class UWorld;
struct FWowGuildEmblem;

/** Independent UI character models and camera views of live unit portraits. */
class FWowUiModelRenderer
{
public:
    FWowUiModelRenderer(FWowFrameManager& Frames,FWowLuaContext& Context);
    ~FWowUiModelRenderer();
    void Portrait(const FString& Name,uint64 Guid);
    void Model(int64 Handle,uint64 Guid);
    void Facing(int64 Handle,float Radians);
    void Scale(int64 Handle,float Value);
    void Position(int64 Handle,const FVector& Value);
    void TryOn(int64 Handle,uint32 ItemId,uint32 Type);
    void Dress(int64 Handle,bool Dressed);
    void Tabard(int64 Handle,const FWowGuildEmblem& Value,uint32 Display);
    void SetAssets(class FMpqManager* InMpq,class FWowAssetCache* InCache) { Mpq=InMpq; Cache=InCache; }
    void Tick(UWorld* World);
private:
    struct FSlot;
    FWowFrameManager& Frames;
    FWowLuaContext& Context;
    TMap<FString,TUniquePtr<FSlot>> Slots;
    struct FView { float Facing=0,Scale=0; FVector Position=FVector::ZeroVector; };
    TMap<int64,FView> Views;
    class FMpqManager* Mpq=nullptr;
    class FWowAssetCache* Cache=nullptr;
    void Configure(const FString& Key,class UImage* Image,uint64 Guid,int64 Handle,bool Portrait);
};
