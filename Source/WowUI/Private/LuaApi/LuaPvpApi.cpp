#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "WowMapUi.h"
#include "Formats/Dbc/DbcStore.h"

struct FWowPvpUiView
{
    uint32 Revision=0,List=0,Scores=0,Positions=0,Arena=0,Invite=0,Result=0,World=MAX_uint32,HonorHash=0,IdentityCount=0;
    uint32 Selected=0,WorldEvent=0; int32 Instance=0,Faction=-1; uint64 RosterSelection[3]={};
    double StatusAt=0,ScoreAt=0,PositionAt=0,ListAt=0;
    TArray<uint32> Types;
    TMap<uint32,double> ArenaQueries;
    TMap<uint64,double> NameQueries;
    uint32 ArenaResult=0,Level=0;
    bool InArena=false;
    TSet<uint64> VisibleOpponents;
};
#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
using FBattlefield=FPvpUiDbc::FBattlefield;
FWowPvpUiView& View(FWowLuaContext* C) {if(!C->PvpUi) C->PvpUi=MakeShared<FWowPvpUiView>(); return *C->PvpUi;}
FWowPacketHandler* Handler(FWowLuaContext* C) {return C && C->ConnectionManager?&C->ConnectionManager->PacketHandler:nullptr;}
const FWowPlayerEntity* Player(FWowLuaContext* C) {return C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;}
const FPvpUiDbc& Data() {return FDbcStore::Get().PvpUi();}
void Reg(lua_State* L,const char* N,lua_CFunction F) {lua_pushcfunction(L,F); lua_setglobal(L,N);}
void String(lua_State* L,const FString& S) {lua_pushstring(L,TCHAR_TO_UTF8(*S));}
void Flag(lua_State* L,bool B) {if(B) lua_pushinteger(L,1); else lua_pushnil(L);}
bool Bool(lua_State* L,int32 I) {return lua_toboolean(L,I)!=0 && (!lua_isnumber(L,I) || lua_tonumber(L,I)!=0);}
struct FWire {TArray<uint8> B; template<typename T> FWire& Add(T V) {B.Append(reinterpret_cast<const uint8*>(&V),sizeof(V)); return *this;} FWire& Text(const FString& S) {FTCHARToUTF8 V(*S); B.Append(reinterpret_cast<const uint8*>(V.Get()),V.Length()+1); return *this;}};
bool Send(FWowLuaContext* C,uint32 Op,const FWire& W=FWire()) {if(!C || !C->ConnectionManager || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame) return false; C->ConnectionManager->SendRawPacket(Op,W.B); return true;}
uint32 Field(FWowLuaContext* C,uint32 Offset) {const auto* P=Player(C); return P?P->GetField(UnitField::END+Offset):0;}
bool Alliance(uint8 Race) {return Race==1 || Race==3 || Race==4 || Race==7 || Race==11;}
const TCHAR* ClassFile(uint8 Class) {static const TCHAR* Files[]={TEXT(""),TEXT("WARRIOR"),TEXT("PALADIN"),TEXT("HUNTER"),TEXT("ROGUE"),TEXT("PRIEST"),TEXT("DEATHKNIGHT"),TEXT("SHAMAN"),TEXT("MAGE"),TEXT("WARLOCK"),TEXT(""),TEXT("DRUID")}; return Class<UE_ARRAY_COUNT(Files)?Files[Class]:TEXT("");}
FString LocalizedClass(lua_State* L,uint8 Class)
{const int32 Top=lua_gettop(L); FString S=ClassFile(Class); lua_getglobal(L,"LOCALIZED_CLASS_NAMES_MALE"); if(lua_istable(L,-1)) {lua_getfield(L,-1,TCHAR_TO_UTF8(*S)); if(lua_isstring(L,-1)) S=UTF8_TO_TCHAR(lua_tostring(L,-1));} lua_settop(L,Top); return S;}
const FWowPvpState::FQueue* Queue(FWowLuaContext* C,int32 Index) {const auto* H=Handler(C); return H && Index>=1 && Index<=3?&H->Pvp.Queues[Index-1]:nullptr;}
const FWowPvpState::FQueue* Active(FWowLuaContext* C) {const auto* H=Handler(C); if(H) for(const auto& Q:H->Pvp.Queues) if(Q.Status==3) return &Q; return nullptr;}
uint32 Elapsed(const FWowPvpState::FQueue* Q) {return Q?uint32(FMath::Min(4294967295.,FMath::Max(0.,FPlatformTime::Seconds()-Q->At)*1000.)):0;}
bool Arena(FWowLuaContext* C) {if(const auto* Q=Active(C)) return Q->Arena!=0; const auto* H=Handler(C); const auto* M=H?FDbcStore::Get().Maps().GetById(H->CurrentMapId):nullptr; return M && M->MapType==4;}
FString QueueName(const FWowPvpState::FQueue* Q) {if(!Q) return FString(); const auto* B=Data().Battlefield(Q->Type); if(Q->Status==3 && Q->Map) if(const auto* M=FDbcStore::Get().Maps().GetById(Q->Map)) return M->Name; return B?B->Name:FString();}
bool Eligible(FWowLuaContext* C,const FBattlefield& B) {const auto* P=Player(C); const uint32 Level=P?uint32(P->GetLevel()):0; return P && (B.Id==32?Level>=80:Level>=B.Min && (!B.Max || Level<=B.Max));}
bool Holiday(FWowLuaContext* C,const FBattlefield& B) {const auto* H=Handler(C); return H && B.Holiday && H->WorldStates.FindRef(B.Holiday)!=0;}
TArray<uint32>& Types(FWowLuaContext* C)
{
    auto& V=View(C); if(V.Types.IsEmpty()) for(const auto& B:Data().Battlefields()) if(B.Type==3) V.Types.Add(B.Id); return V.Types;
}
const FBattlefield* ByIndex(FWowLuaContext* C,int32 Index) {if(!C) return nullptr; const auto& T=Types(C); return T.IsValidIndex(Index-1)?Data().Battlefield(T[Index-1]):nullptr;}
const FBattlefield* Selected(FWowLuaContext* C) {const auto* H=Handler(C); return C?Data().Battlefield(View(C).Selected?View(C).Selected:H?H->Pvp.List.Type:0):nullptr;}
int32 Info(lua_State* L,FWowLuaContext* C,const FBattlefield* B,bool Full)
{
    if(!B) return 0; String(L,B->Name); if(Full) {const auto* P=Player(C); String(L,B->Maps.Num()?Data().Description(B->Maps[0],P && Alliance(P->GetRaceId())):FString()); lua_pushinteger(L,B->Group);}
    Flag(L,Eligible(C,*B)); Flag(L,Holiday(C,*B)); Flag(L,B->Id==32); if(!Full) lua_pushinteger(L,B->Id); return Full?6:5;
}
void ListRequest(FWowLuaContext* C,const FBattlefield* B,uint8 Source)
{if(!C || !B) return; auto& V=View(C); const double Now=FPlatformTime::Seconds(); if(V.Selected==B->Id && Now<V.ListAt) return; V.Selected=B->Id; V.Instance=0; V.ListAt=Now+.25; Send(C,0x23C,FWire().Add(B->Id).Add(Source).Add(uint8(1)));}
uint32 TeamId(FWowLuaContext* C,int32 Slot) {return Slot>=1 && Slot<=3?Field(C,0x454+(Slot-1)*7):0;}
void QueryTeam(FWowLuaContext* C,uint32 Id,bool Roster=false)
{if(!C || !Id) return; auto& At=View(C).ArenaQueries.FindOrAdd(Id); if(FPlatformTime::Seconds()<At) return; At=FPlatformTime::Seconds()+4; Send(C,0x34B,FWire().Add(Id)); if(Roster) Send(C,0x34D,FWire().Add(Id));}
uint8 Race(FWowLuaContext* C,uint64 Guid)
{auto* H=Handler(C); const auto* U=C && C->EntityManager?C->EntityManager->FindPlayer(Guid):nullptr; if(U) return U->GetRaceId(); const auto* I=H?H->PlayerIdentities.Find(Guid):nullptr; return I?I->Race:0;}
uint8 Class(FWowLuaContext* C,uint64 Guid)
{auto* H=Handler(C); const auto* U=C && C->EntityManager?C->EntityManager->FindPlayer(Guid):nullptr; if(U) return U->GetClassId(); const auto* I=H?H->PlayerIdentities.Find(Guid):nullptr; return I?I->Class:0;}
int32 Faction(FWowLuaContext* C,const FWowPvpState::FScore& S) {const auto* H=Handler(C); return H && H->Pvp.ScoreArena?S.Team:Alliance(Race(C,S.Guid))?1:0;}
TArray<const FWowPvpState::FScore*> Scores(FWowLuaContext* C)
{TArray<const FWowPvpState::FScore*> Out; const auto* H=Handler(C); if(H && C) for(const auto& S:H->Pvp.Scores) if(View(C).Faction<0 || Faction(C,S)==View(C).Faction) Out.Add(&S); return Out;}
TArray<const FPvpUiDbc::FWorldState*> WorldRows(FWowLuaContext* C,bool Stats)
{
    TArray<const FPvpUiDbc::FWorldState*> Out; const auto* H=Handler(C); if(!H) return Out;
    for(const auto& S:Data().WorldStates()) if(S.Type==2?Stats:!Stats)
        if((S.Map==MAX_uint32 || S.Map==H->CurrentMapId) && (!S.Area || S.Area==H->ServerZoneId || S.Area==H->ServerAreaId) && (Stats || (S.Variable && H->WorldStates.Contains(S.Variable)))) Out.Add(&S);
    Out.Sort([](const auto& A,const auto& B) {return A.Id<B.Id;}); return Out;
}
FString FormatState(FWowLuaContext* C,FString S)
{
    const auto* H=Handler(C); if(!H) return S;
    for(int32 I=0;I<S.Len();++I) if(S[I]==TCHAR('%')) {int32 End=I+1; while(End<S.Len() && FChar::IsDigit(S[End])) ++End; if(End>I+1 && End<S.Len() && S[End]==TCHAR('w')) {const FString V=FString::FromInt(H->WorldStates.FindRef(FCString::Atoi(*S.Mid(I+1,End-I-1)))); S=S.Left(I)+V+S.Mid(End+1); I+=V.Len()-1;}} return S;
}
int Honor(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const int K=lua_tointeger(L,lua_upvalueindex(1));
    if(K==0) {lua_pushinteger(L,Field(C,0x435)&0xFFFF); lua_pushinteger(L,Field(C,0x436)); return 2;}
    if(K==1) {lua_pushinteger(L,Field(C,0x435)>>16); lua_pushinteger(L,Field(C,0x437)); return 2;}
    lua_pushinteger(L,Field(C,K==2 || K==3?0x438:K==4?0x469:0x46A)); if(K==2) {lua_pushinteger(L,0); return 2;} return 1;
}
int Team(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); auto* H=Handler(C); const int32 Slot=luaL_checkinteger(L,1); const uint32 Id=TeamId(C,Slot); if(!H || !Id) return 0; QueryTeam(C,Id,true);
    const auto* M=H->ArenaIdentities.Find(Id); const auto* T=H->Pvp.ArenaTeams.Find(Id); if(!M || !T) return 0;
    String(L,M->Name); lua_pushinteger(L,M->Size); lua_pushinteger(L,T->Rating); lua_pushinteger(L,T->Games); lua_pushinteger(L,T->Wins); lua_pushinteger(L,T->SeasonGames); lua_pushinteger(L,T->SeasonWins);
    lua_pushinteger(L,Field(C,0x454+(Slot-1)*7+3)); lua_pushinteger(L,Field(C,0x454+(Slot-1)*7+4)); lua_pushinteger(L,T->Rank); lua_pushinteger(L,Field(C,0x454+(Slot-1)*7+6));
    const auto Color=[&](uint32 V) {lua_pushnumber(L,((V>>16)&255)/255.); lua_pushnumber(L,((V>>8)&255)/255.); lua_pushnumber(L,(V&255)/255.);};
    Color(M->Background); lua_pushinteger(L,M->Emblem); Color(M->EmblemColor); lua_pushinteger(L,M->Border); Color(M->BorderColor); return 22;
}
int Port(lua_State* L)
{auto* C=WowLuaApi::GetContext(L); const auto* Q=Queue(C,luaL_checkinteger(L,1)); if(Q && Q->Status && (!Bool(L,2) || Q->Status==2)) Send(C,0x2D5,FWire().Add(Q->Arena).Add(uint8(0)).Add(Q->Type).Add(uint16(0x1F90)).Add(uint8(Bool(L,2)))); return 0;}
}
void WowLuaApi::RegisterPvpApi(lua_State* L)
{
    const char* HonorNames[]={"GetPVPSessionStats","GetPVPYesterdayStats","GetPVPLifetimeStats","GetLifetimeHonorableKills","GetHonorCurrency","GetArenaCurrency"};
    for(int32 I=0;I<UE_ARRAY_COUNT(HonorNames);++I) {lua_pushinteger(L,I); lua_pushcclosure(L,Honor,1); lua_setglobal(L,HonorNames[I]);}
    Reg(L,"GetPVPRankProgress",[](lua_State* S)->int {lua_pushnumber(S,0); return 1;}); // Ranked honor progression was removed before 12340.
    Reg(L,"GetCurrentArenaSeason",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); lua_pushinteger(S,H && H->WorldStates.FindRef(3191)?H->WorldStates.FindRef(3901):0); return 1;});
    Reg(L,"GetPreviousArenaSeason",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const int32 Id=H?H->WorldStates.FindRef(3901):0; lua_pushinteger(S,FMath::Max(0,Id-(H && H->WorldStates.FindRef(3191)?1:0))); return 1;});
    Reg(L,"GetNumBattlegroundTypes",[](lua_State* S)->int {auto* C=GetContext(S); lua_pushinteger(S,C?Types(C).Num():0); return 1;});
    Reg(L,"GetBattlegroundInfo",[](lua_State* S)->int {auto* C=GetContext(S); return Info(S,C,ByIndex(C,luaL_optinteger(S,1,0)),false);});
    Reg(L,"GetBattlefieldInfo",[](lua_State* S)->int {auto* C=GetContext(S); return Info(S,C,Selected(C),true);});
    Reg(L,"SortBGList",[](lua_State* S)->int {auto* C=GetContext(S); if(C) Types(C).Sort([C](uint32 A,uint32 B){const auto* X=Data().Battlefield(A); const auto* Y=Data().Battlefield(B); const int32 PX=A==32?2:Holiday(C,*X)?1:0,PY=B==32?2:Holiday(C,*Y)?1:0; return PX!=PY?PX>PY:A<B;}); return 0;});
    Reg(L,"RequestBattlegroundInstanceInfo",[](lua_State* S)->int {auto* C=GetContext(S); ListRequest(C,ByIndex(C,luaL_optinteger(S,1,1)),1); return 0;});
    Reg(L,"ShowBattlefieldList",[](lua_State* S)->int {auto* C=GetContext(S); ListRequest(C,Data().Battlefield(luaL_checkinteger(S,1)),0); return 0;});
    Reg(L,"GetNumBattlefields",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); lua_pushinteger(S,H && C && H->Pvp.List.Type==View(C).Selected?H->Pvp.List.Instances.Num():0); return 1;});
    Reg(L,"GetBattlefieldInstanceInfo",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const int32 I=luaL_checkinteger(S,1)-1; if(!H || !H->Pvp.List.Instances.IsValidIndex(I)) return 0; lua_pushinteger(S,H->Pvp.List.Instances[I]); return 1;});
    Reg(L,"GetSelectedBattlefield",[](lua_State* S)->int {auto* C=GetContext(S); lua_pushinteger(S,C?View(C).Instance:0); return 1;});
    Reg(L,"SetSelectedBattlefield",[](lua_State* S)->int {auto* C=GetContext(S); if(C) View(C).Instance=FMath::Max(0,int32(luaL_checkinteger(S,1))); return 0;});
    Reg(L,"CanJoinBattlefieldAsGroup",[](lua_State* S)->int {auto* C=GetContext(S); const auto* B=Selected(C); Flag(S,B && B->Groups); return 1;});
    Reg(L,"IsBattlefieldArena",[](lua_State* S)->int {const auto* B=Selected(GetContext(S)); Flag(S,B && B->Type==4); return 1;});
    Reg(L,"JoinBattlefield",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const auto* B=Selected(C); if(!H || !B || !Eligible(C,*B)) return 0; const int32 I=luaL_optinteger(S,1,0)-1; if(B->Type==4) {if(I>=0 && I<3 && H->Pvp.List.Vendor) Send(C,0x358,FWire().Add(H->Pvp.List.Vendor).Add(uint8(I)).Add(uint8(Bool(S,2))).Add(uint8(Bool(S,3)))); return 0;} const uint32 Instance=I>=0 && H->Pvp.List.Type==B->Id && H->Pvp.List.Instances.IsValidIndex(I)?H->Pvp.List.Instances[I]:0; Send(C,0x2EE,FWire().Add(H->Pvp.List.Type==B->Id?H->Pvp.List.Vendor:uint64(0)).Add(B->Id).Add(Instance).Add(uint8(Bool(S,2)))); return 0;});
    Reg(L,"JoinArena",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const int32 Slot=luaL_checkinteger(S,1)-1; if(H && Slot>=0 && Slot<3) Send(C,0x358,FWire().Add(H->Pvp.List.Vendor).Add(uint8(Slot)).Add(uint8(Bool(S,2))).Add(uint8(Bool(S,3)))); return 0;});
    Reg(L,"GetBattlefieldStatus",[](lua_State* S)->int {const auto* Q=Queue(GetContext(S),luaL_checkinteger(S,1)); if(!Q || !Q->Status) {lua_pushstring(S,"none"); return 1;} lua_pushstring(S,Q->Status==1?"queued":Q->Status==2?"confirm":"active"); String(S,QueueName(Q)); lua_pushinteger(S,Q->Instance); lua_pushinteger(S,Q->Min); lua_pushinteger(S,Q->Max); lua_pushinteger(S,Q->Arena); Flag(S,Q->Rated); return 7;});
    Reg(L,"GetBattlefieldEstimatedWaitTime",[](lua_State* S)->int {const auto* Q=Queue(GetContext(S),luaL_checkinteger(S,1)); lua_pushinteger(S,Q?Q->Wait:0); return 1;});
    Reg(L,"GetBattlefieldTimeWaited",[](lua_State* S)->int {const auto* Q=Queue(GetContext(S),luaL_checkinteger(S,1)); lua_pushnumber(S,Q && Q->Status==1?double(Q->Elapsed)+Elapsed(Q):0); return 1;});
    Reg(L,"GetBattlefieldPortExpiration",[](lua_State* S)->int {const auto* Q=Queue(GetContext(S),luaL_checkinteger(S,1)); lua_pushnumber(S,Q && Q->Status==2?FMath::Max(0.,double(Q->Expiry)-Elapsed(Q))/1000.:0); return 1;});
    Reg(L,"GetBattlefieldInstanceExpiration",[](lua_State* S)->int {const auto* Q=Active(GetContext(S)); lua_pushnumber(S,Q && Q->Expiry?FMath::Max(0.,double(Q->Expiry)-Elapsed(Q)):0); return 1;});
    Reg(L,"GetBattlefieldInstanceRunTime",[](lua_State* S)->int {const auto* Q=Active(GetContext(S)); lua_pushnumber(S,Q?double(Q->Elapsed)+Elapsed(Q):0); return 1;});
    Reg(L,"IsActiveBattlefieldArena",[](lua_State* S)->int {auto* C=GetContext(S); Flag(S,Arena(C)); const auto* Q=Active(C); Flag(S,Q && Q->Rated); return 2;});
    Reg(L,"AcceptBattlefieldPort",Port);
    Reg(L,"LeaveBattlefield",[](lua_State* S)->int {auto* C=GetContext(S); const auto* Q=Active(C); if(Q) Send(C,0x2E1,FWire().Add(Q->Arena).Add(uint8(0)).Add(Q->Type).Add(uint16(0x1F90))); return 0;});
    Reg(L,"CloseBattlefield",[](lua_State*)->int {return 0;}); // Closing a list does not leave its queue.
    Reg(L,"GetRandomBGHonorCurrencyBonuses",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const auto R=H?H->Pvp.List.RandomReward:FWowPvpState::FRewards(); Flag(S,R.Won); lua_pushinteger(S,R.Honor); lua_pushinteger(S,R.Arena); lua_pushinteger(S,R.LossHonor); lua_pushinteger(S,0); return 5;});
    Reg(L,"GetHolidayBGHonorCurrencyBonuses",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const auto R=H?H->Pvp.List.Reward:FWowPvpState::FRewards(); Flag(S,R.Won); lua_pushinteger(S,R.Honor); lua_pushinteger(S,R.Arena); lua_pushinteger(S,R.LossHonor); lua_pushinteger(S,0); return 5;});
    Reg(L,"GetBattlefieldWinner",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); if(H && H->Pvp.Winner>=0) lua_pushinteger(S,H->Pvp.Winner); else lua_pushnil(S); return 1;});
    Reg(L,"RequestBattlefieldScoreData",[](lua_State* S)->int {auto* C=GetContext(S); if(C && FPlatformTime::Seconds()>=View(C).ScoreAt) {View(C).ScoreAt=FPlatformTime::Seconds()+1; Send(C,0x2E0);} return 0;});
    Reg(L,"SetBattlefieldScoreFaction",[](lua_State* S)->int {auto* C=GetContext(S); if(C) View(C).Faction=lua_isnumber(S,1)?FMath::Clamp(int32(lua_tointeger(S,1)),0,1):-1; return 0;});
    Reg(L,"GetNumBattlefieldScores",[](lua_State* S)->int {lua_pushinteger(S,Scores(GetContext(S)).Num()); return 1;});
    Reg(L,"GetBattlefieldScore",[](lua_State* S)->int {auto* C=GetContext(S); const auto Rows=Scores(C); const int32 I=luaL_checkinteger(S,1)-1; auto* H=Handler(C); if(!H || !Rows.IsValidIndex(I)) return 0; const auto& R=*Rows[I]; const auto* Name=H->PlayerNameCache.Find(R.Guid); const uint8 Cls=Class(C,R.Guid); if(!Name || !Cls) return 0; String(S,*Name); lua_pushinteger(S,R.Blows); lua_pushinteger(S,R.Kills); lua_pushinteger(S,R.Deaths); lua_pushinteger(S,R.Honor); lua_pushinteger(S,Faction(C,R)); lua_pushinteger(S,0); const auto* RaceInfo=FDbcStore::Get().ChrRaces().GetById(Race(C,R.Guid)); String(S,RaceInfo?RaceInfo->Name:FString()); String(S,LocalizedClass(S,Cls)); lua_pushstring(S,TCHAR_TO_UTF8(ClassFile(Cls))); lua_pushinteger(S,R.Damage); lua_pushinteger(S,R.Healing); return 12;});
    Reg(L,"GetBattlefieldTeamInfo",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const int32 I=luaL_checkinteger(S,1); if(!H || I<0 || I>1) return 0; const auto& T=H->Pvp.Teams[I]; String(S,T.Name); lua_pushinteger(S,T.Lost); lua_pushinteger(S,T.Won); lua_pushinteger(S,T.Skill); return 4;});
    Reg(L,"GetNumBattlefieldStats",[](lua_State* S)->int {lua_pushinteger(S,WorldRows(GetContext(S),true).Num()); return 1;});
    Reg(L,"GetBattlefieldStatInfo",[](lua_State* S)->int {const auto Rows=WorldRows(GetContext(S),true); const int32 I=luaL_checkinteger(S,1)-1; if(!Rows.IsValidIndex(I)) return 0; String(S,Rows[I]->Text); String(S,Rows[I]->Icon); String(S,Rows[I]->Tooltip); return 3;});
    Reg(L,"GetBattlefieldStatData",[](lua_State* S)->int {const auto Rows=Scores(GetContext(S)); const int32 I=luaL_checkinteger(S,1)-1,J=luaL_checkinteger(S,2)-1; if(!Rows.IsValidIndex(I) || !Rows[I]->Extra.IsValidIndex(J)) return 0; lua_pushinteger(S,Rows[I]->Extra[J]); return 1;});
    Reg(L,"RequestBattlefieldPositions",[](lua_State* S)->int {auto* C=GetContext(S); if(C && FPlatformTime::Seconds()>=View(C).PositionAt) {View(C).PositionAt=FPlatformTime::Seconds()+1; Send(C,0x2E9);} return 0;});
    Reg(L,"GetNumBattlefieldPositions",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); lua_pushinteger(S,H?H->Pvp.Positions.Num():0); return 1;});
    Reg(L,"GetNumBattlefieldFlagPositions",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); lua_pushinteger(S,H?H->Pvp.Flags.Num():0); return 1;});
    const auto Position=[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const bool Flags=lua_toboolean(S,lua_upvalueindex(1))!=0; const int32 I=luaL_checkinteger(S,1)-1; if(!H) return 0; const auto& Rows=Flags?H->Pvp.Flags:H->Pvp.Positions; if(!Rows.IsValidIndex(I)) return 0; const auto& R=Rows[I]; const auto XY=WowMapUi::Project(C,H->CurrentMapId,FVector(R.World.X,R.World.Y,0)); lua_pushnumber(S,XY.X); lua_pushnumber(S,XY.Y); if(Flags) {lua_pushstring(S,Alliance(Race(C,R.Guid))?"Alliance":"Horde"); return 3;} const auto* Name=H->PlayerNameCache.Find(R.Guid); String(S,Name?*Name:FString()); return 3;};
    lua_pushboolean(L,false); lua_pushcclosure(L,Position,1); lua_setglobal(L,"GetBattlefieldPosition"); lua_pushboolean(L,true); lua_pushcclosure(L,Position,1); lua_setglobal(L,"GetBattlefieldFlagPosition");
    Reg(L,"GetNumWorldStateUI",[](lua_State* S)->int {lua_pushinteger(S,WorldRows(GetContext(S),false).Num()); return 1;});
    Reg(L,"GetWorldStateUIInfo",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const auto Rows=WorldRows(C,false); const int32 I=luaL_checkinteger(S,1)-1; if(!H || !Rows.IsValidIndex(I)) return 0; const auto& R=*Rows[I]; lua_pushinteger(S,R.Type); lua_pushinteger(S,H->WorldStates.FindRef(R.Variable)); String(S,FormatState(C,R.Text)); String(S,FormatState(C,R.Icon)); String(S,FormatState(C,R.DynamicIcon)); String(S,FormatState(C,R.Tooltip)); String(S,FormatState(C,R.DynamicTooltip)); String(S,R.ExtendedUi); for(auto Id:R.Extended) lua_pushinteger(S,H->WorldStates.FindRef(Id)); return 11;});
    Reg(L,"GetArenaTeam",Team);
    Reg(L,"GetNumArenaTeams",[](lua_State* S)->int {auto* C=GetContext(S); int N=0; for(int I=1;I<=3;++I) N+=TeamId(C,I)!=0; lua_pushinteger(S,N); return 1;});
    Reg(L,"GetNumArenaOpponents",[](lua_State* S)->int {auto* C=GetContext(S); lua_pushinteger(S,C?C->ArenaOpponents.Num():0); return 1;});
    Reg(L,"IsInArenaTeam",[](lua_State* S)->int {auto* C=GetContext(S); bool In=false; for(int I=1;I<=3;++I) In|=TeamId(C,I)!=0; Flag(S,In); return 1;});
    Reg(L,"ArenaTeamRoster",[](lua_State* S)->int {auto* C=GetContext(S); const uint32 Id=TeamId(C,luaL_checkinteger(S,1)); if(Id) Send(C,0x34D,FWire().Add(Id)); return 0;});
    Reg(L,"GetNumArenaTeamMembers",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const uint32 Id=TeamId(C,luaL_checkinteger(S,1)); const auto* T=H?H->Pvp.ArenaTeams.Find(Id):nullptr; lua_pushinteger(S,T?T->Members.Num():0); return 1;});
    Reg(L,"GetArenaTeamRosterInfo",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const auto* T=H?H->Pvp.ArenaTeams.Find(TeamId(C,luaL_checkinteger(S,1))):nullptr; const int I=luaL_checkinteger(S,2)-1; if(!T || !T->Members.IsValidIndex(I)) return 0; const auto& M=T->Members[I]; String(S,M.Name); lua_pushinteger(S,M.Rank); lua_pushinteger(S,M.Level); String(S,LocalizedClass(S,M.Class)); Flag(S,M.Online); lua_pushinteger(S,M.Games); lua_pushinteger(S,M.Wins); lua_pushinteger(S,M.SeasonGames); lua_pushinteger(S,M.SeasonWins); lua_pushinteger(S,M.Rating); return 10;});
    Reg(L,"IsArenaTeamCaptain",[](lua_State* S)->int {auto* C=GetContext(S); const int I=luaL_checkinteger(S,1); Flag(S,TeamId(C,I) && !Field(C,0x454+(I-1)*7+2)); return 1;});
    Reg(L,"SetArenaTeamRosterSelection",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const int I=luaL_checkinteger(S,1)-1,J=luaL_checkinteger(S,2)-1; const auto* T=H?H->Pvp.ArenaTeams.Find(TeamId(C,I+1)):nullptr; if(C && I>=0 && I<3) View(C).RosterSelection[I]=T && T->Members.IsValidIndex(J)?T->Members[J].Guid:0; return 0;});
    Reg(L,"GetArenaTeamRosterSelection",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const int I=luaL_checkinteger(S,1)-1; int Found=0; const auto* T=H?H->Pvp.ArenaTeams.Find(TeamId(C,I+1)):nullptr; if(C && T && I>=0 && I<3) for(int J=0;J<T->Members.Num();++J) if(T->Members[J].Guid==View(C).RosterSelection[I]) Found=J+1; lua_pushinteger(S,Found); return 1;});
    const char* Control[]={"ArenaTeamInviteByName","ArenaTeamUninviteByName","ArenaTeamSetLeaderByName","ArenaTeamLeave","ArenaTeamDisband"}; const uint32 Ops[]={0x34F,0x354,0x356,0x353,0x355};
    for(int I=0;I<5;++I) {lua_pushinteger(L,Ops[I]); lua_pushcclosure(L,[](lua_State* S)->int {auto* C=GetContext(S); const uint32 Id=TeamId(C,luaL_checkinteger(S,1)),Op=lua_tointeger(S,lua_upvalueindex(1)); if(!Id) return 0; FWire W; W.Add(Id); if(Op<=0x356 && Op!=0x353 && Op!=0x355) {const FString Name=UTF8_TO_TCHAR(luaL_checkstring(S,2)); if(Name.IsEmpty() || FTCHARToUTF8(*Name).Length()>48) return 0; W.Text(Name);} Send(C,Op,W); return 0;},1); lua_setglobal(L,Control[I]);}
    Reg(L,"AcceptArenaTeam",[](lua_State* S)->int {auto* C=GetContext(S); auto* H=Handler(C); if(H && !H->Pvp.Invitation.IsEmpty()) {Send(C,0x351); H->Pvp.Invitation.Empty();} return 0;});
    Reg(L,"DeclineArenaTeam",[](lua_State* S)->int {auto* C=GetContext(S); auto* H=Handler(C); if(H && !H->Pvp.Invitation.IsEmpty()) {Send(C,0x352); H->Pvp.Invitation.Empty();} return 0;});
    Reg(L,"CloseArenaTeamRoster",[](lua_State*)->int {return 0;});
    Reg(L,"ReportPlayerIsPVPAFK",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const FString Name=UTF8_TO_TCHAR(luaL_checkstring(S,1)); if(H && Active(C)) for(const auto& Pair:H->PlayerNameCache) if(Pair.Value.Equals(Name,ESearchCase::IgnoreCase)) {Send(C,0x3E4,FWire().Add(Pair.Key)); break;} return 0;});
    Reg(L,"PlayerIsPVPInactive",[](lua_State* S)->int {const auto* P=Player(GetContext(S)); bool Inactive=false; if(P) for(const auto& A:P->Auras) Inactive|=A.bActive && (A.SpellId==43680 || A.SpellId==43681); Flag(S,Inactive); return 1;});
    Reg(L,"CanHearthAndResurrectFromArea",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const auto* P=Player(C); Flag(S,H && P && H->ServerZoneId==4197 && P->GetHealth()==0); return 1;});
    Reg(L,"HearthAndResurrectFromArea",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const auto* P=Player(C); if(H && P && H->ServerZoneId==4197 && !P->GetHealth()) Send(C,0x49C); return 0;});
    Reg(L,"GetWorldPVPQueueStatus",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const int32 Index=luaL_checkinteger(S,1)-1; TArray<uint32> Ids; if(H) H->Pvp.WorldQueues.GetKeys(Ids); Ids.Sort(); if(!Ids.IsValidIndex(Index)) {lua_pushstring(S,"none"); return 1;} const auto& Q=H->Pvp.WorldQueues[Ids[Index]]; lua_pushstring(S,Q.Status==2?"confirm":"queued"); const auto* A=FDbcStore::Get().AreaTable().GetById(Q.Zone); String(S,A?A->Name:FString()); lua_pushinteger(S,Q.Id); return 3;});
    const char* WorldControls[]={"BattlefieldMgrEntryInviteResponse","BattlefieldMgrQueueInviteResponse","BattlefieldMgrExitRequest","BattlefieldMgrQueueRequest"}; const uint32 WorldOps[]={0x4DF,0x4E2,0x4E7,0x4E3};
    for(int32 I=0;I<4;++I) {lua_pushinteger(L,WorldOps[I]); lua_pushcclosure(L,[](lua_State* S)->int {auto* C=GetContext(S); const uint32 Id=luaL_checkinteger(S,1),Op=lua_tointeger(S,lua_upvalueindex(1)); if(!Id) return 0; FWire W; W.Add(Id); if(Op==0x4DF || Op==0x4E2) W.Add(uint8(Bool(S,2))); Send(C,Op,W); return 0;},1); lua_setglobal(L,WorldControls[I]);}
    Reg(L,"GetWintergraspWaitTime",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const uint32 Time=H?H->WorldStates.FindRef(3781):0; if(!Time) return 0; lua_pushnumber(S,FMath::Max(0.,double(Time)-FDateTime::UtcNow().ToUnixTimestamp())); return 1;});
    Reg(L,"CanQueueForWintergrasp",[](lua_State* S)->int {auto* C=GetContext(S); const auto* P=Player(C); const auto* H=Handler(C); const uint32 Time=H?H->WorldStates.FindRef(3781):0; const double Wait=double(Time)-FDateTime::UtcNow().ToUnixTimestamp(); Flag(S,P && P->GetLevel()>=75 && Time && Wait<=900 && !H->WorldStates.FindRef(3801)); return 1;});
}
void WowLuaApi::UpdatePvpState(lua_State* L)
{
    auto* C=GetContext(L); auto* H=Handler(C); if(!H || !C->EventSystem) return; auto& V=View(C); auto& P=H->Pvp;
    const auto Event=[&](const TCHAR* Name) {C->EventSystem->FireEvent(Name);};
    const auto* LocalPlayer=Player(C); const uint32 Level=LocalPlayer?uint32(LocalPlayer->GetLevel()):0;
    if(Level!=V.Level) {V.Level=Level; Event(TEXT("PVPQUEUE_ANYWHERE_UPDATE_AVAILABLE"));}
    if(V.Revision!=P.Revision) {V.Revision=P.Revision; Event(TEXT("UPDATE_BATTLEFIELD_STATUS"));}
    if(V.List!=P.ListRevision) {V.List=P.ListRevision; if(!V.Selected || !P.List.Source) {V.Selected=P.List.Type; V.Instance=0;} if(P.List.Type==V.Selected) Event(P.List.Source?TEXT("PVPQUEUE_ANYWHERE_SHOW"):TEXT("BATTLEFIELDS_SHOW"));}
    for(const auto& E:P.WorldEvents) if(E.Sequence>V.WorldEvent)
    {
        V.WorldEvent=E.Sequence; const TCHAR* Names[]={TEXT("BATTLEFIELD_MGR_ENTRY_INVITE"),TEXT("BATTLEFIELD_MGR_QUEUE_INVITE"),TEXT("BATTLEFIELD_MGR_QUEUE_REQUEST_RESPONSE"),TEXT("BATTLEFIELD_MGR_ENTERED"),TEXT("BATTLEFIELD_MGR_EJECTED")};
        C->EventSystem->FireEventTyped(Names[E.Kind],[&E](lua_State* S) {lua_pushinteger(S,E.Id); if(E.Kind==0) return 1; if(E.Kind==1 || E.Kind==3) {Flag(S,E.Args[0]!=0); return 2;} if(E.Kind==2) {for(int32 I=0;I<4;++I) Flag(S,E.Args[I]!=0); return 5;} Flag(S,E.Args[0]==0); Flag(S,E.Args[1]!=0); Flag(S,E.Args[2]==2); Flag(S,E.Args[0]==4); return 5;});
    }
    if(C->ConnectionManager->GetState()==EWowSessionState::WorldInGame && FPlatformTime::Seconds()>=V.StatusAt) {V.StatusAt=FPlatformTime::Seconds()+30; Send(C,0x2D3);}
    const auto Name=[&](uint64 Guid) {if(!Guid || H->PlayerNameCache.Contains(Guid)) return; double& At=V.NameQueries.FindOrAdd(Guid); if(FPlatformTime::Seconds()>=At) {At=FPlatformTime::Seconds()+10; Send(C,0x50,FWire().Add(Guid));}};
    for(const auto& S:P.Scores) Name(S.Guid); for(const auto& S:P.Positions) Name(S.Guid); for(const auto& S:P.Flags) Name(S.Guid);
    if(V.Scores!=P.ScoreRevision || V.IdentityCount!=uint32(H->PlayerIdentities.Num())) {V.Scores=P.ScoreRevision; V.IdentityCount=H->PlayerIdentities.Num(); Event(TEXT("UPDATE_BATTLEFIELD_SCORE"));}
    if(V.Positions!=P.PositionRevision) {V.Positions=P.PositionRevision; Event(TEXT("UPDATE_BATTLEFIELD_POSITIONS"));}
    if(V.Arena!=P.ArenaRevision) {V.Arena=P.ArenaRevision; Event(TEXT("ARENA_TEAM_UPDATE")); Event(TEXT("ARENA_TEAM_ROSTER_UPDATE"));}
    if(V.ArenaResult!=P.ArenaResultRevision)
    {
        V.ArenaResult=P.ArenaResultRevision;
        if(P.ArenaError)
        {
            const TMap<uint32,const char*> Errors={{1,"ERR_ARENA_TEAM_INTERNAL"},{2,"ERR_ALREADY_IN_ARENA_TEAM"},{3,"ERR_ALREADY_IN_ARENA_TEAM_S"},{4,"ERR_INVITED_TO_ARENA_TEAM"},{5,"ERR_ALREADY_INVITED_TO_ARENA_TEAM_S"},{6,"ERR_ARENA_TEAM_NAME_INVALID"},{7,"ERR_ARENA_TEAM_NAME_EXISTS_S"},{8,"ERR_ARENA_TEAM_PERMISSIONS"},{9,"ERR_ARENA_TEAM_PLAYER_NOT_IN_TEAM"},{10,"ERR_ARENA_TEAM_PLAYER_NOT_IN_TEAM_SS"},{11,"ERR_ARENA_TEAM_PLAYER_NOT_FOUND_S"},{12,"ERR_ARENA_TEAM_NOT_ALLIED"},{19,"ERR_ARENA_TEAM_IGNORING_YOU_S"},{21,"ERR_ARENA_TEAM_TARGET_TOO_LOW_S"},{22,"ERR_ARENA_TEAM_TARGET_TOO_HIGH_S"},{23,"ERR_ARENA_TEAM_TOO_MANY_MEMBERS_S"},{27,"ERR_ARENA_TEAM_NOT_FOUND"},{30,"ERR_ARENA_TEAMS_LOCKED"}};
            const char* const* Key=Errors.Find(P.ArenaError); lua_getglobal(L,Key?*Key:"ERR_ARENA_TEAM_INTERNAL"); FString Message=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):TEXT(""); lua_pop(L,1); Message=Message.Replace(TEXT("%s"),*P.ArenaResultPlayer); C->EventSystem->FireEvent(TEXT("UI_ERROR_MESSAGE"),{Message});
        }
    }
    if(V.Invite!=P.InviteRevision) {V.Invite=P.InviteRevision; if(!P.Invitation.IsEmpty()) C->EventSystem->FireEvent(TEXT("ARENA_TEAM_INVITE_REQUEST"),{P.Inviter,P.Invitation});}
    if(V.Result!=P.ResultRevision) {V.Result=P.ResultRevision; if(P.Result<0) {lua_getglobal(L,"ERR_BATTLEGROUND_JOIN_FAILED"); const FString Text=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):TEXT(""); lua_pop(L,1); C->EventSystem->FireEvent(TEXT("UI_ERROR_MESSAGE"),{Text});}}
    uint32 Hash=0; for(uint32 Offset:{0x435u,0x436u,0x437u,0x438u,0x469u,0x46Au}) Hash=HashCombine(Hash,Field(C,Offset));
    for(int I=1;I<=3;++I) {const uint32 Id=TeamId(C,I); for(int J=0;J<7;++J) Hash=HashCombine(Hash,Field(C,0x454+(I-1)*7+J)); if(Id && (!H->ArenaIdentities.Contains(Id) || !P.ArenaTeams.Contains(Id))) QueryTeam(C,Id,true);}
    if(Hash!=V.HonorHash) {V.HonorHash=Hash; Event(TEXT("PLAYER_PVP_KILLS_CHANGED")); Event(TEXT("HONOR_CURRENCY_UPDATE")); Event(TEXT("ARENA_TEAM_UPDATE"));}
    if(V.World!=H->WorldStateRevision) {V.World=H->WorldStateRevision; Event(TEXT("UPDATE_WORLD_STATES")); Event(TEXT("ARENA_SEASON_WORLD_STATE"));}
    const bool InArena=Arena(C);
    if(!InArena) {if(V.InArena) {C->ArenaOpponents.Reset(); V.VisibleOpponents.Reset();} V.InArena=false;}
    else
    {
        if(!V.InArena) {C->ArenaOpponents.Reset(); V.VisibleOpponents.Reset(); V.InArena=true; lua_getglobal(L,"LoadAddOn"); lua_pushstring(L,"Blizzard_ArenaUI"); lua_call(L,1,1); lua_pop(L,1);}
        TSet<uint64> Visible; const auto* Local=Player(C);
        for(const auto& Pair:H->EntityManager.GetAll()) if(Pair.Value->IsPlayer() && Local && Pair.Key!=Local->Guid && CanUnitAttack(C,Local,Pair.Value.Get()))
        {
            int32 Index=C->ArenaOpponents.Find(Pair.Key); if(Index==INDEX_NONE && C->ArenaOpponents.Num()<5) Index=C->ArenaOpponents.Add(Pair.Key);
            if(Index!=INDEX_NONE) {Visible.Add(Pair.Key); if(!V.VisibleOpponents.Contains(Pair.Key)) C->EventSystem->FireEvent(TEXT("ARENA_OPPONENT_UPDATE"),{FString::Printf(TEXT("arena%d"),Index+1),TEXT("seen")});}
        }
        for(const auto Guid:V.VisibleOpponents) if(!Visible.Contains(Guid)) C->EventSystem->FireEvent(TEXT("ARENA_OPPONENT_UPDATE"),{FString::Printf(TEXT("arena%d"),C->ArenaOpponents.Find(Guid)+1),TEXT("unseen")});
        V.VisibleOpponents=MoveTemp(Visible);
    }
}
#else
void WowLuaApi::RegisterPvpApi(lua_State*) {}
void WowLuaApi::UpdatePvpState(lua_State*) {}
#endif
