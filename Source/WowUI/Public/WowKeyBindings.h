#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
struct lua_State;

class WOWUI_API FWowKeyBindings
{
public:
    static void RegisterApi(lua_State* L);
    static FString Action(lua_State* L,const FString& Key,bool AllowModifierFallback=false);
    static FString KeyName(const FKey& Key);
    static FKey GameplayKey(const FString& Action);
    static bool IsDefaultGameplayKey(const FKey& Key);
};
