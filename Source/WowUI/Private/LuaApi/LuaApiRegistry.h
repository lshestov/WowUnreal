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
struct FWowCombatLogView;
struct FWowClassUiView;
struct FWowChannelUiView;
struct FWowItemTemplate;
struct FWowItemEntity;

/** Context passed to Lua API functions so they can access game state */
struct FWowLuaContext
{
    FWowEntityManager* EntityManager = nullptr;
    UWowConnectionManager* ConnectionManager = nullptr;
    FWowEventSystem* EventSystem = nullptr;
    FWowFrameManager* FrameManager = nullptr;
    TSharedPtr<FWowSpellBookView> SpellBook;
    TSharedPtr<FWowCombatLogView> CombatLog;
    TSharedPtr<FWowClassUiView> ClassUi;
    TSharedPtr<FWowChannelUiView> Channels;
    TFunction<uint32()> ResolveArea;
    TFunction<AActor*(uint64)> ResolveActor;
    TFunction<void()> ReloadUI;
    TMap<FName,int32> SelectedRows;
    FString CursorTexture;
    bool CursorHidden=false,SoftwareCursorVisible=false;
};

namespace WowLuaApi
{
    void RegisterAll(lua_State* L);
    void RegisterGlobals(lua_State* L);
    void RegisterStubs(lua_State* L);
    void RegisterItemApi(lua_State* L);
    void RegisterMerchantApi(lua_State* L);
    void RegisterCursorApi(lua_State* L);
    void UpdateMerchant(lua_State* L);
    void RegisterBankApi(lua_State* L);
    void UpdateBank(lua_State* L);
    void RegisterMailApi(lua_State* L);
    void UpdateMail(lua_State* L);
    void RegisterTradeApi(lua_State* L);
    void RegisterChannelApi(lua_State* L);
    void UpdateChannels(lua_State* L);
    void UpdateTrade(lua_State* L);
    int SetTradePlayerItemTooltip(lua_State* L);
    int SetTradeTargetItemTooltip(lua_State* L);
    int SetInboxItemTooltip(lua_State* L);
    int SetSendMailItemTooltip(lua_State* L);
    const FWowItemTemplate* QueryItemTemplate(FWowLuaContext* Context,uint32 Id);
    FString ItemLinkForUi(FWowLuaContext* Context,uint32 Id);
    FString ItemIconForUi(FWowLuaContext* Context,uint32 Id,uint32 Display=0);
    bool CanUseItem(FWowLuaContext* Context,const FWowItemTemplate& Item);
    uint32 ItemRepairCost(FWowLuaContext* Context,const FWowItemEntity& Item);
    FWowItemEntity* BuybackItemForUi(FWowLuaContext* Context,int32 Index,int32* Slot=nullptr);
    int SetMerchantItemTooltip(lua_State* L);
    int SetBuybackItemTooltip(lua_State* L);
    void RegisterSpellApi(lua_State* L);
    void NotifyCastState(lua_State* L,uint64 Caster,uint32 Spell,uint8 Kind,bool HasRequest=true);
    void RegisterClassApi(lua_State* L);
    void RegisterGlyphApi(lua_State* L);
    int SetGlyphTooltip(lua_State* L);
    int SetTalentTooltip(lua_State* L);
    void RegisterUnitApi(lua_State* L);
    void RegisterPanelApi(lua_State* L);
    void RegisterReputationApi(lua_State* L);
    void RegisterSettingsApi(lua_State* L);
    void ApplySavedUiSettings(lua_State* L);
    int32 GetUiPreference(lua_State* L,const FString& Key,int32 Default=0);
    void SetUiPreference(lua_State* L,const FString& Key,int32 Value);
    int PushSpellCooldown(lua_State* L,uint32 SpellId);
    FWowEntity* ResolveUnitToken(lua_State* L,int32 Index=1);
    uint32 CombatLogFlags(FWowLuaContext* Context,uint64 Guid);
    void RegisterFrameApi(lua_State* L);
    int SetBagItemTooltip(lua_State* L);
    int SetInventoryItemTooltip(lua_State* L);
    int SetHyperlinkTooltip(lua_State* L);
    int SetHyperlinkCompareItem(lua_State* L);
    void UpdateItemComparisons(lua_State* L);
    int TryOnItem(lua_State* L);
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
