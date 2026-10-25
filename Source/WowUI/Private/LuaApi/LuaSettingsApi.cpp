#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "WowFrameManager.h"
#include "WowAudioManager.h"
#include "WowSavedVariables.h"
#include "WowAddonLoader.h"
#include "Formats/Dbc/DbcStore.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void Register(lua_State* L,const char* Name,lua_CFunction Function) { lua_pushcfunction(L,Function); lua_setglobal(L,Name); }
UWorld* World(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->ConnectionManager || !C->ResolveActor) return nullptr;
    const auto* Actor=C->ResolveActor(C->ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid); return Actor?Actor->GetWorld():nullptr;
}
AWowAudioManager* ResolveAudioManager(lua_State* L) { auto* W=World(L); return W?Cast<AWowAudioManager>(UGameplayStatics::GetActorOfClass(W,AWowAudioManager::StaticClass())):nullptr; }
bool NativeValue(lua_State* L,const FString& Name,FString& Value)
{
    if(Name==TEXT("uiscale"))
    {
        const auto* C=WowLuaApi::GetContext(L);
        Value=FString::SanitizeFloat(C && C->FrameManager?C->FrameManager->GetUIScale():1.f);
        return true;
    }
    if(!World(L)) return false;
    const auto* Settings=UGameUserSettings::GetGameUserSettings();
    if(Name.StartsWith(TEXT("sound_"))) if(auto* Manager=ResolveAudioManager(L); Manager && Manager->GetUiOption(Name,Value)) return true;
    if(Name==TEXT("gamma")) Value=FString::SanitizeFloat(GEngine?GEngine->DisplayGamma/2.2f:1.f);
    else if(Name==TEXT("terrainmip") && Settings) Value=FString::FromInt(FMath::Clamp(3-Settings->GetTextureQuality(),0,2));
    else if(Name==TEXT("gxwindow") && Settings) Value=(GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport?GEngine->GameViewport->Viewport->GetWindowMode():Settings->GetFullscreenMode())!=EWindowMode::Fullscreen?TEXT("1"):TEXT("0");
    else if(Name==TEXT("gxmaximize") && Settings) Value=(GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport?GEngine->GameViewport->Viewport->GetWindowMode():Settings->GetFullscreenMode())==EWindowMode::WindowedFullscreen?TEXT("1"):TEXT("0");
    else if(Name==TEXT("gxvsync") && Settings) Value=Settings->IsVSyncEnabled()?TEXT("1"):TEXT("0");
    else if(Name==TEXT("maxfps") && Settings) Value=FString::SanitizeFloat(Settings->GetFrameRateLimit());
    else if(Name==TEXT("gxresolution") && GEngine && GEngine->GameViewport) { FVector2D Size; GEngine->GameViewport->GetViewportSize(Size); Value=FString::Printf(TEXT("%dx%d"),int32(Size.X),int32(Size.Y)); }
    else return false;
    return true;
}
void ApplyNative(lua_State* L,const FString& Name,const FString& Value)
{
    if(!World(L)) return;
    if(Name.StartsWith(TEXT("sound_"))) if(auto* Manager=ResolveAudioManager(L); Manager && Manager->SetUiOption(Name,Value)) return;
    auto* Settings=UGameUserSettings::GetGameUserSettings(); if(!Settings) return;
    const bool Enabled=Value.ToBool() || FCString::Atof(*Value)!=0;
    bool Resolution=false;
    if(Name==TEXT("gamma")) { if(GEngine) GEngine->DisplayGamma=2.2f*FMath::Clamp(FCString::Atof(*Value),.5f,1.5f); return; }
    if(Name==TEXT("terrainmip")) Settings->SetTextureQuality(FMath::Clamp(3-FCString::Atoi(*Value),0,3));
    else if(Name==TEXT("gxwindow")) { Settings->SetFullscreenMode(Enabled?EWindowMode::Windowed:EWindowMode::Fullscreen); Resolution=true; }
    else if(Name==TEXT("gxmaximize")) { Settings->SetFullscreenMode(Enabled?EWindowMode::WindowedFullscreen:EWindowMode::Windowed); Resolution=true; }
    else if(Name==TEXT("gxresolution"))
    {
        FString X,Y; if(!Value.Split(TEXT("x"),&X,&Y)) return; const FIntPoint Size(FCString::Atoi(*X),FCString::Atoi(*Y));
        if(Size.X<640 || Size.X>7680 || Size.Y<360 || Size.Y>4320) return;
        Settings->SetScreenResolution(Size); Resolution=true;
    }
    else if(Name==TEXT("gxvsync")) Settings->SetVSyncEnabled(Enabled);
    else if(Name==TEXT("maxfps")) Settings->SetFrameRateLimit(FMath::Clamp(FCString::Atof(*Value),0.f,360.f));
    else return;
    if(Resolution) Settings->ApplyResolutionSettings(false); else Settings->ApplyNonResolutionSettings();
    Settings->SaveSettings();
}
bool Preference(lua_State* L,const FString& Name,FString& Value)
{
    lua_getglobal(L,"IdleWowUIState"); bool Found=false;
    if(lua_istable(L,-1)) { lua_getfield(L,-1,TCHAR_TO_UTF8(*(TEXT("cvar_")+Name))); if(lua_isstring(L,-1)) { Value=UTF8_TO_TCHAR(lua_tostring(L,-1)); Found=true; } lua_pop(L,1); } lua_pop(L,1); return Found;
}
int GetCvar(lua_State* L)
{
    const FString Name=FString(UTF8_TO_TCHAR(luaL_checkstring(L,1))).ToLower(); FString Value;
    if(NativeValue(L,Name,Value) || Preference(L,Name,Value)) { lua_pushstring(L,TCHAR_TO_UTF8(*Value)); return 1; }
    lua_pushvalue(L,lua_upvalueindex(1)); lua_pushvalue(L,1); lua_call(L,1,1); return 1;
}
int SetCvar(lua_State* L)
{
    const FString Name=FString(UTF8_TO_TCHAR(luaL_checkstring(L,1))).ToLower();
    const FString Value=lua_isboolean(L,2)?lua_toboolean(L,2)?TEXT("1"):TEXT("0"):UTF8_TO_TCHAR(luaL_checkstring(L,2));
    lua_getglobal(L,"GetCVar"); lua_pushvalue(L,1); lua_call(L,1,1); const FString Old=UTF8_TO_TCHAR(lua_tostring(L,-1)); lua_pop(L,1);
    const bool Changed=Old!=Value && !(Old.IsNumeric() && Value.IsNumeric() && FMath::IsNearlyEqual(FCString::Atod(*Old),FCString::Atod(*Value),1.e-6));
    lua_getglobal(L,"IdleWowUIState"); if(!lua_istable(L,-1)) { lua_pop(L,1); lua_newtable(L); lua_pushvalue(L,-1); lua_setglobal(L,"IdleWowUIState"); }
    lua_pushstring(L,TCHAR_TO_UTF8(*Value)); lua_setfield(L,-2,TCHAR_TO_UTF8(*(TEXT("cvar_")+Name))); lua_pop(L,1);
    lua_pushvalue(L,lua_upvalueindex(1)); lua_pushvalue(L,1); lua_pushvalue(L,2); lua_call(L,2,0);
    if(Changed)
    {
        ApplyNative(L,Name,Value);
        if(auto* C=WowLuaApi::GetContext(L); C && C->EventSystem) C->EventSystem->FireEvent(TEXT("CVAR_UPDATE"),{Name.ToUpper(),Value});
    }
    return 0;
}
int SetNamedValue(lua_State* L,const char* Name)
{
    lua_getglobal(L,"SetCVar"); lua_pushstring(L,Name); lua_pushvalue(L,1); lua_call(L,2,0); return 0;
}
TArray<FIntPoint> Resolutions()
{
    TArray<FIntPoint> Values; UKismetSystemLibrary::GetSupportedFullscreenResolutions(Values);
    for(const FIntPoint Size:{FIntPoint(1280,720),FIntPoint(1600,900),FIntPoint(1920,1080)}) Values.AddUnique(Size);
    if(GEngine && GEngine->GameViewport) { FVector2D Current=FVector2D::ZeroVector; GEngine->GameViewport->GetViewportSize(Current); if(Current.X>=640 && Current.Y>=360) Values.AddUnique(FIntPoint(Current.X,Current.Y)); }
    Values.RemoveAll([](const auto& S){return S.X<640 || S.Y<360;}); Values.Sort([](const auto& A,const auto& B){return A.X==B.X?A.Y<B.Y:A.X<B.X;}); return Values;
}
}
void WowLuaApi::ApplySavedUiSettings(lua_State* L)
{
    lua_getglobal(L,"IdleWowUIState");
    if(lua_istable(L,-1)) { lua_pushnil(L); while(lua_next(L,-2)) { if(lua_type(L,-2)==LUA_TSTRING && lua_isstring(L,-1)) { const FString Key=UTF8_TO_TCHAR(lua_tostring(L,-2)); if(Key.StartsWith(TEXT("cvar_"))) ApplyNative(L,Key.Mid(5),UTF8_TO_TCHAR(lua_tostring(L,-1))); } lua_pop(L,1); } } lua_pop(L,1);
}
void WowLuaApi::RegisterSettingsApi(lua_State* L)
{
    Register(L,"ShowingHelm",[](lua_State* S)->int { const auto* P=ResolveUnitToken(S); lua_pushboolean(S,P && P->IsPlayer() && !(P->GetField(PlayerField::FLAGS)&0x400)); return 1; });
    Register(L,"ShowingCloak",[](lua_State* S)->int { const auto* P=ResolveUnitToken(S); lua_pushboolean(S,P && P->IsPlayer() && !(P->GetField(PlayerField::FLAGS)&0x800)); return 1; });
    Register(L,"ShowHelm",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) C->ConnectionManager->SendRawPacket(0x2B9,{uint8(lua_toboolean(S,1)!=0)}); return 0; });
    Register(L,"ShowCloak",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) C->ConnectionManager->SendRawPacket(0x2BA,{uint8(lua_toboolean(S,1)!=0)}); return 0; });
    lua_getglobal(L,"GetCVar"); lua_pushcclosure(L,GetCvar,1); lua_setglobal(L,"GetCVar");
    lua_getglobal(L,"SetCVar"); lua_pushcclosure(L,SetCvar,1); lua_setglobal(L,"SetCVar");
    lua_getglobal(L,"GetCVarDefault"); lua_pushcclosure(L,[](lua_State* S)->int { const FString Name=FString(UTF8_TO_TCHAR(luaL_checkstring(S,1))).ToLower(); if(Name==TEXT("gamma") || Name==TEXT("gxwindow")) { lua_pushstring(S,"1"); return 1; } lua_pushvalue(S,lua_upvalueindex(1)); lua_pushvalue(S,1); lua_call(S,1,1); return 1; },1); lua_setglobal(L,"GetCVarDefault");
    Register(L,"GetCVarBool",[](lua_State* S)->int { lua_getglobal(S,"GetCVar"); lua_pushvalue(S,1); lua_call(S,1,1); const FString Value=UTF8_TO_TCHAR(lua_tostring(S,-1)); lua_pushboolean(S,Value.ToBool() || FCString::Atof(*Value)!=0); return 1; });
    // FrameXML uses a signed brightness offset (-0.5..0.5), while the CVar
    // and engine bridge store a multiplier with neutral value 1.
    Register(L,"GetGamma",[](lua_State* S)->int { lua_getglobal(S,"GetCVar"); lua_pushstring(S,"gamma"); lua_call(S,1,1); const float Gain=lua_tonumber(S,-1); lua_pop(S,1); lua_pushnumber(S,Gain-1.f); return 1; });
    Register(L,"SetGamma",[](lua_State* S)->int { const float Gain=1.f+FMath::Clamp(float(luaL_checknumber(S,1)),-.5f,.5f); lua_pushnumber(S,Gain); lua_replace(S,1); return SetNamedValue(S,"gamma"); });
    Register(L,"GetTerrainMip",[](lua_State* S)->int { FString Value; lua_pushnumber(S,NativeValue(S,TEXT("terrainmip"),Value)?FCString::Atoi(*Value):0); return 1; });
    Register(L,"SetTerrainMip",[](lua_State* S)->int { return SetNamedValue(S,"terrainMip"); });
    Register(L,"GetScreenResolutions",[](lua_State* S)->int { const auto Values=Resolutions(); for(const auto& Size:Values) lua_pushstring(S,TCHAR_TO_UTF8(*FString::Printf(TEXT("%dx%d"),Size.X,Size.Y))); return Values.Num(); });
    Register(L,"GetCurrentResolution",[](lua_State* S)->int { const auto Values=Resolutions(); FVector2D Current=FVector2D::ZeroVector; if(GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Current); const int32 Index=Values.Find(FIntPoint(Current.X,Current.Y)); lua_pushinteger(S,FMath::Max(Index+1,1)); return 1; });
    Register(L,"SetScreenResolution",[](lua_State* S)->int { const auto Values=Resolutions(); const int32 Index=luaL_checkinteger(S,1)-1; if(Values.IsValidIndex(Index)) { lua_pushstring(S,TCHAR_TO_UTF8(*FString::Printf(TEXT("%dx%d"),Values[Index].X,Values[Index].Y))); lua_replace(S,1); SetNamedValue(S,"gxResolution"); } return 0; });
    Register(L,"RegisterForSave",[](lua_State* S)->int { for(int32 I=1;I<=lua_gettop(S);++I) { const FString Name=UTF8_TO_TCHAR(luaL_checkstring(S,I)); if(Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\"))) continue; FWowTocData Toc; Toc.SavedVariables.Add(Name); FWowSavedVariables::RegisterAddon(TEXT("FrameXML_")+Name,Toc); FWowSavedVariables::LoadForAddon(S,TEXT("FrameXML_")+Name,Toc); } return 0; });
    Register(L,"PlaySound",[](lua_State* S)->int {
        uint32 Id=0; if(lua_isnumber(S,1)) Id=lua_tointeger(S,1);
        else { const FString Name=UTF8_TO_TCHAR(luaL_checkstring(S,1)); for(const auto& Sound:FDbcStore::Get().SoundEntries().GetAll()) if(Sound.Name.Equals(Name,ESearchCase::IgnoreCase)) { Id=Sound.ID; break; } }
        if(Id) if(auto* Manager=ResolveAudioManager(S)) Manager->PlaySoundEffect(Id); lua_pushboolean(S,Id!=0); return 1;
    });
}
#else
void WowLuaApi::RegisterSettingsApi(lua_State*) {}
void WowLuaApi::ApplySavedUiSettings(lua_State*) {}
#endif
