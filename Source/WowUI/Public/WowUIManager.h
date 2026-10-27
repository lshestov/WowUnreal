#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WowLuaVM.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowFontManager.h"
#include "WowInventoryManager.h"
#include "SWowBagWindow.h"
#include "SWowBankWindow.h"
#include "SWowCharacterPanel.h"
#include "WowUIManager.generated.h"

struct FWowLuaContext;

class FMpqManager;
class FWowAssetCache;
class UCanvasPanel;
class UWowConnectionManager;

/**
 * GameInstanceSubsystem that owns the WoW UI systems (Lua VM, FrameManager, EventSystem).
 * Initialize() creates the Lua VM and wires the systems.
 * LoadUI() loads FrameXML and addons from MPQ once the data is available.
 */
UCLASS()
class WOWUI_API UWowUIManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	UWowUIManager();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Load FrameXML and addons from MPQ — call once MpqManager and AssetCache are available */
	void LoadUI(FMpqManager* Mpq, FWowAssetCache* AssetCache);

	/** Set the root UMG canvas for frame rendering */
	void SetRootCanvas(UCanvasPanel* Canvas);

	/** Set connection manager for character stats */
	void SetConnectionManager(UWowConnectionManager* InConnectionManager);

	/** Accessors */
	FWowLuaVM* GetLuaVM() const { return LuaVM.Get(); }
	FWowFrameManager* GetFrameManager() const { return FrameManager.Get(); }
	FWowEventSystem* GetEventSystem() const { return EventSystem.Get(); }
	FWowFontManager* GetFontManager() const { return FontManager.Get(); }
	TSharedPtr<FWowInventoryManager> GetInventoryManager() const { return InventoryManager; }

	bool IsUILoaded() const { return bUILoaded; }
    bool HasSoftwareCursor() const;

	/** UI Window Management */
	void ToggleBagWindow();
	void ToggleCharacterPanel();
	void ShowBankWindow();
	void HideBankWindow();
	bool IsPointerOverWindow() const;

	/** Update inventory from player entity */
	void UpdateInventory();
	void SetActorResolver(TFunction<AActor*(uint64)> Resolver);
    void SetAreaResolver(TFunction<uint32()> Resolver);
	void TickWorldImages(UWorld* World);
	void MapCommand(int32 Command);
	void ProcessPendingReload();
	void SetWorldUiVisible(bool Visible);

private:
	void InitializeCore();
    void OnMailLockItems(int32 Index,uint64 Guid);
	UPROPERTY() TObjectPtr<UCanvasPanel> RootCanvas;
	FMpqManager* LoadedMpq = nullptr;
	FWowAssetCache* LoadedCache = nullptr;
	bool bReloadRequested = false;
	TUniquePtr<FWowLuaVM> LuaVM;
	TSharedPtr<class FWowUiModelRenderer> ModelRenderer;
	TSharedPtr<class FWowUiMapRenderer> MapRenderer;
    TSharedPtr<class FWowUiCursorRenderer> CursorRenderer;
	TUniquePtr<FWowFrameManager> FrameManager;
	TUniquePtr<FWowEventSystem> EventSystem;
	TUniquePtr<FWowFontManager> FontManager;
	TSharedPtr<FWowInventoryManager> InventoryManager;
	TObjectPtr<UWowConnectionManager> ConnectionManager;

	FWowLuaContext* UIContext = nullptr;
	bool bUILoaded = false;
    double NextItemComparisonUpdate=0;

	// UI Windows
	TSharedPtr<SWowBagWindow> BagWindow;
	TSharedPtr<SWowBankWindow> BankWindow;
	TSharedPtr<SWowCharacterPanel> CharacterPanel;
	TSharedPtr<class SOverlay> UIOverlay;
	TSharedPtr<SWidget> NativeWindowRoot;
};
