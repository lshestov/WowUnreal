#include "WowModifiedClicks.h"
#include "LuaApi/LuaApiRegistry.h"
#include "WowFrameManager.h"
#include "WowEntityManager.h"
#include "Framework/Application/SlateApplication.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
struct FAction {const char* Name; const char* Binding;};
// Build-12340 Bindings.xml definitions; regression compares these with the MPQ.
const FAction Actions[]={
    {"SELFCAST","ALT"},{"FOCUSCAST","NONE"},{"AUTOLOOTTOGGLE","SHIFT"},
    {"MAILAUTOLOOTTOGGLE","SHIFT"},{"STICKYCAMERA","CTRL"},{"CHATLINK","SHIFT-BUTTON1"},
    {"DRESSUP","CTRL-BUTTON1"},{"SOCKETITEM","SHIFT-BUTTON2"},{"SPLITSTACK","SHIFT"},
    {"PICKUPACTION","SHIFT"},{"COMPAREITEMS","SHIFT"},{"OPENALLBAGS","SHIFT"},
    {"QUESTWATCHTOGGLE","SHIFT"},{"TOKENWATCHTOGGLE","SHIFT"},
    {"SHOWITEMFLYOUT","ALT"},{"SHOWMULTICASTFLYOUT","ALT"}
};
const FAction* Action(const FString& Name)
{
    for(const auto& A:Actions) if(Name.Equals(UTF8_TO_TCHAR(A.Name),ESearchCase::IgnoreCase)) return &A;
    return nullptr;
}
struct FClick {uint8 Modifiers=0,Button=0; FString Text;};
bool Parse(FString Text,FClick& Out)
{
    Out={}; if(Text.Len()>128) return false; Text=Text.ToUpper();
    if(Text.IsEmpty() || Text==TEXT("NONE")) {Out.Text=TEXT("NONE"); return true;}
    TArray<FString> Tokens; Text.ParseIntoArray(Tokens,TEXT("-"),false);
    for(int32 I=0;I<Tokens.Num();++I)
    {
        const auto& T=Tokens[I];
        if(T==TEXT("SHIFT")) Out.Modifiers|=3;
        else if(T==TEXT("LSHIFT")) Out.Modifiers|=1;
        else if(T==TEXT("RSHIFT")) Out.Modifiers|=2;
        else if(T==TEXT("CTRL")) Out.Modifiers|=12;
        else if(T==TEXT("LCTRL")) Out.Modifiers|=4;
        else if(T==TEXT("RCTRL")) Out.Modifiers|=8;
        else if(T==TEXT("ALT")) Out.Modifiers|=48;
        else if(T==TEXT("LALT")) Out.Modifiers|=16;
        else if(T==TEXT("RALT")) Out.Modifiers|=32;
        else if(I==Tokens.Num()-1 && T.Len()==7 && T.StartsWith(TEXT("BUTTON")) && T[6]>='1' && T[6]<='5') Out.Button=T[6]-'0';
        else return false;
    }
    const auto Add=[&](uint8 Mask,const TCHAR* Both,const TCHAR* Left,const TCHAR* Right)
    {const uint8 Value=Out.Modifiers&Mask; if(Value) {if(!Out.Text.IsEmpty()) Out.Text+=TEXT("-"); Out.Text+=Value==Mask?Both:Value==(Mask&(Mask-1))?Right:Left;}};
    Add(48,TEXT("ALT"),TEXT("LALT"),TEXT("RALT")); Add(12,TEXT("CTRL"),TEXT("LCTRL"),TEXT("RCTRL")); Add(3,TEXT("SHIFT"),TEXT("LSHIFT"),TEXT("RSHIFT"));
    if(Out.Button) {if(!Out.Text.IsEmpty()) Out.Text+=TEXT("-"); Out.Text+=FString::Printf(TEXT("BUTTON%d"),Out.Button);}
    return true;
}
void Copy(lua_State* L,int32 Source)
{
    if(Source<0) Source=lua_gettop(L)+Source+1;
    lua_newtable(L); lua_pushnil(L);
    while(lua_next(L,Source)) {lua_pushvalue(L,-2); lua_pushvalue(L,-2); lua_settable(L,-5); lua_pop(L,1);}
}
void Preferences(lua_State* L)
{
    lua_getglobal(L,"IdleWowUIState");
    if(!lua_istable(L,-1)) {lua_pop(L,1); lua_newtable(L); lua_pushvalue(L,-1); lua_setglobal(L,"IdleWowUIState");}
    lua_getfield(L,-1,"modifiedClicks");
    if(!lua_istable(L,-1)) {lua_pop(L,1); lua_newtable(L); lua_pushvalue(L,-1); lua_setfield(L,-3,"modifiedClicks");}
    lua_remove(L,-2);
    lua_getfield(L,LUA_REGISTRYINDEX,"IdleWowModifiedClicksSaved");
    if(lua_isnil(L,-1)) {lua_pop(L,1); Copy(L,-1); lua_setfield(L,LUA_REGISTRYINDEX,"IdleWowModifiedClicksSaved");}
    else lua_pop(L,1);
}
FClick Binding(lua_State* L,const FAction& A)
{
    FClick Result; Preferences(L); lua_getfield(L,-1,A.Name);
    if(!lua_isstring(L,-1) || !Parse(UTF8_TO_TCHAR(lua_tostring(L,-1)),Result)) Parse(UTF8_TO_TCHAR(A.Binding),Result);
    lua_pop(L,2); return Result;
}
FORCENOINLINE bool Store(lua_State* L,const char* Name,const char* Text)
{
    const auto* A=Action(UTF8_TO_TCHAR(Name)); FClick B;
    if(!A || !Parse(UTF8_TO_TCHAR(Text),B)) return false;
    Preferences(L); lua_pushstring(L,TCHAR_TO_UTF8(*B.Text)); lua_setfield(L,-2,A->Name); lua_pop(L,1);
    return true;
}
uint8 Pressed(lua_State* L)
{
    uint8 Keys=0;
    if(const auto* C=WowLuaApi::GetContext(L); C && C->KeyDown)
    {
        const FKey Codes[]={EKeys::LeftShift,EKeys::RightShift,EKeys::LeftControl,EKeys::RightControl,EKeys::LeftAlt,EKeys::RightAlt};
        for(int32 I=0;I<UE_ARRAY_COUNT(Codes);++I) if(C->KeyDown(Codes[I])) Keys|=1<<I;
        return Keys;
    }
    if(FSlateApplication::IsInitialized())
    {
        const auto M=FSlateApplication::Get().GetModifierKeys();
        Keys=(M.IsLeftShiftDown()?1:0)|(M.IsRightShiftDown()?2:0)|(M.IsLeftControlDown()?4:0)|(M.IsRightControlDown()?8:0)|(M.IsLeftAltDown()?16:0)|(M.IsRightAltDown()?32:0);
    }
    return Keys;
}
bool Active(lua_State* L,const FClick& B,uint8 Keys)
{
    if(B.Modifiers ? !(Keys&B.Modifiers) : !B.Button) return false;
    if(B.Button)
    {
        static const TCHAR* Names[]={TEXT(""),TEXT("LeftButton"),TEXT("RightButton"),TEXT("MiddleButton"),TEXT("Button4"),TEXT("Button5")};
        if(const auto* C=WowLuaApi::GetContext(L); C && C->FrameManager && !C->FrameManager->CurrentMouseButton.IsEmpty())
            return C->FrameManager->CurrentMouseButton==Names[B.Button];
    }
    return true;
}
}
void WowModifiedClicks::Save(lua_State* L)
{
    Preferences(L); Copy(L,-1); lua_setfield(L,LUA_REGISTRYINDEX,"IdleWowModifiedClicksSaved"); lua_pop(L,1);
}
void WowModifiedClicks::Load(lua_State* L,bool Defaults)
{
    Preferences(L); lua_pop(L,1); lua_getglobal(L,"IdleWowUIState");
    if(Defaults) lua_newtable(L);
    else {lua_getfield(L,LUA_REGISTRYINDEX,"IdleWowModifiedClicksSaved"); Copy(L,-1); lua_remove(L,-2);}
    lua_setfield(L,-2,"modifiedClicks"); lua_pop(L,1);
}
uint64 WowModifiedClicks::ActionTarget(lua_State* L,uint64 Selected)
{
    const auto* C=WowLuaApi::GetContext(L); if(!C || !C->EntityManager) return Selected;
    const uint8 Keys=Pressed(L);
    if(Active(L,Binding(L,Actions[0]),Keys)) return C->EntityManager->LocalPlayerGuid;
    if(Active(L,Binding(L,Actions[1]),Keys) && C->FocusGuid) return C->FocusGuid;
    return Selected;
}
void WowModifiedClicks::RegisterApi(lua_State* L)
{
    lua_register(L,"GetNumModifiedClickActions",[](lua_State* S)->int {lua_pushinteger(S,UE_ARRAY_COUNT(Actions)); return 1;});
    lua_register(L,"GetModifiedClickAction",[](lua_State* S)->int {const int32 Index=luaL_checkinteger(S,1)-1; lua_pushstring(S,Index>=0 && Index<UE_ARRAY_COUNT(Actions)?Actions[Index].Name:nullptr); return 1;});
    lua_register(L,"GetModifiedClick",[](lua_State* S)->int {const auto* A=Action(UTF8_TO_TCHAR(luaL_checkstring(S,1))); if(!A) lua_pushnil(S); else {const auto B=Binding(S,*A); lua_pushstring(S,TCHAR_TO_UTF8(*B.Text));} return 1;});
    lua_register(L,"SetModifiedClick",[](lua_State* S)->int {
        const char* Name=luaL_checkstring(S,1); const char* Text=luaL_optstring(S,2,"NONE");
        // Keep the protected Lua error boundary free of Unreal string cleanup.
        if(!Store(S,Name,Text)) return luaL_error(S,"SetModifiedClick(): Unknown action or binding");
        return 0;
    });
    lua_register(L,"IsModifiedClick",[](lua_State* S)->int {
        const uint8 Keys=Pressed(S); bool Value=false;
        if(lua_isnoneornil(S,1)) {Value=Keys!=0; if(!Value) for(const auto& A:Actions) if(Active(S,Binding(S,A),Keys)) {Value=true; break;}}
        else if(const auto* A=Action(UTF8_TO_TCHAR(luaL_checkstring(S,1)))) Value=Active(S,Binding(S,*A),Keys);
        else {FClick B; if(Parse(UTF8_TO_TCHAR(lua_tostring(S,1)),B) && B.Modifiers && !B.Button) {Value=true; for(uint8 Mask:{3,12,48}) if((B.Modifiers&Mask) && !(Keys&B.Modifiers&Mask)) Value=false;}}
        if(Value) lua_pushnumber(S,1); else lua_pushnil(S); return 1;
    });
}
#else
void WowModifiedClicks::RegisterApi(lua_State*) {}
void WowModifiedClicks::Save(lua_State*) {}
void WowModifiedClicks::Load(lua_State*,bool) {}
uint64 WowModifiedClicks::ActionTarget(lua_State*,uint64 Selected) {return Selected;}
#endif
