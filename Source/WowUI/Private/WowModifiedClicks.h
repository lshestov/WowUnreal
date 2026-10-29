#pragma once
#include "CoreMinimal.h"
struct lua_State;

namespace WowModifiedClicks
{
    void RegisterApi(lua_State* L);
    void Save(lua_State* L);
    void Load(lua_State* L,bool Defaults);
    uint64 ActionTarget(lua_State* L,uint64 Selected);
}
