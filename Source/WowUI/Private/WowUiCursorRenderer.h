#pragma once
#include "CoreMinimal.h"
class FWowFrameManager;
struct FWowLuaContext;
struct lua_State;

/** Draw held icons in the game viewport, including engine screenshot captures. */
class FWowUiCursorRenderer
{
public:
    FWowUiCursorRenderer(FWowFrameManager& InFrames,FWowLuaContext& InContext):Frames(InFrames),Context(InContext) {}
    void Tick(lua_State* L);
private:
    FWowFrameManager& Frames;
    FWowLuaContext& Context;
    int64 Handle=-1;
    FString PreviousTexture;
    FVector2D PreviousPosition=FVector2D(-1,-1);
};
