#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"

struct FWowLfgUiView
{
    uint8 Roles=8; FString Comment;
    TSet<uint32> Selected,Enabled; TSet<int32> Collapsed;
    uint32 Info=0,Revision=0,Queue=0,Result=0,Level=0,Group=0,Items=0;
    double InfoAt=0,StatusAt=0;
    uint32 Role=0,Chosen=0,Proposal=0,ProposalId=0,Boot=0,Continue=0,Teleport=0,Reward=0;
    bool RoleShown=false;
};
#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
using FDungeon=FLfgUiDbc::FDungeon;
FWowLfgUiView& View(FWowLuaContext* C) {if(!C->LfgUi) C->LfgUi=MakeShared<FWowLfgUiView>(); return *C->LfgUi;}
FWowPacketHandler* Handler(FWowLuaContext* C) {return C && C->ConnectionManager?&C->ConnectionManager->PacketHandler:nullptr;}
const FWowPlayerEntity* Player(FWowLuaContext* C) {return C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;}
const FLfgUiDbc& Data() {return FDbcStore::Get().LfgUi();}
void Reg(lua_State* L,const char* Name,lua_CFunction F) {lua_pushcfunction(L,F); lua_setglobal(L,Name);}
void String(lua_State* L,const FString& S) {lua_pushstring(L,TCHAR_TO_UTF8(*S));}
bool Bool(lua_State* L,int32 I) {return lua_isnumber(L,I)?lua_tonumber(L,I)!=0:lua_toboolean(L,I)!=0;}
void Flag(lua_State* L,bool B) {lua_pushboolean(L,B);}
struct FWire {TArray<uint8> Bytes; template<typename T> FWire& Add(T V) {Bytes.Append(reinterpret_cast<const uint8*>(&V),sizeof(V)); return *this;} FWire& Text(const FString& S) {FTCHARToUTF8 U(*S); Bytes.Append(reinterpret_cast<const uint8*>(U.Get()),U.Length()+1); return *this;}};
bool Send(FWowLuaContext* C,uint32 Op,const FWire& W=FWire()) {if(!C || !C->ConnectionManager || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame) return false; C->ConnectionManager->SendRawPacket(Op,W.Bytes); return true;}
const FWowLfgState::FSelection* Selection(FWowLuaContext* C) {const auto* H=Handler(C); return H?(H->GroupInfo.IsEmpty()?&H->Lfg.Player:&H->Lfg.Party):nullptr;}
FString Mode(FWowLuaContext* C)
{
    const auto* H=Handler(C); if(H && H->Lfg.RoleCheck.State==2) return TEXT("rolecheck");
    if(H && H->Lfg.Proposal.Valid && H->Lfg.Proposal.State==0) return TEXT("proposal");
    const auto* S=Selection(C); if(!S) return FString();
    if(S->Update==3) return TEXT("listed"); if(S->Update==13) return TEXT("proposal");
    if(S->Queued) return TEXT("queued"); if(S->Has && S->Update==5 && !S->Joined) return TEXT("rolecheck"); if(H && H->GroupInfo.IsLfgGroup()) return TEXT("lfgparty"); return FString();
}
uint8 Available(FWowLuaContext* C)
{
    const auto* P=Player(C); if(!P || !P->GetClassId()) return 0; const uint8 Class=P->GetClassId();
    return 8|((Class==1 || Class==2 || Class==6 || Class==11)?2:0)|((Class==2 || Class==5 || Class==7 || Class==11)?4:0);
}
const FWowLfgState::FProposalMember* SelfProposal(FWowLuaContext* C)
{const auto* H=Handler(C); if(H) for(const auto& M:H->Lfg.Proposal.Members) if(M.Self) return &M; return nullptr;}
const char* RoleName(uint32 Roles) {return Roles&2?"TANK":Roles&4?"HEALER":"DAMAGER";}
TArray<const FLfgUiDbc::FEncounter*> Encounters(uint32 Id)
{
    TArray<const FLfgUiDbc::FEncounter*> Out; if(const auto* D=Data().Dungeon(Id)) for(const auto& E:Data().Encounters()) if(E.Map==D->Map && E.Difficulty==D->Difficulty) Out.Add(&E);
    Out.Sort([](const auto& A,const auto& B){return A.Order!=B.Order?A.Order<B.Order:A.Id<B.Id;}); return Out;
}
FString PlayerName(FWowLuaContext* C,uint64 Guid)
{
    const auto* H=Handler(C); const auto* P=Player(C); if(!H) return FString();
    if(P && P->Guid==Guid) return C->ConnectionManager->GetCharacterName();
    if(const auto* M=H->GroupInfo.FindMember(Guid)) return M->Name;
    return H->PlayerNameCache.FindRef(Guid);
}
uint32 LocalLock(FWowLuaContext* C,uint32 Id)
{
    const auto* H=Handler(C); const auto* D=Data().Dungeon(Id); const auto* P=Player(C); if(!D || !P || !H) return 1;
    if(H->Lfg.Disabled) return 1; if(const auto* Reason=H->Lfg.Locks.Find(D->Id)) return *Reason;
    if(P->GetLevel()<int32(D->Min)) return 2; if(D->Max && P->GetLevel()>int32(D->Max)) return 3;
    return D->Expansion>2?1:0;
}
uint32 AnyLock(FWowLuaContext* C,uint32 Id)
{
    if(const auto Reason=LocalLock(C,Id)) return Reason;
    const auto* H=Handler(C); if(!H || !H->Lfg.InfoReady) return 1;
    for(const auto& Member:H->GroupInfo.Members) {const auto* Locks=H->Lfg.PartyLocks.Find(Member.Guid); if(!Locks) return 1; if(const auto* R=Locks->Find(Id)) return *R;} return 0;
}
int Table(lua_State* L)
{
    if(lua_istable(L,1)) {lua_pushvalue(L,1); const int T=lua_gettop(L); lua_pushnil(L); while(lua_next(L,T)) {lua_pop(L,1); lua_pushvalue(L,-1); lua_pushnil(L); lua_rawset(L,T);} }
    else lua_newtable(L); return lua_gettop(L);
}
int DungeonInfo(lua_State* L,const FDungeon* D,const FLfgUiDbc::FGroup* G=nullptr)
{
    if(!D && !G) return 0;
    String(L,D?D->Name:G->Name); lua_pushinteger(L,D?D->Type:G->Type);
    for(uint32 V:{D?D->Min:0,D?D->Max:0,D?D->Recommended:0,D?D->MinRecommended:0,D?D->MaxRecommended:0,D?D->Expansion:0}) lua_pushinteger(L,V);
    lua_pushinteger(L,D?-int32(D->Group):0); String(L,D?D->Texture:FString()); lua_pushinteger(L,D?D->Difficulty:0);
    const auto* Map=D?FDbcStore::Get().Maps().GetById(D->Map):nullptr; lua_pushinteger(L,Map?Map->MaxPlayers:0);
    String(L,D?D->Description:FString()); Flag(L,D && D->Group==11); return 14;
}
TArray<uint32> Randoms(FWowLuaContext* C)
{
    TArray<uint32> Out; const auto* H=Handler(C); if(!H) return Out;
    for(const auto& Pair:H->Lfg.Rewards) if(Data().Dungeon(Pair.Key)) Out.Add(Pair.Key);
    Out.Sort([](uint32 A,uint32 B){const auto* X=Data().Dungeon(A); const auto* Y=Data().Dungeon(B); return X->Recommended!=Y->Recommended?X->Recommended<Y->Recommended:A<B;}); return Out;
}
double Aura(FWowLuaContext* C,const FWowEntity* E,uint32 Id)
{if(!C || !E) return 0; const double Now=FPlatformTime::Seconds(); for(const auto& A:E->Auras) if(A.bActive && A.SpellId==Id && A.RemainingMs && A.UpdatedAt+A.RemainingMs/1000.>Now) return A.UpdatedAt+A.RemainingMs/1000.; return 0;}
int ChoiceTable(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const int32 Kind=lua_tointeger(L,lua_upvalueindex(1)); const int T=Table(L); if(!C) return 1; auto& V=View(C);
    if(Kind==0)
    {
        for(const auto& Pair:Data().Dungeons()) {const int R=lua_gettop(L); const int N=DungeonInfo(L,&Pair.Value); lua_createtable(L,N,0); for(int I=0;I<N;++I) {lua_pushvalue(L,R+1+I); lua_rawseti(L,-2,I+1);} lua_rawseti(L,T,Pair.Key); lua_settop(L,R);}
        for(const auto& Pair:Data().Groups()) {const int R=lua_gettop(L); const int N=DungeonInfo(L,nullptr,&Pair.Value); lua_createtable(L,N,0); for(int I=0;I<N;++I) {lua_pushvalue(L,R+1+I); lua_rawseti(L,-2,I+1);} lua_rawseti(L,T,-int32(Pair.Key)); lua_settop(L,R);}
    }
    else for(const auto& Pair:Data().Dungeons())
    {
        if(Kind==1) {if(!V.Collapsed.Contains(-int32(Pair.Value.Group))) continue; Flag(L,true);}
        else if(Kind==2) Flag(L,V.Enabled.Contains(Pair.Key));
        else {const uint32 Reason=AnyLock(C,Pair.Key); if(!Reason) continue; lua_pushinteger(L,Reason);}
        lua_rawseti(L,T,Pair.Key);
    }
    if(Kind==1) for(const int32 Id:V.Collapsed) {Flag(L,true); lua_rawseti(L,T,Id);}
    return 1;
}
int ChoiceOrder(lua_State* L)
{
    const bool Raid=lua_toboolean(L,lua_upvalueindex(1))!=0; const int T=Table(L); TArray<const FLfgUiDbc::FGroup*> Groups;
    for(const auto& Pair:Data().Groups()) Groups.Add(&Pair.Value);
    Groups.Sort([](const auto& A,const auto& B){return A.Order<B.Order;}); int Index=0;
    for(const auto* G:Groups)
    {
        TArray<const FDungeon*> Rows;
        for(const auto& Pair:Data().Dungeons()) if(Pair.Value.Group==G->Id && (Raid?Pair.Value.Type==2:Pair.Value.Type==1 || Pair.Value.Type==5)) Rows.Add(&Pair.Value);
        if(Rows.IsEmpty()) continue; Rows.Sort([](const auto& A,const auto& B){return A.Order!=B.Order?A.Order<B.Order:A.Name<B.Name;});
        lua_pushinteger(L,-int32(G->Id)); lua_rawseti(L,T,++Index); for(const auto* D:Rows) {lua_pushinteger(L,D->Id); lua_rawseti(L,T,++Index);}
    } return 1;
}
}
void WowLuaApi::RegisterLfgApi(lua_State* L)
{
    const char* Tables[]={"GetLFDChoiceInfo","GetLFDChoiceCollapseState","GetLFDChoiceEnabledState","GetLFDChoiceLockedState"};
    for(int I=0;I<4;++I) {lua_pushinteger(L,I); lua_pushcclosure(L,ChoiceTable,1); lua_setglobal(L,Tables[I]);}
    for(int I=0;I<2;++I) {lua_pushboolean(L,I!=0); lua_pushcclosure(L,ChoiceOrder,1); lua_setglobal(L,I?"GetLFRChoiceOrder":"GetLFDChoiceOrder");}
    Reg(L,"GetLFGDungeonInfo",[](lua_State* S)->int {return DungeonInfo(S,Data().Dungeon(luaL_checkinteger(S,1)));});
    Reg(L,"GetNumRandomDungeons",[](lua_State* S)->int {lua_pushinteger(S,Randoms(GetContext(S)).Num()); return 1;});
    Reg(L,"GetLFGRandomDungeonInfo",[](lua_State* S)->int {const auto Rows=Randoms(GetContext(S)); const int I=luaL_checkinteger(S,1)-1; if(!Rows.IsValidIndex(I)) return 0; lua_pushinteger(S,Rows[I]); String(S,Data().Dungeon(Rows[I])->Name); return 2;});
    Reg(L,"GetRandomDungeonBestChoice",[](lua_State* S)->int {auto* C=GetContext(S); auto Rows=Randoms(C); for(int I=Rows.Num()-1;I>=0;--I) if(!AnyLock(C,Rows[I])) {lua_pushinteger(S,Rows[I]); return 1;} lua_pushnil(S); return 1;});
    Reg(L,"IsLFGDungeonJoinable",[](lua_State* S)->int {Flag(S,!AnyLock(GetContext(S),luaL_checkinteger(S,1))); return 1;});
    Reg(L,"SetLFGDungeonEnabled",[](lua_State* S)->int {auto* C=GetContext(S); const uint32 Id=luaL_checkinteger(S,1); if(C && Data().Dungeon(Id)) {if(Bool(S,2)) View(C).Enabled.Add(Id); else View(C).Enabled.Remove(Id);} return 0;});
    Reg(L,"SetLFGHeaderCollapsed",[](lua_State* S)->int {auto* C=GetContext(S); const int32 Id=luaL_checkinteger(S,1); if(C && Id<0 && Data().Group(-Id)) {if(Bool(S,2)) View(C).Collapsed.Add(Id); else View(C).Collapsed.Remove(Id);} return 0;});
    Reg(L,"ClearAllLFGDungeons",[](lua_State* S)->int {if(auto* C=GetContext(S)) View(C).Selected.Reset(); return 0;});
    Reg(L,"SetLFGDungeon",[](lua_State* S)->int {auto* C=GetContext(S); const uint32 Id=luaL_checkinteger(S,1); if(C && Data().Dungeon(Id) && View(C).Selected.Num()<50) View(C).Selected.Add(Id); return 0;});
    Reg(L,"GetLFGQueuedList",[](lua_State* S)->int {auto* C=GetContext(S); const int T=Table(S); if(const auto* Selected=Selection(C)) for(uint32 Id:Selected->Dungeons) {Flag(S,true); lua_rawseti(S,T,Id);} return 1;});
    Reg(L,"GetLFGMode",[](lua_State* S)->int {auto* C=GetContext(S); const FString M=Mode(C); if(M.IsEmpty()) lua_pushnil(S); else String(S,M); const auto* H=Handler(C); const auto* P=Player(C); if(M==TEXT("proposal")) {const auto* Self=SelfProposal(C); lua_pushstring(S,Self && Self->Answered?"accepted":"unaccepted");} else if((M==TEXT("queued") || M==TEXT("listed")) && H && P && !H->GroupInfo.IsEmpty() && H->GroupInfo.LeaderGuid!=P->Guid) lua_pushstring(S,"unempowered"); else lua_pushnil(S); return 2;});
    Reg(L,"IsListedInLFR",[](lua_State* S)->int {Flag(S,Mode(GetContext(S))==TEXT("listed")); return 1;});
    Reg(L,"RequestLFDPlayerLockInfo",[](lua_State* S)->int {Send(GetContext(S),0x36E); return 0;});
    Reg(L,"RequestLFDPartyLockInfo",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); if(H && !H->GroupInfo.IsEmpty()) Send(C,0x371); return 0;});
    Reg(L,"CanPartyLFGBackfill",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const auto* P=Player(C); Flag(S,H && P && H->GroupInfo.IsLfgGroup() && H->GroupInfo.LfgDungeon && H->GroupInfo.LfgState!=2 && H->GroupInfo.MemberCount<5 && H->GroupInfo.LeaderGuid==P->Guid); return 1;});
    Reg(L,"GetPartyLFGBackfillInfo",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const auto* D=H && H->GroupInfo.IsLfgGroup()?Data().Dungeon(H->GroupInfo.LfgDungeon):nullptr; if(!D) return 0; String(S,D->Name); lua_pushinteger(S,D->Id); lua_pushinteger(S,D->Type); return 3;});
    Reg(L,"GetAvailableRoles",[](lua_State* S)->int {const uint8 Roles=Available(GetContext(S)); for(uint8 Bit:{2,4,8}) Flag(S,(Roles&Bit)!=0); return 3;});
    Reg(L,"GetLFGRoles",[](lua_State* S)->int {auto* C=GetContext(S); const uint8 Roles=C?View(C).Roles:0; for(uint8 Bit:{1,2,4,8}) Flag(S,(Roles&Bit)!=0); return 4;});
    Reg(L,"SetLFGRoles",[](lua_State* S)->int {auto* C=GetContext(S); if(!C) return 0; uint8 Roles=0; for(int I=1;I<=4;++I) if(Bool(S,I)) Roles|=1<<(I-1); Roles&=Available(C)|1; if(View(C).Roles!=Roles) {View(C).Roles=Roles; if(C->EventSystem) C->EventSystem->FireEvent(TEXT("LFG_ROLE_UPDATE"));} return 0;});
    Reg(L,"SetLFGComment",[](lua_State* S)->int {auto* C=GetContext(S); if(!C) return 0; FString Text=UTF8_TO_TCHAR(luaL_checkstring(S,1)); while(FTCHARToUTF8(*Text).Length()>255) Text.LeftChopInline(1); if(View(C).Comment!=Text) {View(C).Comment=Text; Send(C,0x366,FWire().Text(Text));} return 0;});
    Reg(L,"GetLFGInfoServer",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const auto* Selected=Selection(C); Flag(S,H && !H->GroupInfo.IsEmpty()); Flag(S,Selected && Selected->Joined); Flag(S,Selected && Selected->Queued); Flag(S,false); Flag(S,false); String(S,Selected?Selected->Comment:C?View(C).Comment:FString()); lua_pushinteger(S,Selected?Selected->Dungeons.Num():0); return 7;});
    Reg(L,"JoinLFG",[](lua_State* S)->int {auto* C=GetContext(S); auto* H=Handler(C); if(!H || H->Lfg.Disabled || View(C).Selected.IsEmpty() || !(View(C).Roles&14)) return 0; TArray<uint32> Ids=View(C).Selected.Array(); Ids.Sort(); FWire W; W.Add(uint32(View(C).Roles)).Add(uint8(0)).Add(uint8(0)).Add(uint8(Ids.Num())); for(uint32 Id:Ids) {const auto* D=Data().Dungeon(Id); if(!D || AnyLock(C,Id)) return 0; W.Add(D->Entry());} W.Add(uint8(3)).Add(uint8(0)).Add(uint8(0)).Add(uint8(0)).Text(View(C).Comment); Send(C,0x35C,W); return 0;});
    Reg(L,"LeaveLFG",[](lua_State* S)->int {Send(GetContext(S),0x35D); return 0;});
    Reg(L,"GetLFGRoleUpdate",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); Flag(S,H && H->Lfg.RoleCheck.State==2); lua_pushinteger(S,H?H->Lfg.RoleCheck.Dungeons.Num():0); lua_pushinteger(S,H?H->Lfg.RoleCheck.Members.Num():0); return 3;});
    Reg(L,"GetLFGRoleUpdateSlot",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const int I=luaL_checkinteger(S,1)-1; const auto* D=H && H->Lfg.RoleCheck.Dungeons.IsValidIndex(I)?Data().Dungeon(H->Lfg.RoleCheck.Dungeons[I]):nullptr; if(!D) return 0; lua_pushinteger(S,D->Type); lua_pushinteger(S,D->Id); return 2;});
    Reg(L,"CompleteLFGRoleCheck",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const bool Accept=Bool(S,1); Flag(S,H && H->Lfg.RoleCheck.State==2 && (!Accept || (View(C).Roles&14)) && Send(C,0x36A,FWire().Add(uint8(Accept?View(C).Roles:0)))); return 1;});
    Reg(L,"GetLFGProposal",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* H=Handler(C); const auto* M=SelfProposal(C); const auto* D=H && H->Lfg.Proposal.Valid?Data().Dungeon(H->Lfg.Proposal.Dungeon):nullptr;
        if(!D || !M || H->Lfg.Proposal.State==2 || (H->Lfg.Proposal.State==1 && FPlatformTime::Seconds()-H->Lfg.Proposal.At>5)) {Flag(S,false); return 1;} const auto& P=H->Lfg.Proposal; const auto Rows=Encounters(D->Id); int32 Done=0; for(uint32 Bits=P.Encounters;Bits;Bits&=Bits-1) ++Done;
        Flag(S,true); lua_pushinteger(S,D->Type); lua_pushinteger(S,D->Id); String(S,D->Name); String(S,D->Texture); lua_pushstring(S,RoleName(M->Roles)); Flag(S,M->Answered); lua_pushinteger(S,Rows.Num()); lua_pushinteger(S,Done); lua_pushinteger(S,P.Members.Num()); Flag(S,(M->Roles&1)!=0); Flag(S,D->Group==11); return 12;
    });
    Reg(L,"GetLFGProposalMember",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const int I=luaL_checkinteger(S,1)-1; if(!H || !H->Lfg.Proposal.Valid || !H->Lfg.Proposal.Members.IsValidIndex(I)) return 0; const auto& M=H->Lfg.Proposal.Members[I]; Flag(S,(M.Roles&1)!=0); lua_pushstring(S,RoleName(M.Roles)); lua_pushinteger(S,M.Self && Player(C)?Player(C)->GetLevel():0); Flag(S,M.Answered); Flag(S,M.Accepted); if(M.Self) String(S,C->ConnectionManager->GetCharacterName()); else lua_pushnil(S); lua_pushnil(S); return 7;});
    Reg(L,"GetLFGProposalEncounter",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const int I=luaL_checkinteger(S,1)-1; if(!H || !H->Lfg.Proposal.Valid) return 0; const auto Rows=Encounters(H->Lfg.Proposal.Dungeon); if(!Rows.IsValidIndex(I)) return 0; const auto& E=*Rows[I]; const auto* Icon=FDbcStore::Get().SpellIcons().GetById(E.Icon); String(S,E.Name); if(Icon) String(S,Icon->TexturePath); else lua_pushnil(S); Flag(S,E.Index<32 && (H->Lfg.Proposal.Encounters&(1u<<E.Index))); return 3;});
    for(int I=0;I<2;++I) {lua_pushboolean(L,I==0); lua_pushcclosure(L,[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const auto* M=SelfProposal(C); if(H && H->Lfg.Proposal.Valid && H->Lfg.Proposal.State==0 && M && !M->Answered) Send(C,0x362,FWire().Add(H->Lfg.Proposal.Id).Add(uint8(lua_toboolean(S,lua_upvalueindex(1))!=0))); return 0;},1); lua_setglobal(L,I?"RejectProposal":"AcceptProposal");}
    Reg(L,"GetLFGBootProposal",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); if(!H) {Flag(S,false); return 1;} const auto& B=H->Lfg.Boot; const FString Name=PlayerName(C,B.Victim); Flag(S,B.Progress && B.Deadline>FPlatformTime::Seconds()); Flag(S,B.Voted); Flag(S,B.Agree); if(Name.IsEmpty()) lua_pushnil(S); else String(S,Name); lua_pushinteger(S,B.Votes); lua_pushinteger(S,B.Agrees); lua_pushnumber(S,FMath::Max(0.,B.Deadline-FPlatformTime::Seconds())); String(S,B.Reason); return 8;});
    Reg(L,"VoteInBootProposal",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); if(H && H->Lfg.Boot.Progress && !H->Lfg.Boot.Voted && H->Lfg.Boot.Deadline>FPlatformTime::Seconds()) Send(C,0x36C,FWire().Add(uint8(Bool(S,1)))); return 0;});
    Reg(L,"IsInLFGDungeon",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const auto* D=H && H->GroupInfo.IsLfgGroup()?Data().Dungeon(H->GroupInfo.LfgDungeon):nullptr; Flag(S,D && D->Map==H->CurrentMapId); return 1;});
    Reg(L,"HasLFGRestrictions",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); Flag(S,H && H->GroupInfo.IsLfgGroup() && (H->GroupInfo.RawGroupType&WowGroupType::LFG_RESTRICTED)); return 1;});
    Reg(L,"LFGTeleport",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); if(H && H->GroupInfo.IsLfgGroup()) Send(C,0x370,FWire().Add(uint8(Bool(S,1)))); return 0;});
    Reg(L,"PartyLFGStartBackfill",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const auto* P=Player(C); const auto* D=H?Data().Dungeon(H->Lfg.ContinueDungeon?H->Lfg.ContinueDungeon:H->GroupInfo.LfgDungeon):nullptr; if(!P || !D || !H->GroupInfo.IsLfgGroup() || H->GroupInfo.LeaderGuid!=P->Guid || H->GroupInfo.MemberCount>=5 || H->GroupInfo.LfgState==2) return 0; Send(C,0x35C,FWire().Add(uint32(View(C).Roles)).Add(uint8(0)).Add(uint8(0)).Add(uint8(1)).Add(D->Entry()).Add(uint8(3)).Add(uint8(0)).Add(uint8(0)).Add(uint8(0)).Text(View(C).Comment)); return 0;});
    Reg(L,"GetLFGCompletionReward",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const auto* D=H?Data().Dungeon(H->Lfg.RewardSpecific):nullptr; if(!D) return 0; const auto& R=H->Lfg.FinalReward; String(S,D->Name); lua_pushinteger(S,D->Type); String(S,D->Texture); for(uint32 V:{R.Money,R.MoneyVariance,R.Experience,R.ExperienceVariance,0u,uint32(R.Items.Num())}) lua_pushinteger(S,V); return 9;});
    Reg(L,"GetLFGCompletionRewardItem",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const int I=luaL_checkinteger(S,1)-1; if(!H || !H->Lfg.FinalReward.Items.IsValidIndex(I)) return 0; const auto& R=H->Lfg.FinalReward.Items[I]; String(S,ItemIconForUi(C,R.Id,R.Display)); lua_pushinteger(S,R.Count); return 2;});
    Reg(L,"GetLFGQueueStats",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const auto* Selected=Selection(C); const auto* D=H?Data().Dungeon(H->Lfg.Queue.Dungeon):nullptr; if(!H || !Selected || !Selected->Queued || !D || H->Lfg.Queue.At<=0) {Flag(S,false); return 1;} const auto& Q=H->Lfg.Queue; Flag(S,true); lua_pushinteger(S,0); lua_pushinteger(S,Q.Tanks); lua_pushinteger(S,Q.Healers); lua_pushinteger(S,Q.Damage); lua_pushinteger(S,D->Type); String(S,D->Name); for(int32 V:{Q.Average,Q.TankWait,Q.HealerWait,Q.DamageWait,Q.Wait}) lua_pushinteger(S,V); lua_pushnumber(S,Q.At-Q.Elapsed); return 13;});
    Reg(L,"GetLFDLockPlayerCount",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); lua_pushinteger(S,H?H->GroupInfo.Members.Num()+1:0); return 1;});
    Reg(L,"GetLFDLockInfo",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const uint32 Id=luaL_checkinteger(S,1); const int I=luaL_checkinteger(S,2)-1; if(!H || I<0 || I>H->GroupInfo.Members.Num()) return 0; if(!I) {String(S,C->ConnectionManager->GetCharacterName()); lua_pushinteger(S,LocalLock(C,Id));} else {const auto& M=H->GroupInfo.Members[I-1]; String(S,M.Name); const auto* Locks=H->Lfg.PartyLocks.Find(M.Guid); lua_pushinteger(S,Locks?Locks->FindRef(Id):0);} return 2;});
    Reg(L,"GetLFGDungeonRewards",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const uint32 Id=luaL_checkinteger(S,1)&0xFFFFFF; const auto* R=H?H->Lfg.Rewards.Find(Id):nullptr; Flag(S,R && R->Done); for(uint32 V:{R?R->Money:0,R?R->MoneyVariance:0,R?R->Experience:0,R?R->ExperienceVariance:0,uint32(R?R->Items.Num():0)}) lua_pushinteger(S,V); return 6;});
    Reg(L,"GetLFGDungeonRewardInfo",[](lua_State* S)->int {auto* C=GetContext(S); auto* H=Handler(C); const auto* R=H?H->Lfg.Rewards.Find(luaL_checkinteger(S,1)&0xFFFFFF):nullptr; const int I=luaL_checkinteger(S,2)-1; if(!R || !R->Items.IsValidIndex(I)) return 0; const auto& Reward=R->Items[I]; const auto* Item=QueryItemTemplate(C,Reward.Id); if(Item) String(S,Item->Name); else lua_pushnil(S); String(S,ItemIconForUi(C,Reward.Id,Reward.Display)); lua_pushinteger(S,Reward.Count); return 3;});
    Reg(L,"GetLFGDungeonRewardLink",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const auto* R=H?H->Lfg.Rewards.Find(luaL_checkinteger(S,1)&0xFFFFFF):nullptr; const int I=luaL_checkinteger(S,2)-1; if(!R || !R->Items.IsValidIndex(I)) return 0; String(S,ItemLinkForUi(C,R->Items[I].Id)); return 1;});
    const char* Auras[]={"GetLFGDeserterExpiration","GetLFGRandomCooldownExpiration","UnitHasLFGDeserter","UnitHasLFGRandomCooldown"};
    for(int I=0;I<4;++I) {lua_pushinteger(L,I); lua_pushcclosure(L,[](lua_State* S)->int {auto* C=GetContext(S); const int Kind=lua_tointeger(S,lua_upvalueindex(1)); const auto* Unit=Kind<2?Player(C):ResolveUnitToken(S); const double Expires=Aura(C,Unit,Kind%2?71328:71041); if(Kind<2) {if(Expires>0) lua_pushnumber(S,Expires); else lua_pushnil(S);} else Flag(S,Expires>0); return 1;},1); lua_setglobal(L,Auras[I]);}
}
int WowLuaApi::SetLfgCompletionRewardTooltip(lua_State* L)
{
    auto* C=GetContext(L); const auto* H=Handler(C); const int I=luaL_checkinteger(L,2)-1; if(!H || !H->Lfg.FinalReward.Items.IsValidIndex(I)) return 0;
    lua_getfield(L,1,"SetHyperlink"); lua_pushvalue(L,1); String(L,ItemLinkForUi(C,H->Lfg.FinalReward.Items[I].Id)); lua_call(L,2,0); return 0;
}
int WowLuaApi::SetLfgRewardTooltip(lua_State* L)
{
    auto* C=GetContext(L); const auto* H=Handler(C); const auto* R=H?H->Lfg.Rewards.Find(luaL_checkinteger(L,2)&0xFFFFFF):nullptr; const int I=luaL_checkinteger(L,3)-1;
    if(!R || !R->Items.IsValidIndex(I)) return 0; const FString Link=ItemLinkForUi(C,R->Items[I].Id);
    lua_getfield(L,1,"SetHyperlink"); lua_pushvalue(L,1); String(L,Link); lua_call(L,2,0); return 0;
}
void WowLuaApi::UpdateLfgState(lua_State* L)
{
    auto* C=GetContext(L); auto* H=Handler(C); if(!H || !C->EventSystem) return; auto& V=View(C); const double Now=FPlatformTime::Seconds();
    const auto Event=[&](const TCHAR* Name){C->EventSystem->FireEvent(Name);}; const auto* P=Player(C); const uint32 Level=P?P->GetLevel():0;
    if(C->ConnectionManager->GetState()==EWowSessionState::WorldInGame && Level && (Now>=V.InfoAt || V.Level!=Level || V.Group!=H->GroupRevision)) {V.InfoAt=Now+60; V.Level=Level; V.Group=H->GroupRevision; Send(C,0x36E); if(!H->GroupInfo.IsEmpty()) Send(C,0x371); Event(TEXT("LFG_ROLE_UPDATE"));}
    if(C->ConnectionManager->GetState()==EWowSessionState::WorldInGame && Now>=V.StatusAt) {V.StatusAt=Now+30; Send(C,0x296);}
    if(V.Info!=H->Lfg.InfoRevision) {V.Info=H->Lfg.InfoRevision; Event(TEXT("LFG_LOCK_INFO_RECEIVED")); Event(TEXT("LFG_UPDATE_RANDOM_INFO"));}
    if(V.Revision!=H->Lfg.Revision) {V.Revision=H->Lfg.Revision; Event(TEXT("LFG_UPDATE"));}
    if(V.Queue!=H->Lfg.QueueRevision) {V.Queue=H->Lfg.QueueRevision; Event(TEXT("LFG_QUEUE_STATUS_UPDATE"));}
    if(V.Items!=uint32(H->ItemTemplates.Num())) {V.Items=H->ItemTemplates.Num(); Event(TEXT("LFG_UPDATE_RANDOM_INFO"));}
    if(V.Role!=H->Lfg.RoleRevision)
    {
        V.Role=H->Lfg.RoleRevision; bool Needed=H->Lfg.RoleCheck.State==2;
        for(const auto& M:H->Lfg.RoleCheck.Members) if(P && M.Guid==P->Guid && M.Ready) Needed=false;
        if(Needed!=V.RoleShown) {V.RoleShown=Needed; Event(Needed?TEXT("LFG_ROLE_CHECK_SHOW"):TEXT("LFG_ROLE_CHECK_HIDE"));}
        Event(TEXT("LFG_ROLE_UPDATE"));
        if(H->Lfg.RoleCheck.State>=3) {const char* Keys[]={"ERR_LFG_ROLE_CHECK_FAILED_TIMEOUT","ERR_LFG_ROLE_CHECK_FAILED_NOT_VIABLE","ERR_LFG_ROLE_CHECK_ABORTED","ERR_LFG_ROLE_CHECK_FAILED"}; lua_getglobal(L,Keys[H->Lfg.RoleCheck.State-3]); if(lua_isstring(L,-1)) C->EventSystem->FireEvent(TEXT("UI_ERROR_MESSAGE"),{UTF8_TO_TCHAR(lua_tostring(L,-1))}); lua_pop(L,1);}
    }
    if(V.Chosen!=H->Lfg.ChosenRevision)
    {
        for(const auto& Chosen:H->Lfg.ChosenEvents) if(Chosen.Revision>V.Chosen)
        {
            const auto& M=Chosen.Member; const FString Name=PlayerName(C,M.Guid);
            if(!Name.IsEmpty() && M.Ready && (M.Roles&14)) C->EventSystem->FireEventTyped(TEXT("LFG_ROLE_CHECK_ROLE_CHOSEN"),[&](lua_State* S){String(S,Name); Flag(S,(M.Roles&2)!=0); Flag(S,(M.Roles&4)!=0); Flag(S,(M.Roles&8)!=0); return 4;});
        }
        V.Chosen=H->Lfg.ChosenRevision;
    }
    if(V.Proposal!=H->Lfg.ProposalRevision)
    {
        V.Proposal=H->Lfg.ProposalRevision; const auto& Proposal=H->Lfg.Proposal;
        if(Proposal.Valid)
        {
            if(Proposal.State==0 && !Proposal.Silent && V.ProposalId!=Proposal.Id) Event(TEXT("LFG_PROPOSAL_SHOW"));
            V.ProposalId=Proposal.Id; Event(TEXT("LFG_PROPOSAL_UPDATE"));
            if(Proposal.State==1) Event(TEXT("LFG_PROPOSAL_FAILED")); else if(Proposal.State==2) Event(TEXT("LFG_PROPOSAL_SUCCEEDED"));
        }
    }
    if(V.Boot!=H->Lfg.BootRevision) {V.Boot=H->Lfg.BootRevision; Event(TEXT("LFG_BOOT_PROPOSAL_UPDATE"));}
    if(V.Continue!=H->Lfg.ContinueRevision) {V.Continue=H->Lfg.ContinueRevision; if(const auto* D=Data().Dungeon(H->Lfg.ContinueDungeon)) C->EventSystem->FireEventTyped(TEXT("LFG_OFFER_CONTINUE"),[&](lua_State* S){String(S,D->Name); lua_pushinteger(S,D->Id); lua_pushinteger(S,D->Type); return 3;});}
    if(V.Teleport!=H->Lfg.TeleportRevision)
    {
        V.Teleport=H->Lfg.TeleportRevision; static const char* Errors[]={nullptr,"ERR_PLAYER_DEAD","ERR_NOT_WHILE_FALLING","SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW","ERR_NOT_WHILE_FATIGUED",nullptr,"ERR_INVALID_TELEPORT_LOCATION","SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW","ERR_PARTY_LFG_TELEPORT_IN_COMBAT"};
        if(H->Lfg.TeleportError<UE_ARRAY_COUNT(Errors) && Errors[H->Lfg.TeleportError]) {lua_getglobal(L,Errors[H->Lfg.TeleportError]); if(lua_isstring(L,-1)) C->EventSystem->FireEvent(TEXT("UI_ERROR_MESSAGE"),{UTF8_TO_TCHAR(lua_tostring(L,-1))}); lua_pop(L,1);}
    }
    if(V.Reward!=H->Lfg.RewardRevision) {V.Reward=H->Lfg.RewardRevision; if(H->Lfg.RewardSpecific) {Event(TEXT("LFG_COMPLETION_REWARD")); V.InfoAt=0;}}
    if(V.Result!=H->Lfg.ResultRevision)
    {
        V.Result=H->Lfg.ResultRevision;
        static const char* Keys[]={nullptr,"ERR_LFG_ROLE_CHECK_FAILED","ERR_LFG_GROUP_FULL",nullptr,"ERR_LFG_NO_LFG_OBJECT","ERR_LFG_NO_SLOTS_PLAYER","ERR_LFG_NO_SLOTS_PARTY","ERR_LFG_MISMATCHED_SLOTS","ERR_LFG_PARTY_PLAYERS_FROM_DIFFERENT_REALMS","ERR_LFG_MEMBERS_NOT_PRESENT","ERR_LFG_GET_INFO_TIMEOUT","ERR_LFG_INVALID_SLOT","ERR_LFG_DESERTER_PLAYER","ERR_LFG_DESERTER_PARTY","ERR_LFG_RANDOM_COOLDOWN_PLAYER","ERR_LFG_RANDOM_COOLDOWN_PARTY","ERR_LFG_TOO_MANY_MEMBERS","ERR_LFG_CANT_USE_DUNGEONS","ERR_LFG_ROLE_CHECK_FAILED"};
        if(H->Lfg.Result<UE_ARRAY_COUNT(Keys) && Keys[H->Lfg.Result]) {lua_getglobal(L,Keys[H->Lfg.Result]); if(lua_isstring(L,-1)) C->EventSystem->FireEvent(TEXT("UI_ERROR_MESSAGE"),{UTF8_TO_TCHAR(lua_tostring(L,-1))}); lua_pop(L,1);}
    }
}
#else
void WowLuaApi::RegisterLfgApi(lua_State*) {}
void WowLuaApi::UpdateLfgState(lua_State*) {}
int WowLuaApi::SetLfgRewardTooltip(lua_State*) {return 0;}
int WowLuaApi::SetLfgCompletionRewardTooltip(lua_State*) {return 0;}
#endif
