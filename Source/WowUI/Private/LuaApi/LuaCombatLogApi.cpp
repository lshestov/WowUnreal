#include "WowCombatLogUi.h"
#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"

struct FWowCombatLogView
{
    struct FSelector { bool HasMask=false; uint32 Mask=0; FString Guid; };
    struct FFilter { bool HasEvents=false; TSet<FString> Events; FSelector Source,Destination; };
    TArray<FFilter> Filters;
    TArray<uint64> Selected;
    uint64 Cursor=0,End=0,First=0,Target=0;
    uint32 Group=0;
    bool Dirty=true;
    TOptional<FWowCombatEntry> Active;
};
#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void Register(lua_State* L,const char* Name,lua_CFunction Fn) { lua_pushcfunction(L,Fn); lua_setglobal(L,Name); }
FWowCombatHistory* History(lua_State* L) { auto* C=WowLuaApi::GetContext(L); return C && C->ConnectionManager?&C->ConnectionManager->PacketHandler.CombatHistory:nullptr; }
FWowCombatLogView* View(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C) return nullptr;
    if(!C->CombatLog) C->CombatLog=MakeShared<FWowCombatLogView>(); return C->CombatLog.Get();
}
FString GuidString(uint64 Guid) { return FString::Printf(TEXT("0x%016llX"),Guid); }
bool ObjectIsA(uint32 Flags,uint32 Mask)
{
    if(!Mask) return false;
    if(Flags&0x80000000u) return (Mask&0x80000000u)!=0;
    if(Mask==0x80000000u) return false;
    for(uint32 Category:{0xfu,0xf0u,0x300u,0xfc00u}) if(!(Flags&Mask&Category)) return false;
    const uint32 Special=Mask&0x7fff0000u;
    return !Special || Special==0x7fff0000u || (Flags&Special)!=0;
}
bool Selector(FWowLuaContext* C,const FWowCombatLogView::FSelector& S,uint64 Guid)
{
    return S.HasMask?ObjectIsA(WowLuaApi::CombatLogFlags(C,Guid),S.Mask):S.Guid.IsEmpty() || S.Guid.Equals(GuidString(Guid),ESearchCase::IgnoreCase);
}
bool Matches(lua_State* L,const FWowCombatEntry& E)
{
    auto* V=View(L); if(!V || V->Filters.IsEmpty()) return true;
    auto* C=WowLuaApi::GetContext(L);
    for(const auto& F:V->Filters) if((!F.HasEvents || F.Events.Contains(E.Event)) && Selector(C,F.Source,E.Source) && Selector(C,F.Destination,E.Destination)) return true;
    return false;
}
void Refresh(lua_State* L)
{
    auto* V=View(L); auto* H=History(L); if(!V || !H) return;
    auto* C=WowLuaApi::GetContext(L); const uint64 Target=C->ConnectionManager->GetTargetGuid();
    const auto& Group=C->ConnectionManager->PacketHandler.GroupInfo; uint32 GroupKey=Group.GroupType;
    for(const auto& Member:Group.Members) GroupKey=HashCombine(GroupKey,GetTypeHash(Member.Guid));
    if(!V->Dirty && V->End==H->End() && V->First==H->First() && V->Target==Target && V->Group==GroupKey) return;
    V->Selected.Reset();
    for(uint64 I=H->First();I<H->End();++I) if(const auto* E=H->Find(I); E && Matches(L,*E)) V->Selected.Add(I);
    V->Dirty=false; V->End=H->End(); V->First=H->First(); V->Target=Target; V->Group=GroupKey;
}
void PushName(lua_State* L,uint64 Guid)
{
    auto* C=WowLuaApi::GetContext(L); const FString* Name=nullptr;
    if(C && C->ConnectionManager && Guid)
    {
        auto& H=C->ConnectionManager->PacketHandler;
        Name=H.PlayerNameCache.Find(Guid);
        if(!Name) { const auto* E=H.EntityManager.Find(Guid); Name=H.CreatureNameCache.Find(E?E->Entry:uint32((Guid>>24)&0xffffff)); }
    }
    if(Name && !Name->IsEmpty()) lua_pushstring(L,TCHAR_TO_UTF8(**Name)); else lua_pushnil(L);
}
int Push(lua_State* L,const FWowCombatEntry* E)
{
    if(!E) { lua_pushnil(L); return 1; }
    auto* C=WowLuaApi::GetContext(L);
    lua_pushnumber(L,E->Timestamp); lua_pushstring(L,TCHAR_TO_UTF8(*E->Event));
    for(uint64 Guid:{E->Source,E->Destination})
    { if(Guid) lua_pushstring(L,TCHAR_TO_UTF8(*GuidString(Guid))); else lua_pushnil(L); PushName(L,Guid); lua_pushnumber(L,WowLuaApi::CombatLogFlags(C,Guid)); }
    int Count=8;
    if(E->Spell)
    {
        const auto* Spell=FDbcStore::Get().Spells().GetById(E->Spell);
        lua_pushinteger(L,E->Spell); if(Spell) lua_pushstring(L,TCHAR_TO_UTF8(*Spell->SpellName)); else lua_pushnil(L); lua_pushinteger(L,E->School); Count+=3;
    }
    for(const auto& V:E->Values)
    { if(V.Kind==FWowCombatValue::EKind::Boolean) lua_pushboolean(L,V.Number!=0); else if(V.Kind==FWowCombatValue::EKind::String) lua_pushstring(L,TCHAR_TO_UTF8(*V.String)); else lua_pushnumber(L,V.Number); ++Count; }
    return Count;
}
FWowCombatLogView::FSelector ReadSelector(lua_State* L,int I)
{
    FWowCombatLogView::FSelector S;
    if(lua_type(L,I)==LUA_TNUMBER) { S.HasMask=true; S.Mask=uint32(lua_tonumber(L,I)); }
    else if(lua_isstring(L,I)) S.Guid=UTF8_TO_TCHAR(lua_tostring(L,I));
    return S;
}
}
void FWowCombatLogUi::RegisterApi(lua_State* L)
{
    Register(L,"CombatLog_Object_IsA",[](lua_State* S)->int { lua_pushboolean(S,ObjectIsA(uint32(luaL_checknumber(S,1)),uint32(luaL_checknumber(S,2)))); return 1; });
    Register(L,"CombatLogGetNumEntries",[](lua_State* S)->int { Refresh(S); const auto* V=View(S); const auto* H=History(S); lua_pushinteger(S,lua_toboolean(S,1)?(H?H->Num():0):(V?V->Selected.Num():0)); return 1; });
    Register(L,"CombatLogResetFilter",[](lua_State* S)->int { if(auto* V=View(S)) { V->Filters.Reset(); V->Dirty=true; } return 0; });
    Register(L,"CombatLogAddFilter",[](lua_State* S)->int {
        if(auto* V=View(S))
        {
            FWowCombatLogView::FFilter F; F.Source=ReadSelector(S,2); F.Destination=ReadSelector(S,3);
            if(lua_isstring(S,1)) { F.HasEvents=true; TArray<FString> Names; FString(UTF8_TO_TCHAR(lua_tostring(S,1))).ParseIntoArray(Names,TEXT(","),true); for(auto& Name:Names) F.Events.Add(Name.TrimStartAndEnd()); }
            V->Filters.Add(MoveTemp(F)); V->Dirty=true;
        } return 0;
    });
    Register(L,"CombatLogSetCurrentEntry",[](lua_State* S)->int {
        Refresh(S); auto* V=View(S); const int I=luaL_optinteger(S,1,0);
        if(V) V->Cursor=V->Selected.IsValidIndex(I==0?V->Selected.Num()-1:I-1)?V->Selected[I==0?V->Selected.Num()-1:I-1]:0;
        return 0;
    });
    Register(L,"CombatLogGetCurrentEntry",[](lua_State* S)->int { Refresh(S); const auto* V=View(S); const auto* H=History(S); return Push(S,V && H && V->Selected.Contains(V->Cursor)?H->Find(V->Cursor):nullptr); });
    Register(L,"CombatLogAdvanceEntry",[](lua_State* S)->int {
        Refresh(S); auto* V=View(S); bool Valid=false;
        if(V) { const int I=V->Selected.IndexOfByKey(V->Cursor); const int64 Next=int64(I)+luaL_optinteger(S,1,1); Valid=I>=0 && Next>=0 && Next<V->Selected.Num(); V->Cursor=Valid?V->Selected[Next]:0; }
        if(Valid) lua_pushboolean(S,true); else lua_pushnil(S); return 1;
    });
    Register(L,"CombatLogGetCurrentEventInfo",[](lua_State* S)->int { const auto* V=View(S); const auto* H=History(S); return Push(S,V && V->Active.IsSet()?&V->Active.GetValue():H?H->Find(H->End()-1):nullptr); });
    Register(L,"CombatLogClearEntries",[](lua_State* S)->int { if(auto* H=History(S)) H->Reset(); if(auto* V=View(S)) { V->Dirty=true; V->Cursor=0; } auto* C=WowLuaApi::GetContext(S); if(C && C->EventSystem) C->EventSystem->FireEvent(TEXT("COMBAT_LOG_CLEARED")); return 0; });
}
void FWowCombatLogUi::Dispatch(lua_State* L,const FWowCombatEntry& Entry)
{
    auto* C=WowLuaApi::GetContext(L); auto* V=View(L); if(!C || !C->EventSystem || !V) return;
    const auto Previous=V->Active; V->Active=Entry;
    C->EventSystem->FireEventTyped(TEXT("COMBAT_LOG_EVENT_UNFILTERED"),[&](lua_State* S) { return Push(S,&Entry); });
    if(Matches(L,Entry)) C->EventSystem->FireEventTyped(TEXT("COMBAT_LOG_EVENT"),[&](lua_State* S) { return Push(S,&Entry); });
    V->Active=Previous;
}
#else
void FWowCombatLogUi::RegisterApi(lua_State*) {}
void FWowCombatLogUi::Dispatch(lua_State*,const FWowCombatEntry&) {}
#endif
