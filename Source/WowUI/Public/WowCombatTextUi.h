#pragma once
#include "CoreMinimal.h"

struct lua_State;
struct FWowCombatEntry;
class UWorld;

/** The native world numbers and the original Blizzard_CombatText event bridge. */
class WOWUI_API FWowCombatTextUi
{
public:
    static void RegisterApi(lua_State* L);
    static void Dispatch(lua_State* L,const FWowCombatEntry& Entry);
    static void Tick(lua_State* L,UWorld* World);
    static void GetCounts(lua_State* L,int32& Active,uint64& Incoming,uint64& Outgoing);
};
