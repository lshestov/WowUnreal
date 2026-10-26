#include "WowUIManager.h"
#include "WowLuaVM.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowFontManager.h"
#include "WowFrameXmlParser.h"
#include "WowUiModelRenderer.h"
#include "WowUiMapRenderer.h"
#include "WowAddonLoader.h"
#include "LuaApi/LuaApiRegistry.h"
#include "Mpq/MpqManager.h"
#include "Components/CanvasPanel.h"
#include "WowEntity.h"
#include "WowEntityManager.h"
#include "WowConnectionManager.h"
#include "Widgets/SOverlay.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/GameViewportClient.h"
#include "WowSavedVariables.h"
#include "WowMacros.h"
#include "WowCombatLogUi.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#define HAS_LUA 1
#else
#define HAS_LUA 0
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWowUIManager, Log, All);

namespace WowLuaApi
{
	void SetAddonLoaderContext(FMpqManager* Mpq, FWowLuaVM* LuaVM, FWowFrameManager* FrameManager, FWowEventSystem* EventSystem);
}

namespace
{
FString ResolveFrameXmlAssetPath(const FString& InPath)
{
	FString ResolvedPath = InPath;
	ResolvedPath.ReplaceInline(TEXT("/"), TEXT("\\"));

	if (!ResolvedPath.StartsWith(TEXT("Interface\\"), ESearchCase::IgnoreCase))
	{
		ResolvedPath = TEXT("Interface\\FrameXML\\") + ResolvedPath;
	}

	return ResolvedPath;
}

bool TryReadUiAssetFile(FMpqManager* Mpq, const FString& InPath, TArray<uint8>& OutData)
{
	if (!Mpq)
	{
		return false;
	}

	FString NormalizedPath = InPath;
	NormalizedPath.ReplaceInline(TEXT("/"), TEXT("\\"));

	TArray<FString> Candidates;
	Candidates.AddUnique(NormalizedPath);
	Candidates.AddUnique(NormalizedPath.ToLower());
	Candidates.AddUnique(NormalizedPath.Replace(TEXT("\\"), TEXT("/")));
	Candidates.AddUnique(NormalizedPath.ToLower().Replace(TEXT("\\"), TEXT("/")));

	for (const FString& Candidate : Candidates)
	{
		if (Mpq->ReadFile(Candidate, OutData))
		{
			return true;
		}
	}

	return false;
}
}

UWowUIManager::UWowUIManager()
{
}

void UWowUIManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	InitializeCore();
}

void UWowUIManager::InitializeCore()
{
	// Create core UI systems
	LuaVM = MakeUnique<FWowLuaVM>();
	FrameManager = MakeUnique<FWowFrameManager>();
	EventSystem = MakeUnique<FWowEventSystem>();
	FontManager = MakeUnique<FWowFontManager>();
	if(!InventoryManager) InventoryManager = MakeShareable(new FWowInventoryManager());

	// Initialize the Lua VM
	if (!LuaVM->Initialize())
	{
		UE_LOG(LogWowUIManager, Error, TEXT("Failed to initialize Lua VM"));
		return;
	}

	// Wire the systems together
	EventSystem->SetLuaVM(LuaVM.Get());
	EventSystem->SetFrameManager(FrameManager.Get());
	FrameManager->SetEventSystem(EventSystem.Get());

	// Set up the Lua context so API functions can access game systems
	// EntityManager and ConnectionManager will be set later when networking is ready
	UIContext = new FWowLuaContext();
	UIContext->EventSystem = EventSystem.Get();
	UIContext->FrameManager = FrameManager.Get();
	UIContext->ReloadUI = [this] { bReloadRequested = true; };
	WowLuaApi::SetContext(LuaVM->GetState(), UIContext);
	ModelRenderer=MakeShared<FWowUiModelRenderer>(*FrameManager,*UIContext);
	MapRenderer=MakeShared<FWowUiMapRenderer>(*FrameManager,*UIContext);
	FrameManager->OnMapCommand=[this](int32 Command){ MapRenderer->Command(Command); };
	FrameManager->OnPortrait=[this](const FString& Name,uint64 Guid){ ModelRenderer->Portrait(Name,Guid); };
	FrameManager->OnUnitModel=[this](int64 Handle,uint64 Guid){ ModelRenderer->Model(Handle,Guid); };
	FrameManager->OnModelFacing=[this](int64 Handle,float Yaw){ ModelRenderer->Facing(Handle,Yaw); };
    FrameManager->OnModelScale=[this](int64 Handle,float Value){ ModelRenderer->Scale(Handle,Value); };
    FrameManager->OnModelPosition=[this](int64 Handle,const FVector& Value){ ModelRenderer->Position(Handle,Value); };
    FrameManager->OnModelTryOn=[this](int64 Handle,uint32 Id,uint32 Type){ ModelRenderer->TryOn(Handle,Id,Type); };
    FrameManager->OnModelDress=[this](int64 Handle,bool Dressed){ ModelRenderer->Dress(Handle,Dressed); };

	UE_LOG(LogWowUIManager, Log, TEXT("WoW UI Manager initialized (Lua VM + FrameManager + EventSystem)"));
}

void UWowUIManager::Deinitialize()
{
	ModelRenderer.Reset();
	MapRenderer.Reset();
	if (NativeWindowRoot.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(NativeWindowRoot.ToSharedRef());
	NativeWindowRoot.Reset();
	if (LuaVM && LuaVM->GetState())
	{
		WowLuaApi::ClearContext(LuaVM->GetState());
	}

	delete UIContext;
	UIContext = nullptr;

	if (LuaVM)
	{
		LuaVM->Shutdown();
	}

	EventSystem.Reset();
	FrameManager.Reset();
	FontManager.Reset();
	LuaVM.Reset();

	bUILoaded = false;

	UE_LOG(LogWowUIManager, Log, TEXT("WoW UI Manager deinitialized"));

	Super::Deinitialize();
}


void UWowUIManager::LoadUI(FMpqManager* Mpq, FWowAssetCache* AssetCache)
{
	LoadedMpq=Mpq; LoadedCache=AssetCache;
	if (!Mpq || !AssetCache || !LuaVM || !LuaVM->IsInitialized())
	{
		UE_LOG(LogWowUIManager, Error, TEXT("Cannot load UI: null MPQ/AssetCache or Lua VM not initialized"));
		return;
	}

	if (bUILoaded)
	{
		UE_LOG(LogWowUIManager, Warning, TEXT("UI already loaded, skipping"));
		return;
	}

	UE_LOG(LogWowUIManager, Log, TEXT("Loading WoW UI from MPQ..."));

	// 0. Initialize font manager first (required for FrameXML font rendering)
	if (!FontManager->Initialize(Mpq))
	{
		UE_LOG(LogWowUIManager, Error, TEXT("Failed to initialize font manager"));
		return;
	}

	// Wire dependencies to frame manager
	FrameManager->SetFontManager(FontManager.Get());
	FrameManager->SetMpqManager(Mpq);
	FrameManager->SetAssetCache(AssetCache);
    ModelRenderer->SetAssets(Mpq,AssetCache);

	if (FrameManager->FindFrame(TEXT("UIParent")) == -1)
	{
		UE_LOG(LogWowUIManager, Error, TEXT("Cannot load UI before SetRootCanvas initializes the FrameManager/UIParent"));
		return;
	}

	WowLuaApi::SetAddonLoaderContext(Mpq, LuaVM.Get(), FrameManager.Get(), EventSystem.Get());
	if(ConnectionManager) FWowSavedVariables::SetIdentity(ConnectionManager->GetAccountName(),ConnectionManager->GetRealmName(),ConnectionManager->GetCharacterName());
	FWowTocData Preferences; Preferences.SavedVariablesPerCharacter.Add(TEXT("IdleWowUIState"));
	FWowSavedVariables::RegisterAddon(TEXT("IdleWowUI"),Preferences);
	FWowSavedVariables::LoadForAddon(LuaVM->GetState(),TEXT("IdleWowUI"),Preferences);
	WowLuaApi::ApplySavedUiSettings(LuaVM->GetState());
	FWowMacros::LoadSavedVariables(LuaVM->GetState());
	if(ConnectionManager) ConnectionManager->PacketHandler.OnCombatLog.BindWeakLambda(this,[this](const FWowCombatEntry& Entry)
	{ if(LuaVM) FWowCombatLogUi::Dispatch(LuaVM->GetState(),Entry); });
	if(ConnectionManager) ConnectionManager->OnMacroUse.BindWeakLambda(this,[this](uint32 Id)
	{
		if(!LuaVM) return false;
		LuaVM->BeginCallback();
		return FWowMacros::RunWireMacro(LuaVM->GetState(),Id);
	});

	// 1. Load FrameXML (Interface/FrameXML/FrameXML.toc) — the core UI system
	TArray<FWowXmlDirective> FrameXmlDirectives = FWowFrameXmlParser::LoadFrameXml(Mpq);
	UE_LOG(LogWowUIManager, Log, TEXT("LoadFrameXml returned %d directives"), FrameXmlDirectives.Num());

	int32 FrameCount = 0;
	if (EventSystem)
	{
		EventSystem->BeginOnLoadBatch();
	}

	for (const FWowXmlDirective& Dir : FrameXmlDirectives)
	{
		switch (Dir.Type)
		{
		case FWowXmlDirective::EType::Script:
		{
			if (!Dir.FilePath.IsEmpty())
			{
				const FString ScriptPath = ResolveFrameXmlAssetPath(Dir.FilePath);
				TArray<uint8> ScriptData;
				if (TryReadUiAssetFile(Mpq, ScriptPath, ScriptData))
				{
					LuaVM->ExecuteBuffer(ScriptData, ScriptPath);
				}
				else
				{
					UE_LOG(LogWowUIManager, Warning, TEXT("Failed to load FrameXML script: %s"), *ScriptPath);
				}
			}
			break;
		}
		case FWowXmlDirective::EType::Frame:
		{
			if (FrameManager)
			{
				FrameManager->CreateFrame(Dir.FrameDef);
				FrameCount++;
			}
			break;
		}
		case FWowXmlDirective::EType::Include:
			// LoadFrameXml now expands includes recursively; keep this as a no-op guard.
			break;
		case FWowXmlDirective::EType::Texture:
            if(FrameManager) FrameManager->LoadTextureDefinition(Dir.TextureDef,Dir.TextureParent);
            break;
        case FWowXmlDirective::EType::Font:
            FWowEventSystem::RegisterFontDefinition(LuaVM.Get(),FontManager.Get(),Dir);
            break;
		}
	}

	if (EventSystem)
	{
		EventSystem->EndOnLoadBatch();
	}

	UE_LOG(LogWowUIManager, Log, TEXT("FrameXML: processed %d directives, created %d frames"),
		FrameXmlDirectives.Num(), FrameCount);

	// 2. Load startup addons (Blizzard + user addons) in dependency order.
	// Each addon fires ADDON_LOADED(addonName) as it finishes loading.
	// Each addon fires ADDON_LOADED(addonName) after loading
	FWowAddonLoader::LoadAllAddons(Mpq, LuaVM.Get(), FrameManager.Get(), EventSystem.Get());

	if (FrameManager)
	{
		FrameManager->SyncChildVisibility();
	}

	// 2.5. Post-load Lua fixups: initialize fields that OnLoad should have set
	// but didn't (e.g., frame templates missing from MPQ XML, OnLoad not reaching
	// child frames, etc.). This runs after all frames and addons are loaded.
#if HAS_LUA
	if (LuaVM && LuaVM->IsInitialized())
	{
		const char* PostLoadFixup = R"LUA(
			-- BuffFrame: ensure OnLoad fields exist (OnLoad may have run on wrong self)
			if BuffFrame and BuffFrame.BuffFrameUpdateTime == nil then
				BuffFrame.BuffFrameUpdateTime = 0
				BuffFrame.BuffFrameFlashTime = 0
				BuffFrame.BuffFrameFlashState = 1
				BuffFrame.BuffAlphaValue = 1
				BuffFrame.numEnchants = 0
				BuffFrame.numConsolidated = 0
			end
			-- TemporaryEnchantFrame: needs TempEnchant button references
			if TemporaryEnchantFrame and TemporaryEnchantFrame.TemporaryEnchantFrameUpdateTime == nil then
				TemporaryEnchantFrame.TemporaryEnchantFrameUpdateTime = 0
			end
			-- CombatFeedback: initialize feedbackText directly on frames that have
			-- CombatFeedback_OnUpdate but never had CombatFeedback_Initialize called.
			-- The OnUpdate runs on these frames, not their child HitIndicator frames.
			local function EnsureCombatFeedback(frameName)
				local f = _G[frameName]
				if f and not f.feedbackText then
					-- Create a stub FontString that safely no-ops all method calls
					local noop = function() end
					local fs = setmetatable({ __objectType = "FontString" }, { __index = function(_, key)
						if key == "IsShown" or key == "IsVisible" then return function() return false end end
						return noop
					end })
					f.feedbackText = fs
					f.feedbackFontHeight = 20
				end
			end
			EnsureCombatFeedback("PlayerFrame")
			EnsureCombatFeedback("TargetFrame")
			EnsureCombatFeedback("PetFrame")
		)LUA";

		lua_State* L = LuaVM->GetState();
		if (L && luaL_dostring(L, PostLoadFixup) != 0)
		{
			UE_LOG(LogWowUIManager, Warning, TEXT("Post-load fixup error: %hs"), lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}
#endif

	// 3. Fire post-addon startup events in WoW order.
	EventSystem->FireEvent(TEXT("VARIABLES_LOADED"));
	EventSystem->FireEvent(TEXT("PLAYER_LOGIN"));
	EventSystem->FireEvent(TEXT("PLAYER_ENTERING_WORLD"));
	EventSystem->FireEvent(TEXT("UPDATE_CHAT_WINDOWS"));

	// Second visibility pass — catches addon frames (GMChatUI etc.) created after first SyncChildVisibility
	if (FrameManager)
	{
		FrameManager->SyncChildVisibility();
	}

	bUILoaded = true;

	UE_LOG(LogWowUIManager, Log, TEXT("WoW UI loaded successfully (%d frames total)"),
		FrameManager ? FrameManager->GetFrameCount() : 0);

	if (FrameManager)
	{
		FrameManager->DebugDumpLayout();
	}
}

void UWowUIManager::SetRootCanvas(UCanvasPanel* Canvas)
{
	RootCanvas=Canvas;
	// First, initialize the frame manager with the canvas if provided
	if (FrameManager && Canvas)
	{
		FrameManager->Initialize(Canvas);
		UE_LOG(LogWowUIManager, Log, TEXT("Root canvas set for FrameManager"));
	}

	// Create the UI overlay for Slate widgets
	if (FSlateApplication::IsInitialized())
	{
		UIOverlay = SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible); // Pass clicks through to 3D world

		// Create bag window (right side, padded from edge)
		BagWindow = SNew(SWowBagWindow, InventoryManager);
		UIOverlay->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(0, 0, 20, 0)  // 20px from right edge
		[
			BagWindow.ToSharedRef()
		];

		BankWindow = SNew(SWowBankWindow, InventoryManager);
		UIOverlay->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(0, 0, 220, 0)
		[
			BankWindow.ToSharedRef()
		];

		// Create character panel (left side, padded from edge)
		CharacterPanel = SNew(SWowCharacterPanel, InventoryManager, ConnectionManager.Get());
		UIOverlay->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(20, 0, 0, 0)  // 20px from left edge
		[
			CharacterPanel.ToSharedRef()
		];

		// Add the overlay to the viewport
		if (GEngine && GEngine->GameViewport)
		{
			NativeWindowRoot=SNew(SDPIScaler).Visibility(EVisibility::SelfHitTestInvisible)
				.DPIScale_Lambda([]()->float
				{
					FVector2D Viewport;
					if (!GEngine || !GEngine->GameViewport) return 1.f;
					GEngine->GameViewport->GetViewportSize(Viewport);
					if (Viewport.Y<100) return 1.f;
					const float Dpi=GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(Viewport.X,Viewport.Y));
					return Viewport.Y/768.f/FMath::Max(Dpi,.01f);
				})[UIOverlay.ToSharedRef()];
			GEngine->GameViewport->AddViewportWidgetContent(NativeWindowRoot.ToSharedRef(), 80);
		}

		UE_LOG(LogWowUIManager, Log, TEXT("Created bag and character panel UI"));
	}
}

void UWowUIManager::ToggleBagWindow()
{
	if (BagWindow.IsValid())
	{
		BagWindow->ToggleVisibility();
	}
}

void UWowUIManager::SetActorResolver(TFunction<AActor*(uint64)> Resolver)
{
    if(UIContext) UIContext->ResolveActor=MoveTemp(Resolver);
}

void UWowUIManager::TickWorldImages(UWorld* World)
{
    if(ModelRenderer) ModelRenderer->Tick(World);
    if(MapRenderer) MapRenderer->Tick(World);
    const double Now=FPlatformTime::Seconds();
    if(bUILoaded && LuaVM && Now>=NextItemComparisonUpdate)
    {
        NextItemComparisonUpdate=Now+.2; LuaVM->BeginCallback();
        WowLuaApi::UpdateItemComparisons(LuaVM->GetState());
    }
}

void UWowUIManager::MapCommand(int32 Command) { if(MapRenderer) MapRenderer->Command(Command); }

void UWowUIManager::ProcessPendingReload()
{
    if(!bReloadRequested || !bUILoaded || !RootCanvas || !LoadedMpq || !LoadedCache) return;
    bReloadRequested=false;
    // Recreate the VM only after its active callback has returned to the game loop.
    FWowSavedVariables::SaveAll(LuaVM->GetState());
    auto Resolver=UIContext->ResolveActor;
    auto Rows=UIContext->SelectedRows;
    auto Action=FrameManager->OnSecureActionDispatch;
    auto Spell=FrameManager->OnSecureSpellDispatch;
    ModelRenderer.Reset(); MapRenderer.Reset();
    RootCanvas->ClearChildren();
    WowLuaApi::ClearContext(LuaVM->GetState());
    delete UIContext; UIContext=nullptr;
    LuaVM->Shutdown(); EventSystem.Reset(); FrameManager.Reset(); FontManager.Reset(); LuaVM.Reset();
    bUILoaded=false;
    InitializeCore();
    UIContext->ResolveActor=MoveTemp(Resolver); UIContext->SelectedRows=MoveTemp(Rows);
    FrameManager->OnSecureActionDispatch=Action; FrameManager->OnSecureSpellDispatch=Spell;
    FrameManager->Initialize(RootCanvas);
    SetConnectionManager(ConnectionManager);
    LoadUI(LoadedMpq,LoadedCache);
    if(FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
    UE_LOG(LogWowUIManager,Log,TEXT("Interface reload completed"));
}

void UWowUIManager::SetWorldUiVisible(bool Visible)
{
    if(RootCanvas) RootCanvas->SetVisibility(Visible?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(UIOverlay) UIOverlay->SetVisibility(Visible?EVisibility::SelfHitTestInvisible:EVisibility::Collapsed);
}

void UWowUIManager::ToggleCharacterPanel()
{
	if (CharacterPanel.IsValid())
	{
		CharacterPanel->ToggleVisibility();
	}
}

void UWowUIManager::ShowBankWindow()
{
	UpdateInventory();

	if (BankWindow.IsValid())
	{
		BankWindow->Show();
	}
}

void UWowUIManager::HideBankWindow()
{
	if (BankWindow.IsValid())
	{
		BankWindow->Hide();
	}
}

bool UWowUIManager::IsPointerOverWindow() const
{
    if (!FSlateApplication::IsInitialized()) return false;
    const auto Pointer=FSlateApplication::Get().GetCursorPos();
    for (const TSharedPtr<SWidget>& Widget : {TSharedPtr<SWidget>(BagWindow),TSharedPtr<SWidget>(CharacterPanel),TSharedPtr<SWidget>(BankWindow)})
        if (Widget.IsValid() && Widget->GetVisibility().IsVisible() && Widget->GetCachedGeometry().IsUnderLocation(Pointer)) return true;
    return false;
}

void UWowUIManager::UpdateInventory()
{
	if (!InventoryManager.IsValid() || !ConnectionManager)
		return;

	FWowPlayerEntity* LocalPlayer = ConnectionManager->PacketHandler.EntityManager.GetLocalPlayer();
	if (!LocalPlayer)
	{
		UE_LOG(LogWowUIManager, Verbose, TEXT("UpdateInventory skipped: local player entity not ready"));
		return;
	}

	InventoryManager->UpdateFromPlayerEntity(*LocalPlayer, ConnectionManager->PacketHandler.EntityManager);
}

void UWowUIManager::SetConnectionManager(UWowConnectionManager* InConnectionManager)
{
	ConnectionManager = InConnectionManager;

	// Update the Lua context with connection manager and entity manager
	if (UIContext && ConnectionManager)
	{
		UIContext->ConnectionManager = ConnectionManager;
		UIContext->EntityManager = &ConnectionManager->PacketHandler.EntityManager;

		UE_LOG(LogWowUIManager, Log, TEXT("Updated Lua context with ConnectionManager and EntityManager"));
	}

	if (InventoryManager.IsValid())
	{
		InventoryManager->SetConnectionManager(ConnectionManager);
	}

	if (CharacterPanel.IsValid())
	{
		CharacterPanel->SetConnectionManager(ConnectionManager);
	}
}
