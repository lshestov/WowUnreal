#pragma once
#include "CoreMinimal.h"

class FWowFrameManager;
struct FWowLuaContext;
class UWorld;

/** UI-only camera views of the live character meshes; never moves world actors. */
class FWowUiModelRenderer
{
public:
    FWowUiModelRenderer(FWowFrameManager& Frames,FWowLuaContext& Context);
    ~FWowUiModelRenderer();
    void Portrait(const FString& Name,uint64 Guid);
    void Model(int64 Handle,uint64 Guid);
    void Facing(int64 Handle,float Radians);
    void Tick(UWorld* World);
private:
    struct FSlot;
    FWowFrameManager& Frames;
    FWowLuaContext& Context;
    TMap<FString,TUniquePtr<FSlot>> Slots;
    void Configure(const FString& Key,class UImage* Image,uint64 Guid,int64 Handle,bool Portrait);
};
