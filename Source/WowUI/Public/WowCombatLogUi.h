#pragma once
#include "CoreMinimal.h"
struct lua_State;
struct FWowCombatEntry;
class WOWUI_API FWowCombatLogUi
{
public:
    static void RegisterApi(lua_State* L);
    static void Dispatch(lua_State* L,const FWowCombatEntry& Entry);
};
