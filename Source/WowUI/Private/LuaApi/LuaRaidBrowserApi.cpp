#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"

struct FWowLfrUiView
{
    uint32 Revision=0,Names=0,Dungeon=0; FString Column=TEXT("name"); bool Reverse=false;
    TSet<uint64> Requested;
};
#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
using FPlayer=FWowLfgState::FRaidPlayer;
using FGroup=FWowLfgState::FRaidGroup;
FWowLfrUiView& View(FWowLuaContext* C) {if(!C->LfrUi) C->LfrUi=MakeShared<FWowLfrUiView>(); return *C->LfrUi;}
FWowPacketHandler* Handler(FWowLuaContext* C) {return C && C->ConnectionManager?&C->ConnectionManager->PacketHandler:nullptr;}
void Text(lua_State* L,const FString& Value) {lua_pushstring(L,TCHAR_TO_UTF8(*Value));}
void Flag(lua_State* L,bool Value) {lua_pushboolean(L,Value);}
void Reg(lua_State* L,const char* Name,lua_CFunction Function) {lua_pushcfunction(L,Function); lua_setglobal(L,Name);}
FString Name(FWowLuaContext* C,uint64 Guid)
{
    const auto* H=Handler(C); if(!H) return {};
    if(Guid==H->EntityManager.LocalPlayerGuid && !C->ConnectionManager->GetCharacterName().IsEmpty()) return C->ConnectionManager->GetCharacterName();
    if(const auto* M=H->GroupInfo.FindMember(Guid)) return M->Name;
    return H->PlayerNameCache.FindRef(Guid);
}
const char* Class(uint8 Id)
{
    static const char* Tokens[]={nullptr,"WARRIOR","PALADIN","HUNTER","ROGUE","PRIEST","DEATHKNIGHT","SHAMAN","MAGE","WARLOCK",nullptr,"DRUID"};
    return Id<UE_ARRAY_COUNT(Tokens)?Tokens[Id]:nullptr;
}
void ClassName(lua_State* L,uint8 Id)
{
    const auto* Token=Class(Id); lua_getglobal(L,"LOCALIZED_CLASS_NAMES_MALE");
    if(Token && lua_istable(L,-1)) {lua_getfield(L,-1,Token); lua_remove(L,-2); if(lua_isstring(L,-1)) return; lua_pop(L,1);}
    else lua_pop(L,1); lua_pushliteral(L,"");
}
FString Area(uint32 Id) {const auto* A=FDbcStore::Get().AreaTable().GetById(Id); return A?A->Name:FString();}
TArray<const FPlayer*> Members(FWowLuaContext* C,uint64 Guid)
{
    TArray<const FPlayer*> Out; const auto* H=Handler(C); if(!H || !Guid) return Out;
    for(const auto& Pair:H->Lfg.Raid.Players) if(Pair.Value.Group==Guid) Out.Add(&Pair.Value);
    Out.Sort([](const auto& A,const auto& B){return A.Leader!=B.Leader?A.Leader:A.Guid<B.Guid;}); return Out;
}
struct FResult {const FPlayer* Player=nullptr; const FGroup* Group=nullptr; FString Name; int32 Members=0; uint32 Encounters=0;};
TArray<FResult> Results(FWowLuaContext* C,int32* Total=nullptr)
{
    TArray<FResult> Out; const auto* H=Handler(C); if(Total) *Total=0; if(!H || !H->Lfg.Raid.Ready || !H->Lfg.Raid.SearchDungeon) return Out;
    for(const auto& Pair:H->Lfg.Raid.Players)
    {
        const auto& P=Pair.Value; const auto* G=P.Group?H->Lfg.Raid.Groups.Find(P.Group):nullptr;
        if(P.Group && (!P.Leader || !G)) continue; if(Total) ++*Total;
        const FString N=Name(C,P.Guid); if(N.IsEmpty()) continue;
        Out.Add({&P,G,N,G?Members(C,G->Guid).Num():0,G?G->Encounters:P.Encounters});
    }
    const auto& V=View(C);
    Out.Sort([&](const auto& A,const auto& B)
    {
        int32 Order=0;
        if(V.Column==TEXT("level")) Order=int32(A.Player->Level)-int32(B.Player->Level);
        else if(V.Column==TEXT("class")) Order=int32(A.Player->Class)-int32(B.Player->Class);
        else if(V.Column==TEXT("group")) Order=A.Members-B.Members;
        else if(V.Column==TEXT("tank") || V.Column==TEXT("healer") || V.Column==TEXT("damage"))
        {const uint8 Bit=V.Column==TEXT("tank")?2:V.Column==TEXT("healer")?4:8; Order=int32((A.Player->Roles&Bit)!=0)-int32((B.Player->Roles&Bit)!=0);}
        if(!Order) Order=A.Name.Compare(B.Name,ESearchCase::IgnoreCase);
        if(!Order) return A.Player->Guid<B.Player->Guid; return V.Reverse?Order>0:Order<0;
    }); return Out;
}
TArray<const FLfgUiDbc::FEncounter*> Encounters(FWowLuaContext* C)
{
    TArray<const FLfgUiDbc::FEncounter*> Out; const auto* H=Handler(C); const auto* D=H?FDbcStore::Get().LfgUi().Dungeon(H->Lfg.Raid.SearchDungeon):nullptr;
    if(D) for(const auto& E:FDbcStore::Get().LfgUi().Encounters()) if(E.Map==D->Map && E.Difficulty==D->Difficulty) Out.Add(&E);
    Out.Sort([](const auto& A,const auto& B){return A.Order!=B.Order?A.Order<B.Order:A.Id<B.Id;}); return Out;
}
bool Send(FWowLuaContext* C,uint32 Op,uint32 Entry)
{
    if(!C || !C->ConnectionManager || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame) return false;
    TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(&Entry),4); C->ConnectionManager->SendRawPacket(Op,Bytes); return true;
}
void Search(FWowLuaContext* C,uint32 Id)
{
    auto* H=Handler(C); const auto* D=FDbcStore::Get().LfgUi().Dungeon(Id); if(!H || !D || D->Type!=2 || !Send(C,0x35E,D->Entry())) return;
    if(H->Lfg.Raid.SearchDungeon!=D->Id)
    {H->Lfg.Raid.SearchDungeon=D->Id; H->Lfg.Raid.Ready=false; H->Lfg.Raid.Players.Reset(); H->Lfg.Raid.Groups.Reset(); ++H->Lfg.Raid.Revision;}
}
}
void WowLuaApi::RegisterRaidBrowserApi(lua_State* L)
{
    Reg(L,"SearchLFGJoin",[](lua_State* S)->int {const auto* D=FDbcStore::Get().LfgUi().Dungeon(luaL_checkinteger(S,2)); if(D && D->Type==uint32(luaL_checkinteger(S,1))) Search(GetContext(S),D->Id); return 0;});
    Reg(L,"RefreshLFGList",[](lua_State* S)->int {auto* C=GetContext(S); if(const auto* H=Handler(C)) if(H->Lfg.Raid.SearchDungeon) Search(C,H->Lfg.Raid.SearchDungeon); return 0;});
    Reg(L,"SearchLFGLeave",[](lua_State* S)->int {
        auto* C=GetContext(S); auto* H=Handler(C); if(H && H->Lfg.Raid.SearchDungeon)
        {Send(C,0x35F,H->Lfg.Raid.SearchDungeon); H->Lfg.Raid.SearchDungeon=0; H->Lfg.Raid.Ready=false; H->Lfg.Raid.Players.Reset(); H->Lfg.Raid.Groups.Reset(); ++H->Lfg.Raid.Revision;} return 0;
    });
    Reg(L,"SearchLFGGetJoinedID",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); if(H && H->Lfg.Raid.SearchDungeon) lua_pushinteger(S,H->Lfg.Raid.SearchDungeon); else lua_pushnil(S); return 1;});
    Reg(L,"SearchLFGGetNumResults",[](lua_State* S)->int {int32 Total=0; const auto Rows=Results(GetContext(S),&Total); lua_pushinteger(S,Rows.Num()); lua_pushinteger(S,Total); return 2;});
    Reg(L,"SearchLFGGetResults",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto Rows=Results(C); const int32 Index=luaL_checkinteger(S,1)-1; if(!Rows.IsValidIndex(Index)) return 0;
        const auto& R=Rows[Index]; const auto& P=*R.Player; const auto Bosses=Encounters(C); int32 Done=0; for(uint32 Bits=R.Encounters;Bits;Bits&=Bits-1) ++Done;
        Text(S,R.Name); lua_pushinteger(S,P.Level); Text(S,Area(P.Area)); ClassName(S,P.Class); Text(S,R.Group?R.Group->Comment:P.Comment); lua_pushinteger(S,R.Members); lua_pushinteger(S,P.Status);
        if(const auto* Token=Class(P.Class)) lua_pushstring(S,Token); else lua_pushnil(S);
        lua_pushinteger(S,Bosses.Num()); lua_pushinteger(S,Done); Flag(S,P.Leader || (P.Roles&1)); for(uint8 Bit:{2,4,8}) Flag(S,(P.Roles&Bit)!=0); return 14;
    });
    Reg(L,"SearchLFGGetPartyResults",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* H=Handler(C); const auto Rows=Results(C); const int32 Index=luaL_checkinteger(S,1)-1,Member=luaL_checkinteger(S,2)-1;
        if(!H || !Rows.IsValidIndex(Index) || !Rows[Index].Group) return 0; const auto People=Members(C,Rows[Index].Group->Guid); if(!People.IsValidIndex(Member)) return 0;
        const auto& P=*People[Member]; const FString N=Name(C,P.Guid); if(N.IsEmpty()) return 0;
        Text(S,N); lua_pushinteger(S,P.Level); const bool Friend=H->FriendsList.ContainsByPredicate([&](const auto& F){return F.Guid==P.Guid;}); if(Friend) lua_pushliteral(S,"friend"); else lua_pushnil(S);
        ClassName(S,P.Class); Text(S,Area(P.Area)); Text(S,P.Comment); return 6;
    });
    Reg(L,"SearchLFGGetEncounterResults",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto Rows=Results(C); const auto Bosses=Encounters(C); const int32 Row=luaL_checkinteger(S,1)-1,Boss=luaL_checkinteger(S,2)-1;
        if(!Rows.IsValidIndex(Row) || !Bosses.IsValidIndex(Boss)) return 0; const auto& E=*Bosses[Boss]; Text(S,E.Name); const auto* Icon=FDbcStore::Get().SpellIcons().GetById(E.Icon); if(Icon) Text(S,Icon->TexturePath); else lua_pushnil(S); Flag(S,E.Index<32 && (Rows[Row].Encounters&(1u<<E.Index))); return 3;
    });
    Reg(L,"SearchLFGSort",[](lua_State* S)->int {
        auto* C=GetContext(S); if(!C) return 0; const FString Column=UTF8_TO_TCHAR(luaL_checkstring(S,1));
        if(Column!=TEXT("name") && Column!=TEXT("level") && Column!=TEXT("class") && Column!=TEXT("tank") && Column!=TEXT("healer") && Column!=TEXT("damage") && Column!=TEXT("group")) return 0;
        auto& V=View(C); V.Reverse=V.Column==Column?!V.Reverse:false; V.Column=Column; if(C->EventSystem) C->EventSystem->FireEvent(TEXT("UPDATE_LFG_LIST")); return 0;
    });
}
void WowLuaApi::UpdateRaidBrowserState(lua_State* L)
{
    auto* C=GetContext(L); auto* H=Handler(C); if(!H || !C->EventSystem) return; auto& V=View(C); const auto& B=H->Lfg.Raid;
    if(V.Dungeon!=B.SearchDungeon) {V.Dungeon=B.SearchDungeon; V.Requested.Reset();}
    if(B.Ready && C->ConnectionManager->GetState()==EWowSessionState::WorldInGame)
    {
        int32 Sent=0; for(const auto& Pair:B.Players)
        {const uint64 Guid=Pair.Key; if(Name(C,Guid).IsEmpty() && !V.Requested.Contains(Guid)) {V.Requested.Add(Guid); C->ConnectionManager->SendNameQuery(Guid); if(++Sent>=16) break;}}
    }
    if(V.Revision!=B.Revision || V.Names!=uint32(H->PlayerNameCache.Num()))
    {V.Revision=B.Revision; V.Names=H->PlayerNameCache.Num(); C->EventSystem->FireEvent(TEXT("UPDATE_LFG_LIST"));}
}
#else
void WowLuaApi::RegisterRaidBrowserApi(lua_State*) {}
void WowLuaApi::UpdateRaidBrowserState(lua_State*) {}
#endif
