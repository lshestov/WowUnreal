#include "WowTextBlock.h"
#include "WowEntity.h"
#include "WowUiText.h"
#include "LuaApiRegistry.h"
#include "WowEventSystem.h"
#include "WowFrameManager.h"
#include "WowFrameTypes.h"
#include "WowMultiLineEditBox.h"
#include "WowFontManager.h"
#include "Components/ProgressBar.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/Slider.h"
#include "Components/CanvasPanel.h"
#include "Slate/WidgetTransform.h"
#include "Components/CanvasPanelSlot.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Framework/Text/TextLayout.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Slate/SlateBrushAsset.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

DEFINE_LOG_CATEGORY(LogWowLuaFrame);

// ── Helpers ──────────────────────────────────────────────────────────────────────

static UImage* GetTextureImage(lua_State* L, int idx = 1);
static int LF_SetText(lua_State* L);
static int LF_SetFormattedText(lua_State* L)
{
	const int32 Args=lua_gettop(L)-1;
	lua_getglobal(L,"string"); lua_getfield(L,-1,"format"); lua_remove(L,-2);
	for(int32 I=2; I<=Args+1; ++I) lua_pushvalue(L,I);
	if(lua_pcall(L,Args,1,0)!=0) return lua_error(L);
	lua_replace(L,2); lua_settop(L,2); return LF_SetText(L);
}
static int LF_GetText(lua_State* L);
static int LF_FontStringSetTextColor(lua_State* L);
static int LF_SetFontObject(lua_State* L);

// Get the frame handle from a frame table (self argument at ArgIdx)
static int64 GetFrameHandle(lua_State* L, int ArgIdx = 1)
{
	luaL_checktype(L, ArgIdx, LUA_TTABLE);
	lua_getfield(L, ArgIdx, "__handle");
	int64 Handle = static_cast<int64>(lua_tointeger(L, -1));
	lua_pop(L, 1);
	return Handle;
}

static int64 GetOptionalFrameHandle(lua_State* L, int ArgIdx = 1)
{
	luaL_checktype(L, ArgIdx, LUA_TTABLE);
	lua_getfield(L, ArgIdx, "__handle");
	int64 Handle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);
	return Handle;
}

static int LF_ModelScalar(lua_State* L,bool Scale,bool Get)
{
    const char* Key=Scale?"__modelScale":"__modelFacing"; auto* C=WowLuaApi::GetContext(L); const int64 H=GetFrameHandle(L,1);
    if(Get)
    {
        lua_getfield(L,1,Key);
        if(lua_isnil(L,-1)) { lua_pop(L,1); const auto* Def=C && C->FrameManager?C->FrameManager->GetFrameDef(H):nullptr; lua_pushnumber(L,Scale?(Def?Def->ModelScale:1):0); }
        return 1;
    }
    const float Value=luaL_checknumber(L,2); if(!FMath::IsFinite(Value) || (Scale && Value<0)) return 0;
    lua_pushnumber(L,Value); lua_setfield(L,1,Key);
    if(C && C->FrameManager)
    {
        const auto& Callback=Scale?C->FrameManager->OnModelScale:C->FrameManager->OnModelFacing;
        if(Callback) Callback(H,Value);
    }
    return 0;
}
static int LF_ModelPosition(lua_State* L,bool Get)
{
    if(Get)
    {
        lua_getfield(L,1,"__modelPosition");
        if(lua_istable(L,-1)) { const int32 Table=lua_gettop(L); for(int32 I=1;I<=3;++I) lua_rawgeti(L,Table,I); return 3; }
        lua_pushnumber(L,0); lua_pushnumber(L,0); lua_pushnumber(L,0); return 3;
    }
    const FVector Value(luaL_checknumber(L,2),luaL_checknumber(L,3),luaL_checknumber(L,4)); if(Value.ContainsNaN()) return 0;
    lua_newtable(L); for(int32 I=1;I<=3;++I) { lua_pushvalue(L,I+1); lua_rawseti(L,-2,I); } lua_setfield(L,1,"__modelPosition");
    auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager && C->FrameManager->OnModelPosition) C->FrameManager->OnModelPosition(GetFrameHandle(L,1),Value);
    return 0;
}
static int LF_ModelUnit(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) return 0;
    if(lua_isnoneornil(L,2)) { lua_settop(L,1); lua_getfield(L,1,"__modelUnit"); }
    const int64 H=GetFrameHandle(L,1);
    if(const auto* Def=C->FrameManager->GetFrameDef(H); Def && Def->Type==EWowFrameType::GameTooltip) { C->FrameManager->TooltipSetUnit(H,UTF8_TO_TCHAR(luaL_checkstring(L,2))); return 0; }
    if(C->FrameManager->OnUnitModel)
    {
        const auto* Entity=WowLuaApi::ResolveUnitToken(L,2);
        lua_pushvalue(L,2); lua_setfield(L,1,"__modelUnit"); C->FrameManager->OnUnitModel(H,Entity?Entity->Guid:0);
    }
    return 0;
}

static int64 GetMouseQueryHandle(lua_State* L, int ArgIdx = 1)
{
	luaL_checktype(L, ArgIdx, LUA_TTABLE);

	lua_getfield(L, ArgIdx, "__handle");
	if (lua_isnumber(L, -1))
	{
		const int64 Handle = static_cast<int64>(lua_tointeger(L, -1));
		lua_pop(L, 1);
		return Handle;
	}
	lua_pop(L, 1);

	lua_getfield(L, ArgIdx, "__parentHandle");
	const int64 ParentHandle = lua_isnumber(L, -1) ? static_cast<int64>(lua_tointeger(L, -1)) : -1;
	lua_pop(L, 1);
	return ParentHandle;
}

static UButton* GetButtonWidgetForTable(lua_State* L, int Idx = 1)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return nullptr;

	const int64 Handle = GetOptionalFrameHandle(L, Idx);
	if (Handle < 0) return nullptr;

	if (UButton* Button = Ctx->FrameManager->GetButtonWidget(Handle))
	{
		return Button;
	}

	return Cast<UButton>(Ctx->FrameManager->GetWidgetForHandle(Handle));
}

static UEditableTextBox* GetEditBoxWidgetForTable(lua_State* L, int Idx = 1)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return nullptr;

	const int64 Handle = GetOptionalFrameHandle(L, Idx);
	if (Handle < 0) return nullptr;

	if (UEditableTextBox* EditBox = Ctx->FrameManager->GetEditBoxWidget(Handle))
	{
		return EditBox;
	}

	return Cast<UEditableTextBox>(Ctx->FrameManager->GetWidgetForHandle(Handle));
}

static UTextBlock* GetTextBlockWidgetForTable(lua_State* L, int Idx = 1)
{
	luaL_checktype(L, Idx, LUA_TTABLE);

	lua_getfield(L, Idx, "__textWidget");
	if (lua_islightuserdata(L, -1))
	{
		UTextBlock* TextBlock = static_cast<UTextBlock*>(lua_touserdata(L, -1));
		lua_pop(L, 1);
		return TextBlock;
	}
	lua_pop(L, 1);

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return nullptr;

	lua_getfield(L, Idx, "__name");
	if (lua_isstring(L, -1))
	{
		if (UTextBlock* NamedText = Ctx->FrameManager->GetNamedFontString(UTF8_TO_TCHAR(lua_tostring(L, -1))))
		{
			lua_pop(L, 1);
			return NamedText;
		}
	}
	lua_pop(L, 1);

	const int64 Handle = GetOptionalFrameHandle(L, Idx);
	if (Handle >= 0)
	{
		if (UTextBlock* PrimaryText = Ctx->FrameManager->GetPrimaryTextWidget(Handle))
		{
			return PrimaryText;
		}

		return Cast<UTextBlock>(Ctx->FrameManager->GetWidgetForHandle(Handle));
	}

	return nullptr;
}

static UWowMultiLineEditBox* GetMultiLineEditor(lua_State* L,int32 Index=1)
{
    auto* C=WowLuaApi::GetContext(L); return C && C->FrameManager?C->FrameManager->GetMultiLineEditBoxWidget(GetOptionalFrameHandle(L,Index)):nullptr;
}

static bool TableRepresentsRegion(lua_State* L, int Idx = 1)
{
	luaL_checktype(L, Idx, LUA_TTABLE);

	lua_getfield(L, Idx, "__objectType");
	const bool bHasObjectType = (lua_isstring(L, -1) != 0);
	lua_pop(L, 1);
	if (bHasObjectType)
	{
		return true;
	}

	lua_getfield(L, Idx, "__imageWidget");
	const bool bHasImageWidget = lua_islightuserdata(L, -1);
	lua_pop(L, 1);
	if (bHasImageWidget)
	{
		return true;
	}

	lua_getfield(L, Idx, "__textWidget");
	const bool bHasTextWidget = lua_islightuserdata(L, -1);
	lua_pop(L, 1);
	return bHasTextWidget;
}

static int LF_CreateAnimationGroup(lua_State* L)
{
    luaL_checktype(L,1,LUA_TTABLE); auto* C=WowLuaApi::GetContext(L);
    FWowAnimationDef Def; Def.Type=TEXT("AnimationGroup"); Def.Name=UTF8_TO_TCHAR(luaL_optstring(L,2,""));
    lua_pushvalue(L,1); if(C && C->EventSystem && C->EventSystem->CreateAnimationGroup(Def)) return 1;
    lua_pushnil(L); return 1;
}

static int LF_StopAnimating(lua_State* L)
{
    luaL_checktype(L,1,LUA_TTABLE); lua_getfield(L,1,"__animationGroups");
    if(lua_istable(L,-1)) for(int32 I=1;I<=int32(lua_objlen(L,-1));++I)
    {
        lua_rawgeti(L,-1,I); lua_getfield(L,-1,"Stop"); lua_pushvalue(L,-2);
        if(lua_pcall(L,1,0,0)!=0) return lua_error(L); lua_pop(L,1);
    }
    return 0;
}
static int LF_GetAnimationGroups(lua_State* L)
{
    luaL_checktype(L,1,LUA_TTABLE); lua_getfield(L,1,"__animationGroups"); if(!lua_istable(L,-1)) return 0;
    const int32 Table=lua_gettop(L),Count=lua_objlen(L,Table); for(int32 I=1;I<=Count;++I) lua_rawgeti(L,Table,I); return Count;
}

static int LF_PlayObject(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushboolean(L, 1);
	lua_setfield(L, 1, "__playing");
	return 0;
}

static int LF_StopObject(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushboolean(L, 0);
	lua_setfield(L, 1, "__playing");
	return 0;
}

static int LF_IsObjectPlaying(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "__playing");
	const bool bPlaying = lua_toboolean(L, -1) != 0;
	lua_pop(L, 1);
	lua_pushboolean(L, bPlaying ? 1 : 0);
	return 1;
}

static int LF_IsMouseOver(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushboolean(L, 0);
		return 1;
	}

	const int64 Handle = GetMouseQueryHandle(L, 1);
	if (Handle < 0)
	{
		lua_pushboolean(L, 0);
		return 1;
	}

	const float TopOffset = static_cast<float>(luaL_optnumber(L, 2, 0.0));
	const float BottomOffset = static_cast<float>(luaL_optnumber(L, 3, 0.0));
	const float LeftOffset = static_cast<float>(luaL_optnumber(L, 4, 0.0));
	const float RightOffset = static_cast<float>(luaL_optnumber(L, 5, 0.0));

	lua_pushboolean(L, Ctx->FrameManager->IsMouseOverFrame(Handle, TopOffset, BottomOffset, LeftOffset, RightOffset) ? 1 : 0);
	return 1;
}

// ── Global: CreateFrame(type, name, parent, template) ────────────────────────────

static EWowFrameType ParseFrameType(const char* TypeStr)
{
	if (!TypeStr) return EWowFrameType::Frame;
	if (FCStringAnsi::Stricmp(TypeStr, "Button") == 0) return EWowFrameType::Button;
	if (FCStringAnsi::Stricmp(TypeStr, "CheckButton") == 0) return EWowFrameType::CheckButton;
	if (FCStringAnsi::Stricmp(TypeStr, "EditBox") == 0) return EWowFrameType::EditBox;
	if (FCStringAnsi::Stricmp(TypeStr, "ScrollFrame") == 0) return EWowFrameType::ScrollFrame;
	if (FCStringAnsi::Stricmp(TypeStr, "Slider") == 0) return EWowFrameType::Slider;
	if (FCStringAnsi::Stricmp(TypeStr, "StatusBar") == 0) return EWowFrameType::StatusBar;
	if (FCStringAnsi::Stricmp(TypeStr, "SimpleHTML") == 0) return EWowFrameType::SimpleHTML;
	if (FCStringAnsi::Stricmp(TypeStr, "Cooldown") == 0) return EWowFrameType::Cooldown;
	if (FCStringAnsi::Stricmp(TypeStr, "GameTooltip") == 0) return EWowFrameType::GameTooltip;
	if (FCStringAnsi::Stricmp(TypeStr, "Minimap") == 0) return EWowFrameType::Minimap;
	if (FCStringAnsi::Stricmp(TypeStr, "Model") == 0) return EWowFrameType::Model;
	if (FCStringAnsi::Stricmp(TypeStr, "PlayerModel") == 0) return EWowFrameType::PlayerModel;
	if (FCStringAnsi::Stricmp(TypeStr, "MessageFrame") == 0) return EWowFrameType::MessageFrame;
	if (FCStringAnsi::Stricmp(TypeStr, "ScrollingMessageFrame") == 0) return EWowFrameType::ScrollingMessageFrame;
	return EWowFrameType::Frame;
}

static int L_CreateFrame(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager || !Ctx->EventSystem) { lua_pushnil(L); return 1; }

	const char* TypeStr = luaL_optstring(L, 1, "Frame");
	const char* Name = luaL_optstring(L, 2, nullptr);
	// arg 3 = parent (frame table or nil)
	const char* TemplateName = luaL_optstring(L, 4, nullptr);

	FWowFrameDef Def;
	Def.Type = ParseFrameType(TypeStr);
	if (Name) Def.Name = UTF8_TO_TCHAR(Name);
	if (TemplateName) Def.Inherits = UTF8_TO_TCHAR(TemplateName);

	// Resolve parent
	if (lua_istable(L, 3))
	{
		if(const auto* Parent=Ctx->FrameManager->GetFrameDef(GetOptionalFrameHandle(L,3))) Def.Parent=Parent->Name;
	}
	else if (lua_isstring(L, 3))
	{
		Def.Parent = UTF8_TO_TCHAR(lua_tostring(L, 3));
	}

	int64 Handle = Ctx->FrameManager->CreateFrame(Def);
	if (Handle < 0) { lua_pushnil(L); return 1; }

	// Scripts, attributes and arbitrary Lua fields must share one object. Returning
	// a second table for unnamed frames detached callbacks from their saved state.
	if(!Ctx->EventSystem->PushFrameObject(Handle)) lua_pushnil(L);
	return 1;
}

// ── Frame Methods ────────────────────────────────────────────────────────────────

// frame:GetName()
static int LF_GetName(lua_State* L)
{
	lua_getfield(L,1,"__anonymous"); const bool Anonymous=lua_toboolean(L,-1)!=0; lua_pop(L,1); if(Anonymous) { lua_pushnil(L); return 1; }
	lua_getfield(L, 1, "__name");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushnil(L);
	}
	return 1;
}

// frame:GetObjectType()
static int LF_GetObjectType(lua_State* L)
{
	lua_getfield(L, 1, "__objectType");
	if (lua_isstring(L, -1))
	{
		return 1;
	}
	lua_pop(L, 1);

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		if (Def)
		{
			switch (Def->Type)
			{
			case EWowFrameType::Button: lua_pushstring(L, "Button"); break;
			case EWowFrameType::CheckButton: lua_pushstring(L, "CheckButton"); break;
			case EWowFrameType::EditBox: lua_pushstring(L, "EditBox"); break;
			case EWowFrameType::ScrollFrame: lua_pushstring(L, "ScrollFrame"); break;
			case EWowFrameType::Slider: lua_pushstring(L, "Slider"); break;
			case EWowFrameType::StatusBar: lua_pushstring(L, "StatusBar"); break;
			case EWowFrameType::SimpleHTML: lua_pushstring(L, "SimpleHTML"); break;
			case EWowFrameType::Cooldown: lua_pushstring(L, "Cooldown"); break;
			case EWowFrameType::GameTooltip: lua_pushstring(L, "GameTooltip"); break;
			case EWowFrameType::Minimap: lua_pushstring(L, "Minimap"); break;
			case EWowFrameType::Model: lua_pushstring(L, "Model"); break;
			case EWowFrameType::PlayerModel: lua_pushstring(L, "PlayerModel"); break;
			case EWowFrameType::MessageFrame: lua_pushstring(L, "MessageFrame"); break;
			case EWowFrameType::ScrollingMessageFrame: lua_pushstring(L, "ScrollingMessageFrame"); break;
			default: lua_pushstring(L, "Frame"); break;
			}
			return 1;
		}
	}
	lua_pushstring(L, "Frame");
	return 1;
}

// frame:RegisterEvent(event)
static int LF_RegisterEvent(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	const char* Event = luaL_checkstring(L, 2);
	if (Ctx && Ctx->EventSystem)
	{
		Ctx->EventSystem->RegisterEvent(Handle, UTF8_TO_TCHAR(Event));
	}
	return 0;
}

// frame:UnregisterEvent(event)
static int LF_UnregisterEvent(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	const char* Event = luaL_checkstring(L, 2);
	if (Ctx && Ctx->EventSystem)
	{
		Ctx->EventSystem->UnregisterEvent(Handle, UTF8_TO_TCHAR(Event));
	}
	return 0;
}

// frame:UnregisterAllEvents()
static int LF_UnregisterAllEvents(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->EventSystem)
	{
		Ctx->EventSystem->UnregisterAllEvents(Handle);
	}
	return 0;
}

// frame:SetScript(scriptType, handler)
static int LF_SetScript(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	const char* ScriptType = luaL_checkstring(L, 2);

	if (Ctx && Ctx->EventSystem)
	{
		if (lua_isfunction(L, 3))
		{
			// Store the Lua function reference directly in the event system
			lua_pushvalue(L, 3);
			int32 Ref = luaL_ref(L, LUA_REGISTRYINDEX);
			Ctx->EventSystem->SetFrameScriptRef(Handle, UTF8_TO_TCHAR(ScriptType), Ref);

			// Also store in a global key so GetScript can retrieve it
			FString GlobalKey = FString::Printf(TEXT("__WowScript_%lld_%s"),
				Handle, UTF8_TO_TCHAR(ScriptType));
			FTCHARToUTF8 UTF8GlobalKey(*GlobalKey);
			lua_pushvalue(L, 3);
			lua_setglobal(L, UTF8GlobalKey.Get());
		}
		else if (lua_isstring(L, 3))
		{
			// Code string passed - compile it
			const char* CodeStr = lua_tostring(L, 3);
			Ctx->EventSystem->SetFrameScript(Handle, UTF8_TO_TCHAR(ScriptType), UTF8_TO_TCHAR(CodeStr));
		}
		else if (lua_isnil(L, 3))
		{
			// Clear the script
			Ctx->EventSystem->SetFrameScript(Handle, UTF8_TO_TCHAR(ScriptType), FString());
		}
	}
	return 0;
}

// frame:GetScript(scriptType)
static int LF_GetScript(lua_State* L)
{
	int64 Handle = GetFrameHandle(L);
	const char* ScriptType = luaL_checkstring(L, 2);

	// Try to find the stored function in the global
	FString GlobalKey = FString::Printf(TEXT("__WowScript_%lld_%s"),
		Handle, UTF8_TO_TCHAR(ScriptType));
	FTCHARToUTF8 UTF8Key(*GlobalKey);

	lua_getglobal(L, UTF8Key.Get());
	if (lua_isfunction(L, -1))
	{
		return 1;
	}
	lua_pop(L, 1);
	lua_pushnil(L);
	return 1;
}

// frame:Show()
static int LF_Show(lua_State* L)
{
	if (TableRepresentsRegion(L, 1))
	{
		lua_getfield(L,1,"__name"); const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,-1,"")); lua_pop(L,1);
		if(auto* C=WowLuaApi::GetContext(L); C && C->FrameManager && C->FrameManager->SetButtonRegionShown(Name,true)) return 0;
		const auto* C=WowLuaApi::GetContext(L); const auto Visibility=C && C->FrameManager && !C->FrameManager->IsRegionAnchored(Name)?ESlateVisibility::Hidden:ESlateVisibility::SelfHitTestInvisible;
		if (UImage* Image = GetTextureImage(L, 1))
		{
			Image->SetVisibility(Visibility);
			return 0;
		}
		if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
		{
			TextBlock->SetVisibility(Visibility);
			return 0;
		}
	}

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->SetFrameVisible(Handle, true);
	}
	return 0;
}

// frame:Hide()
static int LF_Hide(lua_State* L)
{
	if (TableRepresentsRegion(L, 1))
	{
		lua_getfield(L,1,"__name"); const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,-1,"")); lua_pop(L,1);
		if(auto* C=WowLuaApi::GetContext(L); C && C->FrameManager && C->FrameManager->SetButtonRegionShown(Name,false)) return 0;
		if (UImage* Image = GetTextureImage(L, 1))
		{
			Image->SetVisibility(ESlateVisibility::Collapsed);
			return 0;
		}
		if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
		{
			TextBlock->SetVisibility(ESlateVisibility::Collapsed);
			return 0;
		}
	}

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->SetFrameVisible(Handle, false);
	}
	return 0;
}

// frame:IsShown()
static int LF_IsShown(lua_State* L)
{
	if (TableRepresentsRegion(L, 1))
	{
		if (UImage* Image = GetTextureImage(L, 1))
		{
			lua_pushboolean(L, Image->GetVisibility() != ESlateVisibility::Collapsed);
			return 1;
		}
		if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
		{
			lua_pushboolean(L, TextBlock->GetVisibility() != ESlateVisibility::Collapsed);
			return 1;
		}
	}

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		lua_pushboolean(L, Ctx->FrameManager->IsFrameVisible(Handle) ? 1 : 0);
		return 1;
	}
	lua_pushboolean(L, 1);
	return 1;
}

// frame:IsVisible()
static int LF_IsVisible(lua_State* L)
{
	auto* C=WowLuaApi::GetContext(L);
	if(TableRepresentsRegion(L,1))
	{
		LF_IsShown(L); const bool Own=lua_toboolean(L,-1)!=0; lua_pop(L,1);
		lua_getfield(L,1,"__parentHandle"); const int64 Parent=lua_tointeger(L,-1); lua_pop(L,1);
		lua_getfield(L,1,"__name"); const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,-1,"")); lua_pop(L,1);
		lua_pushboolean(L,Own && C && C->FrameManager && C->FrameManager->IsRegionAnchored(Name) && C->FrameManager->IsFrameHierarchyShown(Parent));
	}
	else lua_pushboolean(L,C && C->FrameManager && C->FrameManager->IsFrameHierarchyShown(GetFrameHandle(L)));
	return 1;
}

// Parse a WoW anchor point string to enum
static EWowAnchorPoint ParseAnchorPoint(const char* Str)
{
	if (!Str) return EWowAnchorPoint::CENTER;
	if (strcmp(Str, "TOPLEFT") == 0) return EWowAnchorPoint::TOPLEFT;
	if (strcmp(Str, "TOP") == 0) return EWowAnchorPoint::TOP;
	if (strcmp(Str, "TOPRIGHT") == 0) return EWowAnchorPoint::TOPRIGHT;
	if (strcmp(Str, "LEFT") == 0) return EWowAnchorPoint::LEFT;
	if (strcmp(Str, "CENTER") == 0) return EWowAnchorPoint::CENTER;
	if (strcmp(Str, "RIGHT") == 0) return EWowAnchorPoint::RIGHT;
	if (strcmp(Str, "BOTTOMLEFT") == 0) return EWowAnchorPoint::BOTTOMLEFT;
	if (strcmp(Str, "BOTTOM") == 0) return EWowAnchorPoint::BOTTOM;
	if (strcmp(Str, "BOTTOMRIGHT") == 0) return EWowAnchorPoint::BOTTOMRIGHT;
	return EWowAnchorPoint::CENTER;
}

// Convert anchor point enum to string
static const char* AnchorPointToString(EWowAnchorPoint Point)
{
	switch (Point)
	{
	case EWowAnchorPoint::TOPLEFT: return "TOPLEFT";
	case EWowAnchorPoint::TOP: return "TOP";
	case EWowAnchorPoint::TOPRIGHT: return "TOPRIGHT";
	case EWowAnchorPoint::LEFT: return "LEFT";
	case EWowAnchorPoint::CENTER: return "CENTER";
	case EWowAnchorPoint::RIGHT: return "RIGHT";
	case EWowAnchorPoint::BOTTOMLEFT: return "BOTTOMLEFT";
	case EWowAnchorPoint::BOTTOM: return "BOTTOM";
	case EWowAnchorPoint::BOTTOMRIGHT: return "BOTTOMRIGHT";
	default: return "CENTER";
	}
}

// frame:SetPoint(point, relativeTo, relativePoint, ofsx, ofsy)
static int LF_SetPoint(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	const char* PointStr = luaL_checkstring(L, 2);
	FWowAnchor Anchor;
	Anchor.Point = ParseAnchorPoint(PointStr);

	// arg3 = relativeTo (frame table, frame name string, or nil)
	if (lua_istable(L, 3))
	{
		lua_getfield(L, 3, "__name");
		if (lua_isstring(L, -1))
			Anchor.RelativeTo = UTF8_TO_TCHAR(lua_tostring(L, -1));
		lua_pop(L, 1);

		// The relative point may be omitted: SetPoint(point, frame, x, y).
		const bool HasOffsets=lua_isnumber(L,4)!=0;
		Anchor.RelativePoint = HasOffsets ? Anchor.Point : ParseAnchorPoint(luaL_optstring(L,4,PointStr));
		Anchor.OffsetX = static_cast<float>(luaL_optnumber(L,HasOffsets?4:5,0));
		Anchor.OffsetY = static_cast<float>(luaL_optnumber(L,HasOffsets?5:6,0));
	}
	else if (lua_isstring(L, 3) && !lua_isnumber(L, 3))
	{
		// Could be relativeTo name or relativePoint if no frame given
		const char* Arg3 = lua_tostring(L, 3);
		// Check if arg3 looks like an anchor point name
		if (strcmp(Arg3, "TOPLEFT") == 0 || strcmp(Arg3, "TOP") == 0 || strcmp(Arg3, "TOPRIGHT") == 0 ||
			strcmp(Arg3, "LEFT") == 0 || strcmp(Arg3, "CENTER") == 0 || strcmp(Arg3, "RIGHT") == 0 ||
			strcmp(Arg3, "BOTTOMLEFT") == 0 || strcmp(Arg3, "BOTTOM") == 0 || strcmp(Arg3, "BOTTOMRIGHT") == 0)
		{
			// SetPoint("POINT", "RELATIVEPOINT", x, y) — no relativeTo frame
			Anchor.RelativePoint = ParseAnchorPoint(Arg3);
			Anchor.OffsetX = static_cast<float>(luaL_optnumber(L, 4, 0));
			Anchor.OffsetY = static_cast<float>(luaL_optnumber(L, 5, 0));
		}
		else
		{
			// SetPoint("POINT", "frameName", "RELATIVEPOINT", x, y)
			Anchor.RelativeTo = UTF8_TO_TCHAR(Arg3);
			const bool HasOffsets=lua_isnumber(L,4)!=0;
			Anchor.RelativePoint = HasOffsets ? Anchor.Point : ParseAnchorPoint(luaL_optstring(L,4,PointStr));
			Anchor.OffsetX = static_cast<float>(luaL_optnumber(L,HasOffsets?4:5,0));
			Anchor.OffsetY = static_cast<float>(luaL_optnumber(L,HasOffsets?5:6,0));
		}
	}
	else
	{
		// SetPoint("POINT", nil, nil, x, y) or SetPoint("POINT", x, y)
		Anchor.RelativePoint = Anchor.Point;
		if (lua_isnumber(L, 3))
		{
			Anchor.OffsetX = static_cast<float>(lua_tonumber(L, 3));
			Anchor.OffsetY = static_cast<float>(luaL_optnumber(L, 4, 0));
		}
		else
		{
			Anchor.OffsetX = static_cast<float>(luaL_optnumber(L, 5, 0));
			Anchor.OffsetY = static_cast<float>(luaL_optnumber(L, 6, 0));
		}
	}

	const bool IsRegion=TableRepresentsRegion(L,1);
	if(Anchor.RelativeTo.Contains(TEXT("$parent")))
	{
		const auto* Self=Ctx->FrameManager->GetFrameDef(IsRegion?GetMouseQueryHandle(L):Handle);
		if(Self) Anchor.RelativeTo.ReplaceInline(TEXT("$parent"),IsRegion?*Self->Name:*Self->Parent);
	}
	if(IsRegion)
	{
		lua_getfield(L,1,"__name"); const FString Name=UTF8_TO_TCHAR(lua_tostring(L,-1)?lua_tostring(L,-1):""); lua_pop(L,1);
		Ctx->FrameManager->SetRegionPoint(Name,Anchor); return 0;
	}
	// Replace existing anchor with same point, or append if new
	FWowFrameDef* Def = Ctx->FrameManager->GetMutableFrameDef(Handle);
	if (Def)
	{
		TArray<FWowAnchor> Anchors = Def->Anchors;
		bool bReplaced = false;
		for (int32 i = 0; i < Anchors.Num(); i++)
		{
			if (Anchors[i].Point == Anchor.Point)
			{
				Anchors[i] = Anchor;
				bReplaced = true;
				break;
			}
		}
		if (!bReplaced)
		{
			Anchors.Add(Anchor);
		}
		Ctx->FrameManager->SetFrameAnchors(Handle, Anchors);
	}
	return 0;
}

// frame:ClearAllPoints()
static int LF_ClearAllPoints(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		if(TableRepresentsRegion(L,1)) { lua_getfield(L,1,"__name"); Ctx->FrameManager->SetRegionPoint(UTF8_TO_TCHAR(lua_tostring(L,-1)?lua_tostring(L,-1):""),FWowAnchor(),true); lua_pop(L,1); return 0; }
		Ctx->FrameManager->ClearFrameAnchors(Handle);
	}
	return 0;
}

// frame:SetAllPoints(relativeTo)
static int LF_SetAllPoints(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) return 0;
    const bool Region=TableRepresentsRegion(L,1); const int64 H=Region?GetMouseQueryHandle(L):GetFrameHandle(L);
    const auto* Def=C->FrameManager->GetFrameDef(H); if(!Def) return 0;
    FString Relative=Region?Def->Name:Def->Parent;
    if(lua_isstring(L,2)) Relative=UTF8_TO_TCHAR(lua_tostring(L,2));
    else if(lua_istable(L,2)) { lua_getfield(L,2,"__name"); Relative=UTF8_TO_TCHAR(luaL_optstring(L,-1,"")); lua_pop(L,1); }
    Relative.ReplaceInline(TEXT("$parent"),Region?*Def->Name:*Def->Parent);
    FWowAnchor Top,Bottom; Top.Point=Top.RelativePoint=EWowAnchorPoint::TOPLEFT; Top.RelativeTo=Relative;
    Bottom.Point=Bottom.RelativePoint=EWowAnchorPoint::BOTTOMRIGHT; Bottom.RelativeTo=Relative;
    if(Region)
    {
        lua_getfield(L,1,"__name"); const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,-1,"")); lua_pop(L,1);
        C->FrameManager->SetRegionPoint(Name,FWowAnchor(),true); C->FrameManager->SetRegionPoint(Name,Top); C->FrameManager->SetRegionPoint(Name,Bottom);
    }
    else C->FrameManager->SetFrameAnchors(H,{Top,Bottom});
    return 0;
}

// frame:SetSize(width, height)
static int LF_SetSize(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		float W = static_cast<float>(luaL_checknumber(L, 2));
		float H = static_cast<float>(luaL_checknumber(L, 3));
		if(TableRepresentsRegion(L,1)) { lua_getfield(L,1,"__name"); Ctx->FrameManager->SetRegionSize(UTF8_TO_TCHAR(lua_tostring(L,-1)?lua_tostring(L,-1):""),W,H); lua_pop(L,1); return 0; }
		Ctx->FrameManager->SetFrameSize(Handle, W, H);
	}
	return 0;
}

// frame:SetWidth(width)
static int LF_SetWidth(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		float W = static_cast<float>(luaL_checknumber(L, 2));
		if(TableRepresentsRegion(L,1)) { lua_getfield(L,1,"__name"); Ctx->FrameManager->SetRegionSize(UTF8_TO_TCHAR(lua_tostring(L,-1)?lua_tostring(L,-1):""),W,-1); lua_pop(L,1); return 0; }
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		float H = Def ? Def->Height : 0.f;
		Ctx->FrameManager->SetFrameSize(Handle, W, H);
	}
	return 0;
}

// frame:SetHeight(height)
static int LF_SetHeight(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		float H = static_cast<float>(luaL_checknumber(L, 2));
		if(TableRepresentsRegion(L,1)) { lua_getfield(L,1,"__name"); Ctx->FrameManager->SetRegionSize(UTF8_TO_TCHAR(lua_tostring(L,-1)?lua_tostring(L,-1):""),-1,H); lua_pop(L,1); return 0; }
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		float W = Def ? Def->Width : 0.f;
		Ctx->FrameManager->SetFrameSize(Handle, W, H);
	}
	return 0;
}

// frame:GetWidth()
static int LF_GetWidth(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		if(TableRepresentsRegion(L,1)) { lua_getfield(L,1,"__name"); const auto S=Ctx->FrameManager->GetRegionSize(UTF8_TO_TCHAR(lua_tostring(L,-1)?lua_tostring(L,-1):"")); lua_pop(L,1); lua_pushnumber(L,S.X); return 1; }
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		if (Def)
		{
			lua_pushnumber(L, Def->Anchors.IsEmpty()?Def->Width:Ctx->FrameManager->GetLayoutRect(Handle).Z / Ctx->FrameManager->GetEffectiveFrameScale(Handle));
			return 1;
		}
	}
	lua_pushnumber(L, 0);
	return 1;
}

// frame:GetHeight()
static int LF_GetHeight(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		if(TableRepresentsRegion(L,1)) { lua_getfield(L,1,"__name"); const auto S=Ctx->FrameManager->GetRegionSize(UTF8_TO_TCHAR(lua_tostring(L,-1)?lua_tostring(L,-1):"")); lua_pop(L,1); lua_pushnumber(L,S.Y); return 1; }
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		if (Def)
		{
			lua_pushnumber(L, Def->Anchors.IsEmpty()?Def->Height:Ctx->FrameManager->GetLayoutRect(Handle).W / Ctx->FrameManager->GetEffectiveFrameScale(Handle));
			return 1;
		}
	}
	lua_pushnumber(L, 0);
	return 1;
}

// frame:SetAlpha(alpha)
static int LF_SetAlpha(lua_State* L)
{
	if (TableRepresentsRegion(L, 1))
	{
		float Alpha = static_cast<float>(luaL_checknumber(L, 2));
		if (UImage* Image = GetTextureImage(L, 1))
		{
			Image->SetRenderOpacity(Alpha);
		}
		else if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
		{
			TextBlock->SetRenderOpacity(Alpha);
		}

		lua_pushnumber(L, Alpha);
		lua_setfield(L, 1, "__alpha");
		return 0;
	}

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		float Alpha = static_cast<float>(luaL_checknumber(L, 2));
		Ctx->FrameManager->SetFrameAlpha(Handle, Alpha);
		// Store alpha on frame table for GetAlpha
		lua_pushnumber(L, Alpha);
		lua_setfield(L, 1, "__alpha");
	}
	return 0;
}

// frame:GetAlpha()
static int LF_GetAlpha(lua_State* L)
{
	lua_getfield(L, 1, "__alpha");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushnumber(L, 1.0);
	}
	return 1;
}

// Parse strata string
static EWowFrameStrata ParseFrameStrata(const char* Str)
{
	if (!Str) return EWowFrameStrata::MEDIUM;
	if (strcmp(Str, "BACKGROUND") == 0) return EWowFrameStrata::BACKGROUND;
	if (strcmp(Str, "LOW") == 0) return EWowFrameStrata::LOW;
	if (strcmp(Str, "MEDIUM") == 0) return EWowFrameStrata::MEDIUM;
	if (strcmp(Str, "HIGH") == 0) return EWowFrameStrata::HIGH;
	if (strcmp(Str, "DIALOG") == 0) return EWowFrameStrata::DIALOG;
	if (strcmp(Str, "FULLSCREEN") == 0) return EWowFrameStrata::FULLSCREEN;
	if (strcmp(Str, "FULLSCREEN_DIALOG") == 0) return EWowFrameStrata::FULLSCREEN_DIALOG;
	if (strcmp(Str, "TOOLTIP") == 0) return EWowFrameStrata::TOOLTIP;
	return EWowFrameStrata::MEDIUM;
}

static const char* StrataToString(EWowFrameStrata S)
{
	switch (S)
	{
	case EWowFrameStrata::BACKGROUND: return "BACKGROUND";
	case EWowFrameStrata::LOW: return "LOW";
	case EWowFrameStrata::MEDIUM: return "MEDIUM";
	case EWowFrameStrata::HIGH: return "HIGH";
	case EWowFrameStrata::DIALOG: return "DIALOG";
	case EWowFrameStrata::FULLSCREEN: return "FULLSCREEN";
	case EWowFrameStrata::FULLSCREEN_DIALOG: return "FULLSCREEN_DIALOG";
	case EWowFrameStrata::TOOLTIP: return "TOOLTIP";
	default: return "MEDIUM";
	}
}

// frame:SetFrameStrata(strata)
static int LF_SetFrameStrata(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		const char* StrataStr = luaL_checkstring(L, 2);
		Ctx->FrameManager->SetFrameStrata(Handle, ParseFrameStrata(StrataStr));
	}
	return 0;
}

// frame:GetFrameStrata()
static int LF_GetFrameStrata(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		if (Def)
		{
			lua_pushstring(L, StrataToString(Def->Strata));
			return 1;
		}
	}
	lua_pushstring(L, "MEDIUM");
	return 1;
}

// frame:SetFrameLevel(level)
static int LF_SetFrameLevel(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		int32 Level = static_cast<int32>(luaL_checknumber(L, 2));
		Ctx->FrameManager->SetFrameLevel(Handle, Level);
	}
	return 0;
}

// frame:GetFrameLevel()
static int LF_GetFrameLevel(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		if (Def)
		{
			lua_pushnumber(L, Def->FrameLevel);
			return 1;
		}
	}
	lua_pushnumber(L, 0);
	return 1;
}

// frame:EnableMouse(enable)
static int LF_EnableMouse(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = GetFrameHandle(L);
	const bool bEnable = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
	if (Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->SetFrameMouseEnabled(Handle, bEnable);
	}
	return 0;
}

// frame:EnableKeyboard(enable)
static int LF_EnableKeyboard(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = GetFrameHandle(L);
	const bool bEnable = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
	if (Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->SetFrameKeyboardEnabled(Handle, bEnable);
	}
	return 0;
}

// frame:EnableMouseWheel(enable)
static int LF_EnableMouseWheel(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = GetFrameHandle(L);
	const bool bEnable = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
	if (Ctx && Ctx->FrameManager)
	{
		Ctx->FrameManager->SetFrameMouseWheelEnabled(Handle, bEnable);
	}
	return 0;
}

// frame:SetMovable(movable)
static int LF_SetMovable(lua_State* L)
{
	auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) if(auto* D=C->FrameManager->GetMutableFrameDef(GetFrameHandle(L))) D->bMovable=lua_isnoneornil(L,2) || lua_toboolean(L,2)!=0;
	return 0;
}

// frame:SetResizable(resizable)
static int LF_SetResizable(lua_State* L)
{
	auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) if(auto* D=C->FrameManager->GetMutableFrameDef(GetFrameHandle(L))) D->bResizable=lua_isnoneornil(L,2) || lua_toboolean(L,2)!=0;
	return 0;
}

// frame:SetClampedToScreen(clamped)
static int LF_SetClampedToScreen(lua_State* L)
{
	auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) if(auto* D=C->FrameManager->GetMutableFrameDef(GetFrameHandle(L))) { D->bClampedToScreen=lua_isnoneornil(L,2) || lua_toboolean(L,2)!=0; C->FrameManager->InvalidateTextLayout(); }
	return 0;
}

// frame:SetParent(parent)
static int LF_SetParent(lua_State* L)
{
	auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) return 0;
	int64 Parent=-1; if(lua_istable(L,2)) Parent=GetOptionalFrameHandle(L,2); else if(lua_isstring(L,2)) Parent=C->FrameManager->FindFrame(UTF8_TO_TCHAR(lua_tostring(L,2)));
	if(TableRepresentsRegion(L,1))
	{
		lua_getfield(L,1,"__name"); const FString Name=UTF8_TO_TCHAR(lua_tostring(L,-1)?lua_tostring(L,-1):""); lua_pop(L,1);
		if(C->FrameManager->ReparentRegion(Name,Parent)) { lua_pushinteger(L,Parent); lua_setfield(L,1,"__parentHandle"); } return 0;
	}
	const int64 Handle=GetOptionalFrameHandle(L); if(Handle<0) return 0;
	C->FrameManager->ReparentFrame(Handle,Parent);
	return 0;
}

// frame:GetParent()
static int LF_GetParent(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetOptionalFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 ParentHandle=Ctx->FrameManager->GetParentHandle(Handle);
		if(Handle<0) { lua_getfield(L,1,"__parentHandle"); ParentHandle=lua_isnumber(L,-1)?lua_tointeger(L,-1):-1; lua_pop(L,1); }
		if(Ctx->EventSystem && Ctx->EventSystem->PushFrameObject(ParentHandle)) return 1;
	}
	lua_pushnil(L);
	return 1;
}

// frame:GetChildren()
static int LF_GetChildren(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		TArray<int64> Children = Ctx->FrameManager->GetChildHandles(Handle);
		int Count = 0;
		for (int64 ChildHandle : Children)
		{
			if(Ctx->EventSystem && Ctx->EventSystem->PushFrameObject(ChildHandle)) ++Count;
		}
		return Count;
	}
	// Push nil when FrameManager is null to maintain stack balance
	lua_pushnil(L);
	return 1;
}

// frame:GetNumChildren()
static int LF_GetNumChildren(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		TArray<int64> Children = Ctx->FrameManager->GetChildHandles(Handle);
		lua_pushnumber(L, Children.Num());
		return 1;
	}
	lua_pushnumber(L, 0);
	return 1;
}

// Helper: get the UImage* from a texture Lua table (either named or dynamic)
static UImage* GetTextureImage(lua_State* L, int idx)
{
	// Try dynamic image widget first (from CreateTexture + SetTexture)
	lua_getfield(L, idx, "__imageWidget");
	if (lua_islightuserdata(L, -1))
	{
		UImage* Img = static_cast<UImage*>(lua_touserdata(L, -1));
		lua_pop(L, 1);
		return Img;
	}
	lua_pop(L, 1);

	// Try named texture region (from XML <Texture name="...">)
	lua_getfield(L, idx, "__name");
	if (lua_isstring(L, -1))
	{
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		if (Ctx && Ctx->FrameManager)
		{
			UImage* Img = Ctx->FrameManager->GetNamedTexture(UTF8_TO_TCHAR(lua_tostring(L, -1)));
			lua_pop(L, 1);
			return Img;
		}
	}
	lua_pop(L, 1);

	return nullptr;
}

static bool TextureCoordinates(lua_State* L,int32 Index,double V[8])
{
    Index=Index<0?lua_gettop(L)+Index+1:Index;
    double Rect[4]={0,1,0,1}; const char* Keys[]={"__texLeft","__texRight","__texTop","__texBottom"};
    for(int32 I=0;I<4;++I) { lua_getfield(L,Index,Keys[I]); Rect[I]=luaL_optnumber(L,-1,Rect[I]); lua_pop(L,1); }
    const int32 Order[]={0,2,0,3,1,2,1,3}; for(int32 I=0;I<8;++I) V[I]=Rect[Order[I]];
    lua_getfield(L,Index,"__texCoords"); const bool Arbitrary=lua_istable(L,-1)!=0;
    if(Arbitrary) for(int32 I=0;I<8;++I) { lua_rawgeti(L,-1,I+1); V[I]=luaL_optnumber(L,-1,V[I]); lua_pop(L,1); }
    lua_pop(L,1); return Arbitrary;
}
static bool ApplyTextureStyle(lua_State* L,int32 Index=1)
{
    auto* Image=GetTextureImage(L,Index); if(!Image) return false;
    auto Brush=Image->GetBrush(); auto* Texture=Cast<UTexture>(Brush.GetResourceObject());
    auto* Instance=Cast<UMaterialInstanceDynamic>(Brush.GetResourceObject());
    if(!Texture && Instance) Texture=Instance->K2_GetTextureParameterValue(TEXT("Image"));
    if(!Texture) return false;
    double UV[8]; const bool Arbitrary=TextureCoordinates(L,Index,UV);
    lua_getfield(L,Index,"__desaturated"); const bool Gray=lua_toboolean(L,-1)!=0; lua_pop(L,1);
    lua_getfield(L,Index,"__blendMode"); const bool Additive=lua_isstring(L,-1) && FCStringAnsi::Stricmp(lua_tostring(L,-1),"ADD")==0; lua_pop(L,1);
    const FString Parent=Instance && Instance->Parent?Instance->Parent->GetName():FString();
    const bool Shared=Parent==TEXT("M_WowUiRegion") || Parent==TEXT("M_WowUiRegionAdd");
    float Circle=0,Opaque=Parent==TEXT("M_WowUiImage")?1:0;
    if(Instance) { Instance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Circle")),Circle); if(Shared) Instance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OpaqueImage")),Opaque); }
    if(Gray || Arbitrary || Additive || (Shared && (Circle>0 || Opaque>0)))
    {
        const TCHAR* Path=Additive?TEXT("/Game/Materials/M_WowUiRegionAdd.M_WowUiRegionAdd"):TEXT("/Game/Materials/M_WowUiRegion.M_WowUiRegion");
        auto* Base=LoadObject<UMaterialInterface>(nullptr,Path); if(!Base) return false;
        auto* Material=Instance && Instance->Parent==Base?Instance:UMaterialInstanceDynamic::Create(Base,Image);
        Material->SetTextureParameterValue(TEXT("Image"),Texture); Material->SetScalarParameterValue(TEXT("Desaturation"),Gray?1:0);
        Material->SetScalarParameterValue(TEXT("Circle"),Circle); Material->SetScalarParameterValue(TEXT("OpaqueImage"),Opaque);
        const TCHAR* Corners[]={TEXT("UVTL"),TEXT("UVBL"),TEXT("UVTR"),TEXT("UVBR")};
        for(int32 I=0;I<4;++I) Material->SetVectorParameterValue(Corners[I],FLinearColor(UV[I*2],UV[I*2+1],0,0));
        Brush.SetResourceObject(Material); Brush.SetUVRegion(FBox2D(FVector2D::ZeroVector,FVector2D(1,1))); Image->SetRenderTransform(FWidgetTransform());
    }
    else
    {
        if(!Instance || Shared) Brush.SetResourceObject(Texture); Brush.SetUVRegion(FBox2D(FVector2D(FMath::Min(UV[0],UV[4]),FMath::Min(UV[1],UV[3])),FVector2D(FMath::Max(UV[0],UV[4]),FMath::Max(UV[1],UV[3]))));
        Image->SetRenderTransformPivot(FVector2D(.5,.5)); Image->SetRenderTransform(FWidgetTransform(FVector2D::ZeroVector,FVector2D(UV[0]>UV[4]?-1:1,UV[1]>UV[3]?-1:1),FVector2D::ZeroVector,0));
    }
    Image->SetBrush(Brush); return true;
}
static int LF_TextureDesaturated(lua_State* L)
{
    const bool Value=lua_toboolean(L,2)!=0 && (!lua_isnumber(L,2) || lua_tonumber(L,2)!=0);
    lua_pushboolean(L,Value); lua_setfield(L,1,"__desaturated"); ApplyTextureStyle(L); lua_pushboolean(L,1); return 1;
}
static int LF_TextureBlendMode(lua_State* L)
{
    const char* Mode=luaL_checkstring(L,2); lua_pushstring(L,Mode); lua_setfield(L,1,"__blendMode"); ApplyTextureStyle(L); return 0;
}

// Texture region methods
static int LF_TextureSetTexture(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	if (lua_isnumber(L, 2) && lua_isnumber(L, 3) && lua_isnumber(L, 4))
	{
		if (auto* Image = GetTextureImage(L, 1))
		{
			FSlateBrush Brush;
			Brush.DrawAs = ESlateBrushDrawType::Box;
			Brush.ImageSize = FVector2D(1, 1);
			Brush.TintColor = FSlateColor(FLinearColor(lua_tonumber(L,2), lua_tonumber(L,3), lua_tonumber(L,4), luaL_optnumber(L,5,1)));
			Image->SetBrush(Brush); Image->SetRenderOpacity(1);
		}
		return 0;
	}
	// WoW allows SetTexture(nil) to clear the texture
	if (lua_isnil(L, 2) || lua_isnone(L, 2) || (lua_type(L,2)==LUA_TSTRING && lua_objlen(L,2)==0))
	{
		lua_pushnil(L);
		lua_setfield(L, 1, "__texture");
		if (auto* Image = GetTextureImage(L, 1))
		{
			FSlateBrush Empty;
			Empty.DrawAs = ESlateBrushDrawType::NoDrawType;
			Image->SetBrush(Empty);
			Image->SetRenderOpacity(0);
		}
		return 0;
	}
	// Also handle number arg (texture ID) — convert to string
	const char* TexturePath;
	if (lua_isnumber(L, 2))
	{
		lua_pushfstring(L, "%d", (int)lua_tonumber(L, 2));
		TexturePath = lua_tostring(L, -1);
		lua_pop(L, 1);
	}
	else
	{
		TexturePath = luaL_checkstring(L, 2);
	}

	// Store in Lua table
	lua_pushstring(L, TexturePath);
	lua_setfield(L, 1, "__texture");

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		// First, try to find this texture region by name (from XML Texture name="Foo")
		lua_getfield(L, 1, "__name");
		const char* TexName = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
		lua_pop(L, 1);

		UImage* NamedImage = TexName ? Ctx->FrameManager->GetNamedTexture(UTF8_TO_TCHAR(TexName)) : nullptr;

		UTexture2D* Texture = Ctx->FrameManager->LoadUITexture(UTF8_TO_TCHAR(TexturePath));
		if (Texture && NamedImage)
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(Texture);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			Brush.Tiling = ESlateBrushTileType::NoTile;

			// Apply stored UV coordinates if any, handling WoW's Left>Right flip convention
			lua_getfield(L, 1, "__texLeft");
			if (lua_isnumber(L, -1))
			{
				float UVLeft = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
				lua_getfield(L, 1, "__texRight"); float UVRight = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
				lua_getfield(L, 1, "__texTop"); float UVTop = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
				lua_getfield(L, 1, "__texBottom"); float UVBottom = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);

				float NL = FMath::Min(UVLeft, UVRight), NR = FMath::Max(UVLeft, UVRight);
				float NT = FMath::Min(UVTop, UVBottom), NB = FMath::Max(UVTop, UVBottom);
				FBox2D UVRegion(FVector2D(NL, NT), FVector2D(NR, NB));
				Brush.SetUVRegion(UVRegion);

				bool bFlipH = UVLeft > UVRight;
				bool bFlipV = UVTop > UVBottom;
				if (bFlipH || bFlipV)
				{
					FVector2D Scale(bFlipH ? -1.0f : 1.0f, bFlipV ? -1.0f : 1.0f);
					NamedImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
					NamedImage->SetRenderTransform(FWidgetTransform(FVector2D::ZeroVector, Scale, FVector2D::ZeroVector, 0.0f));
				}
			}
			else
			{
				lua_pop(L, 1);
			}

			NamedImage->SetBrush(Brush);
			lua_getfield(L,1,"__alpha"); NamedImage->SetRenderOpacity(luaL_optnumber(L,-1,1)); lua_pop(L,1);

            // Stretched canvas slots store edge offsets, not a requested size.
            // Resolve natural dimensions through region layout; changing those
            // offsets here can turn a 37px equipment icon into negative geometry.
            ApplyTextureStyle(L); Ctx->FrameManager->InvalidateTextLayout(); return 0;
		}

		// Fallback: try parent handle approach (dynamic textures from CreateTexture)
		lua_getfield(L, 1, "__parentHandle");
		if (lua_isnumber(L, -1))
		{
			int64 ParentHandle = static_cast<int64>(lua_tointeger(L, -1));
			UWidget* ParentWidget = Ctx->FrameManager->GetWidgetForHandle(ParentHandle);

			if (!Texture)
				Texture = Ctx->FrameManager->LoadUITexture(UTF8_TO_TCHAR(TexturePath));
			if (Texture)
			{
				FSlateBrush Brush;
				Brush.SetResourceObject(Texture);
				Brush.DrawAs = ESlateBrushDrawType::Image;
				Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
				Brush.Tiling = ESlateBrushTileType::NoTile;

				// Apply stored UV coordinates if any, handling WoW flip convention
				bool bDynFlipH = false, bDynFlipV = false;
				lua_getfield(L, 1, "__texLeft");
				if (lua_isnumber(L, -1))
				{
					float UVLeft = static_cast<float>(lua_tonumber(L, -1));
					lua_pop(L, 1);
					lua_getfield(L, 1, "__texRight"); float UVRight = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
					lua_getfield(L, 1, "__texTop"); float UVTop = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
					lua_getfield(L, 1, "__texBottom"); float UVBottom = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);

					bDynFlipH = UVLeft > UVRight;
					bDynFlipV = UVTop > UVBottom;
					float NL = FMath::Min(UVLeft, UVRight), NR = FMath::Max(UVLeft, UVRight);
					float NT = FMath::Min(UVTop, UVBottom), NB = FMath::Max(UVTop, UVBottom);
					FBox2D UVRegion(FVector2D(NL, NT), FVector2D(NR, NB));
					Brush.SetUVRegion(UVRegion);
				}
				else
				{
					lua_pop(L, 1);
				}

				if (UButton* Button = Cast<UButton>(ParentWidget))
				{
					FButtonStyle ButtonStyle = Button->GetStyle();
					ButtonStyle.Normal = Brush;
					Button->SetStyle(ButtonStyle);
				}
				else if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(ParentWidget))
				{
					// Check if we already have an image widget for this texture
					UImage* Img = nullptr;
					lua_getfield(L, 1, "__imageWidget");
					if (lua_islightuserdata(L, -1))
					{
						Img = static_cast<UImage*>(lua_touserdata(L, -1));
					}
					lua_pop(L, 1);

					if (!Img)
					{
						Img = NewObject<UImage>(Canvas);
						UCanvasPanelSlot* ImgSlot = Canvas->AddChildToCanvas(Img);
						if (ImgSlot)
						{
							// Check if SetAllPoints was called
							lua_getfield(L, 1, "__setAllPoints");
							bool bFillParent = lua_toboolean(L, -1) != 0;
							lua_pop(L, 1);

							if (bFillParent)
							{
								ImgSlot->SetAnchors(FAnchors(0, 0, 1, 1));
								ImgSlot->SetOffsets(FMargin(0));
							}
							else
							{
								// Check for stored size
								lua_getfield(L, 1, "__width");
								float W = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.f;
								lua_pop(L, 1);
								lua_getfield(L, 1, "__height");
								float H = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.f;
								lua_pop(L, 1);

								float UIScale = Ctx->FrameManager->GetUIScale();
								ImgSlot->SetAnchors(FAnchors(0.0f, 0.0f));
								ImgSlot->SetAlignment(FVector2D(0.0f, 0.0f));

								if (W > 0.f && H > 0.f)
								{
									ImgSlot->SetSize(FVector2D(W * UIScale, H * UIScale));
								}
								else
								{
									// Use texture natural size as default
									ImgSlot->SetSize(FVector2D(Texture->GetSizeX() * UIScale, Texture->GetSizeY() * UIScale));
								}

								// Apply stored position
								lua_getfield(L, 1, "__posX");
								float PosX = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.f;
								lua_pop(L, 1);
								lua_getfield(L, 1, "__posY");
								float PosY = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 0.f;
								lua_pop(L, 1);
								ImgSlot->SetPosition(FVector2D(PosX * UIScale, PosY * UIScale));
							}
						}

						// Store image widget reference
						lua_pushlightuserdata(L, Img);
						lua_setfield(L, 1, "__imageWidget");
					}

					Img->SetBrush(Brush);

					// Apply stored vertex color
					lua_getfield(L, 1, "__colorR");
					if (lua_isnumber(L, -1))
					{
						float R = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
						lua_getfield(L, 1, "__colorG"); float G = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
						lua_getfield(L, 1, "__colorB"); float B = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
						lua_getfield(L, 1, "__colorA"); float A = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 1.0f; lua_pop(L, 1);
						Img->SetColorAndOpacity(FLinearColor(R, G, B, A));
					}
					else
					{
						lua_pop(L, 1);
					}

					// Apply stored alpha
					lua_getfield(L, 1, "__alpha");
					if (lua_isnumber(L, -1))
					{
						float Alpha = static_cast<float>(lua_tonumber(L, -1));
						Img->SetRenderOpacity(Alpha);
					}
					lua_pop(L, 1);
				}
			}
		}
		lua_pop(L, 1);
	}

	return 0;
}

static int LF_TextureSetTexCoord(lua_State* L)
{
    luaL_checktype(L,1,LUA_TTABLE); const int32 Count=lua_gettop(L)-1;
    if(Count!=4 && Count!=8) return luaL_error(L,"SetTexCoord expects 4 or 8 coordinates");
    for(int32 I=2;I<=Count+1;++I) luaL_checknumber(L,I);
    if(Count==8)
    {
        lua_createtable(L,8,0); for(int32 I=0;I<8;++I) { lua_pushvalue(L,I+2); lua_rawseti(L,-2,I+1); } lua_setfield(L,1,"__texCoords");
    }
    else
    {
        lua_pushnil(L); lua_setfield(L,1,"__texCoords"); const char* Keys[]={"__texLeft","__texRight","__texTop","__texBottom"};
        for(int32 I=0;I<4;++I) { lua_pushvalue(L,I+2); lua_setfield(L,1,Keys[I]); }
    }
    ApplyTextureStyle(L); return 0;
}

static int LF_TextureSetVertexColor(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float R = static_cast<float>(luaL_checknumber(L, 2));
	float G = static_cast<float>(luaL_checknumber(L, 3));
	float B = static_cast<float>(luaL_checknumber(L, 4));
	float A = lua_isnumber(L, 5) ? static_cast<float>(lua_tonumber(L, 5)) : 1.0f;

	// Store in Lua table
	lua_pushnumber(L, R); lua_setfield(L, 1, "__colorR");
	lua_pushnumber(L, G); lua_setfield(L, 1, "__colorG");
	lua_pushnumber(L, B); lua_setfield(L, 1, "__colorB");
	lua_pushnumber(L, A); lua_setfield(L, 1, "__colorA");

	// Apply color tinting to the texture
	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		Img->SetColorAndOpacity(FLinearColor(R, G, B, A));
	}

	return 0;
}

static int LF_SetVertexColorDispatch(lua_State* L)
{
	if (GetTextureImage(L, 1))
	{
		return LF_TextureSetVertexColor(L);
	}

	return LF_FontStringSetTextColor(L);
}

// All dynamic regions share the XML layout/visibility bridge.
static int LF_CreateTexture(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) { lua_pushnil(L); return 1; }
    const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,2,""));
    const FString Layer=UTF8_TO_TCHAR(luaL_optstring(L,3,"ARTWORK")),Inherits=UTF8_TO_TCHAR(luaL_optstring(L,4,""));
    const FString Key=C->FrameManager->CreateTextureRegion(GetFrameHandle(L),Name,Layer,Inherits);
    if(Key.IsEmpty()) { lua_pushnil(L); return 1; }
    lua_getglobal(L,TCHAR_TO_UTF8(*Key));
    if(Name.IsEmpty()) { lua_pushboolean(L,1); lua_setfield(L,-2,"__anonymous"); }
    return 1;
}

// FontString methods
static int LF_FontStringSetText(lua_State* L)
{
	return LF_SetText(L);
}

static int LF_FontStringGetText(lua_State* L)
{
	return LF_GetText(L);
}

static int LF_FontStringSetTextColor(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float R = static_cast<float>(luaL_checknumber(L, 2));
	float G = static_cast<float>(luaL_checknumber(L, 3));
	float B = static_cast<float>(luaL_checknumber(L, 4));
	float A = lua_isnumber(L, 5) ? static_cast<float>(lua_tonumber(L, 5)) : 1.0f;

	// Store in Lua table
	lua_pushnumber(L, R); lua_setfield(L, 1, "__textColorR");
	lua_pushnumber(L, G); lua_setfield(L, 1, "__textColorG");
	lua_pushnumber(L, B); lua_setfield(L, 1, "__textColorB");
	lua_pushnumber(L, A); lua_setfield(L, 1, "__textColorA");

	// Apply to widget
	if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
	{
		FLinearColor TextColor(R, G, B, A);
		TextBlock->SetColorAndOpacity(FSlateColor(TextColor));
		if(auto* Rich=Cast<UWowTextBlock>(TextBlock)) Rich->RefreshRichStyle();
	}
    if(auto* Edit=GetEditBoxWidgetForTable(L)) Edit->SetForegroundColor(FLinearColor(R,G,B,A));
    if(auto* Edit=GetMultiLineEditor(L)) { auto Style=Edit->WidgetStyle; Style.SetColorAndOpacity(FLinearColor(R,G,B,A)); Edit->SetWidgetStyle(Style); }

	return 0;
}

// fontstring:SetFont(fontPath, size, flags)
static int LF_FontStringSetFont(lua_State* L)
{
	// Store font info in the Lua table for later retrieval
	const char* FontPath = luaL_optstring(L, 2, "Fonts\\FRIZQT__.TTF");
	float FontSize = static_cast<float>(luaL_optnumber(L, 3, 12.0));
	const char* Flags = luaL_optstring(L, 4, "");

	lua_pushstring(L, FontPath);
	lua_setfield(L, 1, "__fontPath");
	lua_pushnumber(L, FontSize);
	lua_setfield(L, 1, "__fontSize");
	lua_pushstring(L, Flags);
	lua_setfield(L, 1, "__fontFlags");

	if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
	{
		FSlateFontInfo FontInfo = TextBlock->GetFont();
		const auto* C=WowLuaApi::GetContext(L); FontInfo.Size = FMath::Max(1,FMath::RoundToInt(FontSize*.75f*(C && C->FrameManager?C->FrameManager->GetUIScale():1)));
		if(C && C->FrameManager) if(auto* Fonts=C->FrameManager->GetFontManager(); Fonts && Fonts->IsInitialized()) FontInfo=Fonts->GetFont(UTF8_TO_TCHAR(FontPath),FMath::RoundToInt(FontSize*C->FrameManager->GetUIScale()));
		TextBlock->SetFont(FontInfo);
	}
	const auto* C=WowLuaApi::GetContext(L);
    const auto FontForEditor=[&](FSlateFontInfo Info)
    {
        const float Scale=C && C->FrameManager?C->FrameManager->GetUIScale():1;
        Info.Size=FMath::Max(1,FMath::RoundToInt(FontSize*.75f*Scale));
        if(C && C->FrameManager) if(const auto* Fonts=C->FrameManager->GetFontManager(); Fonts && Fonts->IsInitialized()) Info=Fonts->GetFont(UTF8_TO_TCHAR(FontPath),FMath::RoundToInt(FontSize*Scale));
        return Info;
    };
    if(auto* Edit=GetEditBoxWidgetForTable(L)) { auto Style=Edit->GetWidgetStyle(); Style.SetFont(FontForEditor(Style.TextStyle.Font)); Edit->SetWidgetStyle(Style); }
    if(auto* Edit=GetMultiLineEditor(L)) { auto Style=Edit->WidgetStyle; Style.SetFont(FontForEditor(Style.Font)); Edit->SetWidgetStyle(Style); }

	return 0;
}

// fontstring:GetFont() -> fontPath, size, flags
static int LF_FontStringGetFont(lua_State* L)
{
	lua_getfield(L, 1, "__fontPath");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushstring(L, "Fonts\\FRIZQT__.TTF");
	}

	lua_getfield(L, 1, "__fontSize");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushnumber(L, 12.0);
	}

	lua_getfield(L, 1, "__fontFlags");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushstring(L, "");
	}

	return 3;
}

// fontstring:GetStringWidth() -> approximate width
static int LF_FontStringGetStringWidth(lua_State* L)
{
	if(auto* TextBlock=GetTextBlockWidgetForTable(L,1))
	{
        const float Wrap=TextBlock->GetWrapTextAt(); const bool Auto=TextBlock->GetAutoWrapText(); TextBlock->SetAutoWrapText(false); TextBlock->SetWrapTextAt(0);
		if(auto* Rich=Cast<UWowTextBlock>(TextBlock)) Rich->RefreshRichStyle();
		TextBlock->TakeWidget()->Invalidate(EInvalidateWidgetReason::Prepass); TextBlock->ForceLayoutPrepass();
        const float Width=TextBlock->GetDesiredSize().X; TextBlock->SetWrapTextAt(Wrap); TextBlock->SetAutoWrapText(Auto);
		const auto* C=WowLuaApi::GetContext(L); const float Scale=C && C->FrameManager?C->FrameManager->GetUIScale():1;
		lua_pushnumber(L,Width/FMath::Max(Scale,.001f)); return 1;
	}
	FString Text;
	float FontSize = 12.0f;

	if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
	{
		Text = TextBlock->GetText().ToString();
		FontSize = static_cast<float>(TextBlock->GetFont().Size);
	}
	else
	{
		lua_getfield(L, 1, "__text");
		Text = lua_isstring(L, -1) ? UTF8_TO_TCHAR(lua_tostring(L, -1)) : TEXT("");
		lua_pop(L, 1);

		lua_getfield(L, 1, "__fontSize");
		FontSize = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : 12.0f;
		lua_pop(L, 1);
	}

	// Rough estimate: average character width is ~0.5 * font height
	float Width = static_cast<float>(Text.Len()) * FontSize * 0.5f;
	lua_pushnumber(L, Width);
	return 1;
}

static void InvalidateLabel(lua_State* L)
{
    if(auto* T=Cast<UWowTextBlock>(GetTextBlockWidgetForTable(L))) T->RefreshRichStyle();
    if(auto* C=WowLuaApi::GetContext(L); C && C->FrameManager) C->FrameManager->InvalidateTextLayout();
}
static int LF_Justify(lua_State* L,bool Vertical,bool Get)
{
    auto* T=GetTextBlockWidgetForTable(L); auto* Label=Cast<UWowTextBlock>(T);
    const char* Key=Vertical?"__justifyV":"__justifyH";
    if(Get)
    {
        if(Vertical && Label) lua_pushstring(L,TCHAR_TO_UTF8(*Label->GetVerticalJustification()));
        else if(!Vertical && Label) lua_pushstring(L,Label->GetHorizontalJustification()==ETextJustify::Left?"LEFT":Label->GetHorizontalJustification()==ETextJustify::Right?"RIGHT":"CENTER");
        else { lua_getfield(L,1,Key); if(lua_isnil(L,-1)) { lua_pop(L,1); lua_pushstring(L,Vertical?"MIDDLE":"CENTER"); } }
        return 1;
    }
    const FString Value=UTF8_TO_TCHAR(luaL_checkstring(L,2));
    if(Vertical ? (Value!=TEXT("TOP") && Value!=TEXT("MIDDLE") && Value!=TEXT("BOTTOM")) : (Value!=TEXT("LEFT") && Value!=TEXT("CENTER") && Value!=TEXT("RIGHT"))) return luaL_error(L,"Invalid text justification");
    lua_pushvalue(L,2); lua_setfield(L,1,Key);
    if(Vertical && Label) Label->SetVerticalJustification(Value);
    else if(T) T->SetJustification(Value==TEXT("LEFT")?ETextJustify::Left:Value==TEXT("RIGHT")?ETextJustify::Right:ETextJustify::Center);
    InvalidateLabel(L); return 0;
}
static int LF_TextWrap(lua_State* L,bool NonSpace,bool Get)
{
    auto* Label=Cast<UWowTextBlock>(GetTextBlockWidgetForTable(L)); const char* Key=NonSpace?"__nonSpaceWrap":"__wordWrap";
    if(Get)
    {
        bool Value=!NonSpace;
        if(Label) Value=NonSpace?Label->CanNonSpaceWrap():Label->CanWordWrap();
        else { lua_getfield(L,1,Key); if(!lua_isnil(L,-1)) Value=lua_toboolean(L,-1)!=0; lua_pop(L,1); }
        if(Value) lua_pushinteger(L,1); else lua_pushnil(L); return 1;
    }
    const bool Value=lua_isnoneornil(L,2) || (lua_toboolean(L,2)!=0 && (!lua_isnumber(L,2) || lua_tonumber(L,2)!=0));
    lua_pushboolean(L,Value); lua_setfield(L,1,Key);
    if(Label) { if(NonSpace) Label->SetNonSpaceWrapping(Value); else Label->SetWordWrapping(Value); }
    InvalidateLabel(L); return 0;
}
static int LF_TextSpacing(lua_State* L,bool Get)
{
    auto* Label=Cast<UWowTextBlock>(GetTextBlockWidgetForTable(L));
    if(Get) { if(Label) lua_pushnumber(L,Label->GetPixelSpacing()); else { lua_getfield(L,1,"__spacing"); if(!lua_isnumber(L,-1)) { lua_pop(L,1); lua_pushnumber(L,0); } } return 1; }
    const float Value=luaL_checknumber(L,2); lua_pushnumber(L,Value); lua_setfield(L,1,"__spacing"); if(Label) Label->SetPixelSpacing(Value); InvalidateLabel(L); return 0;
}
static int LF_TextShadow(lua_State* L,bool Offset,bool Get)
{
    auto* T=GetTextBlockWidgetForTable(L); const auto* C=WowLuaApi::GetContext(L); const float Scale=C && C->FrameManager?C->FrameManager->GetUIScale():1;
    const int32 Count=Offset?2:4; const char* Keys[]={Offset?"__shadowX":"__shadowR",Offset?"__shadowY":"__shadowG","__shadowB","__shadowA"};
    double Values[4]={0,0,0,1};
    if(Get && T) { if(Offset) { const auto V=T->GetShadowOffset()/Scale; Values[0]=V.X; Values[1]=-V.Y; } else { const auto V=T->GetShadowColorAndOpacity(); Values[0]=V.R; Values[1]=V.G; Values[2]=V.B; Values[3]=V.A; } }
    else for(int32 I=0;I<Count;++I)
    {
        if(Get) { lua_getfield(L,1,Keys[I]); Values[I]=luaL_optnumber(L,-1,Values[I]); lua_pop(L,1); }
        else { Values[I]=luaL_optnumber(L,I+2,Values[I]); lua_pushnumber(L,Values[I]); lua_setfield(L,1,Keys[I]); }
    }
    if(Get) { for(int32 I=0;I<Count;++I) lua_pushnumber(L,Values[I]); return Count; }
    if(T) { if(Offset) T->SetShadowOffset(FVector2D(Values[0],-Values[1])*Scale); else T->SetShadowColorAndOpacity(FLinearColor(Values[0],Values[1],Values[2],Values[3])); }
    InvalidateLabel(L); return 0;
}
static int LF_TextHeight(lua_State* L,bool Get)
{
    auto* T=GetTextBlockWidgetForTable(L); const auto* C=WowLuaApi::GetContext(L); const float Scale=C && C->FrameManager?C->FrameManager->GetUIScale():1;
    if(Get) { if(T) lua_pushnumber(L,T->GetFont().Size*4.f/(3*Scale)); else { lua_getfield(L,1,"__fontSize"); if(!lua_isnumber(L,-1)) { lua_pop(L,1); lua_pushnumber(L,12); } } return 1; }
    const float Height=luaL_checknumber(L,2); if(!FMath::IsFinite(Height) || Height<=0) return luaL_error(L,"Text height must be positive");
    lua_pushnumber(L,Height); lua_setfield(L,1,"__fontSize");
    if(T) { auto Info=T->GetFont(); Info.Size=FMath::Max(1,FMath::RoundToInt(Height*Scale*.75f)); T->SetFont(Info); }
    InvalidateLabel(L); return 0;
}
static int LF_DrawLayer(lua_State* L,bool Get)
{
    if(Get) { lua_getfield(L,1,"__drawLayer"); if(lua_isnil(L,-1)) { lua_pop(L,1); lua_pushstring(L,"ARTWORK"); } lua_getfield(L,1,"__subLevel"); if(!lua_isnumber(L,-1)) { lua_pop(L,1); lua_pushinteger(L,0); } return 2; }
    const FString Layer=UTF8_TO_TCHAR(luaL_checkstring(L,2)); const TCHAR* Names[]={TEXT("BACKGROUND"),TEXT("BORDER"),TEXT("ARTWORK"),TEXT("OVERLAY"),TEXT("HIGHLIGHT")};
    int32 Index=-1; for(int32 I=0;I<5;++I) if(Layer==Names[I]) Index=I;
    if(Index<0) return luaL_error(L,"Invalid draw layer");
    const int32 Sub=FMath::Clamp(int32(luaL_optinteger(L,3,0)),-8,7);
    lua_pushvalue(L,2); lua_setfield(L,1,"__drawLayer"); lua_pushinteger(L,Sub); lua_setfield(L,1,"__subLevel");
    UWidget* W=GetTextureImage(L); if(!W) W=GetTextBlockWidgetForTable(L);
    if(W) if(auto* Slot=Cast<UCanvasPanelSlot>(W->Slot)) Slot->SetZOrder(Index*100+Sub);
    return 0;
}
static int LF_GetTexCoord(lua_State* L)
{
    double UV[8]; TextureCoordinates(L,1,UV); for(const double Value:UV) lua_pushnumber(L,Value); return 8;
}

// frame:CreateFontString(name, layer, inherits)
static int LF_CreateFontString(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) { lua_pushnil(L); return 1; }
    const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,2,"")),Inherits=UTF8_TO_TCHAR(luaL_optstring(L,4,"GameFontNormal"));
    const FString Key=C->FrameManager->CreateFontStringRegion(GetFrameHandle(L),Name,Inherits);
    if(Key.IsEmpty()) { lua_pushnil(L); return 1; }
    lua_getglobal(L,TCHAR_TO_UTF8(*Key));
    if(Name.IsEmpty()) { lua_pushboolean(L,1); lua_setfield(L,-2,"__anonymous"); }
    lua_getfield(L,-1,"SetFontObject"); lua_pushvalue(L,-2); lua_pushstring(L,TCHAR_TO_UTF8(*Inherits)); lua_call(L,2,0);
    return 1;
}
// Global CreateTexture(name, parent, drawLayer, inherits, subLevel) - WoW 3.3.5 API
static int LF_CreateTextureGlobal(lua_State* L)
{
    lua_settop(L,4);
    if(lua_isnoneornil(L,2)) { lua_getglobal(L,"UIParent"); lua_replace(L,2); }
    lua_pushvalue(L,2); lua_pushvalue(L,1); lua_replace(L,2); lua_replace(L,1);
    return LF_CreateTexture(L);
}

// Global CreateFont() - WoW 3.3.5 API for creating font objects
static int LF_CreateFont(lua_State* L)
{
    const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,1,""));
    lua_newtable(L); lua_pushstring(L,"Font"); lua_setfield(L,-2,"__objectType");
    if(!Name.IsEmpty()) { lua_pushstring(L,TCHAR_TO_UTF8(*Name)); lua_setfield(L,-2,"__name"); }
    luaL_getmetatable(L,WowLuaApi::FRAME_METATABLE); lua_setmetatable(L,-2);
    if(!Name.IsEmpty()) { lua_pushvalue(L,-1); lua_setglobal(L,TCHAR_TO_UTF8(*Name)); }
    return 1;
}

// frame:SetBackdrop(backdrop)
static int LF_SetBackdrop(lua_State* L)
{
    luaL_checktype(L,1,LUA_TTABLE);
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) return 0;
    TOptional<FWowBackdrop> Value;
    if(lua_istable(L,2))
    {
        FWowBackdrop D;
        auto String=[&](const char* Key) { lua_getfield(L,2,Key); FString V=UTF8_TO_TCHAR(luaL_optstring(L,-1,"")); lua_pop(L,1); return V; };
        auto Number=[&](int32 Table,const char* Key) { lua_getfield(L,Table,Key); const float V=luaL_optnumber(L,-1,0); lua_pop(L,1); return FMath::IsFinite(V)?V:0.f; };
        D.BgFile=String("bgFile"); D.EdgeFile=String("edgeFile"); D.EdgeSize=Number(2,"edgeSize"); D.TileSize=Number(2,"tileSize");
        lua_getfield(L,2,"tile"); D.Tile=lua_toboolean(L,-1)!=0; lua_pop(L,1);
        lua_getfield(L,2,"insets"); if(lua_istable(L,-1))
        {
            const int32 Index=lua_gettop(L); D.InsetLeft=Number(Index,"left"); D.InsetRight=Number(Index,"right"); D.InsetTop=Number(Index,"top"); D.InsetBottom=Number(Index,"bottom");
        }
        lua_pop(L,1); Value=D;
    }
    else if(!lua_isnoneornil(L,2)) return luaL_argerror(L,2,"expected backdrop table or nil");
    C->FrameManager->SetFrameBackdrop(GetFrameHandle(L),Value); return 0;
}
static int LF_GetBackdrop(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const auto* Def=C && C->FrameManager?C->FrameManager->GetFrameDef(GetFrameHandle(L)):nullptr;
    if(!Def || !Def->Backdrop.IsSet()) { lua_pushnil(L); return 1; }
    const auto& D=Def->Backdrop.GetValue(); lua_newtable(L);
    auto Number=[&](const char* Key,float V) { lua_pushnumber(L,V); lua_setfield(L,-2,Key); };
    if(!D.BgFile.IsEmpty()) { lua_pushstring(L,TCHAR_TO_UTF8(*D.BgFile)); lua_setfield(L,-2,"bgFile"); }
    if(!D.EdgeFile.IsEmpty()) { lua_pushstring(L,TCHAR_TO_UTF8(*D.EdgeFile)); lua_setfield(L,-2,"edgeFile"); }
    Number("edgeSize",D.EdgeSize); Number("tileSize",D.TileSize); lua_pushboolean(L,D.Tile); lua_setfield(L,-2,"tile");
    lua_newtable(L); Number("left",D.InsetLeft); Number("right",D.InsetRight); Number("top",D.InsetTop); Number("bottom",D.InsetBottom); lua_setfield(L,-2,"insets"); return 1;
}
static int PushBackdropColor(lua_State* L,bool Border)
{
    auto* C=WowLuaApi::GetContext(L); const auto* Def=C && C->FrameManager?C->FrameManager->GetFrameDef(GetFrameHandle(L)):nullptr;
    const FLinearColor Color=Def && Def->Backdrop.IsSet()?(Border?Def->Backdrop->BorderColor:Def->Backdrop->Color):FLinearColor::White;
    lua_pushnumber(L,Color.R); lua_pushnumber(L,Color.G); lua_pushnumber(L,Color.B); lua_pushnumber(L,Color.A); return 4;
}

// Helper: read a color value from the stack, tolerating tables at any position
static double SafeColorArg(lua_State* L, int idx, double def)
{
	if (lua_isnumber(L, idx)) return lua_tonumber(L, idx);
	if (lua_istable(L, idx)) return def; // skip tables silently
	return def;
}

// frame:SetBackdropColor(r, g, b, a) or frame:SetBackdropColor(colorTable)
static int LF_SetBackdropColor(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_newtable(L);

	if (lua_type(L, 2) == LUA_TTABLE)
	{
		lua_getfield(L, 2, "r"); double r = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, 2, "g"); double g = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, 2, "b"); double b = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, 2, "a"); double a = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 1.0; lua_pop(L, 1);
		lua_pushnumber(L, r); lua_rawseti(L, -2, 1);
		lua_pushnumber(L, g); lua_rawseti(L, -2, 2);
		lua_pushnumber(L, b); lua_rawseti(L, -2, 3);
		lua_pushnumber(L, a); lua_rawseti(L, -2, 4);
	}
	else
	{
		lua_pushnumber(L, SafeColorArg(L, 2, 1.0)); lua_rawseti(L, -2, 1);
		lua_pushnumber(L, SafeColorArg(L, 3, 1.0)); lua_rawseti(L, -2, 2);
		lua_pushnumber(L, SafeColorArg(L, 4, 1.0)); lua_rawseti(L, -2, 3);
		lua_pushnumber(L, SafeColorArg(L, 5, 1.0)); lua_rawseti(L, -2, 4);
	}

	FLinearColor Color;
	float* Components[]={&Color.R,&Color.G,&Color.B,&Color.A};
	for(int32 I=0;I<4;++I) { lua_rawgeti(L,-1,I+1); *Components[I]=lua_tonumber(L,-1); lua_pop(L,1); }
	if(auto* C=WowLuaApi::GetContext(L); C && C->FrameManager) C->FrameManager->SetFrameBackdropColor(GetFrameHandle(L),Color);
	lua_setfield(L, 1, "__backdropColor");
	return 0;
}

// frame:SetBackdropBorderColor(r, g, b, a) or frame:SetBackdropBorderColor(colorTable)
static int LF_SetBackdropBorderColor(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_newtable(L);

	if (lua_type(L, 2) == LUA_TTABLE)
	{
		lua_getfield(L, 2, "r"); double r = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, 2, "g"); double g = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, 2, "b"); double b = lua_tonumber(L, -1); lua_pop(L, 1);
		lua_getfield(L, 2, "a"); double a = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 1.0; lua_pop(L, 1);
		lua_pushnumber(L, r); lua_rawseti(L, -2, 1);
		lua_pushnumber(L, g); lua_rawseti(L, -2, 2);
		lua_pushnumber(L, b); lua_rawseti(L, -2, 3);
		lua_pushnumber(L, a); lua_rawseti(L, -2, 4);
	}
	else
	{
		lua_pushnumber(L, SafeColorArg(L, 2, 1.0)); lua_rawseti(L, -2, 1);
		lua_pushnumber(L, SafeColorArg(L, 3, 1.0)); lua_rawseti(L, -2, 2);
		lua_pushnumber(L, SafeColorArg(L, 4, 1.0)); lua_rawseti(L, -2, 3);
		lua_pushnumber(L, SafeColorArg(L, 5, 1.0)); lua_rawseti(L, -2, 4);
	}

    FLinearColor Color; float* Components[]={&Color.R,&Color.G,&Color.B,&Color.A};
    for(int32 I=0;I<4;++I) { lua_rawgeti(L,-1,I+1); *Components[I]=lua_tonumber(L,-1); lua_pop(L,1); }
    if(auto* C=WowLuaApi::GetContext(L); C && C->FrameManager) C->FrameManager->SetFrameBackdropBorderColor(GetFrameHandle(L),Color);
	lua_setfield(L, 1, "__backdropBorderColor");
	return 0;
}

// frame:SetID(id)
static int LF_SetID(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushvalue(L, 2);
	lua_setfield(L, 1, "__id");
	return 0;
}

// frame:GetID()
static int LF_GetID(lua_State* L)
{
	lua_getfield(L, 1, "__id");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushnumber(L, 0);
	}
	return 1;
}

// frame:SetAttribute(name, value)
static int LF_SetAttribute(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	const char* AttrName = luaL_checkstring(L, 2);
	const int Top = lua_gettop(L);
	// Store as __attr_<name> on the frame table
	FString Key = FString::Printf(TEXT("__attr_%s"), UTF8_TO_TCHAR(AttrName));
	FTCHARToUTF8 UTF8Key(*Key);
	if (Top >= 3)
	{
		lua_pushvalue(L, 3);
	}
	else
	{
		lua_pushnil(L);
	}
	lua_setfield(L, 1, UTF8Key.Get());

	const int64 Handle = GetOptionalFrameHandle(L);
	if (Handle >= 0)
	{
		FString GlobalKey = FString::Printf(TEXT("__WowScript_%lld_OnAttributeChanged"), Handle);
		FTCHARToUTF8 UTF8GlobalKey(*GlobalKey);
		lua_getglobal(L, UTF8GlobalKey.Get());
		if (lua_isfunction(L, -1))
		{
			lua_pushvalue(L, 1);
			lua_pushvalue(L, 2);
			if (Top >= 3)
			{
				lua_pushvalue(L, 3);
			}
			else
			{
				lua_pushnil(L);
			}

			if (lua_pcall(L, 3, 0, 0) != 0)
			{
				UE_LOG(LogWowLuaFrame, Warning, TEXT("SetAttribute OnAttributeChanged error [%lld] %s: %s"),
					Handle, UTF8_TO_TCHAR(AttrName), UTF8_TO_TCHAR(lua_tostring(L, -1)));
				lua_pop(L, 1);
			}
		}
		else
		{
			lua_pop(L, 1);
		}
	}
	return 0;
}

// frame:GetAttribute(name)
static int LF_GetAttribute(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	// WoW GetAttribute supports 1-3 string args: GetAttribute(name) or GetAttribute(prefix, name, suffix)
	// With multiple args, the attribute key is their concatenation.
	FString AttrName;
	int32 NumArgs = lua_gettop(L) - 1; // exclude self
	for (int32 i = 2; i <= lua_gettop(L); i++)
	{
		const char* Part = lua_tostring(L, i);
		if (Part)
		{
			AttrName += UTF8_TO_TCHAR(Part);
		}
	}
	if (AttrName.IsEmpty())
	{
		lua_pushnil(L);
		return 1;
	}
	FString Key = FString::Printf(TEXT("__attr_%s"), *AttrName);
	FTCHARToUTF8 UTF8Key(*Key);
	lua_getfield(L, 1, UTF8Key.Get());
	return 1;
}

// frame:HookScript(scriptType, handler)
static int LF_HookScript(lua_State* L)
{
	// For now, just behave like SetScript
	return LF_SetScript(L);
}

// frame:HasScript(scriptType)
static int LF_HasScript(lua_State* L)
{
	lua_pushboolean(L, 1);
	return 1;
}

// Helper: compute frame position from first anchor and dimensions
static void GetFrameRect(lua_State* L, float& OutLeft, float& OutBottom, float& OutRight, float& OutTop)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	OutLeft = OutBottom = OutRight = OutTop = 0.f;
	if (!Ctx || !Ctx->FrameManager) return;
    int64 Handle=GetOptionalFrameHandle(L); FVector4 Rect;
    if(TableRepresentsRegion(L,1))
    {
        lua_getfield(L,1,"__name"); const FString Name=UTF8_TO_TCHAR(lua_tostring(L,-1)?lua_tostring(L,-1):""); lua_pop(L,1);
        Rect=Ctx->FrameManager->GetRegionRect(Name);
        lua_getfield(L,1,"__parentHandle"); Handle=lua_tointeger(L,-1); lua_pop(L,1);
    }
    else { if(!Ctx->FrameManager->GetFrameDef(Handle)) return; Rect=Ctx->FrameManager->GetLayoutRect(Handle); }
    const auto Root = Ctx->FrameManager->GetLayoutRect(Ctx->FrameManager->FindFrame(TEXT("UIParent")));
    const float Scale = Ctx->FrameManager->GetEffectiveFrameScale(Handle);
    OutLeft = Rect.X / Scale;
    OutRight = (Rect.X + Rect.Z) / Scale;
    OutTop = (Root.W - Rect.Y) / Scale;
    OutBottom = (Root.W - Rect.Y - Rect.W) / Scale;
}

// frame:GetCenter()
static int LF_GetCenter(lua_State* L)
{
	float Left, Bottom, Right, Top;
	GetFrameRect(L, Left, Bottom, Right, Top);
	lua_pushnumber(L, (Left + Right) * 0.5f);
	lua_pushnumber(L, (Bottom + Top) * 0.5f);
	return 2;
}

// frame:GetLeft/Right/Top/Bottom
static int LF_GetLeft(lua_State* L)
{
	float Left, Bottom, Right, Top;
	GetFrameRect(L, Left, Bottom, Right, Top);
	lua_pushnumber(L, Left);
	return 1;
}

static int LF_GetRight(lua_State* L)
{
	float Left, Bottom, Right, Top;
	GetFrameRect(L, Left, Bottom, Right, Top);
	lua_pushnumber(L, Right);
	return 1;
}

static int LF_GetTop(lua_State* L)
{
	float Left, Bottom, Right, Top;
	GetFrameRect(L, Left, Bottom, Right, Top);
	lua_pushnumber(L, Top);
	return 1;
}

static int LF_GetBottom(lua_State* L)
{
	float Left, Bottom, Right, Top;
	GetFrameRect(L, Left, Bottom, Right, Top);
	lua_pushnumber(L, Bottom);
	return 1;
}

// Button state textures are real regions, shared by XML and Lua setters.
static int PushButtonTexture(lua_State* L,const FString& State)
{
    auto* C=WowLuaApi::GetContext(L);
    const FString Name=C && C->FrameManager?C->FrameManager->EnsureButtonTexture(GetFrameHandle(L),State):FString();
    if(Name.IsEmpty()) lua_pushnil(L); else lua_getglobal(L,TCHAR_TO_UTF8(*Name));
    return 1;
}
static int SetButtonTexture(lua_State* L,const FString& State)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) return 0;
    const int64 Handle=GetFrameHandle(L);
    const FString Name=C->FrameManager->EnsureButtonTexture(Handle,State); if(Name.IsEmpty()) return 0;
    lua_getglobal(L,TCHAR_TO_UTF8(*Name)); lua_replace(L,1);
    if(lua_istable(L,2))
    {
        if(auto* Source=GetTextureImage(L,2)) if(auto* Target=C->FrameManager->GetNamedTexture(Name))
        { Target->SetBrush(Source->GetBrush()); Target->SetRenderOpacity(Source->GetRenderOpacity()); }
    }
    else LF_TextureSetTexture(L);
    ApplyTextureStyle(L); C->FrameManager->RefreshButtonVisuals(Handle);
    return 0;
}
static int LF_SetNormalTexture(lua_State* L) { return SetButtonTexture(L,TEXT("Normal")); }
static int LF_SetPushedTexture(lua_State* L) { return SetButtonTexture(L,TEXT("Pushed")); }
static int LF_SetHighlightTexture(lua_State* L) { return SetButtonTexture(L,TEXT("Highlight")); }
static int LF_SetDisabledTexture(lua_State* L) { return SetButtonTexture(L,TEXT("Disabled")); }

static int LF_SetText(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	// WoW uses lua_tolstring here: a deferred label function initially clears
	// the string and FrameXML evaluates it during its following update pass.
	const char* Text = lua_tostring(L,2); if(!Text) Text="";

	// Store in Lua table
	lua_pushstring(L, Text);
	lua_setfield(L, 1, "__text");

	// Check if this is a tooltip frame
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L, 1);
		const FWowFrameDef* FrameDef = Ctx->FrameManager->GetFrameDef(Handle);
		if (FrameDef && FrameDef->Type == EWowFrameType::GameTooltip)
		{
			// This is a tooltip - use tooltip SetText
			Ctx->FrameManager->TooltipSetText(Handle, UTF8_TO_TCHAR(Text));
			return 0;
		}
	}

	// Update UMG widget for other frame types
	if(auto* Edit=GetMultiLineEditor(L)) { Edit->SetText(FText::FromString(UTF8_TO_TCHAR(Text))); return 0; }
	if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
	{
		FText TextContent = FText::FromString(UTF8_TO_TCHAR(Text));
		if(!TextBlock->GetText().EqualTo(TextContent))
		{
			TextBlock->SetText(TextContent);
			if(Ctx && Ctx->FrameManager) Ctx->FrameManager->InvalidateTextLayout();
		}
	}
	else if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L, 1))
	{
		FText EditContent = FText::FromString(UTF8_TO_TCHAR(Text));
		EditBox->SetText(EditContent);
	}

	return 0;
}

static int ButtonFontObject(lua_State* L,const FString& State,bool Get)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) return 0;
    if(Get)
    {
        const auto* Def=C->FrameManager->GetFrameDef(GetFrameHandle(L));
        const FString Font=Def?(State==TEXT("Normal")?Def->NormalFont:State==TEXT("Highlight")?Def->HighlightFont:Def->DisabledFont):FString();
        lua_getglobal(L,TCHAR_TO_UTF8(*Font)); return 1;
    }
    FString Font;
    if(lua_isstring(L,2)) Font=UTF8_TO_TCHAR(lua_tostring(L,2));
    else if(lua_istable(L,2)) { lua_getfield(L,2,"__name"); Font=UTF8_TO_TCHAR(luaL_optstring(L,-1,"")); lua_pop(L,1); }
    C->FrameManager->SetButtonFont(GetFrameHandle(L),State,Font); return 0;
}

static int LF_GetText(lua_State* L)
{
	if(auto* Edit=GetMultiLineEditor(L)) { const FString Text=Edit->LuaText(); lua_pushstring(L,TCHAR_TO_UTF8(*Text)); return 1; }
	// Lua reads the original markup; the native text widget paints plain glyphs.
	if(TableRepresentsRegion(L,1))
	{
		lua_getfield(L,1,"__text");
		if(lua_isstring(L,-1)) return 1;
		lua_pop(L,1);
	}
	// Try to get text from widget first
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
	{
		FString TextContent = TextBlock->GetText().ToString();
		lua_pushstring(L, (const char*)StringCast<UTF8CHAR>(*TextContent).Get());
		return 1;
	}
	else if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L, 1))
	{
		FString EditText = EditBox->GetText().ToString();
		lua_pushstring(L, (const char*)StringCast<UTF8CHAR>(*EditText).Get());
		return 1;
	}

	// Fallback to stored value
	lua_getfield(L, 1, "__text");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushstring(L, "");
	}
	return 1;
}

static int LF_Disable(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushboolean(L, 0);
	lua_setfield(L, 1, "__enabled");
	if (auto* C=WowLuaApi::GetContext(L); C && C->FrameManager) C->FrameManager->SetButtonEnabled(GetFrameHandle(L),false);
	if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L, 1)) EditBox->SetIsEnabled(false);
	if(auto* Edit=GetMultiLineEditor(L)) Edit->SetIsEnabled(false);
	return 0;
}

static int LF_Enable(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushboolean(L, 1);
	lua_setfield(L, 1, "__enabled");
	if (auto* C=WowLuaApi::GetContext(L); C && C->FrameManager) C->FrameManager->SetButtonEnabled(GetFrameHandle(L),true);
	if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L, 1)) EditBox->SetIsEnabled(true);
	if(auto* Edit=GetMultiLineEditor(L)) Edit->SetIsEnabled(true);
	return 0;
}

static int LF_IsEnabled(lua_State* L)
{
	lua_getfield(L, 1, "__enabled");
	const bool Enabled=lua_isnil(L,-1) || lua_toboolean(L,-1)!=0;
	lua_pop(L,1); lua_pushinteger(L,Enabled?1:0);
	return 1;
}

static int LF_Click(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->DispatchClick(GetFrameHandle(L),UTF8_TO_TCHAR(luaL_optstring(L,2,"LeftButton")),lua_toboolean(L,3)!=0); return 0;
}

// StatusBar methods
static int LF_SetMinMaxValues(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float MinValue = static_cast<float>(luaL_checknumber(L, 2));
	float MaxValue = static_cast<float>(luaL_checknumber(L, 3));

	UE_LOG(LogWowLuaFrame, Verbose, TEXT("SetMinMaxValues called: Min=%.1f, Max=%.1f"), MinValue, MaxValue);

	// Store in Lua table
	lua_pushnumber(L, MinValue);
	lua_setfield(L, 1, "__minValue");
	lua_pushnumber(L, MaxValue);
	lua_setfield(L, 1, "__maxValue");

	// Update UMG widget if current value exists
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L);
		if (USlider* Slider = Ctx->FrameManager->GetSliderWidget(Handle))
		{
            Ctx->FrameManager->SetSliderRange(Handle,MinValue,MaxValue);
		}
		UProgressBar* ProgressBar = Ctx->FrameManager->GetStatusBarWidget(Handle);
		if (ProgressBar)
		{
			// Get current value and recalculate percentage
			lua_getfield(L, 1, "__value");
			float CurrentValue = lua_isnil(L, -1) ? 0.0f : static_cast<float>(lua_tonumber(L, -1));
			lua_pop(L, 1);

			float Range = MaxValue - MinValue;
			float Percent = (Range > 0.0f) ? (CurrentValue - MinValue) / Range : 0.0f;
			ProgressBar->SetPercent(FMath::Clamp(Percent, 0.0f, 1.0f));
		}
	}

	return 0;
}

static int LF_GetMinMaxValues(lua_State* L)
{
    if(auto* C=WowLuaApi::GetContext(L); C && C->FrameManager) if(const auto* Slider=C->FrameManager->GetSliderWidget(GetFrameHandle(L))) { lua_pushnumber(L,Slider->GetMinValue()); lua_pushnumber(L,Slider->GetMaxValue()); return 2; }
	lua_getfield(L, 1, "__minValue");
	if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, 0); }
	lua_getfield(L, 1, "__maxValue");
	if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushnumber(L, 100); }
	return 2;
}

static int LF_SetValue(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float Value = static_cast<float>(luaL_checknumber(L, 2));
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = GetFrameHandle(L);
	USlider* Slider = Ctx && Ctx->FrameManager ? Ctx->FrameManager->GetSliderWidget(Handle) : nullptr;
	if (Slider) Value = FMath::Clamp(Value, Slider->GetMinValue(), Slider->GetMaxValue());
	lua_getfield(L, 1, "__value");
	const float SliderBefore=Slider?Ctx->FrameManager->GetSliderValue(Handle):0;
    bool bChanged = Slider ? !FMath::IsNearlyEqual(SliderBefore, Value) : !lua_isnumber(L, -1) || lua_tonumber(L, -1) != Value;
	lua_pop(L, 1);

	// Store in Lua table
	lua_pushnumber(L, Value);
	lua_setfield(L, 1, "__value");

	// Update UMG widget
	if (Ctx && Ctx->FrameManager)
	{
		UProgressBar* ProgressBar = Ctx->FrameManager->GetStatusBarWidget(Handle);
		if (ProgressBar)
		{
			// Get min/max values from Lua table
			lua_getfield(L, 1, "__minValue");
			float MinValue = lua_isnil(L, -1) ? 0.0f : static_cast<float>(lua_tonumber(L, -1));
			lua_pop(L, 1);

			lua_getfield(L, 1, "__maxValue");
			float MaxValue = lua_isnil(L, -1) ? 100.0f : static_cast<float>(lua_tonumber(L, -1));
			lua_pop(L, 1);

			// Calculate percentage and update progress bar
			float Range = MaxValue - MinValue;
			float Percent = (Range > 0.0f) ? (Value - MinValue) / Range : 0.0f;
			float ClampedPercent = FMath::Clamp(Percent, 0.0f, 1.0f);

			ProgressBar->SetPercent(ClampedPercent);
		}
		else if (Slider)
		{
			// Keep native and Lua values in the same units. Polling a normalized
			// thumb position as a WoW offset otherwise repeatedly divides it.
			Ctx->FrameManager->SetSliderValue(Handle, Value);
            Value=Ctx->FrameManager->GetSliderValue(Handle); bChanged=!FMath::IsNearlyEqual(SliderBefore,Value);
            lua_pushnumber(L,Value); lua_setfield(L,1,"__value");
		}
	}
	else
	{
		UE_LOG(LogWowLuaFrame, Warning, TEXT("SetValue called but missing context or non-numeric value"));
	}
	if (bChanged && Ctx && Ctx->EventSystem)
	{
		Ctx->EventSystem->RunFrameScript(Handle, TEXT("OnValueChanged"), {FString::Printf(TEXT("%.17g"),double(Value))});
	}

	return 0;
}

static int LF_GetValue(lua_State* L)
{
	if (auto* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->FrameManager)
		if (auto* Slider = Ctx->FrameManager->GetSliderWidget(GetFrameHandle(L)))
		{
			lua_pushnumber(L, Ctx->FrameManager->GetSliderValue(GetFrameHandle(L)));
			return 1;
		}
	lua_getfield(L, 1, "__value");
	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		lua_pushnumber(L, 0);
	}
	return 1;
}

static int LF_SetStatusBarTexture(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	// Even if no texture is specified, ensure the bar has a solid fill brush
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L);
		UProgressBar* ProgressBar = Ctx->FrameManager->GetStatusBarWidget(Handle);
		if (ProgressBar)
		{
			// Ensure the progress bar has a solid fill brush
			FProgressBarStyle BarStyle = ProgressBar->GetWidgetStyle();
			FSlateBrush FillBrush = BarStyle.FillImage;
			if (FillBrush.DrawAs == ESlateBrushDrawType::NoDrawType)
			{
				FillBrush.DrawAs = ESlateBrushDrawType::Box;
				FillBrush.SetResourceObject(nullptr); // solid color brush
				BarStyle.SetFillImage(FillBrush);
				ProgressBar->SetWidgetStyle(BarStyle);
			}
		}
	}
	return 0;
}
static int LF_SetStatusBarColor(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float R = static_cast<float>(luaL_checknumber(L, 2));
	float G = static_cast<float>(luaL_checknumber(L, 3));
	float B = static_cast<float>(luaL_checknumber(L, 4));
	float A = lua_isnumber(L, 5) ? static_cast<float>(lua_tonumber(L, 5)) : 1.0f;

	// Store in Lua table
	lua_pushnumber(L, R); lua_setfield(L, 1, "__barColorR");
	lua_pushnumber(L, G); lua_setfield(L, 1, "__barColorG");
	lua_pushnumber(L, B); lua_setfield(L, 1, "__barColorB");
	lua_pushnumber(L, A); lua_setfield(L, 1, "__barColorA");

	// Update UMG widget
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L);
		UProgressBar* ProgressBar = Ctx->FrameManager->GetStatusBarWidget(Handle);
		if (ProgressBar)
		{
			// Ensure the progress bar has a solid fill brush
			FProgressBarStyle BarStyle = ProgressBar->GetWidgetStyle();
			FSlateBrush FillBrush = BarStyle.FillImage;
			if (FillBrush.DrawAs == ESlateBrushDrawType::NoDrawType)
			{
				FillBrush.DrawAs = ESlateBrushDrawType::Box;
				FillBrush.SetResourceObject(nullptr); // solid color brush
				BarStyle.SetFillImage(FillBrush);
				ProgressBar->SetWidgetStyle(BarStyle);
			}

			FLinearColor Color(R, G, B, A);
			ProgressBar->SetFillColorAndOpacity(Color);
		}
	}

	return 0;
}
static int LF_SetOrientation(lua_State* L)
{
    const FString Orientation=UTF8_TO_TCHAR(luaL_checkstring(L,2)); const bool Vertical=Orientation.Equals(TEXT("VERTICAL"),ESearchCase::IgnoreCase);
    if(auto* C=WowLuaApi::GetContext(L); C && C->FrameManager)
    {
        C->FrameManager->SetSliderOrientation(GetFrameHandle(L),Vertical);
        if(auto* Bar=C->FrameManager->GetStatusBarWidget(GetFrameHandle(L))) Bar->SetBarFillType(Vertical?EProgressBarFillType::BottomToTop:EProgressBarFillType::LeftToRight);
    }
    return 0;
}

// EditBox methods
static int LF_SetMaxLetters(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	int MaxLetters = static_cast<int>(luaL_checknumber(L, 2));

	// Store in Lua table
	lua_pushnumber(L, MaxLetters);
	lua_setfield(L, 1, "__maxLetters");

	// Apply to widget
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L);
		if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L))
		{
			Ctx->FrameManager->EditBoxMaxLetters.Add(Handle,FMath::Max(MaxLetters,0));
		}
	}

	return 0;
}

static int LF_SetAutoFocus(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	bool AutoFocus = (lua_toboolean(L, 2) != 0);

	// Store in Lua table
	lua_pushboolean(L, AutoFocus);
	lua_setfield(L, 1, "__autoFocus");

	return 0;
}

static int LF_ClearFocus(lua_State* L)
{
	if(auto* Edit=GetMultiLineEditor(L); Edit && Edit->HasEditFocus()) { if(FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport(); return 0; }
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L))
		{
			// Clear focus from the edit box
			if(FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
	}

	return 0;
}

static int LF_SetFocus(lua_State* L)
{
	if(auto* Edit=GetMultiLineEditor(L)) { Edit->SetKeyboardFocus(); return 0; }
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L))
		{
			// Set focus to the edit box
			EditBox->SetKeyboardFocus();
		}
	}

	return 0;
}

static int LF_HasFocus(lua_State* L)
{
	if(auto* Edit=GetMultiLineEditor(L)) { lua_pushboolean(L,Edit->HasEditFocus()); return 1; }
	bool HasFocus = false;

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		if (UEditableTextBox* EditBox = GetEditBoxWidgetForTable(L))
		{
			HasFocus = EditBox->HasKeyboardFocus();
		}
	}

	lua_pushboolean(L, HasFocus);
	return 1;
}
static int LF_HighlightText(lua_State* L) { return 0; }
static int LF_SetCursorPosition(lua_State* L)
{
    if(auto* Edit=GetMultiLineEditor(L)) Edit->SetCursorOffset(luaL_checkinteger(L,2));
    else if(auto* Single=GetEditBoxWidgetForTable(L)) StaticCastSharedRef<SEditableTextBox>(Single->TakeWidget())->GoTo(FTextLocation(0,FMath::Clamp(int32(luaL_checkinteger(L,2)),0,Single->GetText().ToString().Len())));
    return 0;
}
static int LF_SetTextInsets(lua_State* L) { return 0; }
static int LF_SetMultiLine(lua_State* L) { return 0; }
static int LF_SetFontObject(lua_State* L)
{
    if(lua_isstring(L,2)) { lua_getglobal(L,lua_tostring(L,2)); lua_replace(L,2); }
    if(!lua_istable(L,2)) return 0;
    lua_pushvalue(L,2); lua_setfield(L,1,"__fontObject");
    for(const char* Key:{"__fontPath","__fontSize","__fontFlags","__textColorR","__textColorG","__textColorB","__textColorA"}) { lua_getfield(L,2,Key); lua_setfield(L,1,Key); }
    if(auto* Text=GetTextBlockWidgetForTable(L))
    {
        auto* C=WowLuaApi::GetContext(L); auto* Fonts=C && C->FrameManager?C->FrameManager->GetFontManager():nullptr;
        lua_getfield(L,2,"__name"); const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,-1,"GameFontNormal")); lua_pop(L,1);
        lua_getfield(L,2,"__fontSize"); const float Size=luaL_optnumber(L,-1,Fonts?Fonts->GetFontSize(Name):12); lua_pop(L,1);
        const float Scale=C && C->FrameManager?C->FrameManager->GetUIScale():1;
        lua_getfield(L,2,"__fontPath"); const FString Path=UTF8_TO_TCHAR(luaL_optstring(L,-1,TCHAR_TO_UTF8(*Name))); lua_pop(L,1);
        if(Fonts) Text->SetFont(Fonts->GetFont(Path,FMath::RoundToInt(Size*Scale)));
        else { auto Info=Text->GetFont(); Info.Size=FMath::Max(1,FMath::RoundToInt(Size*Scale*.75f)); Text->SetFont(Info); }
        FLinearColor Color; float* Components[]={&Color.R,&Color.G,&Color.B,&Color.A}; const char* Keys[]={"__textColorR","__textColorG","__textColorB","__textColorA"};
        for(int32 I=0;I<4;++I) { lua_getfield(L,2,Keys[I]); *Components[I]=luaL_optnumber(L,-1,1); lua_pop(L,1); }
        Text->SetColorAndOpacity(Color);
    }
    const int32 Source=2;
    const auto Apply=[&](const char* Method,std::initializer_list<const char*> Keys)
    {
        lua_getfield(L,Source,*Keys.begin()); const bool Present=!lua_isnil(L,-1); lua_pop(L,1); if(!Present) return;
        lua_getfield(L,1,Method); lua_pushvalue(L,1); for(const auto Key:Keys) lua_getfield(L,Source,Key);
        lua_call(L,1+static_cast<int32>(Keys.size()),0);
    };
    Apply("SetFont",{"__fontPath","__fontSize","__fontFlags"});
    Apply("SetTextColor",{"__textColorR","__textColorG","__textColorB","__textColorA"});
    Apply("SetJustifyH",{"__justifyH"}); Apply("SetJustifyV",{"__justifyV"});
    Apply("SetWordWrap",{"__wordWrap"}); Apply("SetNonSpaceWrap",{"__nonSpaceWrap"}); Apply("SetSpacing",{"__spacing"});
    Apply("SetShadowOffset",{"__shadowX","__shadowY"}); Apply("SetShadowColor",{"__shadowR","__shadowG","__shadowB","__shadowA"});
    InvalidateLabel(L);
    return 0;
}
static int LF_GetFontObject(lua_State* L)
{
    lua_getfield(L,1,"__fontObject"); if(lua_istable(L,-1)) return 1; lua_pop(L,1);
    lua_getglobal(L,"GameFontNormal"); return 1;
}

// ScrollFrame methods
static int LF_SetScrollChild(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 ScrollFrameHandle = GetFrameHandle(L, 1);
	if (ScrollFrameHandle < 0) return 0;

	// Check if second parameter is a frame table or nil
	if (lua_isnil(L, 2))
	{
		// Clear scroll child - for now, just log
		UE_LOG(LogTemp, Log, TEXT("SetScrollChild: clearing child for frame %lld"), ScrollFrameHandle);
		return 0;
	}

	int64 ChildHandle = GetOptionalFrameHandle(L, 2);
	if (ChildHandle < 0) return 0;

	Ctx->FrameManager->SetScrollChild(ScrollFrameHandle, ChildHandle);
	UE_LOG(LogTemp, Log, TEXT("ScrollFrame API: SetScrollChild called for frames %lld -> %lld"),
		ScrollFrameHandle, ChildHandle);
	return 0;
}

static int LF_GetScrollChild(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager || !Ctx->EventSystem)
	{
		lua_pushnil(L);
		return 1;
	}

	int64 ScrollFrameHandle = GetFrameHandle(L, 1);
	if (ScrollFrameHandle < 0)
	{
		lua_pushnil(L);
		return 1;
	}

	int64 ChildHandle = Ctx->FrameManager->GetScrollChild(ScrollFrameHandle);
	if (ChildHandle < 0)
	{
		lua_pushnil(L);
		return 1;
	}

	// Push the child's Lua frame table (stored as global by name)
	const FString ChildName = Ctx->FrameManager->GetFrameName(ChildHandle);
	if (!ChildName.IsEmpty())
	{
		FTCHARToUTF8 UTF8Name(*ChildName);
		lua_getglobal(L, UTF8Name.Get());
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}

static int LF_SetVerticalScroll(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 ScrollFrameHandle = GetFrameHandle(L, 1);
	if (ScrollFrameHandle < 0) return 0;

	float Offset = static_cast<float>(luaL_checknumber(L, 2));
	Ctx->FrameManager->SetVerticalScroll(ScrollFrameHandle, Offset);
	UE_LOG(LogTemp, Log, TEXT("ScrollFrame API: SetVerticalScroll called for frame %lld offset %.2f"),
		ScrollFrameHandle, Offset);
	return 0;
}

static int LF_GetVerticalScroll(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushnumber(L, 0);
		return 1;
	}

	int64 ScrollFrameHandle = GetFrameHandle(L, 1);
	if (ScrollFrameHandle < 0)
	{
		lua_pushnumber(L, 0);
		return 1;
	}

	float Offset = Ctx->FrameManager->GetVerticalScroll(ScrollFrameHandle);
	lua_pushnumber(L, Offset);
	return 1;
}

static int LF_SetHorizontalScroll(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 ScrollFrameHandle = GetFrameHandle(L, 1);
	if (ScrollFrameHandle < 0) return 0;

	float Offset = static_cast<float>(luaL_checknumber(L, 2));
	Ctx->FrameManager->SetHorizontalScroll(ScrollFrameHandle, Offset);
	return 0;
}

static int LF_GetHorizontalScroll(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushnumber(L, 0);
		return 1;
	}

	int64 ScrollFrameHandle = GetFrameHandle(L, 1);
	if (ScrollFrameHandle < 0)
	{
		lua_pushnumber(L, 0);
		return 1;
	}

	float Offset = Ctx->FrameManager->GetHorizontalScroll(ScrollFrameHandle);
	lua_pushnumber(L, Offset);
	return 1;
}

// Cooldown methods
static int LF_SetCooldown(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float StartTime = static_cast<float>(luaL_checknumber(L, 2));
	float Duration = static_cast<float>(luaL_checknumber(L, 3));

	// Store in Lua table
	lua_pushnumber(L, StartTime);
	lua_setfield(L, 1, "__cooldownStart");
	lua_pushnumber(L, Duration);
	lua_setfield(L, 1, "__cooldownDuration");

	// Update visual cooldown overlay
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		int64 Handle = GetFrameHandle(L);
		Ctx->FrameManager->SetCooldown(Handle, static_cast<double>(StartTime), Duration);
	}

	return 0;
}

// frame:Raise() / frame:Lower()
static int LF_Raise(lua_State* L) { return 0; }
static int LF_Lower(lua_State* L) { return 0; }

// frame:SetToplevel
static int LF_SetToplevel(lua_State* L) { return 0; }

// frame:RegisterForDrag
static int LF_RegisterForDrag(lua_State* L)
{
    lua_pushnil(L); lua_setfield(L,1,"__attr_drag_LeftButton"); lua_pushnil(L); lua_setfield(L,1,"__attr_drag_RightButton");
    for(int32 I=2; I<=lua_gettop(L); ++I)
    {
        const FString Key=TEXT("__attr_drag_")+FString(UTF8_TO_TCHAR(luaL_checkstring(L,I)));
        lua_pushinteger(L,1); lua_setfield(L,1,TCHAR_TO_UTF8(*Key));
    }
    return 0;
}

// frame:StartMoving / StopMovingOrSizing
static int LF_StartMoving(lua_State* L) { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager && !C->FrameManager->StartFrameMovement(GetFrameHandle(L))) return luaL_error(L,"Frame is not movable or another frame is being moved"); return 0; }
static int LF_StartSizing(lua_State* L) { auto* C=WowLuaApi::GetContext(L); const char* P=luaL_optstring(L,2,"BOTTOMRIGHT"); if(C && C->FrameManager && !C->FrameManager->StartFrameMovement(GetFrameHandle(L),UTF8_TO_TCHAR(P))) return luaL_error(L,"Frame is not resizable or another frame is being moved"); return 0; }
static int LF_StopMovingOrSizing(lua_State* L) { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->StopFrameMovement(GetFrameHandle(L)); return 0; }

static int LF_GetFrameFlag(lua_State* L,bool Resize=false,bool Clamp=false)
{
    auto* C=WowLuaApi::GetContext(L); const auto* D=C && C->FrameManager?C->FrameManager->GetFrameDef(GetFrameHandle(L)):nullptr;
    if(D && (Clamp?D->bClampedToScreen:Resize?D->bResizable:D->bMovable)) lua_pushinteger(L,1); else lua_pushnil(L); return 1;
}
static int LF_ResizeBounds(lua_State* L,bool Maximum,bool Get)
{
    auto* C=WowLuaApi::GetContext(L); auto* D=C && C->FrameManager?C->FrameManager->GetMutableFrameDef(GetFrameHandle(L)):nullptr;
    if(Get) { const auto V=D?(Maximum?D->MaxResize:D->MinResize):FVector2D::ZeroVector; lua_pushnumber(L,V.X); lua_pushnumber(L,V.Y); return 2; }
    if(D) { auto& V=Maximum?D->MaxResize:D->MinResize; V=FVector2D(FMath::Max(0.,luaL_checknumber(L,2)),FMath::Max(0.,luaL_checknumber(L,3))); } return 0;
}
static int LF_ClampInsets(lua_State* L,bool Get)
{
    auto* C=WowLuaApi::GetContext(L); auto* D=C && C->FrameManager?C->FrameManager->GetMutableFrameDef(GetFrameHandle(L)):nullptr;
    if(Get) { const auto V=D?D->ClampInsets:FVector4(0,0,0,0); lua_pushnumber(L,V.X); lua_pushnumber(L,V.Y); lua_pushnumber(L,V.Z); lua_pushnumber(L,V.W); return 4; }
    if(D) { D->ClampInsets=FVector4(luaL_checknumber(L,2),luaL_checknumber(L,3),luaL_checknumber(L,4),luaL_checknumber(L,5)); C->FrameManager->InvalidateTextLayout(); } return 0;
}
static int LF_HitInsets(lua_State* L,bool Get)
{
    auto* C=WowLuaApi::GetContext(L); auto* D=C && C->FrameManager?C->FrameManager->GetMutableFrameDef(GetFrameHandle(L)):nullptr;
    if(Get) { const auto V=D?D->HitInsets:FVector4(0,0,0,0); lua_pushnumber(L,V.X); lua_pushnumber(L,V.Y); lua_pushnumber(L,V.Z); lua_pushnumber(L,V.W); return 4; }
    if(D) D->HitInsets=FVector4(luaL_checknumber(L,2),luaL_checknumber(L,3),luaL_checknumber(L,4),luaL_checknumber(L,5)); return 0;
}
static int LF_MotionWhileDisabled(lua_State* L,bool Get)
{
    auto* C=WowLuaApi::GetContext(L); auto* D=C && C->FrameManager?C->FrameManager->GetMutableFrameDef(GetFrameHandle(L)):nullptr;
    if(Get) { lua_pushboolean(L,D && D->bMotionScriptsWhileDisabled); return 1; }
    if(D) D->bMotionScriptsWhileDisabled=lua_toboolean(L,2)!=0 && (!lua_isnumber(L,2) || lua_tonumber(L,2)!=0); return 0;
}

// ── Fixed Frame API Methods ───────────────────────────────────────────────────────

// frame:GetScale() — returns the frame's explicit scale (default 1.0)
// Local frame scale; ancestors are included by GetEffectiveScale.
static int LF_GetScale(lua_State* L)
{
	const auto* Ctx = WowLuaApi::GetContext(L);
	lua_pushnumber(L, Ctx && Ctx->FrameManager ? Ctx->FrameManager->GetFrameScale(GetFrameHandle(L)) : 1.f);
	return 1;
}

// frame:SetScale(scale) updates layout, rendering and input bounds.
static int LF_SetScale(lua_State* L)
{
	const auto* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager) Ctx->FrameManager->SetFrameScale(GetFrameHandle(L), luaL_checknumber(L, 2));
	return 0;
}

// frame:GetEffectiveScale() — returns the cumulative scale (frame * parent * UIScale)
static int LF_GetEffectiveScale(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		lua_pushnumber(L, Ctx->FrameManager->GetUIScale() * Ctx->FrameManager->GetEffectiveFrameScale(GetFrameHandle(L)));
	}
	else
	{
		lua_pushnumber(L, 1.0);
	}
	return 1;
}

// frame:GetPoint(index) — returns anchor info for the nth anchor (1-based)
static TArray<FWowAnchor> GetObjectPoints(lua_State* L)
{
    const auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) return {};
    if(TableRepresentsRegion(L,1))
    {
        lua_getfield(L,1,"__name"); const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,-1,"")); lua_pop(L,1);
        return C->FrameManager->GetRegionPoints(Name);
    }
    const auto* D=C->FrameManager->GetFrameDef(GetFrameHandle(L));
    if(!D) return {};
    if(D->bSetAllPoints) return {{EWowAnchorPoint::TOPLEFT,D->Parent,EWowAnchorPoint::TOPLEFT,0,0},{EWowAnchorPoint::BOTTOMRIGHT,D->Parent,EWowAnchorPoint::BOTTOMRIGHT,0,0}};
    return D->Anchors;
}
static int LF_GetPoint(lua_State* L)
{
    const auto Points=GetObjectPoints(L); const int32 I=luaL_optinteger(L,2,1)-1;
    if(!Points.IsValidIndex(I)) return 0;
    const auto& A=Points[I]; lua_pushstring(L,AnchorPointToString(A.Point));
    if(!A.RelativeTo.IsEmpty()) lua_getglobal(L,TCHAR_TO_UTF8(*A.RelativeTo));
    else
    {
        auto* C=WowLuaApi::GetContext(L); const int64 Parent=TableRepresentsRegion(L,1)?GetMouseQueryHandle(L):C->FrameManager->GetParentHandle(GetFrameHandle(L));
        if(!C->EventSystem || !C->EventSystem->PushFrameObject(Parent)) lua_pushnil(L);
    }
    lua_pushstring(L,AnchorPointToString(A.RelativePoint)); lua_pushnumber(L,A.OffsetX); lua_pushnumber(L,A.OffsetY); return 5;
}
static int LF_GetNumPoints(lua_State* L) { lua_pushinteger(L,GetObjectPoints(L).Num()); return 1; }

// frame:GetRect() — returns left, bottom, width, height in WoW coordinates
static int LF_GetRect(lua_State* L)
{
	float Left,Bottom,Right,Top; GetFrameRect(L,Left,Bottom,Right,Top);
	lua_pushnumber(L,Left); lua_pushnumber(L,Bottom); lua_pushnumber(L,Right-Left); lua_pushnumber(L,Top-Bottom); return 4;
}

// button:RegisterForClicks(...) — store explicit click edge registrations
static int LF_RegisterForClicks(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	const int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager && Handle >= 0)
	{
		TArray<FString> ClickTypes;
		const int32 NumArgs = lua_gettop(L);
		for (int32 ArgIdx = 2; ArgIdx <= NumArgs; ++ArgIdx)
		{
			ClickTypes.Add(UTF8_TO_TCHAR(luaL_checkstring(L, ArgIdx)));
		}

		Ctx->FrameManager->SetFrameRegisteredClicks(Handle, ClickTypes);
	}

	return 0;
}

// scrollingmsgframe:AddMessage(text, r, g, b) — for now, just log the message
static int LF_AddMessage(lua_State* L)
{
	const char* Text = luaL_optstring(L, 2, "");
	UE_LOG(LogWowLuaFrame, Log, TEXT("ScrollingMessageFrame:AddMessage: %s"), UTF8_TO_TCHAR(Text));
	if(auto* C=WowLuaApi::GetContext(L); C && C->FrameManager) C->FrameManager->AddMessage(GetFrameHandle(L),UTF8_TO_TCHAR(Text),FLinearColor(luaL_optnumber(L,3,1),luaL_optnumber(L,4,1),luaL_optnumber(L,5,1)),luaL_optinteger(L,6,0));
	return 0;
}

// (FontString API functions defined above near LF_CreateFontString)

// ── Tooltip Functions ─────────────────────────────────────────────────────────────

// tooltip:SetOwner(owner, anchor)
static int LF_TooltipSetOwner(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	int64 OwnerHandle = GetOptionalFrameHandle(L, 2);
	const char* AnchorType = luaL_optstring(L, 3, "ANCHOR_CURSOR");

	Ctx->FrameManager->TooltipSetOwner(TooltipHandle, OwnerHandle, UTF8_TO_TCHAR(AnchorType));
	lua_pushvalue(L,2); lua_setfield(L,1,"__tooltipOwner");
	return 0;
}

// tooltip:AddLine(text, r, g, b, wrap)
static int LF_TooltipAddLine(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const char* Text = luaL_optstring(L, 2, "");
    // Native 12340 ignores a nonnumeric optional color, including MailFrame's empty string.
	float R = lua_isnumber(L,3)?static_cast<float>(lua_tonumber(L,3)):1.f;
	float G = lua_isnumber(L,3) && lua_isnumber(L,4)?static_cast<float>(lua_tonumber(L,4)):1.f;
	float B = lua_isnumber(L,3) && lua_isnumber(L,5)?static_cast<float>(lua_tonumber(L,5)):1.f;
	bool bWrap = (lua_toboolean(L, 6) != 0);

	Ctx->FrameManager->TooltipAddLine(TooltipHandle, UTF8_TO_TCHAR(Text), R, G, B, bWrap);
	return 0;
}

// tooltip:AddDoubleLine(leftText, rightText, lr, lg, lb, rr, rg, rb)
static int LF_TooltipAddDoubleLine(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const char* LeftText = luaL_checkstring(L, 2);
	const char* RightText = luaL_checkstring(L, 3);
	float LR = static_cast<float>(luaL_optnumber(L, 4, 1.0));
	float LG = static_cast<float>(luaL_optnumber(L, 5, 1.0));
	float LB = static_cast<float>(luaL_optnumber(L, 6, 1.0));
	float RR = static_cast<float>(luaL_optnumber(L, 7, 1.0));
	float RG = static_cast<float>(luaL_optnumber(L, 8, 1.0));
	float RB = static_cast<float>(luaL_optnumber(L, 9, 1.0));

	Ctx->FrameManager->TooltipAddDoubleLine(TooltipHandle, UTF8_TO_TCHAR(LeftText), UTF8_TO_TCHAR(RightText),
											LR, LG, LB, RR, RG, RB);
	return 0;
}

// tooltip:ClearLines()
static int LF_TooltipClearLines(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	Ctx->FrameManager->TooltipClearLines(TooltipHandle);
	return 0;
}

// tooltip:NumLines()
static int LF_TooltipNumLines(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushnumber(L, 0);
		return 1;
	}

	int64 TooltipHandle = GetFrameHandle(L, 1);
	int32 LineCount = Ctx->FrameManager->TooltipNumLines(TooltipHandle);
	lua_pushnumber(L, LineCount);
	return 1;
}

// tooltip:SetText(text)
static int LF_TooltipSetText(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const char* Text = luaL_checkstring(L, 2);
	Ctx->FrameManager->TooltipSetText(TooltipHandle, UTF8_TO_TCHAR(Text));
	return 0;
}

// tooltip:AppendText(text)
static int LF_TooltipAppendText(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const char* Text = luaL_checkstring(L, 2);
	Ctx->FrameManager->TooltipAppendText(TooltipHandle, UTF8_TO_TCHAR(Text));
	return 0;
}

// tooltip:GetItem() -- item name and link, as consumed by stock comparison tooltips.
static int LF_TooltipGetItem(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushnil(L);
		return 1;
	}

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const FTooltipState* State = Ctx->FrameManager->GetTooltipState(TooltipHandle);
	if (State && !State->ItemLink.IsEmpty())
	{
		lua_pushstring(L, TCHAR_TO_UTF8(*State->ItemName));
		lua_pushstring(L, TCHAR_TO_UTF8(*State->ItemLink));
		return 2;
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}

// tooltip:GetSpell() -- returns spell ID for currently displayed spell
static int LF_TooltipGetSpell(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushnil(L);
		return 1;
	}

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const FTooltipState* State = Ctx->FrameManager->GetTooltipState(TooltipHandle);
	if (State && State->SpellId > 0)
	{
		lua_pushnumber(L, State->SpellId);
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}

// tooltip:GetUnit() -- returns unit token for currently displayed unit
static int LF_TooltipGetUnit(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager)
	{
		lua_pushnil(L);
		return 1;
	}

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const FTooltipState* State = Ctx->FrameManager->GetTooltipState(TooltipHandle);
	if (State && !State->UnitToken.IsEmpty())
	{
		lua_pushstring(L, TCHAR_TO_UTF8(*State->UnitToken));
	}
	else
	{
		lua_pushnil(L);
	}
	return 1;
}

// tooltip:SetItem(itemLink) -- sets item context for tooltip
static int LF_TooltipSetItem(lua_State* L)
{
	return WowLuaApi::SetHyperlinkTooltip(L);
}

// tooltip:SetSpell(spellId) -- sets spell context for tooltip
static int LF_TooltipSetSpell(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	int32 SpellId = static_cast<int32>(luaL_checknumber(L, 2));
	Ctx->FrameManager->TooltipSetSpell(TooltipHandle, SpellId);
	return 0;
}

// tooltip:SetUnit(unitToken) -- sets unit context for tooltip
static int LF_TooltipSetUnit(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const char* UnitToken = luaL_checkstring(L, 2);
	Ctx->FrameManager->TooltipSetUnit(TooltipHandle, UTF8_TO_TCHAR(UnitToken));
	return 0;
}

// ── Metatable Setup ──────────────────────────────────────────────────────────────

static const luaL_Reg FrameMethods[] =
{
	// Core
	{"GetName", LF_GetName},
	{"GetObjectType", LF_GetObjectType},
	{"IsObjectType", [](lua_State* L)->int {
        const FString Wanted=UTF8_TO_TCHAR(luaL_checkstring(L,2)); LF_GetObjectType(L); const FString Actual=UTF8_TO_TCHAR(lua_tostring(L,-1)); lua_pop(L,1);
        bool Match=Actual.Equals(Wanted,ESearchCase::IgnoreCase) || Wanted.Equals(TEXT("Object"),ESearchCase::IgnoreCase);
        const bool Frame=Actual!=TEXT("Texture") && Actual!=TEXT("FontString") && Actual!=TEXT("Font");
        Match|=Frame && (Wanted.Equals(TEXT("Frame"),ESearchCase::IgnoreCase) || Wanted.Equals(TEXT("ScriptObject"),ESearchCase::IgnoreCase));
        Match|=Actual!=TEXT("Font") && Wanted.Equals(TEXT("Region"),ESearchCase::IgnoreCase);
        Match|=Actual==TEXT("CheckButton") && Wanted.Equals(TEXT("Button"),ESearchCase::IgnoreCase);
        Match|=Actual==TEXT("PlayerModel") && Wanted.Equals(TEXT("Model"),ESearchCase::IgnoreCase);
        lua_pushboolean(L,Match); return 1;
    }},
	{"RegisterEvent", LF_RegisterEvent},
	{"UnregisterEvent", LF_UnregisterEvent},
	{"UnregisterAllEvents", LF_UnregisterAllEvents},
	{"SetScript", LF_SetScript},
	{"GetScript", LF_GetScript},
	{"HookScript", LF_HookScript},
	{"HasScript", LF_HasScript},

	// Visibility
	{"Show", LF_Show},
	{"Hide", LF_Hide},
	{"IsShown", LF_IsShown},
	{"IsVisible", LF_IsVisible},
	{"CreateAnimationGroup", LF_CreateAnimationGroup},
    {"StopAnimating", LF_StopAnimating},
    {"GetAnimationGroups", LF_GetAnimationGroups},
	{"Play", LF_PlayObject},
	{"Stop", LF_StopObject},
	{"IsPlaying", LF_IsObjectPlaying},
	{"IsMouseOver", LF_IsMouseOver},

	// Positioning
	{"SetPoint", LF_SetPoint},
	{"ClearAllPoints", LF_ClearAllPoints},
	{"SetAllPoints", LF_SetAllPoints},
	{"SetSize", LF_SetSize},
	{"SetWidth", LF_SetWidth},
	{"SetHeight", LF_SetHeight},
	{"GetWidth", LF_GetWidth},
	{"GetHeight", LF_GetHeight},
	{"GetCenter", LF_GetCenter},
	{"GetLeft", LF_GetLeft},
	{"GetRight", LF_GetRight},
	{"GetTop", LF_GetTop},
	{"GetBottom", LF_GetBottom},

	// Appearance
	{"SetAlpha", LF_SetAlpha},
	{"GetAlpha", LF_GetAlpha},
	{"SetBackdrop", LF_SetBackdrop},
    {"GetBackdrop", LF_GetBackdrop},
    {"GetBackdropColor", [](lua_State* L)->int { return PushBackdropColor(L,false); }},
    {"GetBackdropBorderColor", [](lua_State* L)->int { return PushBackdropColor(L,true); }},
	{"SetBackdropColor", LF_SetBackdropColor},
	{"SetBackdropBorderColor", LF_SetBackdropBorderColor},

	// Strata/Level
	{"SetFrameStrata", LF_SetFrameStrata},
	{"GetFrameStrata", LF_GetFrameStrata},
	{"SetFrameLevel", LF_SetFrameLevel},
	{"GetFrameLevel", LF_GetFrameLevel},
	{"Raise", LF_Raise},
	{"Lower", LF_Lower},
	{"SetToplevel", LF_SetToplevel},

	// Input
	{"EnableMouse", LF_EnableMouse},
	{"EnableKeyboard", LF_EnableKeyboard},
	{"EnableMouseWheel", LF_EnableMouseWheel},
	{"SetMovable", LF_SetMovable},
	{"SetResizable", LF_SetResizable},
	{"SetClampedToScreen", LF_SetClampedToScreen},
	{"RegisterForDrag", LF_RegisterForDrag},
	{"StartMoving", LF_StartMoving},
	{"StartSizing", LF_StartSizing},
	{"IsMovable", [](lua_State* L)->int { return LF_GetFrameFlag(L); }},
	{"IsResizable", [](lua_State* L)->int { return LF_GetFrameFlag(L,true); }},
	{"StopMovingOrSizing", LF_StopMovingOrSizing},

	// Hierarchy
	{"SetParent", LF_SetParent},
	{"GetParent", LF_GetParent},
	{"GetChildren", LF_GetChildren},
	{"GetNumChildren", LF_GetNumChildren},

	// ID/Attributes
	{"SetID", LF_SetID},
	{"GetID", LF_GetID},
	{"SetAttribute", LF_SetAttribute},
	{"GetAttribute", LF_GetAttribute},

	// Creation
	{"CreateTexture", LF_CreateTexture},
	{"CreateFontString", LF_CreateFontString},

	// Button
	{"SetNormalTexture", LF_SetNormalTexture},
	{"SetPushedTexture", LF_SetPushedTexture},
	{"SetHighlightTexture", LF_SetHighlightTexture},
	{"SetDisabledTexture", LF_SetDisabledTexture},
	{"SetText", LF_SetText},
	{"GetText", LF_GetText},
	{"Disable", LF_Disable},
	{"Enable", LF_Enable},
	{"IsEnabled", LF_IsEnabled},
	{"Click", LF_Click},

	// StatusBar
	{"SetMinMaxValues", LF_SetMinMaxValues},
	{"GetMinMaxValues", LF_GetMinMaxValues},
	{"SetValue", LF_SetValue},
	{"GetValue", LF_GetValue},
	{"SetStatusBarTexture", LF_SetStatusBarTexture},
	{"SetStatusBarColor", LF_SetStatusBarColor},
	{"SetOrientation", LF_SetOrientation},

	// EditBox
	{"SetMaxLetters", LF_SetMaxLetters},
	{"SetMaxBytes", [](lua_State* L)->int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->EditBoxMaxBytes.Add(GetFrameHandle(L),FMath::Max(int32(luaL_checkinteger(L,2)),0)); return 0; }},
	{"GetMaxBytes", [](lua_State* L)->int { auto* C=WowLuaApi::GetContext(L); lua_pushinteger(L,C && C->FrameManager?C->FrameManager->EditBoxMaxBytes.FindRef(GetFrameHandle(L)):0); return 1; }},
	{"IgnoreDepth", [](lua_State* L)->int { return 0; }},
	{"SetAutoFocus", LF_SetAutoFocus},
	{"ClearFocus", LF_ClearFocus},
	{"SetFocus", LF_SetFocus},
	{"HasFocus", LF_HasFocus},
	{"HighlightText", LF_HighlightText},
	{"SetCursorPosition", LF_SetCursorPosition},
	{"GetCursorPosition", [](lua_State* L)->int { if(auto* Edit=GetMultiLineEditor(L)) { lua_pushinteger(L,Edit->CursorOffset()); return 1; } return luaL_error(L,"Cursor queries are not supported by this input widget"); }},
	{"Insert", [](lua_State* L)->int { if(auto* Edit=GetMultiLineEditor(L)) { Edit->InsertAtCursor(UTF8_TO_TCHAR(luaL_checkstring(L,2))); return 0; } return luaL_error(L,"Cursor insertion is not supported by this input widget"); }},
	{"GetNumLetters", [](lua_State* L)->int { LF_GetText(L); const FString Text=UTF8_TO_TCHAR(lua_tostring(L,-1)); lua_pushinteger(L,Text.Len()); return 1; }},
	{"IsMultiLine", [](lua_State* L)->int { lua_pushboolean(L,GetMultiLineEditor(L)!=nullptr); return 1; }},
	{"SetTextInsets", LF_SetTextInsets},
	{"SetMultiLine", LF_SetMultiLine},
	{"SetFontObject", LF_SetFontObject},
	{"GetInputLanguage", [](lua_State* L) -> int { lua_pushstring(L, "ROMAN"); return 1; }},

	// ScrollFrame
	{"SetScrollChild", LF_SetScrollChild},
	{"UpdateScrollChildRect", [](lua_State* L)->int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->RefreshLayout(); return 0; }},
	{"GetScrollChild", LF_GetScrollChild},
	{"SetVerticalScroll", LF_SetVerticalScroll},
	{"GetVerticalScroll", LF_GetVerticalScroll},
	{"SetHorizontalScroll", LF_SetHorizontalScroll},
	{"GetHorizontalScroll", LF_GetHorizontalScroll},

	// Cooldown
	{"SetCooldown", LF_SetCooldown},

	// Additional stubs to prevent OnUpdate error spam
	{"IsOwned", [](lua_State* L) -> int { lua_getfield(L,1,"__tooltipOwner"); lua_pushboolean(L,lua_rawequal(L,-1,2)); return 1; }},
	{"GetFontString", [](lua_State* L) -> int {
		luaL_checktype(L, 1, LUA_TTABLE);
		lua_getfield(L, 1, "__name");
		if (lua_isstring(L, -1)) {
			// Try FrameNameText first, then FrameNameFontString
			FString BaseName = UTF8_TO_TCHAR(lua_tostring(L, -1));
			lua_pop(L, 1);
			lua_getglobal(L, TCHAR_TO_UTF8(*(BaseName + TEXT("Text"))));
			if (!lua_isnil(L, -1)) return 1;
			lua_pop(L, 1);
			lua_getglobal(L, TCHAR_TO_UTF8(*(BaseName + TEXT("FontString"))));
			if (!lua_isnil(L, -1)) return 1;
			lua_pop(L, 1);
		} else { lua_pop(L, 1); }
		// Return minimal table so callers don't crash
		lua_newtable(L);
		luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
		if (!lua_isnil(L, -1)) lua_setmetatable(L, -2); else lua_pop(L, 1);
		return 1;
	}},
	{"SetChecked", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->SetChecked(GetFrameHandle(L),lua_toboolean(L,2)!=0 && (!lua_isnumber(L,2) || lua_tonumber(L,2)!=0)); return 0; }},
	{"GetChecked", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); lua_pushboolean(L,C && C->FrameManager && C->FrameManager->GetChecked(GetFrameHandle(L))); return 1; }},
	{"SetNormalFontObject", [](lua_State* L) -> int { return ButtonFontObject(L,TEXT("Normal"),false); }},
    {"GetNormalFontObject", [](lua_State* L) -> int { return ButtonFontObject(L,TEXT("Normal"),true); }},
	{"SetHighlightFontObject", [](lua_State* L) -> int { return ButtonFontObject(L,TEXT("Highlight"),false); }},
    {"GetHighlightFontObject", [](lua_State* L) -> int { return ButtonFontObject(L,TEXT("Highlight"),true); }},
	{"SetDisabledFontObject", [](lua_State* L) -> int { return ButtonFontObject(L,TEXT("Disabled"),false); }},
    {"GetDisabledFontObject", [](lua_State* L) -> int { return ButtonFontObject(L,TEXT("Disabled"),true); }},
	{"LockHighlight", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->LockButtonHighlight(GetFrameHandle(L),true); return 0; }},
	{"UnlockHighlight", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->LockButtonHighlight(GetFrameHandle(L),false); return 0; }},
	{"GetNormalTexture", [](lua_State* L) -> int { return PushButtonTexture(L,TEXT("Normal")); }},
	{"GetPushedTexture", [](lua_State* L) -> int { return PushButtonTexture(L,TEXT("Pushed")); }},
	{"GetHighlightTexture", [](lua_State* L) -> int { return PushButtonTexture(L,TEXT("Highlight")); }},
	{"GetDisabledTexture", [](lua_State* L) -> int { return PushButtonTexture(L,TEXT("Disabled")); }},
    {"GetCheckedTexture", [](lua_State* L) -> int { return PushButtonTexture(L,TEXT("Checked")); }},
    {"GetDisabledCheckedTexture", [](lua_State* L) -> int { return PushButtonTexture(L,TEXT("DisabledChecked")); }},
    {"SetCheckedTexture", [](lua_State* L) -> int { return SetButtonTexture(L,TEXT("Checked")); }},
    {"SetDisabledCheckedTexture", [](lua_State* L) -> int { return SetButtonTexture(L,TEXT("DisabledChecked")); }},
	{"SetButtonState", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->SetButtonState(GetFrameHandle(L),UTF8_TO_TCHAR(luaL_checkstring(L,2)),lua_toboolean(L,3)!=0 && (!lua_isnumber(L,3) || lua_tonumber(L,3)!=0)); return 0; }},
	{"GetButtonState", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); const FString State=C && C->FrameManager?C->FrameManager->GetButtonState(GetFrameHandle(L)):TEXT("NORMAL"); lua_pushstring(L,TCHAR_TO_UTF8(*State)); return 1; }},
	{"RegisterForClicks", LF_RegisterForClicks},

	// Layout/Hit testing
	{"SetHitRectInsets", [](lua_State* L)->int { return LF_HitInsets(L,false); }},
	{"GetHitRectInsets", [](lua_State* L)->int { return LF_HitInsets(L,true); }},
	{"GetRect", LF_GetRect},
	{"GetScale", LF_GetScale},
	{"SetScale", LF_SetScale},
	{"GetEffectiveScale", LF_GetEffectiveScale},
	{"SetClipsChildren", [](lua_State* L) -> int { return 0; }},
	{"IsDragging", [](lua_State* L)->int { const auto* C=WowLuaApi::GetContext(L); lua_pushboolean(L,C && C->FrameManager && C->FrameManager->IsFrameDragging(GetMouseQueryHandle(L))); return 1; }},
	{"GetNumPoints", LF_GetNumPoints},
	{"GetPoint", LF_GetPoint},
	{"IsProtected", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
	{"CanChangeProtectedState", [](lua_State* L) -> int { lua_pushboolean(L, 1); return 1; }},
	{"SetUserPlaced", [](lua_State* L) -> int { return 0; }},
	{"IsUserPlaced", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
	{"SetDontSavePosition", [](lua_State* L) -> int { return 0; }},
	{"SetMaxResize", [](lua_State* L)->int { return LF_ResizeBounds(L,true,false); }},
	{"SetMinResize", [](lua_State* L)->int { return LF_ResizeBounds(L,false,false); }},
	{"GetMaxResize", [](lua_State* L)->int { return LF_ResizeBounds(L,true,true); }},
	{"GetMinResize", [](lua_State* L)->int { return LF_ResizeBounds(L,false,true); }},

	// Texture methods on frames
	{"SetRotation", [](lua_State* L) -> int { return LF_ModelScalar(L,false,false); }},
	{"SetSequence", [](lua_State* L) -> int { return 0; }},
	{"SetDesaturated", LF_TextureDesaturated},
	{"IsDesaturated", [](lua_State* L) -> int { lua_getfield(L,1,"__desaturated"); lua_pushboolean(L,lua_toboolean(L,-1)); return 1; }},
	{"SetBlendMode", LF_TextureBlendMode},
    {"GetBlendMode", [](lua_State* L) -> int { lua_getfield(L,1,"__blendMode"); if(lua_isnil(L,-1)) { lua_pop(L,1); lua_pushstring(L,"BLEND"); } return 1; }},
	{"SetNonBlocking", [](lua_State* L) -> int { return 0; }},
	{"SetDrawLayer", [](lua_State* L)->int { return LF_DrawLayer(L,false); }},
	{"GetDrawLayer", [](lua_State* L)->int { return LF_DrawLayer(L,true); }},

	// Text methods on frames
	{"GetTextHeight", [](lua_State* L)->int { return LF_TextHeight(L,true); }},
	{"SetVertexColor", LF_SetVertexColorDispatch},
	{"GetTextWidth", LF_FontStringGetStringWidth},
	{"GetStringWidth", LF_FontStringGetStringWidth},
	{"GetStringHeight", LF_GetHeight},
	{"SetAlphaGradient", [](lua_State* L)->int { lua_pushboolean(L,0); return 1; }},
	{"SetTextHeight", [](lua_State* L)->int { return LF_TextHeight(L,false); }},
	{"SetShadowOffset", [](lua_State* L)->int { return LF_TextShadow(L,true,false); }},
	{"SetShadowColor", [](lua_State* L)->int { return LF_TextShadow(L,false,false); }},
	{"SetSpacing", [](lua_State* L)->int { return LF_TextSpacing(L,false); }},
	{"SetJustifyH", [](lua_State* L)->int { return LF_Justify(L,false,false); }},
    {"GetJustifyH", [](lua_State* L)->int { return LF_Justify(L,false,true); }},
    {"GetJustifyV", [](lua_State* L)->int { return LF_Justify(L,true,true); }},
    {"CanWordWrap", [](lua_State* L)->int { return LF_TextWrap(L,false,true); }},
    {"CanNonSpaceWrap", [](lua_State* L)->int { return LF_TextWrap(L,true,true); }},
    {"GetSpacing", [](lua_State* L)->int { return LF_TextSpacing(L,true); }},
    {"GetShadowOffset", [](lua_State* L)->int { return LF_TextShadow(L,true,true); }},
    {"GetShadowColor", [](lua_State* L)->int { return LF_TextShadow(L,false,true); }},
	{"SetFormattedText", LF_SetFormattedText},
	{"SetWordWrap", [](lua_State* L)->int { return LF_TextWrap(L,false,false); }},
	{"SetNonSpaceWrap", [](lua_State* L)->int { return LF_TextWrap(L,true,false); }},
	{"SetJustifyV", [](lua_State* L)->int { return LF_Justify(L,true,false); }},

	// Button methods
	{"SetMotionScriptsWhileDisabled", [](lua_State* L)->int { return LF_MotionWhileDisabled(L,false); }},
	{"GetMotionScriptsWhileDisabled", [](lua_State* L)->int { return LF_MotionWhileDisabled(L,true); }},

	// Missing common methods
	{"SetPadding", [](lua_State* L) -> int { return 0; }},
	{"GetPadding", [](lua_State* L) -> int { lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 4; }},
	{"SetIndentedWordWrap", [](lua_State* L) -> int { return 0; }},
	{"SetClampRectInsets", [](lua_State* L)->int { return LF_ClampInsets(L,false); }},
	{"GetClampRectInsets", [](lua_State* L)->int { return LF_ClampInsets(L,true); }},
	{"IsClampedToScreen", [](lua_State* L)->int { return LF_GetFrameFlag(L,false,true); }},
	{"SetHyperlinksEnabled", [](lua_State* L) -> int { return 0; }},
	{"GetMaxLines", [](lua_State* L) -> int { lua_pushnumber(L, 0); return 1; }},
	{"SetHistoryLines", [](lua_State* L) -> int { lua_pushinteger(L,FMath::Clamp(int32(luaL_checkinteger(L,2)),0,100)); lua_setfield(L,1,"__historyLimit"); return 0; }},
	{"AddHistoryLine", [](lua_State* L) -> int {
		const char* Text=luaL_checkstring(L,2); lua_getfield(L,1,"__historyLimit"); const int32 Limit=lua_isnumber(L,-1)?lua_tointeger(L,-1):32; lua_pop(L,1);
		if(!Limit || !*Text) return 0;
		lua_getfield(L,1,"__history"); if(!lua_istable(L,-1)) { lua_pop(L,1); lua_newtable(L); lua_pushvalue(L,-1); lua_setfield(L,1,"__history"); }
		int32 Count=lua_objlen(L,-1); if(Count>=Limit) { for(int32 I=1;I<Count;++I) { lua_rawgeti(L,-1,I+1); lua_rawseti(L,-2,I); } --Count; }
		lua_pushstring(L,Text); lua_rawseti(L,-2,Count+1); lua_pop(L,1); return 0;
	}},
	{"SetFontObject", LF_SetFontObject},
	{"GetFontObject", LF_GetFontObject},

	// ScrollingMessageFrame methods
	{"AddMessage", LF_AddMessage},
	{"SetMaxLines", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->MessageFrames.FindOrAdd(GetFrameHandle(L)).Limit=FMath::Clamp(int32(luaL_checkinteger(L,2)),1,1000); return 0; }},
	{"SetFading", [](lua_State* L) -> int { return 0; }},
	{"SetFadeDuration", [](lua_State* L) -> int { return 0; }},
	{"SetTimeVisible", [](lua_State* L) -> int { return 0; }},
	{"SetInsertMode", [](lua_State* L) -> int { return 0; }},
	{"Clear", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) { C->FrameManager->MessageFrames.FindOrAdd(GetFrameHandle(L)).Lines.Reset(); C->FrameManager->DrawMessages(GetFrameHandle(L)); } return 0; }},
	{"GetVerticalScrollRange", [](lua_State* L) -> int {
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		if (!Ctx || !Ctx->FrameManager)
		{
			lua_pushnumber(L, 0);
			return 1;
		}

		int64 ScrollFrameHandle = GetFrameHandle(L, 1);
		if (ScrollFrameHandle < 0)
		{
			lua_pushnumber(L, 0);
			return 1;
		}

		float Range = Ctx->FrameManager->GetVerticalScrollRange(ScrollFrameHandle);
		lua_pushnumber(L, Range);
		return 1;
	}},
	{"GetHorizontalScrollRange", [](lua_State* L) -> int {
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		if (!Ctx || !Ctx->FrameManager)
		{
			lua_pushnumber(L, 0);
			return 1;
		}

		int64 ScrollFrameHandle = GetFrameHandle(L, 1);
		if (ScrollFrameHandle < 0)
		{
			lua_pushnumber(L, 0);
			return 1;
		}

		float Range = Ctx->FrameManager->GetHorizontalScrollRange(ScrollFrameHandle);
		lua_pushnumber(L, Range);
		return 1;
	}},
	{"SetScrollRange", [](lua_State* L) -> int { return 0; }},
	{"GetNumMessages", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); lua_pushinteger(L,C && C->FrameManager?C->FrameManager->MessageFrames.FindOrAdd(GetFrameHandle(L)).Lines.Num():0); return 1; }},
	{"GetNumLinesDisplayed", [](lua_State* L) -> int { lua_pushnumber(L, 0); return 1; }},
	{"AtTop", [](lua_State* L) -> int { lua_pushboolean(L, 1); return 1; }},
	{"AtBottom", [](lua_State* L) -> int { lua_pushboolean(L, 1); return 1; }},
	{"ScrollUp", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->ScrollMessages(GetFrameHandle(L),1); return 0; }},
	{"ScrollDown", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->ScrollMessages(GetFrameHandle(L),-1); return 0; }},
	{"ScrollToTop", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->ScrollMessages(GetFrameHandle(L),10000,true); return 0; }},
	{"ScrollToBottom", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->ScrollMessages(GetFrameHandle(L),0,true); return 0; }},
	{"PageUp", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->ScrollMessages(GetFrameHandle(L),10); return 0; }},
	{"PageDown", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->ScrollMessages(GetFrameHandle(L),-10); return 0; }},

	// Model/Dress methods
	{"SetModel", [](lua_State* L) -> int { return 0; }},
	{"SetModelScale", [](lua_State* L) -> int { return LF_ModelScalar(L,true,false); }},
    {"GetModelScale", [](lua_State* L) -> int { return LF_ModelScalar(L,true,true); }},
	{"SetPosition", [](lua_State* L) -> int { return LF_ModelPosition(L,false); }},
    {"GetPosition", [](lua_State* L) -> int { return LF_ModelPosition(L,true); }},
	{"SetFacing", [](lua_State* L) -> int { return LF_ModelScalar(L,false,false); }},
    {"GetFacing", [](lua_State* L) -> int { return LF_ModelScalar(L,false,true); }},
	{"SetUnit", LF_ModelUnit},
	{"SetCreature", [](lua_State* L) -> int { return 0; }},
	{"SetCamera", [](lua_State* L) -> int { return 0; }},
	{"RefreshUnit", LF_ModelUnit},
	{"SetPortraitZoom", [](lua_State* L) -> int { return 0; }},
	{"ClearModel", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager && C->FrameManager->OnUnitModel) C->FrameManager->OnUnitModel(GetFrameHandle(L,1),0); return 0; }},
	{"HasCustomBackground", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
	{"SetCustomRace", [](lua_State* L) -> int { return 0; }},
	{"Undress", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager && C->FrameManager->OnModelDress) C->FrameManager->OnModelDress(GetFrameHandle(L,1),false); return 0; }},
    {"Dress", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager && C->FrameManager->OnModelDress) C->FrameManager->OnModelDress(GetFrameHandle(L,1),true); return 0; }},
	{"TryOn", WowLuaApi::TryOnItem},

	// Minimap methods
	{"SetZoom", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->MinimapZoom=FMath::Clamp(int32(luaL_checkinteger(L,2)),0,5); return 0; }},
	{"GetZoom", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); lua_pushinteger(L,C && C->FrameManager?C->FrameManager->MinimapZoom:0); return 1; }},
	{"SetPlayerTextureWidth", [](lua_State* L) -> int { return 0; }},
	{"SetPlayerTextureHeight", [](lua_State* L) -> int { return 0; }},
	{"GetZoomLevels", [](lua_State* L) -> int { lua_pushnumber(L, 6); return 1; }},
	{"SetBlipTexture", [](lua_State* L) -> int { return 0; }},
	{"SetStaticArrowTexture", [](lua_State* L) -> int { return 0; }},

	// Tooltip methods
	{"SetOwner", LF_TooltipSetOwner},
	{"SetUnitBuff", [](lua_State* L) -> int { lua_pushstring(L,"UnitBuff"); lua_pushcclosure(L,WowLuaApi::SetAuraTooltip,1); lua_pushvalue(L,1); lua_pushvalue(L,2); lua_pushvalue(L,3); lua_pushvalue(L,4); lua_call(L,4,0); return 0; }},
	{"SetUnitDebuff", [](lua_State* L) -> int { lua_pushstring(L,"UnitDebuff"); lua_pushcclosure(L,WowLuaApi::SetAuraTooltip,1); lua_pushvalue(L,1); lua_pushvalue(L,2); lua_pushvalue(L,3); lua_pushvalue(L,4); lua_call(L,4,0); return 0; }},
	{"SetUnitAura", WowLuaApi::SetAuraTooltip},
	{"SetInventoryItem", WowLuaApi::SetInventoryItemTooltip},
	{"SetBagItem", WowLuaApi::SetBagItemTooltip},
    {"SetMerchantItem",WowLuaApi::SetMerchantItemTooltip},
    {"SetBuybackItem",WowLuaApi::SetBuybackItemTooltip},
    {"SetInboxItem",WowLuaApi::SetInboxItemTooltip},
    {"SetSendMailItem",WowLuaApi::SetSendMailItemTooltip},
	{"SetAction", WowLuaApi::SetActionTooltip},
	{"SetSpell", WowLuaApi::SetSpellTooltip},
	{"SetSpellByID", WowLuaApi::SetSpellTooltip},
	{"SetHyperlink", WowLuaApi::SetHyperlinkTooltip},
	{"SetHyperlinkCompareItem", WowLuaApi::SetHyperlinkCompareItem},
	{"AddLine", LF_TooltipAddLine},
	{"AddDoubleLine", LF_TooltipAddDoubleLine},
	{"ClearLines", LF_TooltipClearLines},
	{"NumLines", LF_TooltipNumLines},
	{"SetMinimumWidth", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->TooltipSetMinimumWidth(GetFrameHandle(L,1),luaL_checknumber(L,2)); return 0; }},
	{"GetMinimumWidth", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); const auto* S=C && C->FrameManager?C->FrameManager->GetTooltipState(GetFrameHandle(L,1)):nullptr; lua_pushnumber(L,S?S->MinimumWidth:0); return 1; }},
	{"AppendText", LF_TooltipAppendText},
	{"GetItem", LF_TooltipGetItem},
	{"GetSpell", LF_TooltipGetSpell},
	{"GetUnit", LF_TooltipGetUnit},
	{"SetItem", LF_TooltipSetItem},
	{"GetAnchorType", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); const auto* S=C && C->FrameManager?C->FrameManager->GetTooltipState(GetFrameHandle(L,1)):nullptr; lua_pushstring(L,S?TCHAR_TO_UTF8(*S->AnchorType):"ANCHOR_NONE"); return 1; }},
	{"SetAnchorType", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->TooltipSetAnchorType(GetFrameHandle(L,1),UTF8_TO_TCHAR(luaL_checkstring(L,2)),luaL_optnumber(L,3,0),luaL_optnumber(L,4,0)); return 0; }},
	{"FadeOut", [](lua_State* L) -> int { return 0; }},

	// Cooldown frame
	{"SetReverse", [](lua_State* L) -> int { return 0; }},
	{"SetSwipeColor", [](lua_State* L) -> int { return 0; }},
	{"SetBlingTexture", [](lua_State* L) -> int { return 0; }},
	{"SetEdgeTexture", [](lua_State* L) -> int { return 0; }},

	// Slider
	{"SetValueStep", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->SetSliderStep(GetFrameHandle(L),luaL_checknumber(L,2)); return 0; }},
    {"GetValueStep", [](lua_State* L) -> int { const auto* C=WowLuaApi::GetContext(L); const auto* D=C && C->FrameManager?C->FrameManager->GetFrameDef(GetFrameHandle(L)):nullptr; lua_pushnumber(L,D?D->ValueStep:0); return 1; }},
    {"GetOrientation", [](lua_State* L) -> int { const auto* C=WowLuaApi::GetContext(L); const auto* D=C && C->FrameManager?C->FrameManager->GetFrameDef(GetFrameHandle(L)):nullptr; lua_pushstring(L,D && D->Orientation==TEXT("VERTICAL")?"VERTICAL":"HORIZONTAL"); return 1; }},
    {"GetThumbTexture", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); const FString Name=C && C->FrameManager?C->FrameManager->SliderThumbRegions.FindRef(GetFrameHandle(L)):FString(); if(Name.IsEmpty()) lua_pushnil(L); else lua_getglobal(L,TCHAR_TO_UTF8(*Name)); return 1; }},
    {"SetThumbTexture", [](lua_State* L) -> int {
        auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) return 0; auto* Frames=C->FrameManager;
        const int64 Handle=GetFrameHandle(L); if(!Frames->GetSliderWidget(Handle)) return 0;
        const FString Before=Frames->SliderThumbRegions.FindRef(Handle);
        if(lua_isnoneornil(L,2))
        {
            if(auto* Old=Frames->GetNamedTexture(Before)) Old->SetVisibility(ESlateVisibility::Collapsed);
            Frames->SliderThumbRegions.Remove(Handle); Frames->GetSliderWidget(Handle)->SetLocked(true); return 0;
        }
        if(lua_istable(L,2))
        {
            lua_getfield(L,2,"__name"); const FString Name=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):TEXT(""); lua_pop(L,1);
            if(!GetTextureImage(L,2) || !Frames->ReparentRegion(Name,Handle)) return luaL_error(L,"SetThumbTexture expects a texture region");
            if(Before!=Name) if(auto* Old=Frames->GetNamedTexture(Before)) Old->SetVisibility(ESlateVisibility::Collapsed);
            Frames->SliderThumbRegions.Add(Handle,Name); Frames->GetNamedTexture(Name)->SetVisibility(ESlateVisibility::HitTestInvisible);
            lua_pushinteger(L,Handle); lua_setfield(L,2,"__parentHandle");
        }
        else
        {
            const FString Name=Frames->EnsureSliderThumb(Handle); lua_getglobal(L,TCHAR_TO_UTF8(*Name)); lua_replace(L,1); LF_TextureSetTexture(L);
        }
        Frames->UpdateSliderThumb(Handle); return 0;
    }},
	{"SetObeyStepOnDrag", [](lua_State* L) -> int { return 0; }},

	// Texture access (region globals use frame metatable)
	{"SetTexture", LF_TextureSetTexture},
	{"SetTexCoord", LF_TextureSetTexCoord},
	{"SetTalent", WowLuaApi::SetTalentTooltip},
    {"SetGlyph", WowLuaApi::SetGlyphTooltip},
	{"GetTexCoord", LF_GetTexCoord},
	{"GetTexture", [](lua_State* L) -> int {
		luaL_checktype(L, 1, LUA_TTABLE);
		lua_getfield(L, 1, "__texture");
		return 1;
	}},
	{"GetOwner", [](lua_State* L) -> int {
		luaL_checktype(L, 1, LUA_TTABLE);
		lua_getfield(L, 1, "__tooltipOwner");
		return 1;
	}},

	// Font access
	{"SetFont", LF_FontStringSetFont},
	{"GetFont", LF_FontStringGetFont},
	{"SetTextColor", LF_FontStringSetTextColor},
	{"GetTextColor", [](lua_State* L) -> int {
		const auto* Text=GetTextBlockWidgetForTable(L);
		auto Color=Text?Text->GetColorAndOpacity().GetSpecifiedColor():FLinearColor::White;
		if(!Text) { float* Parts[]={&Color.R,&Color.G,&Color.B,&Color.A}; const char* Keys[]={"__textColorR","__textColorG","__textColorB","__textColorA"}; for(int32 I=0;I<4;++I) { lua_getfield(L,1,Keys[I]); *Parts[I]=luaL_optnumber(L,-1,1); lua_pop(L,1); } }
		lua_pushnumber(L,Color.R); lua_pushnumber(L,Color.G); lua_pushnumber(L,Color.B); lua_pushnumber(L,Color.A); return 4;
	}},
	{"GetVertexColor", [](lua_State* L) -> int {
		const auto* Image=GetTextureImage(L,1);
		const auto Color=Image?Image->GetColorAndOpacity():FLinearColor::White;
		lua_pushnumber(L,Color.R); lua_pushnumber(L,Color.G); lua_pushnumber(L,Color.B); lua_pushnumber(L,Color.A); return 4;
	}},

	{nullptr, nullptr}
};

void WowLuaApi::RegisterFrameApi(lua_State* L)
{
	// Create the WowFrame metatable
	luaL_newmetatable(L, FRAME_METATABLE);

	// Set __index to itself so methods are found via metatable lookup
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	// Register all frame methods
	for (const luaL_Reg* Reg = FrameMethods; Reg->name; Reg++)
	{
		lua_pushcfunction(L, Reg->func);
		lua_setfield(L, -2, Reg->name);
	}

	lua_pop(L, 1); // pop metatable

	// Register global CreateFrame
	lua_register(L, "CreateFrame", L_CreateFrame);

	// Register global CreateTexture(name, parent, drawLayer, inherits, subLevel)
	// WoW API — MoneyFrame.lua and others call this as a global function
	lua_register(L, "CreateTexture", LF_CreateTextureGlobal);

	// Register global CreateFont() - WoW 3.3.5 API for creating font objects
	lua_register(L, "CreateFont", LF_CreateFont);

	UE_LOG(LogWowLuaFrame, Log, TEXT("Registered WoW Frame API (CreateFrame + %d methods)"),
		(int)(sizeof(FrameMethods) / sizeof(FrameMethods[0]) - 1));
}

#else
void WowLuaApi::RegisterFrameApi(lua_State*) {}
#endif
