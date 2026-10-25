#pragma once
#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogWowLuaFrame, Log, All);

struct lua_State;
class FWowEntityManager;
class UWowConnectionManager;
class FWowEventSystem;
class FWowFrameManager;
class FMpqManager;
class AActor;
struct FWowEntity;
struct FWowSpellBookView;

/** Context passed to Lua API functions so they can access game state */
struct FWowLuaContext
{
    FWowEntityManager* EntityManager = nullptr;
    UWowConnectionManager* ConnectionManager = nullptr;
    FWowEventSystem* EventSystem = nullptr;
    FWowFrameManager* FrameManager = nullptr;
    TSharedPtr<FWowSpellBookView> SpellBook;
    TFunction<AActor*(uint64)> ResolveActor;
    TFunction<void()> ReloadUI;
    TMap<FName,int32> SelectedRows;
};

namespace WowLuaApi
{
    void RegisterAll(lua_State* L);
    void RegisterGlobals(lua_State* L);
    void RegisterStubs(lua_State* L);
    void RegisterItemApi(lua_State* L);
    void RegisterSpellApi(lua_State* L);
    void RegisterUnitApi(lua_State* L);
    void RegisterPanelApi(lua_State* L);
    void RegisterReputationApi(lua_State* L);
    void RegisterSettingsApi(lua_State* L);
    void ApplySavedUiSettings(lua_State* L);
    int32 GetUiPreference(lua_State* L,const FString& Key,int32 Default=0);
    void SetUiPreference(lua_State* L,const FString& Key,int32 Value);
    int PushSpellCooldown(lua_State* L,uint32 SpellId);
    FWowEntity* ResolveUnitToken(lua_State* L,int32 Index=1);
    void RegisterFrameApi(lua_State* L);
    int SetBagItemTooltip(lua_State* L);
    int SetInventoryItemTooltip(lua_State* L);
    int SetHyperlinkTooltip(lua_State* L);
    int SetSpellTooltip(lua_State* L);
    int SetActionTooltip(lua_State* L);
    int SetAuraTooltip(lua_State* L);

    /** Store game context pointer in Lua registry for API functions to use */
    void SetContext(lua_State* L, FWowLuaContext* Ctx);

    /** Clear context from Lua registry (call before VM shutdown to prevent use-after-free) */
    void ClearContext(lua_State* L);

    /** Retrieve context from Lua registry (returns nullptr if not set) */
    FWowLuaContext* GetContext(lua_State* L);

    /** Update the loaded addons list for GetNumAddOns/GetAddOnInfo */
    void UpdateLoadedAddons(FMpqManager* Mpq);

    /** Key for the frame metatable in the Lua registry */
    extern const char* FRAME_METATABLE;
}
