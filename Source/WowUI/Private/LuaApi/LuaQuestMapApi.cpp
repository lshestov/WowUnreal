#include "LuaApiRegistry.h"
#include "WowMapUi.h"
#include "WowConnectionManager.h"
#include "Formats/Dbc/DbcStore.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
using namespace WowMapUi;
void Reg(lua_State* L,const char* Name,lua_CFunction F) {lua_pushcfunction(L,F); lua_setglobal(L,Name);}
FWowMapBlob* Blob(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C) return nullptr; luaL_checktype(L,1,LUA_TTABLE); lua_getfield(L,1,"__handle"); const int64 H=lua_tointeger(L,-1); lua_pop(L,1); return &View(C).Blobs.FindOrAdd(H);
}
int LogIndex(FWowLuaContext* C,uint32 Id) {if(C && C->ConnectionManager) {const int32 I=C->ConnectionManager->PacketHandler.QuestLog.IndexOfByPredicate([&](const auto& Q){return Q.QuestId==Id;}); if(I>=0) return I+1;} return 0;}
void Icons(FWowLuaContext* C)
{
    if(!C) return; auto& V=View(C); V.VisibleQuests.Reset(); V.Icons.Reset(); const auto Rows=QuestIcons(C);
    for(const auto& I:Rows) {V.VisibleQuests.Add(I.Quest); V.Icons.Add(I.Quest,I.Point);}
    if(V.IconOverlap<=0 || V.IconPush<=0) return;
    // Deterministic repulsion is restricted to visible server POIs.
    for(int32 Pass=0;Pass<4;++Pass) for(int32 I=0;I<Rows.Num();++I) for(int32 J=I+1;J<Rows.Num();++J)
    {
        auto& A=V.Icons[Rows[I].Quest]; auto& B=V.Icons[Rows[J].Quest]; FVector2D Delta=A-B; const double D=Delta.Length();
        if(D>=V.IconOverlap) continue; Delta=D>1.e-6?Delta/D:FVector2D(1,0); Delta*=FMath::Min(double(V.IconPush),double(V.IconOverlap-D))*.5;
        A.X=FMath::Clamp(A.X+Delta.X,.01,.99); A.Y=FMath::Clamp(A.Y+Delta.Y,.01,.99); B.X=FMath::Clamp(B.X-Delta.X,.01,.99); B.Y=FMath::Clamp(B.Y-Delta.Y,.01,.99);
    }
}
int Leaderboard(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const int32 Raw=luaL_checkinteger(L,1),Index=luaL_checkinteger(L,2);
    if(!C || !C->ConnectionManager || !C->ConnectionManager->PacketHandler.QuestLog.IsValidIndex(Index-1)) return 0;
    const auto& Q=C->ConnectionManager->PacketHandler.QuestLog[Index-1]; const auto* T=C->ConnectionManager->PacketHandler.QuestTemplates.Find(Q.QuestId); if(!T) return 0;
    int32 Dense=0;
    if(Raw>=0 && Raw<10)
    {
        for(int32 I=0;I<Raw && I<4;++I) Dense+=T->CreatureId[I]!=0;
        if(Raw>=4) for(int32 I=0;I<Raw-4;++I) Dense+=T->ItemId[I]!=0;
        if((Raw<4 && !T->CreatureId[Raw]) || (Raw>=4 && !T->ItemId[Raw-4])) return 0;
        lua_getglobal(L,"GetQuestLogLeaderBoard"); lua_pushinteger(L,Dense+1); lua_pushinteger(L,Index); lua_call(L,2,3); return 3;
    }
    if((Raw==16 || Raw==17) && !T->AreaDescription.IsEmpty()) {lua_pushstring(L,TCHAR_TO_UTF8(*T->AreaDescription)); lua_pushstring(L,"event"); lua_pushboolean(L,Q.State==1); return 3;} return 0;
}
}
void WowLuaApi::RegisterQuestMapApi(lua_State* L)
{
    Reg(L,"QuestMapUpdateAllQuests",[](lua_State* S)->int {auto* C=GetContext(S); if(C && C->ConnectionManager) C->ConnectionManager->RequestQuestPois(); lua_getglobal(S,"GetCVarBool"); lua_pushstring(S,"questPOI"); lua_call(S,1,1); const bool Enabled=lua_toboolean(S,-1)!=0; lua_pop(S,1); if(C && !Enabled) {View(C).VisibleQuests.Reset(); View(C).Icons.Reset();} else Icons(C); lua_pushinteger(S,C?View(C).VisibleQuests.Num():0); return 1;});
    Reg(L,"QuestPOIUpdateIcons",[](lua_State* S)->int {Icons(GetContext(S)); return 0;});
    Reg(L,"QuestPOIGetQuestIDByVisibleIndex",[](lua_State* S)->int {auto* C=GetContext(S); const int32 I=luaL_checkinteger(S,1)-1; if(!C || !View(C).VisibleQuests.IsValidIndex(I)) return 0; const uint32 Id=View(C).VisibleQuests[I]; lua_pushinteger(S,Id); lua_pushinteger(S,LogIndex(C,Id)); return 2;});
    Reg(L,"QuestPOIGetQuestIDByIndex",[](lua_State* S)->int {auto* C=GetContext(S); const int32 I=luaL_checkinteger(S,1)-1; if(!C || !C->ConnectionManager || !C->ConnectionManager->PacketHandler.QuestLog.IsValidIndex(I)) return 0; lua_pushinteger(S,C->ConnectionManager->PacketHandler.QuestLog[I].QuestId); return 1;});
    Reg(L,"QuestPOIGetIconInfo",[](lua_State* S)->int {auto* C=GetContext(S); const uint32 Id=luaL_checkinteger(S,1); for(const auto& I:QuestIcons(C)) if(I.Quest==Id) {const auto P=C?View(C).Icons.FindRef(Id):I.Point; lua_pushboolean(S,I.Complete); lua_pushnumber(S,P.IsNearlyZero()?I.Point.X:P.X); lua_pushnumber(S,P.IsNearlyZero()?I.Point.Y:P.Y); lua_pushinteger(S,I.Objective); return 4;} return 0;});
    Reg(L,"GetQuestWorldMapAreaID",[](lua_State* S)->int {
        auto* C=GetContext(S); const uint32 Id=luaL_checkinteger(S,1); uint32 Floor=0; uint32 Area=QuestDestination(C,Id,Floor);
        if(C && C->ConnectionManager) {const auto& H=C->ConnectionManager->PacketHandler;
            if(!Area) if(const auto* T=H.QuestTemplates.Find(Id)) for(const auto& A:FDbcStore::Get().MapUi().Areas()) if(T->Zone>0 && A.AreaId==uint32(T->Zone)) {Area=A.Id; break;}}
        lua_pushinteger(S,Area); lua_pushinteger(S,Floor); return 2;
    });
    Reg(L,"GetQuestSortIndex",[](lua_State* S)->int {auto* C=GetContext(S); const int32 I=luaL_checkinteger(S,1)-1; int32 Result=0; if(C && C->ConnectionManager) {TArray<int32> Zones; const auto& H=C->ConnectionManager->PacketHandler; for(int32 J=0;J<H.QuestLog.Num();++J) {const auto* T=H.QuestTemplates.Find(H.QuestLog[J].QuestId); if(T) {const int32 Z=Zones.AddUnique(T->Zone)+1; if(J==I) Result=Z;}}} lua_pushinteger(S,Result); return 1;});
    Reg(L,"GetQuestPOILeaderBoard",Leaderboard);
    // This compatibility entry point is also empty in build 12340; FrameXML sets its atlas.
    Reg(L,"QuestPOIUpdateTexture",[](lua_State*)->int {return 0;});
    Reg(L,"SetPOIIconOverlapDistance",[](lua_State* S)->int {if(auto* C=GetContext(S)) View(C).IconOverlap=FMath::Clamp(float(luaL_checknumber(S,1)),0.f,.1f); return 0;});
    Reg(L,"SetPOIIconPushDistance",[](lua_State* S)->int {if(auto* C=GetContext(S)) View(C).IconPush=FMath::Clamp(float(luaL_checknumber(S,1)),0.f,.1f); return 0;});

    luaL_getmetatable(L,FRAME_METATABLE);
    const auto Method=[&](const char* Name,lua_CFunction F) {lua_pushcfunction(L,F); lua_setfield(L,-2,Name);};
    Method("SetFillTexture",[](lua_State* S)->int {if(auto* B=Blob(S)) B->FillTexture=UTF8_TO_TCHAR(luaL_checkstring(S,2)); return 0;});
    Method("SetBorderTexture",[](lua_State* S)->int {if(auto* B=Blob(S)) B->BorderTexture=UTF8_TO_TCHAR(luaL_checkstring(S,2)); return 0;});
    Method("SetFillAlpha",[](lua_State* S)->int {if(auto* B=Blob(S)) B->FillAlpha=FMath::Clamp(float(luaL_checknumber(S,2)),0.f,255.f); return 0;});
    Method("SetBorderAlpha",[](lua_State* S)->int {if(auto* B=Blob(S)) B->BorderAlpha=FMath::Clamp(float(luaL_checknumber(S,2)),0.f,255.f); return 0;});
    Method("SetBorderScalar",[](lua_State* S)->int {if(auto* B=Blob(S)) B->BorderScale=FMath::Clamp(float(luaL_checknumber(S,2)),0.f,10.f); return 0;});
    Method("EnableSmoothing",[](lua_State* S)->int {if(auto* B=Blob(S)) B->Smooth=lua_toboolean(S,2)!=0; return 0;});
    Method("EnableMerging",[](lua_State* S)->int {if(auto* B=Blob(S)) B->Merge=lua_toboolean(S,2)!=0; return 0;});
    Method("SetMergeThreshold",[](lua_State* S)->int {if(auto* B=Blob(S)) B->MergeThreshold=FMath::Clamp(float(luaL_checknumber(S,2)),.1f,.5f); return 0;});
    Method("SetNumSplinePoints",[](lua_State* S)->int {if(auto* B=Blob(S)) B->SplinePoints=FMath::Clamp(int32(luaL_checkinteger(S,2)),8,30); return 0;});
    Method("DrawQuestBlob",[](lua_State* S)->int {if(auto* B=Blob(S)) {const uint32 Id=luaL_checkinteger(S,2); if(lua_toboolean(S,3)) B->Quests.Add(Id); else B->Quests.Remove(Id);} return 0;});
    Method("UpdateQuestPOI",[](lua_State*)->int {return 0;});
    Method("UpdateMouseOverTooltip",[](lua_State* S)->int {auto* C=GetContext(S); auto* B=Blob(S); if(!B) return 0; B->HoverQuest=0; B->HoverObjectives.Reset(); const FVector2D P(luaL_checknumber(S,2),luaL_checknumber(S,3)); for(const auto& Shape:QuestShapes(C,*B)) if(Contains(Shape.Points,P)) {if(!B->HoverQuest) B->HoverQuest=Shape.Quest; if(B->HoverQuest==Shape.Quest) for(int32 O:Shape.Objectives) B->HoverObjectives.AddUnique(O);} if(!B->HoverQuest) return 0; lua_pushinteger(S,LogIndex(C,B->HoverQuest)); lua_pushinteger(S,B->HoverObjectives.Num()); return 2;});
    Method("GetNumTooltips",[](lua_State* S)->int {const auto* B=Blob(S); lua_pushinteger(S,B?B->HoverObjectives.Num():0); return 1;});
    Method("GetTooltipIndex",[](lua_State* S)->int {const auto* B=Blob(S); const int32 I=luaL_checkinteger(S,2)-1; lua_pushinteger(S,B && B->HoverObjectives.IsValidIndex(I)?B->HoverObjectives[I]:0); return 1;});
    lua_pop(L,1);
}
#else
void WowLuaApi::RegisterQuestMapApi(lua_State*) {}
#endif
