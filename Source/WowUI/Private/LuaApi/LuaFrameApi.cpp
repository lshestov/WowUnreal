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
		if (UImage* Image = GetTextureImage(L, 1))
		{
			Image->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			return 0;
		}
		if (UTextBlock* TextBlock = GetTextBlockWidgetForTable(L, 1))
		{
			TextBlock->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
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
		lua_pushboolean(L,Own && C && C->FrameManager && C->FrameManager->IsFrameHierarchyShown(Parent));
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
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		FWowFrameDef* Def = Ctx->FrameManager->GetMutableFrameDef(Handle);
		if (Def)
		{
			FString Relative = Def->Parent;
			if (lua_isstring(L, 2)) Relative = UTF8_TO_TCHAR(lua_tostring(L, 2));
			else if (lua_istable(L, 2))
			{
				const auto* Target = Ctx->FrameManager->GetFrameDef(GetFrameHandle(L, 2));
				if (Target) Relative = Target->Name;
			}
			FWowAnchor Top, Bottom;
			Top.Point = Top.RelativePoint = EWowAnchorPoint::TOPLEFT; Top.RelativeTo = Relative;
			Bottom.Point = Bottom.RelativePoint = EWowAnchorPoint::BOTTOMRIGHT; Bottom.RelativeTo = Relative;
			Ctx->FrameManager->SetFrameAnchors(Handle, {Top, Bottom});
		}
	}
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
	return 0;
}

// frame:SetResizable(resizable)
static int LF_SetResizable(lua_State* L)
{
	return 0;
}

// frame:SetClampedToScreen(clamped)
static int LF_SetClampedToScreen(lua_State* L)
{
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
			NamedImage->SetRenderOpacity(1.0f); // Make visible (may have been hidden as empty placeholder)

			// If the slot was auto-sized (from creation with no explicit size),
			// update it to match the texture dimensions × UIScale.
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(NamedImage->Slot))
			{
				FVector2D CurrentSize = Slot->GetSize();
				if (CurrentSize.X <= 0.f || CurrentSize.Y <= 0.f)
				{
					float UIScale = Ctx->FrameManager->GetUIScale();
					Slot->SetAutoSize(false);
					Slot->SetSize(FVector2D(Brush.ImageSize.X * UIScale, Brush.ImageSize.Y * UIScale));
				}
			}
			return 0;
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
	luaL_checktype(L, 1, LUA_TTABLE);
	float Left = static_cast<float>(luaL_checknumber(L, 2));
	float Right = static_cast<float>(luaL_checknumber(L, 3));
	float Top = static_cast<float>(luaL_checknumber(L, 4));
	float Bottom = static_cast<float>(luaL_checknumber(L, 5));

	// Store in Lua table
	lua_pushnumber(L, Left); lua_setfield(L, 1, "__texLeft");
	lua_pushnumber(L, Right); lua_setfield(L, 1, "__texRight");
	lua_pushnumber(L, Top); lua_setfield(L, 1, "__texTop");
	lua_pushnumber(L, Bottom); lua_setfield(L, 1, "__texBottom");

	// Apply UV region to the UImage widget
	// WoW uses Left > Right for horizontal flip and Top > Bottom for vertical flip.
	// Normalize UVs for FBox2D and apply render transform for flipping.
	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		float UVL = FMath::Min(Left, Right);
		float UVR = FMath::Max(Left, Right);
		float UVT = FMath::Min(Top, Bottom);
		float UVB = FMath::Max(Top, Bottom);

		FSlateBrush Brush = Img->GetBrush();
		FBox2D UVRegion(FVector2D(UVL, UVT), FVector2D(UVR, UVB));
		Brush.SetUVRegion(UVRegion);
		Img->SetBrush(Brush);

		bool bFlipH = Left > Right;
		bool bFlipV = Top > Bottom;
		if (bFlipH || bFlipV)
		{
			FVector2D Scale(bFlipH ? -1.0f : 1.0f, bFlipV ? -1.0f : 1.0f);
			Img->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			Img->SetRenderTransform(FWidgetTransform(FVector2D::ZeroVector, Scale, FVector2D::ZeroVector, 0.0f));
		}
		else
		{
			// Clear any previous flip transform
			Img->SetRenderTransform(FWidgetTransform());
		}
	}

	return 0;
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

static int LF_TextureSetAlpha(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float Alpha = static_cast<float>(luaL_checknumber(L, 2));

	// Store in Lua table
	lua_pushnumber(L, Alpha);
	lua_setfield(L, 1, "__alpha");

	// Apply alpha to the texture region
	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		Img->SetRenderOpacity(Alpha);
	}

	return 0;
}

// Dynamic texture: Show
static int LF_TextureShow(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L,1,"__name"); const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,-1,"")); lua_pop(L,1);
	if(auto* C=WowLuaApi::GetContext(L); C && C->FrameManager && C->FrameManager->SetButtonRegionShown(Name,true)) return 0;
	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	return 0;
}

// Dynamic texture: Hide
static int LF_TextureHide(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L,1,"__name"); const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,-1,"")); lua_pop(L,1);
	if(auto* C=WowLuaApi::GetContext(L); C && C->FrameManager && C->FrameManager->SetButtonRegionShown(Name,false)) return 0;
	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		Img->SetVisibility(ESlateVisibility::Collapsed);
	}
	return 0;
}

// Dynamic texture: SetSize
static int LF_TextureSetSize(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float W = static_cast<float>(luaL_checknumber(L, 2));
	float H = static_cast<float>(luaL_checknumber(L, 3));

	lua_pushnumber(L, W); lua_setfield(L, 1, "__width");
	lua_pushnumber(L, H); lua_setfield(L, 1, "__height");

	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Img->Slot);
		if (Slot)
		{
			FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
			float UIScale = (Ctx && Ctx->FrameManager) ? Ctx->FrameManager->GetUIScale() : 1.0f;
			Slot->SetSize(FVector2D(W * UIScale, H * UIScale));
		}
	}
	return 0;
}

// Dynamic texture: SetWidth
static int LF_TextureSetWidth(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float W = static_cast<float>(luaL_checknumber(L, 2));
	lua_pushnumber(L, W); lua_setfield(L, 1, "__width");

	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Img->Slot);
		if (Slot)
		{
			FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
			float UIScale = (Ctx && Ctx->FrameManager) ? Ctx->FrameManager->GetUIScale() : 1.0f;
			FVector2D Size = Slot->GetSize();
			Slot->SetSize(FVector2D(W * UIScale, Size.Y));
		}
	}
	return 0;
}

// Dynamic texture: SetHeight
static int LF_TextureSetHeight(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	float H = static_cast<float>(luaL_checknumber(L, 2));
	lua_pushnumber(L, H); lua_setfield(L, 1, "__height");

	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Img->Slot);
		if (Slot)
		{
			FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
			float UIScale = (Ctx && Ctx->FrameManager) ? Ctx->FrameManager->GetUIScale() : 1.0f;
			FVector2D Size = Slot->GetSize();
			Slot->SetSize(FVector2D(Size.X, H * UIScale));
		}
	}
	return 0;
}

// Dynamic texture: GetWidth
static int LF_TextureGetWidth(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "__width");
	if (!lua_isnumber(L, -1))
	{
		lua_pop(L, 1);
		lua_pushnumber(L, 0);
	}
	return 1;
}

// Dynamic texture: GetHeight
static int LF_TextureGetHeight(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_getfield(L, 1, "__height");
	if (!lua_isnumber(L, -1))
	{
		lua_pop(L, 1);
		lua_pushnumber(L, 0);
	}
	return 1;
}

// Dynamic texture: SetAllPoints — flag for filling parent
static int LF_TextureSetAllPoints(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushboolean(L, 1);
	lua_setfield(L, 1, "__setAllPoints");

	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Img->Slot);
		if (Slot)
		{
			Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			Slot->SetOffsets(FMargin(0));
		}
	}
	return 0;
}

// Dynamic texture: SetPoint — stores position for when SetTexture creates the image
static int LF_TextureSetPoint(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);
	// Simplified: just store offset X/Y for positioning
	// Full anchor system would need the parent reference resolution
	int nargs = lua_gettop(L);
	float offX = 0.f, offY = 0.f;

	if (nargs >= 5 && lua_isnumber(L, 4))
	{
		offX = static_cast<float>(lua_tonumber(L, 4));
		offY = static_cast<float>(lua_tonumber(L, 5));
	}
	else if (nargs >= 4 && lua_isnumber(L, 3))
	{
		offX = static_cast<float>(lua_tonumber(L, 3));
		offY = static_cast<float>(lua_tonumber(L, 4));
	}

	lua_pushnumber(L, offX); lua_setfield(L, 1, "__posX");
	lua_pushnumber(L, offY); lua_setfield(L, 1, "__posY");

	UImage* Img = GetTextureImage(L, 1);
	if (Img)
	{
		UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Img->Slot);
		if (Slot)
		{
			FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
			float UIScale = (Ctx && Ctx->FrameManager) ? Ctx->FrameManager->GetUIScale() : 1.0f;
			Slot->SetPosition(FVector2D(offX * UIScale, offY * UIScale));
		}
	}
	return 0;
}

// frame:CreateTexture(name, layer, inherits)
static int LF_CreateTexture(lua_State* L)
{
	// Create a minimal texture table with a metatable
	lua_newtable(L);

	// Self should be a table (frame) at arg 1 from colon-syntax call
	if (!lua_istable(L, 1))
	{
		// Gracefully handle wrong arg types - return empty texture table
		luaL_getmetatable(L, WowLuaApi::FRAME_METATABLE);
		if (!lua_isnil(L, -1)) lua_setmetatable(L, -2); else lua_pop(L, 1);
		return 1;
	}

	// Store parent frame handle for texture regions
	int64 ParentHandle = GetFrameHandle(L);
	lua_pushinteger(L, ParentHandle);
	lua_setfield(L, -2, "__parentHandle");

	// Store name if provided (more forgiving - accept string, nil, or ignore tables)
	const char* Name = nullptr;
	if (lua_isstring(L, 2)) {
		Name = lua_tostring(L, 2);
	} else if (lua_isnil(L, 2) || lua_isnone(L, 2)) {
		Name = nullptr;
	} else {
		// Ignore other types (like tables) - don't error
		Name = nullptr;
	}

	if (Name && Name[0])
	{
		lua_pushstring(L, Name);
		lua_setfield(L, -2, "__name");
	}

	lua_pushstring(L, "Texture");
	lua_setfield(L, -2, "__objectType");

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager)
	{
		if (UCanvasPanel* ParentCanvas = Cast<UCanvasPanel>(Ctx->FrameManager->GetWidgetForHandle(ParentHandle)))
		{
			UImage* ImageWidget = NewObject<UImage>(ParentCanvas);
			FSlateBrush Empty; Empty.DrawAs = ESlateBrushDrawType::NoDrawType; ImageWidget->SetBrush(Empty);
			ImageWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* Slot = ParentCanvas->AddChildToCanvas(ImageWidget))
			{
				Slot->SetAnchors(FAnchors(0.f, 0.f));
				Slot->SetAutoSize(true);
			}

			lua_pushlightuserdata(L, ImageWidget);
			lua_setfield(L, -2, "__imageWidget");
		}
	}

	// Texture methods
	lua_pushcfunction(L, LF_TextureSetTexture);
	lua_setfield(L, -2, "SetTexture");
	lua_pushcfunction(L, LF_TextureSetTexCoord);
	lua_setfield(L, -2, "SetTexCoord");
	lua_pushcfunction(L, LF_TextureSetVertexColor);
	lua_setfield(L, -2, "SetVertexColor");
	lua_pushcfunction(L, LF_TextureSetAlpha);
	lua_setfield(L, -2, "SetAlpha");
	lua_pushcfunction(L, LF_TextureShow);
	lua_setfield(L, -2, "Show");
	lua_pushcfunction(L, LF_TextureHide);
	lua_setfield(L, -2, "Hide");
	lua_pushcfunction(L, LF_TextureSetPoint);
	lua_setfield(L, -2, "SetPoint");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // ClearAllPoints
	lua_setfield(L, -2, "ClearAllPoints");
	lua_pushcfunction(L, LF_TextureSetAllPoints);
	lua_setfield(L, -2, "SetAllPoints");
	lua_pushcfunction(L, LF_TextureSetSize);
	lua_setfield(L, -2, "SetSize");
	lua_pushcfunction(L, LF_TextureSetWidth);
	lua_setfield(L, -2, "SetWidth");
	lua_pushcfunction(L, LF_TextureSetHeight);
	lua_setfield(L, -2, "SetHeight");
	lua_pushcfunction(L, LF_TextureGetWidth);
	lua_setfield(L, -2, "GetWidth");
	lua_pushcfunction(L, LF_TextureGetHeight);
	lua_setfield(L, -2, "GetHeight");
	lua_pushcfunction(L, [](lua_State* L2) -> int { lua_pushboolean(L2, 1); return 1; }); // IsShown
	lua_setfield(L, -2, "IsShown");
	lua_pushcfunction(L, [](lua_State* L2) -> int { lua_pushstring(L2, "Texture"); return 1; }); // GetObjectType
	lua_setfield(L, -2, "GetObjectType");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetBlendMode
	lua_setfield(L, -2, "SetBlendMode");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetDesaturated
	lua_setfield(L, -2, "SetDesaturated");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetNonBlocking
	lua_setfield(L, -2, "SetNonBlocking");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetDrawLayer
	lua_setfield(L, -2, "SetDrawLayer");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetGradient
	lua_setfield(L, -2, "SetGradient");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetGradientAlpha
	lua_setfield(L, -2, "SetGradientAlpha");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetRotation
	lua_setfield(L, -2, "SetRotation");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetTexCoordModifiesRect
	lua_setfield(L, -2, "SetTexCoordModifiesRect");
	lua_pushcfunction(L, [](lua_State* L2) -> int { lua_pushnumber(L2, 0); lua_pushnumber(L2, 0); lua_pushnumber(L2, 1); lua_pushnumber(L2, 1); return 4; }); // GetTexCoord
	lua_setfield(L, -2, "GetTexCoord");

	if (Name && Name[0])
	{
		lua_pushvalue(L, -1);
		lua_setglobal(L, Name);
	}

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
	}

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
		TextBlock->SetFont(FontInfo);
	}

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
	// Create a minimal texture table with a metatable
	lua_newtable(L);

	// Store name if provided (arg 1)
	const char* Name = luaL_optstring(L, 1, nullptr);
	if (Name && Name[0]) {
		lua_pushstring(L, Name);
		lua_setfield(L, -2, "__name");
	}

	lua_pushstring(L, "Texture");
	lua_setfield(L, -2, "__objectType");

	// Store parent frame handle if provided (arg 2)
	int64 ParentHandle = -1;
	if (lua_istable(L, 2)) {
		lua_pushvalue(L, 2); // Push parent table to top
		ParentHandle = GetFrameHandle(L);
		lua_pop(L, 1); // Remove parent table copy
		lua_pushinteger(L, ParentHandle);
		lua_setfield(L, -2, "__parentHandle");
	}

	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (Ctx && Ctx->FrameManager && ParentHandle >= 0)
	{
		if (UCanvasPanel* ParentCanvas = Cast<UCanvasPanel>(Ctx->FrameManager->GetWidgetForHandle(ParentHandle)))
		{
			UImage* ImageWidget = NewObject<UImage>(ParentCanvas);
			ImageWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* Slot = ParentCanvas->AddChildToCanvas(ImageWidget))
			{
				Slot->SetAnchors(FAnchors(0.f, 0.f));
				Slot->SetAutoSize(true);
			}

			lua_pushlightuserdata(L, ImageWidget);
			lua_setfield(L, -2, "__imageWidget");
		}
	}

	// Store draw layer if provided (arg 3)
	const char* DrawLayer = luaL_optstring(L, 3, nullptr);
	if (DrawLayer && DrawLayer[0]) {
		lua_pushstring(L, DrawLayer);
		lua_setfield(L, -2, "__drawLayer");
	}

	// Store inherits template if provided (arg 4)
	const char* Inherits = luaL_optstring(L, 4, nullptr);
	if (Inherits && Inherits[0]) {
		lua_pushstring(L, Inherits);
		lua_setfield(L, -2, "__inherits");
	}

	// Store sublevel if provided (arg 5)
	if (lua_isnumber(L, 5)) {
		lua_pushvalue(L, 5);
		lua_setfield(L, -2, "__subLevel");
	}

	// Texture methods
	lua_pushcfunction(L, LF_TextureSetTexture);
	lua_setfield(L, -2, "SetTexture");
	lua_pushcfunction(L, LF_TextureSetTexCoord);
	lua_setfield(L, -2, "SetTexCoord");
	lua_pushcfunction(L, LF_TextureSetVertexColor);
	lua_setfield(L, -2, "SetVertexColor");
	lua_pushcfunction(L, LF_TextureSetAlpha);
	lua_setfield(L, -2, "SetAlpha");
	lua_pushcfunction(L, LF_TextureShow);
	lua_setfield(L, -2, "Show");
	lua_pushcfunction(L, LF_TextureHide);
	lua_setfield(L, -2, "Hide");

	// Common region methods
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetPoint
	lua_setfield(L, -2, "SetPoint");
	lua_pushcfunction(L, [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; }); // GetWidth
	lua_setfield(L, -2, "GetWidth");
	lua_pushcfunction(L, [](lua_State* L2) -> int { lua_pushnumber(L2, 0); return 1; }); // GetHeight
	lua_setfield(L, -2, "GetHeight");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetWidth
	lua_setfield(L, -2, "SetWidth");
	lua_pushcfunction(L, [](lua_State* L2) -> int { return 0; }); // SetHeight
	lua_setfield(L, -2, "SetHeight");
	lua_pushcfunction(L, [](lua_State* L2) -> int {
		lua_pushstring(L2, "Texture");
		return 1;
	}); // GetObjectType
	lua_setfield(L, -2, "GetObjectType");

	if (Name && Name[0])
	{
		lua_pushvalue(L, -1);
		lua_setglobal(L, Name);
	}

	return 1;
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
    C->FrameManager->RefreshButtonVisuals(Handle);
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
		FText TextContent = FText::FromString(WowUiPlainText(UTF8_TO_TCHAR(Text)));
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
			Slider->SetMinValue(MinValue);
			Slider->SetMaxValue(FMath::Max(MinValue, MaxValue));
			Slider->SetValue(FMath::Clamp(Slider->GetValue(), MinValue, FMath::Max(MinValue, MaxValue)));
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
	const bool bChanged = Slider ? !FMath::IsNearlyEqual(Slider->GetValue(), Value) : !lua_isnumber(L, -1) || lua_tonumber(L, -1) != Value;
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
		}
	}
	else
	{
		UE_LOG(LogWowLuaFrame, Warning, TEXT("SetValue called but missing context or non-numeric value"));
	}
	if (bChanged && Ctx && Ctx->EventSystem)
	{
		Ctx->EventSystem->RunFrameScript(Handle, TEXT("OnValueChanged"), {FString::SanitizeFloat(Value)});
	}

	return 0;
}

static int LF_GetValue(lua_State* L)
{
	if (auto* Ctx = WowLuaApi::GetContext(L); Ctx && Ctx->FrameManager)
		if (auto* Slider = Ctx->FrameManager->GetSliderWidget(GetFrameHandle(L)))
		{
			lua_pushnumber(L, Slider->GetValue());
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
static int LF_SetOrientation(lua_State* L) { return 0; }

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
        if(Fonts) Text->SetFont(Fonts->GetFont(Name,FMath::RoundToInt(Size*Scale)));
        else { auto Info=Text->GetFont(); Info.Size=FMath::Max(1,FMath::RoundToInt(Size*Scale*.75f)); Text->SetFont(Info); }
        FLinearColor Color; float* Components[]={&Color.R,&Color.G,&Color.B,&Color.A}; const char* Keys[]={"__textColorR","__textColorG","__textColorB","__textColorA"};
        for(int32 I=0;I<4;++I) { lua_getfield(L,2,Keys[I]); *Components[I]=luaL_optnumber(L,-1,1); lua_pop(L,1); }
        Text->SetColorAndOpacity(Color);
    }
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
static int LF_StartMoving(lua_State* L) { return 0; }
static int LF_StopMovingOrSizing(lua_State* L) { return 0; }

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
static int LF_GetPoint(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int Index = static_cast<int>(luaL_optnumber(L, 2, 1)) - 1; // 1-based to 0-based
	const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
	if (!Def || Index < 0 || Index >= Def->Anchors.Num()) return 0;

	const FWowAnchor& A = Def->Anchors[Index];
	lua_pushstring(L, AnchorPointToString(A.Point));
	// Push relativeTo frame (as table or nil)
	if (!A.RelativeTo.IsEmpty())
	{
		lua_getglobal(L, TCHAR_TO_UTF8(*A.RelativeTo));
	}
	else
	{
		lua_pushnil(L);
	}
	lua_pushstring(L, AnchorPointToString(A.RelativePoint));
	lua_pushnumber(L, A.OffsetX);
	lua_pushnumber(L, A.OffsetY);
	return 5;
}

// frame:GetNumPoints() — returns the number of anchors
static int LF_GetNumPoints(lua_State* L)
{
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	int64 Handle = GetFrameHandle(L);
	if (Ctx && Ctx->FrameManager)
	{
		const FWowFrameDef* Def = Ctx->FrameManager->GetFrameDef(Handle);
		if (Def)
		{
			lua_pushnumber(L, Def->Anchors.Num());
			return 1;
		}
	}
	lua_pushnumber(L, 0);
	return 1;
}

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
	const char* Text = luaL_checkstring(L, 2);
	float R = static_cast<float>(luaL_optnumber(L, 3, 1.0));
	float G = static_cast<float>(luaL_optnumber(L, 4, 1.0));
	float B = static_cast<float>(luaL_optnumber(L, 5, 1.0));
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

// tooltip:GetItem() -- returns item link for currently displayed item
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
		lua_pushstring(L, TCHAR_TO_UTF8(*State->ItemLink));
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
	FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
	if (!Ctx || !Ctx->FrameManager) return 0;

	int64 TooltipHandle = GetFrameHandle(L, 1);
	const char* ItemLink = luaL_checkstring(L, 2);
	Ctx->FrameManager->TooltipSetItem(TooltipHandle, UTF8_TO_TCHAR(ItemLink));
	return 0;
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
	{"SetButtonState", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager) C->FrameManager->SetButtonState(GetFrameHandle(L),UTF8_TO_TCHAR(luaL_checkstring(L,2)),lua_toboolean(L,3)!=0 && (!lua_isnumber(L,3) || lua_tonumber(L,3)!=0)); return 0; }},
	{"GetButtonState", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); const FString State=C && C->FrameManager?C->FrameManager->GetButtonState(GetFrameHandle(L)):TEXT("NORMAL"); lua_pushstring(L,TCHAR_TO_UTF8(*State)); return 1; }},
	{"RegisterForClicks", LF_RegisterForClicks},

	// Layout/Hit testing
	{"SetHitRectInsets", [](lua_State* L) -> int { return 0; }},
	{"GetRect", LF_GetRect},
	{"GetScale", LF_GetScale},
	{"SetScale", LF_SetScale},
	{"GetEffectiveScale", LF_GetEffectiveScale},
	{"SetClipsChildren", [](lua_State* L) -> int { return 0; }},
	{"IsDragging", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
	{"GetNumPoints", LF_GetNumPoints},
	{"GetPoint", LF_GetPoint},
	{"IsProtected", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
	{"CanChangeProtectedState", [](lua_State* L) -> int { lua_pushboolean(L, 1); return 1; }},
	{"SetUserPlaced", [](lua_State* L) -> int { return 0; }},
	{"IsUserPlaced", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
	{"SetDontSavePosition", [](lua_State* L) -> int { return 0; }},
	{"SetMaxResize", [](lua_State* L) -> int { return 0; }},
	{"SetMinResize", [](lua_State* L) -> int { return 0; }},

	// Texture methods on frames
	{"SetRotation", [](lua_State* L) -> int { return 0; }},
	{"SetSequence", [](lua_State* L) -> int { return 0; }},
	{"SetDesaturated", [](lua_State* L) -> int { return 0; }},
	{"IsDesaturated", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
	{"SetBlendMode", [](lua_State* L) -> int { return 0; }},
	{"SetNonBlocking", [](lua_State* L) -> int { return 0; }},
	{"SetDrawLayer", [](lua_State* L) -> int { return 0; }},

	// Text methods on frames
	{"GetTextHeight", [](lua_State* L) -> int { lua_pushnumber(L, 12); return 1; }},
	{"SetVertexColor", LF_SetVertexColorDispatch},
	{"GetTextWidth", LF_FontStringGetStringWidth},
	{"GetStringWidth", LF_FontStringGetStringWidth},
	{"GetStringHeight", LF_GetHeight},
	{"SetAlphaGradient", [](lua_State* L)->int { lua_pushboolean(L,0); return 1; }},
	{"SetTextHeight", [](lua_State* L) -> int { return 0; }},
	{"SetShadowOffset", [](lua_State* L) -> int { return 0; }},
	{"SetShadowColor", [](lua_State* L) -> int { return 0; }},
	{"SetSpacing", [](lua_State* L) -> int { return 0; }},
	{"SetJustifyH", [](lua_State* L) -> int { return 0; }},
	{"SetFormattedText", LF_SetFormattedText},
	{"SetWordWrap", [](lua_State* L) -> int { return 0; }},
	{"SetNonSpaceWrap", [](lua_State* L) -> int { return 0; }},
	{"SetJustifyV", [](lua_State* L) -> int { return 0; }},

	// Button methods
	{"SetMotionScriptsWhileDisabled", [](lua_State* L) -> int { return 0; }},

	// Missing common methods
	{"SetPadding", [](lua_State* L) -> int { return 0; }},
	{"GetPadding", [](lua_State* L) -> int { lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 4; }},
	{"SetIndentedWordWrap", [](lua_State* L) -> int { return 0; }},
	{"SetClampRectInsets", [](lua_State* L) -> int { return 0; }},
	{"GetClampRectInsets", [](lua_State* L) -> int { lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 4; }},
	{"SetClampedToScreen", [](lua_State* L) -> int { return 0; }},
	{"IsClampedToScreen", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
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
	{"SetModelScale", [](lua_State* L) -> int { return 0; }},
	{"SetPosition", [](lua_State* L) -> int { return 0; }},
	{"SetFacing", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager && C->FrameManager->OnModelFacing) C->FrameManager->OnModelFacing(GetFrameHandle(L,1),luaL_checknumber(L,2)); return 0; }},
	{"SetUnit", [](lua_State* L) -> int {
		// Check if this is a tooltip frame
		FWowLuaContext* Ctx = WowLuaApi::GetContext(L);
		if (Ctx && Ctx->FrameManager)
		{
			int64 Handle = GetFrameHandle(L, 1);
			const FWowFrameDef* FrameDef = Ctx->FrameManager->GetFrameDef(Handle);
			if (FrameDef && FrameDef->Type == EWowFrameType::GameTooltip)
			{
				const char* UnitToken = luaL_checkstring(L, 2);
				Ctx->FrameManager->TooltipSetUnit(Handle, UTF8_TO_TCHAR(UnitToken));
				return 0;
			}
		}
		if(Ctx && Ctx->FrameManager && Ctx->FrameManager->OnUnitModel)
		{
			auto* Entity=WowLuaApi::ResolveUnitToken(L,2);
			Ctx->FrameManager->OnUnitModel(GetFrameHandle(L,1),Entity?Entity->Guid:0);
		}
		return 0;
	}},
	{"SetCreature", [](lua_State* L) -> int { return 0; }},
	{"SetCamera", [](lua_State* L) -> int { return 0; }},
	{"RefreshUnit", [](lua_State* L) -> int { return 0; }},
	{"SetPortraitZoom", [](lua_State* L) -> int { return 0; }},
	{"ClearModel", [](lua_State* L) -> int { auto* C=WowLuaApi::GetContext(L); if(C && C->FrameManager && C->FrameManager->OnUnitModel) C->FrameManager->OnUnitModel(GetFrameHandle(L,1),0); return 0; }},
	{"HasCustomBackground", [](lua_State* L) -> int { lua_pushboolean(L, 0); return 1; }},
	{"SetCustomRace", [](lua_State* L) -> int { return 0; }},
	{"Undress", [](lua_State* L) -> int { return 0; }},
	{"TryOn", [](lua_State* L) -> int { return 0; }},

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
	{"SetAction", WowLuaApi::SetActionTooltip},
	{"SetSpell", WowLuaApi::SetSpellTooltip},
	{"SetSpellByID", WowLuaApi::SetSpellTooltip},
	{"SetHyperlink", WowLuaApi::SetHyperlinkTooltip},
	{"AddLine", LF_TooltipAddLine},
	{"AddDoubleLine", LF_TooltipAddDoubleLine},
	{"ClearLines", LF_TooltipClearLines},
	{"NumLines", LF_TooltipNumLines},
	{"SetMinimumWidth", [](lua_State* L) -> int { return 0; }},
	{"AppendText", LF_TooltipAppendText},
	{"GetItem", LF_TooltipGetItem},
	{"GetSpell", LF_TooltipGetSpell},
	{"GetUnit", LF_TooltipGetUnit},
	{"SetItem", LF_TooltipSetItem},
	{"GetAnchorType", [](lua_State* L) -> int { lua_pushstring(L, "ANCHOR_NONE"); return 1; }},
	{"FadeOut", [](lua_State* L) -> int { return 0; }},

	// Cooldown frame
	{"SetReverse", [](lua_State* L) -> int { return 0; }},
	{"SetSwipeColor", [](lua_State* L) -> int { return 0; }},
	{"SetBlingTexture", [](lua_State* L) -> int { return 0; }},
	{"SetEdgeTexture", [](lua_State* L) -> int { return 0; }},

	// Slider
	{"SetValueStep", [](lua_State* L) -> int { return 0; }},
	{"SetObeyStepOnDrag", [](lua_State* L) -> int { return 0; }},

	// Texture access (region globals use frame metatable)
	{"SetTexture", LF_TextureSetTexture},
	{"SetTexCoord", LF_TextureSetTexCoord},
	{"GetTexture", [](lua_State* L) -> int {
		luaL_checktype(L, 1, LUA_TTABLE);
		lua_getfield(L, 1, "__texture");
		if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushstring(L, ""); }
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
