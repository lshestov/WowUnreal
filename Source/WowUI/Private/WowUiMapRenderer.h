#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
class FWowFrameManager;
struct FWowLuaContext;
class UImage;
class UTexture2D;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class UWorld;

/** Original MPQ map art, streamed one texture per tick with a bounded tile cache. */
class FWowUiMapRenderer
{
public:
    FWowUiMapRenderer(FWowFrameManager& Frames,FWowLuaContext& Context);
    void Tick(UWorld* World);
    void Command(int32 Command);
private:
    FWowFrameManager& Frames;
    FWowLuaContext& Context;
    TMap<FString,FString> TileNames;
    TMap<FString,TStrongObjectPtr<UTexture2D>> Tiles;
    TArray<FString> Pending;
    TStrongObjectPtr<UTextureRenderTarget2D> MiniTarget,MapTarget;
    TStrongObjectPtr<UMaterialInstanceDynamic> MiniMaterial;
    TWeakObjectPtr<UImage> MiniImage,MapImage;
    int64 MapHandle=-1;
    int32 AreaIndex=INDEX_NONE;
    double NextDraw=0;
    bool bLoadedIndex=false;
    UTexture2D* Texture(const FString& Path);
    void CreateMap();
    void CurrentZone();
    FVector Position() const;
};
