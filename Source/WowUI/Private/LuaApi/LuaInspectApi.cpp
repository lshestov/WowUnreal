#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "WowFrameManager.h"
#include "Formats/Dbc/DbcStore.h"

struct FWowInspectUiView {uint32 Revision=0,Honor=0; uint64 Guid=0; FString Token; bool ItemsAnnounced=false;};
#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
FWowPacketHandler* Handler(FWowLuaContext* C) {return C && C->ConnectionManager?&C->ConnectionManager->PacketHandler:nullptr;}
FWowInspectUiView& View(FWowLuaContext* C) {if(!C->InspectUi) C->InspectUi=MakeShared<FWowInspectUiView>(); return *C->InspectUi;}
bool Flag(lua_State* L,int I) {return lua_toboolean(L,I)!=0 && (!lua_isnumber(L,I) || lua_tonumber(L,I)!=0);}
void PushFlag(lua_State* L,bool Value) {if(Value) lua_pushinteger(L,1); else lua_pushnil(L);}
void Reg(lua_State* L,const char* Name,lua_CFunction F) {lua_pushcfunction(L,F); lua_setglobal(L,Name);}
bool Can(FWowLuaContext* C,const FWowEntity* Unit)
{
    const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    return P && Unit && Unit->IsPlayer() && Unit->Guid!=P->Guid && C->EntityManager->Find(Unit->Guid) && FVector::DistSquared(P->Movement.Position,Unit->Movement.Position)<=28.*28. && !WowLuaApi::CanUnitAttack(C,P,Unit);
}
bool Send(FWowLuaContext* C,uint32 Op,uint64 Guid) {if(!C || !C->ConnectionManager || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame) return false; TArray<uint8> B; B.Append(reinterpret_cast<const uint8*>(&Guid),8); C->ConnectionManager->SendRawPacket(Op,B); return true;}
int Notify(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); auto* H=Handler(C); const auto* U=WowLuaApi::ResolveUnitToken(L); if(!H || !Can(C,U)) return 0;
    auto& V=View(C); H->ResetInspectState(U->Guid); V.Guid=U->Guid; V.Token=UTF8_TO_TCHAR(luaL_checkstring(L,1)); V.ItemsAnnounced=false; Send(C,0x114,U->Guid); Send(C,0x377,U->Guid); return 0;
}
TArray<const FTalentTabDbcEntry*> Tabs(FWowLuaContext* C)
{
    TArray<const FTalentTabDbcEntry*> Out; auto* H=Handler(C); const auto* U=H?H->EntityManager.FindPlayer(H->Inspect.Guid):nullptr; if(!U || !U->GetClassId()) return Out;
    const uint32 Mask=1u<<(U->GetClassId()-1),Race=U->GetRaceId()?1u<<(U->GetRaceId()-1):0;
    for(const auto& T:FDbcStore::Get().TalentTabs().GetAll()) if((T.ClassMask&Mask) && (!T.RaceMask || (T.RaceMask&Race))) Out.Add(&T);
    Out.Sort([](const auto& A,const auto& B) {return A.TabPage<B.TabPage;}); return Out;
}
const FTalentTabDbcEntry* Tab(FWowLuaContext* C,int32 I) {const auto T=Tabs(C); return T.IsValidIndex(I-1)?T[I-1]:nullptr;}
TArray<const FTalentDbcEntry*> Nodes(FWowLuaContext* C,int32 I)
{
    TArray<const FTalentDbcEntry*> Out; const auto* T=Tab(C,I); if(T) for(const auto& N:FDbcStore::Get().Talents().GetAll()) if(N.TalentTab==T->TalentTabID) Out.Add(&N);
    Out.Sort([](const auto& A,const auto& B) {return A.Row==B.Row?(A.Col==B.Col?A.TalentID<B.TalentID:A.Col<B.Col):A.Row<B.Row;}); return Out;
}
const FTalentDbcEntry* Node(FWowLuaContext* C,int32 T,int32 I) {const auto N=Nodes(C,T); return N.IsValidIndex(I-1)?N[I-1]:nullptr;}
int32 Group(lua_State* L,FWowLuaContext* C,int I) {const auto* H=Handler(C); return lua_isnumber(L,I)?lua_tointeger(L,I)-1:H?H->Inspect.Active:0;}
int32 Rank(FWowLuaContext* C,uint32 Id,int32 G) {const auto* H=Handler(C); if(H && H->Inspect.Ready && H->Inspect.Groups.IsValidIndex(G)) for(const auto& T:H->Inspect.Groups[G].Talents) if(T.TalentId==Id) return T.Rank+1; return 0;}
int32 Maximum(const FTalentDbcEntry& N) {int32 Max=0; for(int32 I=0;I<5;++I) if(N.RankID[I]) Max=I+1; return Max;}
int32 Spent(FWowLuaContext* C,uint32 Tab,int32 G) {const auto* H=Handler(C); int32 Sum=0; if(H && H->Inspect.Groups.IsValidIndex(G)) for(const auto& T:H->Inspect.Groups[G].Talents) {const auto* N=FDbcStore::Get().Talents().GetById(T.TalentId); if(N && N->TalentTab==Tab) Sum+=T.Rank+1;} return Sum;}
enum ERead {NumTabs,NumNodes,NumGroups,ActiveGroup,FreePoints,TabInfo,NodeInfo,Prerequisites,Link};
int Read(lua_State* L)
{
    const int32 Kind=lua_tointeger(L,lua_upvalueindex(1)),FlagIndex=Kind==NumNodes || Kind==TabInfo?2:Kind==NodeInfo || Kind==Prerequisites || Kind==Link?3:1;
    if(!Flag(L,FlagIndex)) {const int N=lua_gettop(L); lua_pushvalue(L,lua_upvalueindex(2)); lua_insert(L,1); lua_call(L,N,LUA_MULTRET); return lua_gettop(L);}
    auto* C=WowLuaApi::GetContext(L); const auto* H=Handler(C); const bool Pet=Flag(L,FlagIndex+1); if(Pet || !H) {if(Kind<=FreePoints) {lua_pushinteger(L,0); return 1;} return 0;}
    const auto& State=H->Inspect;
    if(Kind==NumTabs) {lua_pushinteger(L,Tabs(C).Num()); return 1;}
    if(Kind==NumNodes) {lua_pushinteger(L,Nodes(C,luaL_checkinteger(L,1)).Num()); return 1;}
    if(Kind==NumGroups) {lua_pushinteger(L,State.Groups.Num()); return 1;}
    if(Kind==ActiveGroup) {lua_pushinteger(L,State.Active+1); return 1;}
    if(Kind==FreePoints) {lua_pushinteger(L,State.Ready?State.FreePoints:0); return 1;}
    if(Kind==TabInfo)
    {
        const auto* T=Tab(C,luaL_checkinteger(L,1)); if(!T) return 0; const int32 G=Group(L,C,4); lua_pushstring(L,TCHAR_TO_UTF8(*T->Name)); const auto* Icon=FDbcStore::Get().SpellIcons().GetById(T->SpellIcon); if(Icon) lua_pushstring(L,TCHAR_TO_UTF8(*Icon->TexturePath)); else lua_pushnil(L);
        lua_pushinteger(L,Spent(C,T->TalentTabID,G)); lua_pushstring(L,TCHAR_TO_UTF8(*T->InternalName)); lua_pushinteger(L,0); return 5;
    }
    const auto* N=Node(C,luaL_checkinteger(L,1),luaL_checkinteger(L,2)); if(!N || !Maximum(*N)) return 0; const int32 G=Group(L,C,5),R=Rank(C,N->TalentID,G);
    if(Kind==Prerequisites)
    {int Count=0; for(int I=0;I<3;++I) if(const auto* Required=FDbcStore::Get().Talents().GetById(N->Prerequisite[I])) {lua_pushinteger(L,Required->Row+1); lua_pushinteger(L,Required->Col+1); const bool Met=Rank(C,Required->TalentID,G)>int32(N->PrerequisiteRank[I]); PushFlag(L,Met); PushFlag(L,Met); Count+=4;} return Count;}
    const auto* Spell=FDbcStore::Get().Spells().GetById(N->RankID[FMath::Clamp(R-1,0,Maximum(*N)-1)]); if(!Spell) return 0;
    if(Kind==Link) {const FString Text=FString::Printf(TEXT("|cff4e96f7|Htalent:%u:%d|h[%s]|h|r"),N->TalentID,R-1,*Spell->SpellName); lua_pushstring(L,TCHAR_TO_UTF8(*Text)); return 1;}
    lua_pushstring(L,TCHAR_TO_UTF8(*Spell->SpellName)); const auto* Icon=FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID); if(Icon) lua_pushstring(L,TCHAR_TO_UTF8(*Icon->TexturePath)); else lua_pushnil(L);
    bool Met=true; for(int32 Index=0;Index<3;++Index) if(N->Prerequisite[Index]) Met&=Rank(C,N->Prerequisite[Index],G)>int32(N->PrerequisiteRank[Index]);
    int32 Earlier=0; for(const auto* Other:Nodes(C,luaL_checkinteger(L,1))) if(Other->Row<N->Row) Earlier+=Rank(C,Other->TalentID,G); Met&=Earlier>=int32(N->Row)*5;
    lua_pushinteger(L,N->Row+1); lua_pushinteger(L,N->Col+1); lua_pushinteger(L,R); lua_pushinteger(L,Maximum(*N)); PushFlag(L,(N->Flags&1)!=0); PushFlag(L,Met); lua_pushinteger(L,R); PushFlag(L,Met); return 10;
}
}
void WowLuaApi::RegisterInspectApi(lua_State* L)
{
    Reg(L,"CanInspect",[](lua_State* S)->int {PushFlag(S,Can(GetContext(S),ResolveUnitToken(S))); return 1;});
    Reg(L,"NotifyInspect",Notify);
    Reg(L,"ClearInspectPlayer",[](lua_State* S)->int {auto* C=GetContext(S); if(auto* H=Handler(C)) H->ResetInspectState(); if(C) {View(C).Guid=0; View(C).Token.Empty();} return 0;});
    Reg(L,"InspectUnit",[](lua_State* S)->int {if(!Can(GetContext(S),ResolveUnitToken(S))) return 0; lua_getglobal(S,"LoadAddOn"); lua_pushstring(S,"Blizzard_InspectUI"); lua_call(S,1,1); const bool Loaded=lua_toboolean(S,-1)!=0; lua_pop(S,1); if(Loaded) {lua_getglobal(S,"InspectFrame_Show"); lua_pushvalue(S,1); lua_call(S,1,0);} return 0;});
    Reg(L,"HasInspectHonorData",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); PushFlag(S,H && H->Inspect.HonorReady); return 1;});
    Reg(L,"RequestInspectHonorData",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); if(H && H->Inspect.Guid) Send(C,0x2D6,H->Inspect.Guid); return 0;});
    Reg(L,"GetInspectHonorData",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const auto* I=H?&H->Inspect:nullptr; lua_pushinteger(S,I?I->TodayKills:0); lua_pushinteger(S,I?I->TodayHonor:0); lua_pushinteger(S,I?I->YesterdayKills:0); lua_pushinteger(S,I?I->YesterdayHonor:0); lua_pushinteger(S,I?I->LifetimeKills:0); lua_pushinteger(S,0); return 6;});
    Reg(L,"GetInspectArenaTeamData",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const int32 I=luaL_checkinteger(S,1)-1; if(!H || I<0 || I>=3) return 0; const auto& A=H->Inspect.Arena[I]; const auto* M=H->ArenaIdentities.Find(A.Id); if(!M) return 0; lua_pushstring(S,TCHAR_TO_UTF8(*M->Name)); lua_pushinteger(S,M->Size); lua_pushinteger(S,A.Rating); lua_pushinteger(S,A.Games); lua_pushinteger(S,A.Wins); lua_pushinteger(S,A.Played); lua_pushinteger(S,A.Personal); const auto Color=[&](uint32 C){lua_pushnumber(S,((C>>16)&255)/255.); lua_pushnumber(S,((C>>8)&255)/255.); lua_pushnumber(S,(C&255)/255.);}; Color(M->Background); lua_pushinteger(S,M->Emblem); Color(M->EmblemColor); lua_pushinteger(S,M->Border); Color(M->BorderColor); return 19;});
    const char* Names[]={"GetNumTalentTabs","GetNumTalents","GetNumTalentGroups","GetActiveTalentGroup","GetUnspentTalentPoints","GetTalentTabInfo","GetTalentInfo","GetTalentPrereqs","GetTalentLink"};
    for(int I=0;I<UE_ARRAY_COUNT(Names);++I) {lua_pushinteger(L,I); lua_getglobal(L,Names[I]); lua_pushcclosure(L,Read,2); lua_setglobal(L,Names[I]);}
}
void WowLuaApi::UpdateInspectState(lua_State* L)
{
    auto* C=GetContext(L); auto* H=Handler(C); if(!H || !C->EventSystem) return; auto& V=View(C); auto& I=H->Inspect;
    if(!V.Guid || I.Guid!=V.Guid) return;
    if(V.Revision!=I.Revision && I.Ready)
    {
        bool Ready=true; for(const auto& Item:I.Items) if(!QueryItemTemplate(C,Item.Value.Entry)) Ready=false;
        if(Ready) {V.Revision=I.Revision; V.ItemsAnnounced=true; C->EventSystem->FireEvent(TEXT("INSPECT_TALENT_READY")); C->EventSystem->FireEvent(TEXT("UNIT_INVENTORY_CHANGED"),{V.Token}); C->EventSystem->FireEvent(TEXT("UNIT_MODEL_CHANGED"),{V.Token});}
    }
    if(V.Honor!=I.HonorRevision) {V.Honor=I.HonorRevision; C->EventSystem->FireEvent(TEXT("INSPECT_HONOR_UPDATE"));}
}
int WowLuaApi::SetInspectTalentTooltip(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->FrameManager) return 0; const auto* N=Node(C,luaL_checkinteger(L,2),luaL_checkinteger(L,3)); if(!N || !Maximum(*N)) return 0;
    const int32 R=Rank(C,N->TalentID,Group(L,C,6)); lua_getfield(L,1,"__handle"); const int64 Handle=lua_tointeger(L,-1); lua_pop(L,1); auto* H=Handler(C); const auto* U=H?H->EntityManager.FindPlayer(H->Inspect.Guid):nullptr;
    C->FrameManager->TooltipSetSpell(Handle,N->RankID[FMath::Clamp(R-1,0,Maximum(*N)-1)],U?U->GetLevel():1); return 0;
}
#else
void WowLuaApi::RegisterInspectApi(lua_State*) {}
void WowLuaApi::UpdateInspectState(lua_State*) {}
int WowLuaApi::SetInspectTalentTooltip(lua_State*) {return 0;}
#endif
