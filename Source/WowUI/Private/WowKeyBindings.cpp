#include "WowKeyBindings.h"
#include "LuaApi/LuaApiRegistry.h"
#include "WowEventSystem.h"
#include "WowSavedVariables.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
struct FBinding { const char* Action; const char* Key; FKey Native; };
const TArray<FBinding>& Defaults()
{
    static const TArray<FBinding> Values={
        {"HEADER_MOVEMENT","",{}},{"MOVEFORWARD","W",EKeys::W},{"MOVEBACKWARD","S",EKeys::S},{"STRAFELEFT","A",EKeys::A},{"STRAFERIGHT","D",EKeys::D},
        {"JUMP","SPACE",EKeys::SpaceBar},{"TOGGLEAUTORUN","NUMLOCK",EKeys::NumLock},{"TOGGLERUN","/",EKeys::Slash},
        {"HEADER_ACTIONBAR","",{}},{"ACTIONBUTTON1","1",EKeys::One},{"ACTIONBUTTON2","2",EKeys::Two},{"ACTIONBUTTON3","3",EKeys::Three},
        {"ACTIONBUTTON4","4",EKeys::Four},{"ACTIONBUTTON5","5",EKeys::Five},{"ACTIONBUTTON6","6",EKeys::Six},{"ACTIONBUTTON7","7",EKeys::Seven},
        {"ACTIONBUTTON8","8",EKeys::Eight},{"ACTIONBUTTON9","9",EKeys::Nine},{"ACTIONBUTTON10","0",EKeys::Zero},{"ACTIONBUTTON11","-",EKeys::Hyphen},{"ACTIONBUTTON12","=",EKeys::Equals},
        {"HEADER_INTERFACE","",{}},{"TOGGLEBACKPACK","B",EKeys::B},{"TOGGLECHARACTER0","C",EKeys::C},{"TOGGLEQUESTLOG","L",EKeys::L},
        {"TOGGLESPELLBOOK","P",EKeys::P},{"TOGGLETALENTS","N",EKeys::N},{"TOGGLEWORLDMAP","M",EKeys::M},{"TOGGLEGUILDTAB","J",EKeys::J},
        {"OPENCHAT","ENTER",EKeys::Enter},{"TOGGLEGAMEMENU","ESCAPE",EKeys::Escape},{"TARGETNEARESTENEMY","TAB",EKeys::Tab}
    }; return Values;
}
void Preferences(lua_State* L)
{
    lua_getglobal(L,"IdleWowUIState");
    if(!lua_istable(L,-1)) { lua_pop(L,1); lua_newtable(L); lua_pushvalue(L,-1); lua_setglobal(L,"IdleWowUIState"); }
    lua_getfield(L,-1,"bindings");
    if(!lua_istable(L,-1)) { lua_pop(L,1); lua_newtable(L); lua_pushvalue(L,-1); lua_setfield(L,-3,"bindings"); }
    lua_remove(L,-2);
    lua_getfield(L,LUA_REGISTRYINDEX,"IdleWowBindingsSaved");
    if(lua_isnil(L,-1))
    {
        lua_pop(L,1); lua_newtable(L); lua_pushnil(L);
        while(lua_next(L,-3)) { lua_pushvalue(L,-2); lua_pushvalue(L,-2); lua_settable(L,-5); lua_pop(L,1); }
        lua_setfield(L,LUA_REGISTRYINDEX,"IdleWowBindingsSaved");
    }
    else lua_pop(L,1);
}
TMap<FString,FString> Bindings(lua_State* L)
{
    TMap<FString,FString> Result;
    for(const auto& D:Defaults()) if(*D.Key) Result.Add(UTF8_TO_TCHAR(D.Key),UTF8_TO_TCHAR(D.Action));
    Preferences(L); lua_pushnil(L);
    while(lua_next(L,-2)) { if(lua_type(L,-2)==LUA_TSTRING && lua_type(L,-1)==LUA_TSTRING) Result.Add(UTF8_TO_TCHAR(lua_tostring(L,-2)),UTF8_TO_TCHAR(lua_tostring(L,-1))); lua_pop(L,1); } lua_pop(L,1);
    return Result;
}
TArray<FString> Keys(lua_State* L,const FString& Action)
{
    TArray<FString> Result; for(const auto& Pair:Bindings(L)) if(Pair.Value==Action) Result.Add(Pair.Key); Result.Sort(); return Result;
}
void Register(lua_State* L,const char* Name,lua_CFunction Fn) { lua_pushcfunction(L,Fn); lua_setglobal(L,Name); }
}
FString FWowKeyBindings::Action(lua_State* L,const FString& Key) { return L?Bindings(L).FindRef(Key.ToUpper()):FString(); }
void FWowKeyBindings::RegisterApi(lua_State* L)
{
    Register(L,"GetNumBindings",[](lua_State* S)->int { lua_pushinteger(S,Defaults().Num()); return 1; });
    Register(L,"GetBinding",[](lua_State* S)->int { const int32 I=luaL_checkinteger(S,1)-1; if(!Defaults().IsValidIndex(I)) { lua_pushnil(S); return 1; } const auto& D=Defaults()[I]; lua_pushstring(S,D.Action); const auto Bound=Keys(S,UTF8_TO_TCHAR(D.Action)); for(int32 N=0;N<2;++N) { if(Bound.IsValidIndex(N)) lua_pushstring(S,TCHAR_TO_UTF8(*Bound[N])); else lua_pushnil(S); } return 3; });
    Register(L,"GetBindingKey",[](lua_State* S)->int { const auto Bound=Keys(S,UTF8_TO_TCHAR(luaL_checkstring(S,1))); for(const auto& Key:Bound) lua_pushstring(S,TCHAR_TO_UTF8(*Key)); return Bound.Num(); });
    Register(L,"GetBindingAction",[](lua_State* S)->int { const FString Value=Action(S,UTF8_TO_TCHAR(luaL_checkstring(S,1))); lua_pushstring(S,TCHAR_TO_UTF8(*Value)); return 1; });
    lua_getglobal(L,"GetBindingAction"); lua_setglobal(L,"GetBindingByKey");
    lua_getglobal(L,"GetBindingAction"); lua_setglobal(L,"GetBindingFromClick");
    Register(L,"SetBinding",[](lua_State* S)->int {
        const FString Key=FString(UTF8_TO_TCHAR(luaL_checkstring(S,1))).ToUpper(),Command=UTF8_TO_TCHAR(luaL_optstring(S,2,"")); bool Valid=Command.IsEmpty();
        for(const auto& D:Defaults()) Valid|=Command==UTF8_TO_TCHAR(D.Action) && *D.Key;
        if(Key.IsEmpty() || Key.Len()>64 || !Valid) { lua_pushboolean(S,0); return 1; }
        Preferences(S); lua_pushstring(S,TCHAR_TO_UTF8(*Command)); lua_setfield(S,-2,TCHAR_TO_UTF8(*Key)); lua_pop(S,1);
        if(auto* C=WowLuaApi::GetContext(S); C && C->EventSystem) C->EventSystem->FireEvent(TEXT("UPDATE_BINDINGS")); lua_pushboolean(S,1); return 1;
    });
    Register(L,"SaveBindings",[](lua_State* S)->int {
        Preferences(S); lua_newtable(S); lua_pushnil(S);
        while(lua_next(S,-3)) { lua_pushvalue(S,-2); lua_pushvalue(S,-2); lua_settable(S,-5); lua_pop(S,1); }
        lua_setfield(S,LUA_REGISTRYINDEX,"IdleWowBindingsSaved"); lua_pop(S,1); FWowSavedVariables::SaveAll(S); return 0;
    });
    Register(L,"GetCurrentBindingSet",[](lua_State* S)->int { lua_pushinteger(S,2); return 1; });
    Register(L,"LoadBindings",[](lua_State* S)->int {
        const bool Reset=luaL_optinteger(S,1,0)==0; Preferences(S); lua_pop(S,1); lua_getglobal(S,"IdleWowUIState"); lua_newtable(S);
        if(!Reset) { lua_getfield(S,LUA_REGISTRYINDEX,"IdleWowBindingsSaved"); lua_pushnil(S); while(lua_next(S,-2)) { lua_pushvalue(S,-2); lua_pushvalue(S,-2); lua_settable(S,-6); lua_pop(S,1); } lua_pop(S,1); }
        lua_setfield(S,-2,"bindings"); lua_pop(S,1); if(auto* C=WowLuaApi::GetContext(S); C && C->EventSystem) C->EventSystem->FireEvent(TEXT("UPDATE_BINDINGS")); return 0;
    });
}
#else
FString FWowKeyBindings::Action(lua_State*,const FString&) { return {}; }
void FWowKeyBindings::RegisterApi(lua_State*) {}
#endif
FString FWowKeyBindings::KeyName(const FKey& Key)
{
#if __has_include("lua.h")
    for(const auto& D:Defaults()) if(D.Native==Key) return UTF8_TO_TCHAR(D.Key);
#endif
    if(Key==EKeys::BackSpace) return TEXT("BACKSPACE");
    if(Key==EKeys::Delete) return TEXT("DELETE");
    if(Key==EKeys::LeftShift) return TEXT("LSHIFT"); if(Key==EKeys::RightShift) return TEXT("RSHIFT");
    if(Key==EKeys::LeftControl) return TEXT("LCTRL"); if(Key==EKeys::RightControl) return TEXT("RCTRL");
    if(Key==EKeys::LeftAlt) return TEXT("LALT"); if(Key==EKeys::RightAlt) return TEXT("RALT");
    return Key.ToString().ToUpper();
}
FKey FWowKeyBindings::GameplayKey(const FString& Action)
{
#if __has_include("lua.h")
    for(const auto& D:Defaults()) if(Action==UTF8_TO_TCHAR(D.Action)) return D.Native;
#endif
    return EKeys::Invalid;
}
bool FWowKeyBindings::IsDefaultGameplayKey(const FKey& Key)
{
#if __has_include("lua.h")
    for(const auto& D:Defaults()) if(D.Native==Key) return true;
#endif
    return false;
}
