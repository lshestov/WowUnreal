#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"

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
struct FWowMapUiView;
struct FWowMinimapUiView;
struct FWowGroupUiView;
struct FWowLootUiView;
struct FWowGuildUiView;
struct FWowInspectUiView;
struct FWowTabardUiView;
struct FWowPvpUiView;
struct FWowLfgUiView;
struct FWowLfrUiView;
struct FWowAchievementUiView;
struct FWowItemTemplate;
struct FWowItemEntity;
struct FWowGuildEmblem;

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
    TSharedPtr<FWowMapUiView> Map;
    TSharedPtr<FWowMinimapUiView> Minimap;
    TSharedPtr<FWowGroupUiView> GroupUi;
    TSharedPtr<FWowLootUiView> LootUi;
    TSharedPtr<FWowGuildUiView> GuildUi;
    TSharedPtr<FWowInspectUiView> InspectUi;
    TSharedPtr<FWowTabardUiView> TabardUi;
    TSharedPtr<FWowPvpUiView> PvpUi;
    TSharedPtr<FWowLfgUiView> LfgUi;
    TSharedPtr<FWowLfrUiView> LfrUi;
    TSharedPtr<FWowAchievementUiView> AchievementUi;
    TArray<uint64> ArenaOpponents;
    TFunction<uint32()> ResolveDungeonFloor;
    TFunction<uint32()> ResolveArea;
    uint32 LastAreaId=0,LastZoneId=0;
    uint32 LastWmoHash=0;
    TFunction<AActor*(uint64)> ResolveActor;
    TFunction<bool(const FKey&)> KeyDown;
    TFunction<void()> ReloadUI;
    TFunction<void(int64,const FWowGuildEmblem&,uint32)> TabardPreview;
    TMap<FName,int32> SelectedRows;
    TMap<FString,uint64> UnitReactionState;
    uint64 CharacterStatGuid=0;
    TMap<uint16,uint32> CharacterStatFields;
    uint64 FocusGuid=0;
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
    void RegisterMapApi(lua_State* L);
    void RegisterWorldMapApi(lua_State* L);
    void RegisterQuestMapApi(lua_State* L);
    void UpdateMapArt(lua_State* L);
    void RegisterMinimapApi(lua_State* L);
    void UpdateMinimapState(lua_State* L);
    void UpdateMapState(lua_State* L);
    uint32 ResolveAreaId(const FWowLuaContext* Context);
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
    void NotifyCastState(lua_State* L,uint64 Caster,uint32 Spell,uint8 Kind,bool HasRequest=true,uint8 Counter=0);
    void RegisterClassApi(lua_State* L);
    void RegisterGlyphApi(lua_State* L);
    int SetGlyphTooltip(lua_State* L);
    int SetTalentTooltip(lua_State* L);
    void RegisterUnitApi(lua_State* L);
    void UpdateCharacterStats(lua_State* L);
    void RegisterGroupApi(lua_State* L);
    void UpdateGroupState(lua_State* L);
    void RegisterLootApi(lua_State* L);
    void UpdateLootState(lua_State* L);
    void RegisterGuildApi(lua_State* L);
    void UpdateGuildState(lua_State* L);
    void RegisterInspectApi(lua_State* L);
    void UpdateInspectState(lua_State* L);
    int SetInspectTalentTooltip(lua_State* L);
    void RegisterTabardApi(lua_State* L);
    void RegisterPvpApi(lua_State* L);
    void RegisterLfgApi(lua_State* L);
    void UpdateLfgState(lua_State* L);
    void RegisterRaidBrowserApi(lua_State* L);
    void UpdateRaidBrowserState(lua_State* L);
    void RegisterAchievementApi(lua_State* L);
    void UpdateAchievementState(lua_State* L);
    int SetAchievementTooltip(lua_State* L,uint32 Id);
    int SetLfgRewardTooltip(lua_State* L);
    int SetLfgCompletionRewardTooltip(lua_State* L);
    void UpdatePvpState(lua_State* L);
    void UpdateTabardState(lua_State* L);
    int TabardModelMethod(lua_State* L,int32 Method);
    int SetLootItemTooltip(lua_State* L);
    int SetLootRollItemTooltip(lua_State* L);
    int32 UnitReactionRank(const FWowLuaContext* Context,const FWowEntity* From,const FWowEntity* To);
    bool CanUnitAttack(const FWowLuaContext* Context,const FWowEntity* From,const FWowEntity* To);
    FLinearColor UnitSelectionColor(const FWowLuaContext* Context,const FWowEntity* Unit);
    void UpdateUnitReactions(lua_State* L);
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
