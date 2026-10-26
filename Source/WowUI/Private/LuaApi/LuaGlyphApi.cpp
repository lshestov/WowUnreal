#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowFrameManager.h"
#include "WowUpdateFields.h"
#include "Formats/Dbc/DbcStore.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
struct FSocket
{
    FWowLuaContext* Context=nullptr;
    const FDbcStore::FGlyphSlot* Slot=nullptr;
    const FDbcStore::FGlyph* Glyph=nullptr;
    uint32 SlotId=0,Property=0;
    int32 Index=0,Group=0;
    bool Enabled=false;
};
FSocket Socket(lua_State* L,int32 IndexArg,int32 GroupArg)
{
    FSocket S; S.Context=WowLuaApi::GetContext(L); S.Index=luaL_checkinteger(L,IndexArg)-1;
    auto* C=S.Context; if(!C || !C->ConnectionManager || !C->EntityManager || S.Index<0 || S.Index>=6) return S;
    const auto& H=C->ConnectionManager->PacketHandler; const auto* Player=C->EntityManager->GetLocalPlayer();
    S.Group=lua_isnoneornil(L,GroupArg)?H.ActiveTalentGroup:int32(luaL_checkinteger(L,GroupArg))-1;
    if(!Player || !H.TalentGroups.IsValidIndex(S.Group)) return S;
    S.Enabled=(Player->GetField(PlayerField::GLYPHS_ENABLED)&(1u<<S.Index))!=0;
    S.SlotId=Player->GetField(PlayerField::GLYPH_SLOTS_1+S.Index); S.Slot=FDbcStore::Get().GlyphSlot(S.SlotId);
    S.Property=H.TalentGroups[S.Group].Glyphs[S.Index]; S.Glyph=FDbcStore::Get().Glyph(S.Property); return S;
}
FString Text(lua_State* L,const FString& Name,const FString& Fallback)
{
    lua_getglobal(L,TCHAR_TO_UTF8(*Name)); const FString Result=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):Fallback; lua_pop(L,1); return Result;
}
void Register(lua_State* L,const char* Name,lua_CFunction Function) { lua_pushcfunction(L,Function); lua_setglobal(L,Name); }
}
int WowLuaApi::SetGlyphTooltip(lua_State* L)
{
    const auto S=Socket(L,2,3); auto* C=S.Context; if(!C || !C->FrameManager || !S.Slot) return 0;
    lua_getfield(L,1,"__handle"); const int64 Handle=lua_tointeger(L,-1); lua_pop(L,1);
    auto* Frames=C->FrameManager; Frames->TooltipClearLines(Handle);
    const auto* Spell=S.Glyph?FDbcStore::Get().Spells().GetById(S.Glyph->Spell):nullptr;
    auto* State=Frames->GetTooltipState(Handle); State->SpellId=Spell?Spell->ID:0; State->ItemLink.Empty(); State->UnitToken.Empty();
    Frames->TooltipAddLine(Handle,Spell?Spell->SpellName:Text(L,S.Enabled?TEXT("GLYPH_INACTIVE"):TEXT("GLYPH_LOCKED"),S.Enabled?TEXT("Empty Glyph Slot"):TEXT("Locked Glyph Slot")),1,S.Enabled?1:.1f,S.Enabled?1:.1f,false);
    Frames->TooltipAddLine(Handle,Text(L,S.Slot->Type?TEXT("MINOR_GLYPH"):TEXT("MAJOR_GLYPH"),S.Slot->Type?TEXT("Minor Glyph"):TEXT("Major Glyph")),.4f,.7f,1,false);
    if(Spell)
    {
        const auto* Player=C->EntityManager->GetLocalPlayer(); Frames->TooltipAddLine(Handle,FDbcStore::Get().Spells().Description(Spell->ID,Player?Player->GetLevel():1),1,.82f,0,true);
        if(S.Group==C->ConnectionManager->PacketHandler.ActiveTalentGroup) Frames->TooltipAddLine(Handle,Text(L,TEXT("GLYPH_SLOT_REMOVE_TOOLTIP"),TEXT("Shift-right-click to remove this glyph")),.5f,.5f,.5f,false);
    }
    else Frames->TooltipAddLine(Handle,Text(L,S.Enabled?TEXT("GLYPH_EMPTY_DESC"):FString::Printf(TEXT("GLYPH_SLOT_TOOLTIP%u"),S.Slot->Tooltip),S.Enabled?TEXT("Use a glyph to fill this slot."):TEXT("This glyph slot unlocks at a higher level.")),1,.82f,0,true);
    Frames->SetFrameVisible(Handle,true); return 0;
}
void WowLuaApi::RegisterGlyphApi(lua_State* L)
{
    Register(L,"GetNumGlyphSockets",[](lua_State* S)->int { lua_pushinteger(S,6); return 1; });
    Register(L,"GetGlyphSocketInfo",[](lua_State* S)->int {
        const auto V=Socket(S,1,2); if(!V.Slot) return 0;
        if(V.Enabled) lua_pushinteger(S,1); else lua_pushnil(S); lua_pushinteger(S,V.Slot->Type+1);
        if(V.Glyph) lua_pushinteger(S,V.Glyph->Spell); else lua_pushnil(S);
        const auto* Icon=V.Glyph?FDbcStore::Get().SpellIcons().GetById(V.Glyph->Icon):nullptr; if(Icon) lua_pushstring(S,TCHAR_TO_UTF8(*Icon->TexturePath)); else lua_pushnil(S); return 4;
    });
    Register(L,"GetGlyphLink",[](lua_State* S)->int {
        const auto V=Socket(S,1,2); if(!V.Slot) return 0; const auto* Spell=V.Glyph?FDbcStore::Get().Spells().GetById(V.Glyph->Spell):nullptr;
        const FString Link=Spell?FString::Printf(TEXT("|cff66bbff|Hglyph:%u:%u|h[%s]|h|r"),V.SlotId,V.Property,*Spell->SpellName):TEXT(""); lua_pushstring(S,TCHAR_TO_UTF8(*Link)); return 1;
    });
    Register(L,"GlyphMatchesSocket",[](lua_State* S)->int { const auto* C=GetContext(S); const int32 Index=luaL_checkinteger(S,1)-1; if(C && C->ConnectionManager && C->ConnectionManager->GlyphMatchesSocket(Index)) lua_pushinteger(S,1); else lua_pushnil(S); return 1; });
    Register(L,"PlaceGlyphInSocket",[](lua_State* S)->int { auto* C=GetContext(S); const int32 Index=luaL_checkinteger(S,1)-1; if(C && C->ConnectionManager && C->ConnectionManager->PlaceGlyph(Index)) lua_pushinteger(S,1); else lua_pushnil(S); return 1; });
    Register(L,"RemoveGlyphFromSocket",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) C->ConnectionManager->SendRemoveGlyph(luaL_checkinteger(S,1)-1); return 0; });
    Register(L,"SpellIsTargeting",[](lua_State* S)->int { const auto* C=GetContext(S); lua_pushboolean(S,C && C->ConnectionManager && (C->ConnectionManager->HasPendingGlyph() || C->ConnectionManager->HasPendingItemSpell())); return 1; });
    Register(L,"SpellStopTargeting",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) {C->ConnectionManager->CancelGlyphUse(); C->ConnectionManager->CancelItemSpell();} return 0; });
}
#else
void WowLuaApi::RegisterGlyphApi(lua_State*) {}
int WowLuaApi::SetGlyphTooltip(lua_State*) { return 0; }
#endif
