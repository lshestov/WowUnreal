#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"

struct FWowGuildUiView
{
    uint32 Id=0,Announced=MAX_uint32,Revision=0,InviteRevision=1,ResultRevision=1,LogRevision=1,Event=0;
    uint64 Selected=0;
    bool Offline=false,Descending=false;
    FString Sort=TEXT("name");
    int32 Rank=0;
    FWowGuildRankAccess Draft;
    double NextRoster=0;
    TMap<uint64,double> NameQueries;
    FString Motd;
    bool MotdKnown=false;
};
#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void Reg(lua_State* L,const char* N,lua_CFunction F) {lua_pushcfunction(L,F); lua_setglobal(L,N);}
FWowGuildUiView& View(FWowLuaContext* C) {if(!C->GuildUi) C->GuildUi=MakeShared<FWowGuildUiView>(); return *C->GuildUi;}
FWowPacketHandler* Handler(FWowLuaContext* C) {return C && C->ConnectionManager?&C->ConnectionManager->PacketHandler:nullptr;}
const FWowPlayerEntity* Player(FWowLuaContext* C) {return C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;}
uint32 Id(FWowLuaContext* C) {const auto* P=Player(C); return P?P->GetField(PlayerField::GUILD_ID):0;}
uint32 Rank(FWowLuaContext* C) {const auto* P=Player(C); return P?P->GetField(PlayerField::GUILD_RANK):MAX_uint32;}
bool Allowed(FWowLuaContext* C,uint32 Mask) {const auto* H=Handler(C); const uint32 R=Rank(C); return Id(C) && H && (R==0 || (H->GuildRankAccess.IsValidIndex(R) && (H->GuildRankAccess[R].Rights&Mask)!=0));}
void Flag(lua_State* L,bool B) {if(B) lua_pushinteger(L,1); else lua_pushnil(L);}
bool Boolean(lua_State* L,int I) {return lua_toboolean(L,I)!=0 && (!lua_isnumber(L,I) || lua_tonumber(L,I)!=0);}
void Push(lua_State* L,const FString& S) {lua_pushstring(L,TCHAR_TO_UTF8(*S));}
template<typename T> void Add(TArray<uint8>& B,T V) {B.Append(reinterpret_cast<const uint8*>(&V),sizeof(V));}
void AddText(TArray<uint8>& B,const FString& S) {FTCHARToUTF8 U(*S); B.Append(reinterpret_cast<const uint8*>(U.Get()),U.Length()+1);}
bool TextOk(const FString& S,int32 Max,bool Empty=false) {FTCHARToUTF8 U(*S); return (Empty || !S.IsEmpty()) && U.Length()<=Max && !S.Contains(TEXT("|"));}
bool Send(FWowLuaContext* C,uint32 Op,const TArray<uint8>& B={}) {if(!C || !C->ConnectionManager || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame) return false; C->ConnectionManager->SendRawPacket(Op,B); return true;}
void Query(FWowLuaContext* C,uint32 Guild,bool Force=false)
{
    auto* H=Handler(C); if(!Guild || !H || (!Force && H->Guilds.Contains(Guild))) return;
    const double Now=FPlatformTime::Seconds(); if(const auto* Last=H->GuildQueries.Find(Guild); Last && Now-*Last<4) return;
    TArray<uint8> B; Add(B,Guild); if(Send(C,0x54,B)) H->GuildQueries.Add(Guild,Now);
}
void Roster(FWowLuaContext* C)
{if(C && Id(C)) {auto& V=View(C); const double Now=FPlatformTime::Seconds(); if(Now>=V.NextRoster && Send(C,0x89)) V.NextRoster=Now+.5; Query(C,Id(C));}}
const FWowPacketHandler::FGuildMetadata* Metadata(FWowLuaContext* C,uint32 Guild) {Query(C,Guild); const auto* H=Handler(C); return H?H->Guilds.Find(Guild):nullptr;}
FString RankName(FWowLuaContext* C,uint32 R) {const auto* G=Metadata(C,Id(C)); return G && G->Ranks.IsValidIndex(R)?G->Ranks[R]:FString();}
const TCHAR* ClassFile(uint8 C) {static const TCHAR* Names[]={TEXT(""),TEXT("WARRIOR"),TEXT("PALADIN"),TEXT("HUNTER"),TEXT("ROGUE"),TEXT("PRIEST"),TEXT("DEATHKNIGHT"),TEXT("SHAMAN"),TEXT("MAGE"),TEXT("WARLOCK"),TEXT(""),TEXT("DRUID")}; return C<UE_ARRAY_COUNT(Names)?Names[C]:TEXT("");}
FString ClassName(lua_State* L,const FWowGuildMember& M)
{
    const int Top=lua_gettop(L); lua_getglobal(L,M.Gender?"LOCALIZED_CLASS_NAMES_FEMALE":"LOCALIZED_CLASS_NAMES_MALE");
    FString Name=ClassFile(M.Class); if(lua_istable(L,-1)) {lua_getfield(L,-1,TCHAR_TO_UTF8(*Name)); if(lua_isstring(L,-1)) Name=UTF8_TO_TCHAR(lua_tostring(L,-1));} lua_settop(L,Top); return Name;
}
FString Zone(const FWowGuildMember& M) {const auto* A=FDbcStore::Get().AreaTable().GetById(M.ZoneId); return A?A->Name:FString();}
TArray<const FWowGuildMember*> Rows(FWowLuaContext* C)
{
    TArray<const FWowGuildMember*> Out; const auto* H=Handler(C); if(!H || !C) return Out; const auto& V=View(C);
    for(const auto& M:H->GuildRoster) if(V.Offline || (M.Status&1)) Out.Add(&M);
    Out.Sort([&](const auto& A,const auto& B)
    {
        int Compare=0;
        if(V.Sort==TEXT("level")) Compare=int(A.Level)-int(B.Level);
        else if(V.Sort==TEXT("rank")) Compare=int(A.RankId)-int(B.RankId);
        else if(V.Sort==TEXT("class")) Compare=FCString::Stricmp(ClassFile(A.Class),ClassFile(B.Class));
        else if(V.Sort==TEXT("zone")) Compare=Zone(A).Compare(Zone(B),ESearchCase::IgnoreCase);
        else if(V.Sort==TEXT("note")) Compare=A.PublicNote.Compare(B.PublicNote,ESearchCase::IgnoreCase);
        else if(V.Sort==TEXT("online")) Compare=int(B.Status!=0)-int(A.Status!=0);
        if(!Compare) Compare=A.Name.Compare(B.Name,ESearchCase::IgnoreCase); if(!Compare) Compare=A.Guid<B.Guid?-1:A.Guid>B.Guid?1:0;
        return V.Descending?Compare>0:Compare<0;
    }); return Out;
}
const FWowGuildMember* Row(FWowLuaContext* C,int32 Index) {const auto R=Rows(C); return R.IsValidIndex(Index-1)?R[Index-1]:nullptr;}
int Note(lua_State* L,bool Officer)
{
    auto* C=WowLuaApi::GetContext(L); const auto* M=Row(C,luaL_checkinteger(L,1)); const FString Value=UTF8_TO_TCHAR(luaL_checkstring(L,2));
    if(M && Allowed(C,Officer?0x8000:0x2000) && TextOk(Value,31,true)) {TArray<uint8> B; AddText(B,M->Name); AddText(B,Value); Send(C,Officer?0x235:0x234,B);} return 0;
}
int Named(lua_State* L,uint32 Op,uint32 Rights,bool Leader=false)
{auto* C=WowLuaApi::GetContext(L); const FString N=UTF8_TO_TCHAR(luaL_checkstring(L,1)); if((Leader?Id(C) && Rank(C)==0:Allowed(C,Rights)) && TextOk(N,48)) {TArray<uint8> B; AddText(B,N); Send(C,Op,B);} return 0;}
constexpr uint32 Masks[]={1,2,4,8,0x80,0x100,0x10,0x20,0x1000,0x2000,0x4000,0x8000,0x10000,0,0x40000,0x80000,0x100000};
void Elapsed(lua_State* L,uint32 Seconds) {const uint32 Hours=Seconds/3600,Days=Hours/24; lua_pushinteger(L,Days/365); lua_pushinteger(L,(Days%365)/30); lua_pushinteger(L,(Days%365)%30); lua_pushinteger(L,Hours%24);}
FString Name(FWowPacketHandler& H,uint64 Guid) {return H.PlayerNameCache.FindRef(Guid);}
FString Global(lua_State* L,const char* Key) {lua_getglobal(L,Key); FString S=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):TEXT(""); lua_pop(L,1); return S;}
FString Format(lua_State* L,const FString& S,const TArray<FString>& Args)
{const int Top=lua_gettop(L); lua_getglobal(L,"format"); Push(L,S); for(const auto& A:Args) Push(L,A); FString Out; if(lua_pcall(L,Args.Num()+1,1,0)==0 && lua_isstring(L,-1)) Out=UTF8_TO_TCHAR(lua_tostring(L,-1)); lua_settop(L,Top); return Out;}
}
void WowLuaApi::RegisterGuildApi(lua_State* L)
{
    Reg(L,"IsInGuild",[](lua_State* S)->int {Flag(S,Id(GetContext(S))!=0); return 1;});
    Reg(L,"IsGuildLeader",[](lua_State* S)->int {auto* C=GetContext(S); const FString N=UTF8_TO_TCHAR(luaL_optstring(S,1,"")); const auto* H=Handler(C); bool Leader=Id(C) && Rank(C)==0; if(H && !N.IsEmpty()) {Leader=false; for(const auto& M:H->GuildRoster) if(M.Name.Equals(N,ESearchCase::IgnoreCase)) Leader=M.RankId==0;} Flag(S,Leader); return 1;});
    Reg(L,"GetGuildInfo",[](lua_State* S)->int {auto* C=GetContext(S); const auto* U=ResolveUnitToken(S); const uint32 G=U?U->GetField(PlayerField::GUILD_ID):0; const auto* M=Metadata(C,G); if(!M) return 0; const uint32 R=U->GetField(PlayerField::GUILD_RANK); Push(S,M->Name); if(M->Ranks.IsValidIndex(R)) Push(S,M->Ranks[R]); else lua_pushnil(S); lua_pushinteger(S,R); return 3;});
    Reg(L,"GuildRoster",[](lua_State* S)->int {Roster(GetContext(S)); return 0;});
    Reg(L,"GetNumGuildMembers",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const int32 Count=Boolean(S,1) && H?H->GuildRoster.Num():Rows(C).Num(); int32 Online=0; if(H) for(const auto& M:H->GuildRoster) Online+=(M.Status&1)!=0; lua_pushinteger(S,Count); lua_pushinteger(S,Online); return 2;});
    Reg(L,"GetGuildRosterInfo",[](lua_State* S)->int {auto* C=GetContext(S); const auto* M=Row(C,luaL_checkinteger(S,1)); if(!M) return 0; Push(S,M->Name); Push(S,RankName(C,M->RankId)); lua_pushinteger(S,M->RankId); lua_pushinteger(S,M->Level); Push(S,ClassName(S,*M)); Push(S,Zone(*M)); Push(S,M->PublicNote); Push(S,Allowed(C,0x4000)?M->OfficerNote:FString()); Flag(S,(M->Status&1)!=0); Push(S,(M->Status&4)?Global(S,"DND"):(M->Status&2)?Global(S,"AFK"):FString()); lua_pushstring(S,TCHAR_TO_UTF8(ClassFile(M->Class))); return 11;});
    Reg(L,"GetGuildRosterLastOnline",[](lua_State* S)->int {const auto* M=Row(GetContext(S),luaL_checkinteger(S,1)); if(!M || M->Status) return 0; Elapsed(S,uint32(FMath::Min(double(M->DaysOffline)*86400.,double(MAX_uint32)))); return 4;});
    Reg(L,"SetGuildRosterShowOffline",[](lua_State* S)->int {auto* C=GetContext(S); if(C) {View(C).Offline=Boolean(S,1); if(C->EventSystem) C->EventSystem->FireEventBoolean(TEXT("GUILD_ROSTER_UPDATE"),false);} return 0;});
    Reg(L,"GetGuildRosterShowOffline",[](lua_State* S)->int {auto* C=GetContext(S); Flag(S,C && View(C).Offline); return 1;});
    Reg(L,"SortGuildRoster",[](lua_State* S)->int {auto* C=GetContext(S); if(C) {auto& V=View(C); const FString Key=UTF8_TO_TCHAR(luaL_checkstring(S,1)); V.Descending=V.Sort==Key?!V.Descending:false; V.Sort=Key; if(C->EventSystem) C->EventSystem->FireEventBoolean(TEXT("GUILD_ROSTER_UPDATE"),false);} return 0;});
    Reg(L,"SetGuildRosterSelection",[](lua_State* S)->int {auto* C=GetContext(S); if(C) {const auto* M=Row(C,luaL_checkinteger(S,1)); View(C).Selected=M?M->Guid:0;} return 0;});
    Reg(L,"GetGuildRosterSelection",[](lua_State* S)->int {auto* C=GetContext(S); const auto R=Rows(C); int32 Index=0; if(C) for(int32 I=0;I<R.Num();++I) if(R[I]->Guid==View(C).Selected) Index=I+1; lua_pushinteger(S,Index); return 1;});
    Reg(L,"GetGuildRosterMOTD",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); Push(S,H?H->GuildMotd:FString()); return 1;});
    Reg(L,"GetGuildInfoText",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); Push(S,H?H->GuildInfoText:FString()); return 1;});
    Reg(L,"GuildSetMOTD",[](lua_State* S)->int {auto* C=GetContext(S); const FString Text=UTF8_TO_TCHAR(luaL_checkstring(S,1)); if(Allowed(C,0x1000) && TextOk(Text,127,true)) {TArray<uint8> B; AddText(B,Text); Send(C,0x91,B);} return 0;});
    Reg(L,"SetGuildInfoText",[](lua_State* S)->int {auto* C=GetContext(S); const FString Text=UTF8_TO_TCHAR(luaL_checkstring(S,1)); if(Allowed(C,0x10000) && TextOk(Text,500,true)) {TArray<uint8> B; AddText(B,Text); Send(C,0x2FC,B);} return 0;});
    Reg(L,"GuildRosterSetPublicNote",[](lua_State* S)->int {return Note(S,false);});
    Reg(L,"GuildRosterSetOfficerNote",[](lua_State* S)->int {return Note(S,true);});
    Reg(L,"GuildInvite",[](lua_State* S)->int {return Named(S,0x82,0x10);});
    Reg(L,"GuildUninvite",[](lua_State* S)->int {return Named(S,0x8E,0x20);});
    Reg(L,"GuildPromote",[](lua_State* S)->int {return Named(S,0x8B,0x80);});
    Reg(L,"GuildDemote",[](lua_State* S)->int {return Named(S,0x8C,0x100);});
    Reg(L,"GuildSetLeader",[](lua_State* S)->int {return Named(S,0x90,0,true);});
    Reg(L,"GuildDisband",[](lua_State* S)->int {auto* C=GetContext(S); if(Id(C) && Rank(C)==0) Send(C,0x8F); return 0;});
    Reg(L,"GuildLeave",[](lua_State* S)->int {auto* C=GetContext(S); if(Id(C)) Send(C,0x8D); return 0;});
    Reg(L,"AcceptGuild",[](lua_State* S)->int {auto* C=GetContext(S); auto* H=Handler(C); if(H && !H->GuildInvitation.IsEmpty() && Send(C,0x84)) {H->GuildInvitation.Empty(); H->GuildInviter.Empty(); ++H->GuildInviteRevision;} return 0;});
    Reg(L,"DeclineGuild",[](lua_State* S)->int {auto* C=GetContext(S); auto* H=Handler(C); if(H && !H->GuildInvitation.IsEmpty() && Send(C,0x85)) {H->GuildInvitation.Empty(); H->GuildInviter.Empty(); ++H->GuildInviteRevision;} return 0;});
    const auto Permission=[&](const char* N,uint32 Mask) {lua_pushinteger(L,Mask); lua_pushcclosure(L,[](lua_State* S)->int {Flag(S,Allowed(GetContext(S),uint32(lua_tointeger(S,lua_upvalueindex(1))))); return 1;},1); lua_setglobal(L,N);};
    Permission("CanGuildInvite",0x10); Permission("CanGuildRemove",0x20); Permission("CanGuildPromote",0x80); Permission("CanGuildDemote",0x100); Permission("CanEditMOTD",0x1000); Permission("CanEditPublicNote",0x2000); Permission("CanViewOfficerNote",0x4000); Permission("CanEditOfficerNote",0x8000); Permission("CanEditGuildInfo",0x10000);
    Reg(L,"GuildControlGetNumRanks",[](lua_State* S)->int {auto* C=GetContext(S); const auto* G=Metadata(C,Id(C)); lua_pushinteger(S,G?G->Ranks.Num():0); return 1;});
    Reg(L,"GuildControlGetRankName",[](lua_State* S)->int {Push(S,RankName(GetContext(S),luaL_checkinteger(S,1)-1)); return 1;});
    Reg(L,"GuildControlSetRank",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const int32 R=luaL_checkinteger(S,1)-1; if(H && H->GuildRankAccess.IsValidIndex(R)) {View(C).Rank=R; View(C).Draft=H->GuildRankAccess[R];} return 0;});
    Reg(L,"GuildControlGetRankFlags",[](lua_State* S)->int {auto* C=GetContext(S); const uint32 Rights=C?View(C).Draft.Rights:0; for(uint32 Mask:Masks) Flag(S,Mask && (Rights&Mask)); return UE_ARRAY_COUNT(Masks);});
    Reg(L,"GuildControlSetRankFlag",[](lua_State* S)->int {auto* C=GetContext(S); const int32 I=luaL_checkinteger(S,1)-1; if(C && I>=0 && I<UE_ARRAY_COUNT(Masks)) {auto& R=View(C).Draft.Rights; if(Boolean(S,2)) R|=Masks[I]; else R&=~Masks[I];} return 0;});
    Reg(L,"GetGuildBankWithdrawLimit",[](lua_State* S)->int {auto* C=GetContext(S); const uint32 V=C?View(C).Draft.GoldPerDay:0; lua_pushnumber(S,V==MAX_uint32?-1.:V/10000.); return 1;});
    Reg(L,"SetGuildBankWithdrawLimit",[](lua_State* S)->int {auto* C=GetContext(S); const double V=luaL_checknumber(S,1); if(C && FMath::IsFinite(V) && V>=0 && V<=MAX_uint32/10000.) View(C).Draft.GoldPerDay=uint32(V*10000); return 0;});
    Reg(L,"GetNumGuildBankTabs",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); lua_pushinteger(S,H?H->GuildBankTabs:0); return 1;});
    Reg(L,"GetGuildBankTabInfo",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const int32 I=luaL_checkinteger(S,1)-1; if(!H || !H->GuildBankTabNames.IsValidIndex(I)) return 0; const auto& T=H->GuildBankTabNames[I]; const auto& A=H->GuildBankMemberAccess; Push(S,T.Name); Push(S,T.Icon); Flag(S,Rank(C)==0 || (A.TabRights[I]&1)); Flag(S,Rank(C)==0 || (A.TabRights[I]&2)); lua_pushnumber(S,A.TabSlots[I]==MAX_uint32?-1.:A.TabSlots[I]); return 5;});
    Reg(L,"GetGuildBankTabPermissions",[](lua_State* S)->int {auto* C=GetContext(S); const int32 T=luaL_checkinteger(S,1)-1; if(!C || T<0 || T>=6) return 0; const auto& D=View(C).Draft; Flag(S,(D.TabRights[T]&1)!=0); Flag(S,(D.TabRights[T]&2)!=0); Flag(S,(D.TabRights[T]&4)!=0); lua_pushnumber(S,D.TabSlots[T]==MAX_uint32?-1.:D.TabSlots[T]); return 4;});
    Reg(L,"SetGuildBankTabPermissions",[](lua_State* S)->int {auto* C=GetContext(S); const int32 T=luaL_checkinteger(S,1)-1,I=luaL_checkinteger(S,2)-1; if(C && T>=0 && T<6 && I>=0 && I<3) {auto& R=View(C).Draft.TabRights[T]; if(Boolean(S,3)) R|=1u<<I; else R&=~(1u<<I);} return 0;});
    Reg(L,"SetGuildBankTabWithdraw",[](lua_State* S)->int {auto* C=GetContext(S); const int32 T=luaL_checkinteger(S,1)-1; const double V=luaL_checknumber(S,2); if(C && T>=0 && T<6 && FMath::IsFinite(V) && V>=-1 && V<=MAX_uint32) View(C).Draft.TabSlots[T]=V<0?MAX_uint32:uint32(V); return 0;});
    Reg(L,"GuildControlSaveRank",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const FString N=UTF8_TO_TCHAR(luaL_checkstring(S,1)); if(H && Id(C) && Rank(C)==0 && H->GuildRankAccess.IsValidIndex(View(C).Rank) && TextOk(N,15)) {const auto& V=View(C); TArray<uint8> B; Add(B,uint32(V.Rank)); Add(B,V.Draft.Rights|0x40u); AddText(B,N); Add(B,V.Draft.GoldPerDay); for(int32 T=0;T<6;++T) {Add(B,V.Draft.TabRights[T]); Add(B,V.Draft.TabSlots[T]);} Send(C,0x231,B);} return 0;});
    Reg(L,"GuildControlAddRank",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const FString N=UTF8_TO_TCHAR(luaL_checkstring(S,1)); if(H && Id(C) && Rank(C)==0 && H->GuildRankAccess.Num()<10 && TextOk(N,15)) {TArray<uint8> B; AddText(B,N); Send(C,0x232,B);} return 0;});
    Reg(L,"GuildControlDelRank",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); if(H && Id(C) && Rank(C)==0 && H->GuildRankAccess.Num()>5) Send(C,0x233); return 0;});
    Reg(L,"QueryGuildEventLog",[](lua_State* S)->int {auto* C=GetContext(S); if(Id(C)) Send(C,0x3FF); return 0;});
    Reg(L,"GetNumGuildEvents",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); lua_pushinteger(S,H?H->GuildLogs.Num():0); return 1;});
    Reg(L,"GetGuildEventInfo",[](lua_State* S)->int {auto* C=GetContext(S); auto* H=Handler(C); const int32 I=luaL_checkinteger(S,1)-1; if(!H || !H->GuildLogs.IsValidIndex(I)) return 0; const auto& E=H->GuildLogs[I]; static const char* Types[]={"","invite","join","promote","demote","remove","quit"}; lua_pushstring(S,Types[E.Type]); Push(S,Name(*H,E.First)); if(E.Second) Push(S,Name(*H,E.Second)); else lua_pushnil(S); Push(S,RankName(C,E.Rank)); Elapsed(S,E.Seconds); return 8;});
}
void WowLuaApi::UpdateGuildState(lua_State* L)
{
    auto* C=GetContext(L); auto* H=Handler(C); if(!H || !C->EventSystem) return; auto& V=View(C); auto* E=C->EventSystem; const uint32 Guild=Id(C);
    if(V.Id!=Guild) {const uint32 Old=V.Id; V.Id=Guild; V.Selected=0; V.NextRoster=0; V.MotdKnown=false; H->GuildBankTabNames.Reset(); H->GuildBankMemberAccess=FWowGuildRankAccess(); if(!Guild && Old) {H->GuildRoster.Reset(); H->GuildRankAccess.Reset(); H->GuildName.Empty(); H->GuildMotd.Empty(); H->GuildInfoText.Empty(); H->GuildBankTabs=0; ++H->GuildRevision;} if(Guild) {Roster(C); Send(C,0x3FD);}}
    Query(C,Guild);
    if(const auto* G=H->Guilds.Find(Guild)) H->GuildName=G->Name;
    if(V.Announced!=Guild && (!Guild || H->Guilds.Contains(Guild))) {V.Announced=Guild; E->FireEvent(TEXT("PLAYER_GUILD_UPDATE"),{TEXT("player")});}
    if(V.InviteRevision!=H->GuildInviteRevision) {V.InviteRevision=H->GuildInviteRevision; if(!H->GuildInvitation.IsEmpty()) E->FireEvent(TEXT("GUILD_INVITE_REQUEST"),{H->GuildInviter,H->GuildInvitation}); else E->FireEvent(TEXT("GUILD_INVITE_CANCEL"));}
    TArray<FWowPacketHandler::FGuildEvent> Pending; for(const auto& Event:H->GuildEvents) if(Event.Sequence>V.Event) Pending.Add(Event);
    for(const auto& Event:Pending)
    {
        V.Event=Event.Sequence;
        if(Event.Type==2 && Event.Text.Num()==1) {if(!V.MotdKnown || V.Motd!=Event.Text[0]) {V.Motd=Event.Text[0]; V.MotdKnown=true; E->FireEvent(TEXT("GUILD_MOTD"),Event.Text);}}
        else
        {
            if(Event.Type==9 || Event.Type==10 || Event.Type==11) {H->GuildQueries.Remove(Guild); Query(C,Guild,true);}
            V.NextRoster=0; Roster(C);
            const char* Key=Event.Type==0?"ERR_GUILD_PROMOTE_SSS":Event.Type==1?"ERR_GUILD_DEMOTE_SSS":Event.Type==3?"ERR_GUILD_JOIN_S":Event.Type==4?"ERR_GUILD_LEAVE_S":Event.Type==5?"ERR_GUILD_REMOVE_SS":Event.Type==6?"ERR_GUILD_LEADER_IS_S":Event.Type==7?"ERR_GUILD_LEADER_CHANGED_SS":Event.Type==8?"ERR_GUILD_DISBAND_S":"";
            if(*Key) {const FString Message=Format(L,Global(L,Key),Event.Text); if(!Message.IsEmpty()) E->FireEvent(TEXT("CHAT_MSG_SYSTEM"),{Message,TEXT(""),TEXT(""),TEXT(""),TEXT(""),TEXT(""),TEXT("0"),TEXT("0"),TEXT(""),TEXT(""),TEXT("0"),TEXT("0x0000000000000000")});}
        }
    }
    if(V.ResultRevision!=H->GuildResultRevision)
    {
        V.ResultRevision=H->GuildResultRevision;
        static const char* Errors[]={"","ERR_GUILD_INTERNAL","ERR_ALREADY_IN_GUILD","ERR_ALREADY_IN_GUILD_S","ERR_INVITED_TO_GUILD","ERR_ALREADY_INVITED_TO_GUILD_S","ERR_GUILD_NAME_INVALID","ERR_GUILD_NAME_EXISTS_S","ERR_GUILD_PERMISSIONS","ERR_GUILD_PLAYER_NOT_IN_GUILD","ERR_GUILD_PLAYER_NOT_IN_GUILD_S","ERR_GUILD_PLAYER_NOT_FOUND_S","ERR_GUILD_NOT_ALLIED","ERR_GUILD_RANK_TOO_HIGH_S","ERR_GUILD_RANK_TOO_LOW_S"};
        const uint32 Code=H->GuildResultCode;
        if(Code) {const char* Key=Code<UE_ARRAY_COUNT(Errors)?Errors[Code]:Code==17?"ERR_GUILD_RANKS_LOCKED":Code==18?"ERR_GUILD_RANK_IN_USE":"ERR_GUILD_INTERNAL"; FString Message=Global(L,Key); if(Message.Contains(TEXT("%s"))) Message=Format(L,Message,{H->GuildResultName}); E->FireEvent(TEXT("UI_ERROR_MESSAGE"),{Message});}
    }
    if(V.Revision!=H->GuildRevision) {V.Revision=H->GuildRevision; if(Guild && (!V.MotdKnown || V.Motd!=H->GuildMotd)) {V.Motd=H->GuildMotd; V.MotdKnown=true; E->FireEvent(TEXT("GUILD_MOTD"),{H->GuildMotd});} E->FireEventBoolean(TEXT("GUILD_ROSTER_UPDATE"),false);}
    if(V.LogRevision!=H->GuildLogRevision)
    {
        for(const auto& Log:H->GuildLogs) for(const uint64 Guid:{Log.First,Log.Second}) if(Guid && !H->PlayerNameCache.Contains(Guid)) {const double Now=FPlatformTime::Seconds(); const auto* Last=V.NameQueries.Find(Guid); if(!Last || Now-*Last>10) {TArray<uint8> B; Add(B,Guid); if(Send(C,0x50,B)) V.NameQueries.Add(Guid,Now);}}
        bool Ready=true; for(const auto& Log:H->GuildLogs) Ready&=H->PlayerNameCache.Contains(Log.First) && (!Log.Second || H->PlayerNameCache.Contains(Log.Second));
        if(Ready) {V.LogRevision=H->GuildLogRevision; E->FireEvent(TEXT("GUILD_EVENT_LOG_UPDATE"));}
    }
}
#else
void WowLuaApi::RegisterGuildApi(lua_State*) {}
void WowLuaApi::UpdateGuildState(lua_State*) {}
#endif
