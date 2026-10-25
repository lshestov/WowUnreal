#pragma once
#include "CoreMinimal.h"
struct lua_State;
class WOWUI_API FWowMacros
{
public:
    static void RegisterApi(lua_State* L);
    static void LoadSavedVariables(lua_State* L);
    static bool RunWireMacro(lua_State* L,uint32 Id);
    static bool SetActionTooltip(lua_State* L,int64 Handle,uint32 Id);
};
