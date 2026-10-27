#include "LuaApiRegistry.h"
#include "WowMinimapUi.h"
#include "WowConnectionManager.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
using namespace WowMinimapUi;
FString Cvar(lua_State* L,const char* Key) {lua_getglobal(L,"GetCVar"); lua_pushstring(L,Key); lua_call(L,1,1); const FString Value=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):TEXT(""); lua_pop(L,1); return Value;}
void Cvar(lua_State* L,const char* Key,const FString& Value) {lua_getglobal(L,"SetCVar"); lua_pushstring(L,Key); lua_pushstring(L,TCHAR_TO_UTF8(*Value)); lua_call(L,2,0);}
void String(lua_State* L,const FString& Value) {lua_pushstring(L,TCHAR_TO_UTF8(*Value));}
FString Icon(FWowLuaContext* C,const FTracking& Row)
{
    if(!Row.Spell) return TEXT("Interface\\Minimap\\Tracking\\")+Row.Icon;
    if(const auto* Spell=FDbcStore::Get().Spells().GetById(Row.Spell))
        if(const auto* I=FDbcStore::Get().SpellIcons().GetById(TrackingSpell(C)==Row.Spell && Spell->ActiveIconID?Spell->ActiveIconID:Spell->SpellIconID)) return I->TexturePath;
    return FString();
}
void Changed(FWowLuaContext* C) {if(C && C->EventSystem) C->EventSystem->FireEvent(TEXT("MINIMAP_UPDATE_TRACKING"));}
void Cancel(FWowLuaContext* C,uint32 Spell) {if(C && C->ConnectionManager && Spell) {TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Spell),4); C->ConnectionManager->SendRawPacket(0x136,Data);}}
int SetTracking(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C) return 0; auto& V=View(C); const auto Rows=Tracking(C);
    const int32 Index=lua_isnumber(L,1)?lua_tointeger(L,1)-1:INDEX_NONE;
    if(lua_isnumber(L,1) && !Rows.IsValidIndex(Index)) return 0;
    const uint32 Active=TrackingSpell(C);
    if(Index>=0 && Rows[Index].Spell)
    {
        if(C->ConnectionManager && Rows[Index].Spell!=Active) C->ConnectionManager->SendCastSpell(Rows[Index].Spell);
        return 0; // The server's aura update acknowledges a tracking spell.
    }
    Cancel(C,Active); V.TrackingSpell=Active;
    V.CommonKey=Index>=0?Rows[Index].Key:FString(); Cvar(L,"minimapTrackedInfo",V.CommonKey); Changed(C); return 0;
}
FVector2D PingPosition(FWowLuaContext* C)
{
    if(!C || !C->EntityManager || !C->ConnectionManager) return FVector2D::ZeroVector;
    const auto* P=C->EntityManager->GetLocalPlayer(); const auto& H=C->ConnectionManager->PacketHandler;
    if(!P || !H.MinimapPing.Guid || H.MinimapPing.Map!=H.CurrentMapId) return FVector2D::ZeroVector;
    const auto V=Offset(C,Position(C,P->Guid),H.MinimapPing.Position,Radius(C),P->Movement.Orientation); return FVector2D(V.X,-V.Y);
}
int Ping(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->EntityManager || !C->ConnectionManager || !C->FrameManager) return 0;
    const auto* P=C->EntityManager->GetLocalPlayer(); if(!P) return 0; const auto Rect=C->FrameManager->GetLayoutRect(C->FrameManager->FindFrame(TEXT("Minimap")));
    FVector Delta=FVector::ZeroVector;
    if(lua_isnumber(L,2) && lua_isnumber(L,3) && Rect.Z>0 && Rect.W>0)
    {
        // Script coordinates are logical UI pixels, positive upward.
        const double Scale=C->FrameManager->GetEffectiveFrameScale(C->FrameManager->FindFrame(TEXT("Minimap")));
        Delta.X=lua_tonumber(L,3)*Scale/Rect.W*Radius(C)*2; Delta.Y=-lua_tonumber(L,2)*Scale/Rect.Z*Radius(C)*2;
        if(View(C).Rotates) {const auto A=Delta; const float F=P->Movement.Orientation; Delta.X=A.X*FMath::Cos(F)-A.Y*FMath::Sin(F); Delta.Y=A.X*FMath::Sin(F)+A.Y*FMath::Cos(F);}
    }
    const FVector Point=Position(C,P->Guid)+Delta; if(Point.ContainsNaN() || Point.GetAbsMax()>20000) return 0;
    auto& H=C->ConnectionManager->PacketHandler;
    if(!H.GroupInfo.IsEmpty()) {TArray<uint8> Data; for(float N:{float(Point.X),float(Point.Y)}) Data.Append(reinterpret_cast<const uint8*>(&N),4); C->ConnectionManager->SendRawPacket(0x1D5,Data);}
    H.MinimapPing.Guid=P->Guid; H.MinimapPing.Map=H.CurrentMapId; H.MinimapPing.Position=Point; H.MinimapPing.At=FPlatformTime::Seconds(); ++H.MinimapPingRevision; return 0;
}
}
void WowLuaApi::RegisterMinimapApi(lua_State* L)
{
    const auto Reg=[&](const char* Name,lua_CFunction F){lua_pushcfunction(L,F); lua_setglobal(L,Name);};
    Reg("GetNumTrackingTypes",[](lua_State* S)->int {lua_pushinteger(S,Tracking(GetContext(S)).Num()); return 1;});
    Reg("GetTrackingInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto Rows=Tracking(C); const int32 I=luaL_checkinteger(S,1)-1; if(!Rows.IsValidIndex(I)) return 0; const auto& T=Rows[I];
        if(T.Spell) String(S,T.Name); else {lua_getglobal(S,TCHAR_TO_UTF8(*T.Key)); if(!lua_isstring(S,-1)) {lua_pop(S,1); String(S,T.Name);}}
        const FString Art=Icon(C,T); if(Art.IsEmpty()) lua_pushnil(S); else String(S,Art);
        if(C && (T.Spell?TrackingSpell(C)==T.Spell:View(C).CommonKey==T.Key)) lua_pushinteger(S,1); else lua_pushnil(S);
        lua_pushstring(S,T.Spell?"spell":"other"); return 4;
    });
    Reg("GetTrackingTexture",[](lua_State* S)->int {auto* C=GetContext(S); const uint32 Active=C && View(C).CommonKey.IsEmpty()?TrackingSpell(C):0; for(const auto& T:Tracking(C)) if(C && ((!T.Spell && T.Key==View(C).CommonKey) || (T.Spell && T.Spell==Active))) {String(S,Icon(C,T)); return 1;} lua_pushstring(S,"Interface\\Minimap\\Tracking\\None"); return 1;});
    Reg("SetTracking",SetTracking);
    luaL_getmetatable(L,FRAME_METATABLE);
    const auto Method=[&](const char* Name,lua_CFunction F){lua_pushcfunction(L,F); lua_setfield(L,-2,Name);};
    Method("SetZoom",[](lua_State* S)->int {auto* C=GetContext(S); if(C && C->FrameManager) {const int32 Z=FMath::Clamp(int32(luaL_checkinteger(S,2)),0,5); if(Z!=C->FrameManager->MinimapZoom) {C->FrameManager->MinimapZoom=Z; Cvar(S,"minimapZoom",FString::FromInt(Z)); if(C->EventSystem) C->EventSystem->FireEvent(TEXT("MINIMAP_UPDATE_ZOOM"));}} return 0;});
    Method("SetBlipTexture",[](lua_State* S)->int {if(auto* C=GetContext(S)) View(C).BlipTexture=UTF8_TO_TCHAR(luaL_checkstring(S,2)); return 0;});
    Method("SetPlayerTexture",[](lua_State* S)->int {if(auto* C=GetContext(S)) View(C).PlayerTexture=UTF8_TO_TCHAR(luaL_checkstring(S,2)); return 0;});
    Method("SetStaticArrowTexture",[](lua_State* S)->int {if(auto* C=GetContext(S)) View(C).StaticArrowTexture=UTF8_TO_TCHAR(luaL_checkstring(S,2)); return 0;});
    Method("SetPlayerTextureWidth",[](lua_State* S)->int {if(auto* C=GetContext(S)) View(C).PlayerSize.X=FMath::Clamp(luaL_checknumber(S,2),0.,1024.); return 0;});
    Method("SetPlayerTextureHeight",[](lua_State* S)->int {if(auto* C=GetContext(S)) View(C).PlayerSize.Y=FMath::Clamp(luaL_checknumber(S,2),0.,1024.); return 0;});
    Method("PingLocation",Ping);
    Method("GetPingPosition",[](lua_State* S)->int {const auto P=PingPosition(GetContext(S)); lua_pushnumber(S,P.X); lua_pushnumber(S,P.Y); return 2;});
    Method("SetTracking",[](lua_State* S)->int {
        auto* C=GetContext(S); if(!C || !C->FrameManager) return 0; lua_getfield(S,1,"__handle"); const int64 Handle=lua_tointeger(S,-1); lua_pop(S,1);
        const uint32 Active=TrackingSpell(C); const auto& V=View(C);
        if(V.CommonKey.IsEmpty() && Active) C->FrameManager->TooltipSetSpell(Handle,Active,C->EntityManager && C->EntityManager->GetLocalPlayer()?C->EntityManager->GetLocalPlayer()->GetLevel():1,true);
        else
        {
            FString Text=TEXT("No tracking"); FString Key=TEXT("MINIMAP_TRACKING_TOOLTIP_NONE");
            for(const auto& T:Tracking(C)) if(!T.Spell && T.Key==V.CommonKey) {Key=T.Key; Text=T.Name; break;}
            lua_getglobal(S,TCHAR_TO_UTF8(*Key)); if(lua_isstring(S,-1)) Text=UTF8_TO_TCHAR(lua_tostring(S,-1)); lua_pop(S,1); C->FrameManager->TooltipSetText(Handle,Text);
        }
        C->FrameManager->SetFrameVisible(Handle,true); return 0;
    });
    lua_pop(L,1);
}
void WowLuaApi::UpdateMinimapState(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->EntityManager || !C->ConnectionManager || !C->EventSystem) return;
    const auto* P=C->EntityManager->GetLocalPlayer(); if(!P) return; auto& H=C->ConnectionManager->PacketHandler; auto& V=View(C);
    const uint32 Spell=TrackingSpell(C); const bool Initial=V.TrackingHash==MAX_uint32;
    V.CommonKey=Cvar(L,"minimapTrackedInfo"); V.Rotates=Cvar(L,"rotateMinimap")==TEXT("1");
    if(Spell!=V.TrackingSpell && Spell) {V.CommonKey.Empty(); Cvar(L,"minimapTrackedInfo",V.CommonKey);}
    V.TrackingSpell=Spell;
    uint32 Hash=HashCombineFast(Spell,GetTypeHash(V.CommonKey)); for(const auto& T:Tracking(C)) Hash=HashCombineFast(Hash,T.Spell?T.Spell:GetTypeHash(T.Key));
    if(Hash!=V.TrackingHash) {V.TrackingHash=Hash; Changed(C);}
    if(Initial && C->FrameManager) {C->FrameManager->MinimapZoom=FMath::Clamp(FCString::Atoi(*Cvar(L,"minimapZoom")),0,5); C->EventSystem->FireEvent(TEXT("MINIMAP_UPDATE_ZOOM"));}
    uint32 QuestHash=HashCombineFast(H.CurrentMapId,P->GetLevel());
    TArray<uint64> Givers; for(const auto& Pair:C->EntityManager->GetAll()) if((Pair.Value->IsUnit() && Pair.Value->GetField(UnitField::NPC_FLAGS)&2) || Pair.Value->IsGameObject()) Givers.Add(Pair.Key);
    Givers.Sort(); for(uint64 Guid:Givers) QuestHash=HashCombineFast(QuestHash,GetTypeHash(Guid));
    for(int32 I=0;I<125;++I) QuestHash=HashCombineFast(QuestHash,P->GetField(PlayerField::BASE+0x000A+I));
    const double Now=FPlatformTime::Seconds();
    if(QuestHash!=V.QuestgiverHash && Now>=V.NextQuestgiverQuery && C->ConnectionManager->GetState()==EWowSessionState::WorldInGame)
    {V.QuestgiverHash=QuestHash; V.NextQuestgiverQuery=Now+2; C->ConnectionManager->SendRawPacket(0x417,{});}
    for(auto I=H.QuestGiverStatuses.CreateIterator();I;++I) if(!C->EntityManager->Find(I.Key())) I.RemoveCurrent();
    if(C->FrameManager && H.MinimapPing.Guid && H.MinimapPing.Map==H.CurrentMapId && Now-H.MinimapPing.At<=5.5)
    {
        auto& Frames=*C->FrameManager; const int64 Handle=Frames.FindFrame(TEXT("MinimapPing")),Mini=Frames.FindFrame(TEXT("Minimap"));
        if(Handle>=0 && Mini>=0 && Frames.IsFrameDrawn(Handle))
        {
            const auto Point=PingPosition(C); const auto Rect=Frames.GetLayoutRect(Mini); const float Scale=Frames.GetEffectiveFrameScale(Mini);
            FWowAnchor A; A.Point=A.RelativePoint=EWowAnchorPoint::CENTER; A.RelativeTo=TEXT("Minimap"); A.OffsetX=Point.X*Rect.Z/Scale; A.OffsetY=Point.Y*Rect.W/Scale; Frames.SetFrameAnchors(Handle,{A});
        }
    }
    if(H.MinimapPingRevision!=V.PingRevision)
    {
        V.PingRevision=H.MinimapPingRevision; if(!H.MinimapPing.Guid || H.MinimapPing.Map!=H.CurrentMapId || Now-H.MinimapPing.At>5.5) return;
        FString Unit=H.MinimapPing.Guid==P->Guid?TEXT("player"):TEXT("");
        for(int32 I=0;I<H.GroupInfo.Members.Num();++I) if(H.GroupInfo.Members[I].Guid==H.MinimapPing.Guid) {Unit=(H.GroupInfo.IsRaidGroup()?TEXT("raid"):TEXT("party"))+FString::FromInt(I+1); break;}
        const auto Point=PingPosition(C); C->EventSystem->FireEventTyped(TEXT("MINIMAP_PING"),[&](lua_State* S){String(S,Unit); lua_pushnumber(S,Point.X); lua_pushnumber(S,Point.Y); return 3;});
    }
}
#else
void WowLuaApi::RegisterMinimapApi(lua_State*) {}
void WowLuaApi::UpdateMinimapState(lua_State*) {}
#endif
