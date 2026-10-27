#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"

struct FWowClassUiView
{
    uint64 Guid=0,PlayerRevision=MAX_uint64,PetRevision=MAX_uint64;
    uint8 Class=0,Race=0;
    uint16 Family=0;
    bool Deferred=false;
    TArray<uint32> Tabs,PetTabs;
    TMap<uint32,TArray<uint32>> Nodes;
    TMap<uint32,int32> Preview[3]; // Two player groups and one pet group; absolute planned ranks.
};

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
FWowLuaContext* Context(lua_State* L) { return WowLuaApi::GetContext(L); }
bool Flag(lua_State* L,int32 I)
{
    if(lua_isnoneornil(L,I)) return false;
    if(lua_isnumber(L,I)) return lua_tonumber(L,I)!=0;
    if(lua_isstring(L,I)) { const FString V=UTF8_TO_TCHAR(lua_tostring(L,I)); return !V.IsEmpty() && V!=TEXT("0") && !V.Equals(TEXT("false"),ESearchCase::IgnoreCase); }
    return lua_toboolean(L,I)!=0;
}
void PushFlag(lua_State* L,bool Value) { if(Value) lua_pushinteger(L,1); else lua_pushnil(L); }
FWowClassUiView* View(FWowLuaContext* C)
{
    if(!C || !C->ConnectionManager || !C->EntityManager) return nullptr;
    const auto* P=C->EntityManager->GetLocalPlayer(); if(!P) return nullptr;
    auto& H=C->ConnectionManager->PacketHandler;
    const bool Deferred=FDbcStore::Get().IsDeferredLoaded();
    if(!C->ClassUi || C->ClassUi->Guid!=P->Guid || C->ClassUi->Class!=P->GetClassId() || C->ClassUi->Race!=P->GetRaceId() || C->ClassUi->Family!=H.PetActionBar.Family || C->ClassUi->Deferred!=Deferred)
    {
        C->ClassUi=MakeShared<FWowClassUiView>(); auto& V=*C->ClassUi;
        V.Guid=P->Guid; V.Class=P->GetClassId(); V.Race=P->GetRaceId(); V.Family=H.PetActionBar.Family; V.Deferred=Deferred;
        const uint32 ClassBit=V.Class>0 && V.Class<=32?1u<<(V.Class-1):0,RaceBit=V.Race>0 && V.Race<=32?1u<<(V.Race-1):0;
        const auto& D=FDbcStore::Get(); const auto* Family=D.TalentTabs().PetFamily(V.Family);
        for(const auto& Tab:D.TalentTabs().GetAll())
        {
            if(Tab.RaceMask?((Tab.RaceMask&RaceBit)!=0 && (!Tab.ClassMask || (Tab.ClassMask&ClassBit)!=0)):(Tab.ClassMask&ClassBit)!=0) V.Tabs.Add(Tab.TalentTabID);
            if(Family && Family->TalentType>=0 && Family->TalentType<32 && (Tab.PetTalentMask&(1u<<Family->TalentType))) V.PetTabs.Add(Tab.TalentTabID);
        }
        // Preserve DBC record order, which defines the stock talent indices.
        for(const auto& Node:D.Talents().GetAll()) if(V.Tabs.Contains(Node.TalentTab) || V.PetTabs.Contains(Node.TalentTab)) V.Nodes.FindOrAdd(Node.TalentTab).Add(Node.TalentID);
    }
    auto& V=*C->ClassUi;
    if(V.PlayerRevision!=H.PlayerTalentRevision) { V.Preview[0].Reset(); V.Preview[1].Reset(); V.PlayerRevision=H.PlayerTalentRevision; }
    if(V.PetRevision!=H.PetTalentRevision) { V.Preview[2].Reset(); V.PetRevision=H.PetTalentRevision; }
    return &V;
}
int32 Active(FWowLuaContext* C,bool Pet) { return !Pet && C && C->ConnectionManager?C->ConnectionManager->PacketHandler.ActiveTalentGroup:0; }
int32 Group(lua_State* L,FWowLuaContext* C,int32 I,bool Pet) { return lua_isnoneornil(L,I)?Active(C,Pet):int32(luaL_checkinteger(L,I))-1; }
int32 Slot(bool Pet,int32 G) { return Pet?(G==0?2:INDEX_NONE):(G>=0 && G<2?G:INDEX_NONE); }
uint32 TabId(FWowLuaContext* C,int32 I,bool Pet)
{
    const auto* V=View(C); if(!V) return 0; const auto& Tabs=Pet?V->PetTabs:V->Tabs;
    return Tabs.IsValidIndex(I)?Tabs[I]:0;
}
const FTalentDbcEntry* NodeAt(FWowLuaContext* C,int32 Tab,int32 Index,bool Pet)
{
    const uint32 Id=TabId(C,Tab,Pet); const auto* V=View(C); const auto* Nodes=V?V->Nodes.Find(Id):nullptr;
    return Nodes && Nodes->IsValidIndex(Index)?FDbcStore::Get().Talents().GetById((*Nodes)[Index]):nullptr;
}
const TArray<FWowTalentInfo>* Ranks(FWowLuaContext* C,bool Pet,int32 G)
{
    if(!C || !C->ConnectionManager) return nullptr; const auto& H=C->ConnectionManager->PacketHandler;
    if(Pet) return G==0?&H.PetTalents:nullptr;
    return H.TalentGroups.IsValidIndex(G)?&H.TalentGroups[G].Talents:nullptr;
}
int32 Rank(FWowLuaContext* C,uint32 Id,bool Pet,int32 G)
{
    if(const auto* List=Ranks(C,Pet,G)) if(const auto* T=List->FindByPredicate([&](const auto& N){return N.TalentId==Id;})) return T->Rank+1;
    return 0;
}
int32 Planned(FWowLuaContext* C,uint32 Id,bool Pet,int32 G)
{
    const int32 Base=Rank(C,Id,Pet,G),Key=Slot(Pet,G); const auto* V=View(C);
    return V && Key>=0?FMath::Max(Base,V->Preview[Key].FindRef(Id)):Base;
}
int32 MaxRank(const FTalentDbcEntry& N) { int32 Max=0; for(int32 I=0;I<UE_ARRAY_COUNT(N.RankID);++I) if(N.RankID[I]) Max=I+1; return Max; }
int32 Total(FWowLuaContext* C,bool Pet,int32 G,bool Preview)
{
    int32 Sum=0; const auto* V=View(C); if(!V) return 0;
    if(!Preview) { if(const auto* List=Ranks(C,Pet,G)) for(const auto& N:*List) Sum+=N.Rank+1; return Sum; }
    for(const uint32 Id:Pet?V->PetTabs:V->Tabs) if(const auto* Nodes=V->Nodes.Find(Id)) for(const uint32 Node:*Nodes) Sum+=Planned(C,Node,Pet,G);
    return Sum;
}
int32 TreeSpent(FWowLuaContext* C,uint32 Id,bool Pet,int32 G,bool Preview,int32 BeforeRow=MAX_int32)
{
    int32 Sum=0; const auto* V=View(C); const auto* Nodes=V?V->Nodes.Find(Id):nullptr; if(!Nodes) return 0;
    for(const uint32 Node:*Nodes) if(const auto* N=FDbcStore::Get().Talents().GetById(Node); N && int32(N->Row)<BeforeRow) Sum+=Preview?Planned(C,Node,Pet,G):Rank(C,Node,Pet,G);
    return Sum;
}
int32 Budget(FWowLuaContext* C,bool Pet)
{
    if(!C || !C->ConnectionManager) return 0; const auto& H=C->ConnectionManager->PacketHandler;
    return int32(FMath::Min<int64>(MAX_int32,int64(Pet?H.PetFreeTalentPoints:H.FreeTalentPoints)+Total(C,Pet,Active(C,Pet),false)));
}
int32 FreePoints(FWowLuaContext* C,bool Pet,int32 G)
{
    if(Slot(Pet,G)<0 || !Ranks(C,Pet,G)) return 0;
    return FMath::Max(0,Budget(C,Pet)-Total(C,Pet,G,false));
}
bool PetAvailable(FWowLuaContext* C,const FTalentDbcEntry& N)
{
    const auto* V=View(C); const auto* F=V?FDbcStore::Get().TalentTabs().PetFamily(V->Family):nullptr;
    if(!F || !C->ConnectionManager->PacketHandler.PetActionBar.PetGuid) return false;
    if((!N.CategoryMask[0] && !N.CategoryMask[1]) || F->Category<0) return true;
    return F->Category<64 && (N.CategoryMask[F->Category/32]&(1u<<(F->Category%32)))!=0;
}
bool Available(FWowLuaContext* C,const FTalentDbcEntry& N,bool Pet)
{
    return C && C->ConnectionManager && (!Pet || PetAvailable(C,N)) && (!N.RequiredSpell || C->ConnectionManager->PacketHandler.KnownSpells.Contains(N.RequiredSpell));
}
bool Prerequisites(FWowLuaContext* C,const FTalentDbcEntry& N,bool Pet,int32 G,bool Preview)
{
    for(int32 I=0;I<3;++I) if(N.Prerequisite[I] && (Preview?Planned(C,N.Prerequisite[I],Pet,G):Rank(C,N.Prerequisite[I],Pet,G))<int32(N.PrerequisiteRank[I])+1) return false;
    return true;
}
bool ValidPlan(FWowLuaContext* C,bool Pet,int32 G)
{
    const auto* V=View(C); if(!V || Total(C,Pet,G,true)>Budget(C,Pet)) return false;
    for(const uint32 Id:Pet?V->PetTabs:V->Tabs) if(const auto* Nodes=V->Nodes.Find(Id)) for(const uint32 Node:*Nodes)
    {
        const auto* N=FDbcStore::Get().Talents().GetById(Node); if(!N) continue; const int32 P=Planned(C,Node,Pet,G); if(!P) continue;
        if(P>MaxRank(*N) || (!Available(C,*N,Pet) && P>Rank(C,Node,Pet,G)) || !Prerequisites(C,*N,Pet,G,true) || TreeSpent(C,Id,Pet,G,true,N->Row)<int32(N->Row)*(Pet?3:5)) return false;
    }
    return true;
}
void PreviewEvent(FWowLuaContext* C,bool Pet,const TArray<double>& Args={})
{
    if(C && C->EventSystem) C->EventSystem->FireEventNumbers(Pet?TEXT("PREVIEW_PET_TALENT_POINTS_CHANGED"):TEXT("PREVIEW_TALENT_POINTS_CHANGED"),Args);
}
void Register(lua_State* L,const char* Name,lua_CFunction F) { lua_pushcfunction(L,F); lua_setglobal(L,Name); }
int32 GroupCount(FWowLuaContext* C,bool Pet)
{
    const auto* V=View(C); if(!V) return 0;
    return Pet?(C->ConnectionManager->PacketHandler.PetActionBar.PetGuid && !V->PetTabs.IsEmpty()?1:0):C->ConnectionManager->PacketHandler.TalentGroups.Num();
}
}

int WowLuaApi::SetTalentTooltip(lua_State* L)
{
    if(Flag(L,4)) return SetInspectTalentTooltip(L);
    auto* C=Context(L); if(!C || !C->FrameManager) return 0;
    const bool Pet=Flag(L,5); const int32 G=Group(L,C,6,Pet);
    const auto* N=NodeAt(C,luaL_checkinteger(L,2)-1,luaL_checkinteger(L,3)-1,Pet);
    if(!N || !MaxRank(*N) || Flag(L,4)) return 0;
    const bool Preview=Flag(L,7); const int32 Actual=Rank(C,N->TalentID,Pet,G);
    const int32 R=Preview?Planned(C,N->TalentID,Pet,G):Actual,Max=MaxRank(*N);
    const auto& Spells=FDbcStore::Get().Spells(); const auto* Spell=Spells.GetById(N->RankID[FMath::Clamp(R-1,0,Max-1)]);
    if(!Spell) return 0;
    lua_getfield(L,1,"__handle"); const int64 Handle=lua_tointeger(L,-1); lua_pop(L,1);
    const auto* P=C->EntityManager->GetLocalPlayer(); const int32 Level=P?P->GetLevel():1;
    auto* Frames=C->FrameManager;
    if(N->Flags&1) Frames->TooltipSetSpell(Handle,Spell->ID,Level);
    else Frames->TooltipClearLines(Handle);
    const auto Text=[&](const char* Key,const FString& Fallback,const TArray<FString>& Args=TArray<FString>())
    {
        lua_getglobal(L,Key); const FString Pattern=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):Fallback; lua_pop(L,1);
        if(Args.IsEmpty()) return Pattern;
        lua_getglobal(L,"string"); lua_getfield(L,-1,"format"); lua_remove(L,-2); lua_pushstring(L,TCHAR_TO_UTF8(*Pattern));
        for(const auto& A:Args) lua_pushstring(L,TCHAR_TO_UTF8(*A));
        if(lua_pcall(L,Args.Num()+1,1,0)!=0) { lua_pop(L,1); return Fallback; }
        const FString Result=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):Fallback; lua_pop(L,1); return Result;
    };
    const auto Add=[&](const FString& T,float Red=1.f,float Green=1.f,float Blue=1.f,bool Wrap=false) { if(!T.IsEmpty()) Frames->TooltipAddLine(Handle,T,Red,Green,Blue,Wrap); };
    if(!(N->Flags&1))
    {
        auto* State=Frames->GetTooltipState(Handle); State->SpellId=R?Spell->ID:0; State->ItemLink.Empty(); State->UnitToken.Empty();
        Add(Spell->SpellName); Add(Text("TOOLTIP_TALENT_RANK",FString::Printf(TEXT("Rank %d/%d"),R,Max),{FString::FromInt(R),FString::FromInt(Max)}));
    }
    const bool Ready=Available(C,*N,Pet) && Prerequisites(C,*N,Pet,G,Preview) && TreeSpent(C,N->TalentTab,Pet,G,Preview)>=int32(N->Row)*(Pet?3:5);
    if(!R)
    {
        for(int32 I=0;I<3;++I) if(const auto* Required=FDbcStore::Get().Talents().GetById(N->Prerequisite[I]); Required && (Preview?Planned(C,Required->TalentID,Pet,G):Rank(C,Required->TalentID,Pet,G))<int32(N->PrerequisiteRank[I])+1)
            if(const auto* S=Spells.GetById(Required->RankID[0])) Add(Text("TOOLTIP_TALENT_PREREQ",TEXT("Requires prerequisite talent"),{FString::FromInt(N->PrerequisiteRank[I]+1),S->SpellName}),1,.1f,.1f,false);
        const int32 TierCost=N->Row*(Pet?3:5);
        if(TreeSpent(C,N->TalentTab,Pet,G,Preview)<TierCost) if(const auto* Tab=FDbcStore::Get().TalentTabs().GetById(N->TalentTab)) Add(Text("TOOLTIP_TALENT_TIER_POINTS",TEXT("Requires more points in this tree"),{FString::FromInt(TierCost),Tab->Name}),1,.1f,.1f,false);
        if(N->RequiredSpell && !Available(C,*N,Pet)) if(const auto* Required=Spells.GetById(N->RequiredSpell)) Add(Text("ITEM_REQ_SKILL",TEXT("Requires a class ability"),{Required->SpellName}),1,.1f,.1f,false);
    }
    if(!(N->Flags&1) && R>0) Add(Spells.Description(Spell->ID,Level),1,.82f,0,true);
    if(R<Max)
    {
        if(R>0) { Add(TEXT(" ")); Add(Text("TOOLTIP_TALENT_NEXT_RANK",TEXT("Next rank:"))); }
        Add(Spells.Description(N->RankID[R],Level),1,.82f,0,true);
    }
    if(G==Active(C,Pet) && Ready)
    {
        const int32 Unspent=Budget(C,Pet)-Total(C,Pet,G,Preview);
        if(R<Max && Unspent>0) Add(Text(Preview?"TALENT_TOOLTIP_ADDPREVIEWPOINT":"TOOLTIP_TALENT_LEARN",Preview?TEXT("Click to add a point"):TEXT("Click to learn")),.1f,1,.1f,false);
        if(Preview && R>Actual) Add(Text("TALENT_TOOLTIP_REMOVEPREVIEWPOINT",TEXT("Right-click to remove a point")),1,.1f,.1f,false);
    }
    Frames->SetFrameVisible(Handle,true);
    return 0;
}
void WowLuaApi::RegisterClassApi(lua_State* L)
{
    Register(L,"GetNumTalentTabs",[](lua_State* S)->int { const auto* V=View(Context(S)); lua_pushinteger(S,Flag(S,1)||!V?0:Flag(S,2)?V->PetTabs.Num():V->Tabs.Num()); return 1; });
    Register(L,"GetNumTalents",[](lua_State* S)->int { auto* C=Context(S); const auto* V=View(C); const uint32 Id=TabId(C,luaL_checkinteger(S,1)-1,Flag(S,3)); const auto* List=V?V->Nodes.Find(Id):nullptr; lua_pushinteger(S,Flag(S,2)||!List?0:List->Num()); return 1; });
    Register(L,"GetNumTalentGroups",[](lua_State* S)->int { lua_pushinteger(S,Flag(S,1)?0:GroupCount(Context(S),Flag(S,2))); return 1; });
    Register(L,"GetActiveTalentGroup",[](lua_State* S)->int { lua_pushinteger(S,Active(Context(S),Flag(S,2))+1); return 1; });
    Register(L,"GetUnspentTalentPoints",[](lua_State* S)->int { auto* C=Context(S); const bool Pet=Flag(S,2); lua_pushinteger(S,Flag(S,1)?0:FreePoints(C,Pet,Group(S,C,3,Pet))); return 1; });
    Register(L,"GetTalentTabInfo",[](lua_State* S)->int {
        auto* C=Context(S); const bool Pet=Flag(S,3); const int32 G=Group(S,C,4,Pet); const auto* T=Flag(S,2)?nullptr:FDbcStore::Get().TalentTabs().GetById(TabId(C,luaL_checkinteger(S,1)-1,Pet));
        if(!T) { lua_pushnil(S); lua_pushnil(S); lua_pushinteger(S,0); lua_pushnil(S); lua_pushinteger(S,0); return 5; }
        lua_pushstring(S,TCHAR_TO_UTF8(*T->Name)); const auto* Icon=FDbcStore::Get().SpellIcons().GetById(T->SpellIcon); if(Icon) lua_pushstring(S,TCHAR_TO_UTF8(*Icon->TexturePath)); else lua_pushnil(S);
        const int32 Spent=TreeSpent(C,T->TalentTabID,Pet,G,false); lua_pushinteger(S,Spent); lua_pushstring(S,TCHAR_TO_UTF8(*T->InternalName)); lua_pushinteger(S,TreeSpent(C,T->TalentTabID,Pet,G,true)-Spent); return 5;
    });
    Register(L,"GetTalentInfo",[](lua_State* S)->int {
        auto* C=Context(S); const bool Pet=Flag(S,4); const int32 G=Group(S,C,5,Pet); const auto* N=Flag(S,3)?nullptr:NodeAt(C,luaL_checkinteger(S,1)-1,luaL_checkinteger(S,2)-1,Pet);
        if(!N || (Pet && !PetAvailable(C,*N)) || !MaxRank(*N)) { lua_pushnil(S); return 1; }
        const int32 R=Rank(C,N->TalentID,Pet,G); const auto* Spell=FDbcStore::Get().Spells().GetById(N->RankID[FMath::Clamp(R-1,0,MaxRank(*N)-1)]); if(!Spell) { lua_pushnil(S); return 1; }
        lua_pushstring(S,TCHAR_TO_UTF8(*Spell->SpellName)); const auto* Icon=FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID); if(Icon) lua_pushstring(S,TCHAR_TO_UTF8(*Icon->TexturePath)); else lua_pushnil(S);
        lua_pushinteger(S,N->Row+1); lua_pushinteger(S,N->Col+1); lua_pushinteger(S,R); lua_pushinteger(S,MaxRank(*N)); PushFlag(S,(N->Flags&1)!=0); PushFlag(S,Available(C,*N,Pet)); lua_pushinteger(S,Planned(C,N->TalentID,Pet,G)); PushFlag(S,Available(C,*N,Pet)); return 10;
    });
    Register(L,"GetTalentPrereqs",[](lua_State* S)->int {
        auto* C=Context(S); const bool Pet=Flag(S,4); const int32 G=Group(S,C,5,Pet); const auto* N=Flag(S,3)?nullptr:NodeAt(C,luaL_checkinteger(S,1)-1,luaL_checkinteger(S,2)-1,Pet); if(!N) return 0; int32 Count=0;
        for(int32 I=0;I<3;++I) if(const auto* Required=FDbcStore::Get().Talents().GetById(N->Prerequisite[I])) { lua_pushinteger(S,Required->Row+1); lua_pushinteger(S,Required->Col+1); PushFlag(S,Rank(C,Required->TalentID,Pet,G)>int32(N->PrerequisiteRank[I])); PushFlag(S,Planned(C,Required->TalentID,Pet,G)>int32(N->PrerequisiteRank[I])); Count+=4; } return Count;
    });
    Register(L,"GetTalentLink",[](lua_State* S)->int {
        auto* C=Context(S); const bool Pet=Flag(S,4); const int32 G=Group(S,C,5,Pet); const auto* N=Flag(S,3)?nullptr:NodeAt(C,luaL_checkinteger(S,1)-1,luaL_checkinteger(S,2)-1,Pet); const auto* Spell=N?FDbcStore::Get().Spells().GetById(N->RankID[0]):nullptr;
        if(!N || !Spell) { lua_pushnil(S); return 1; } const FString Link=FString::Printf(TEXT("|cff4e96f7|Htalent:%u:%d|h[%s]|h|r"),N->TalentID,(Flag(S,6)?Planned(C,N->TalentID,Pet,G):Rank(C,N->TalentID,Pet,G))-1,*Spell->SpellName); lua_pushstring(S,TCHAR_TO_UTF8(*Link)); return 1;
    });
    Register(L,"LearnTalent",[](lua_State* S)->int {
        auto* C=Context(S); const bool Pet=Flag(S,3); const int32 G=Active(C,Pet); const auto* N=NodeAt(C,luaL_checkinteger(S,1)-1,luaL_checkinteger(S,2)-1,Pet);
        if(!N || !C || !C->ConnectionManager || !Available(C,*N,Pet)) return 0; const int32 R=Rank(C,N->TalentID,Pet,G);
        if(R<MaxRank(*N) && FreePoints(C,Pet,G)>0 && Prerequisites(C,*N,Pet,G,false) && TreeSpent(C,N->TalentTab,Pet,G,false,N->Row)>=int32(N->Row)*(Pet?3:5)) { if(Pet) C->ConnectionManager->SendLearnPetTalent(N->TalentID,R); else C->ConnectionManager->SendLearnTalent(N->TalentID,R); } return 0;
    });
    Register(L,"SetActiveTalentGroup",[](lua_State* S)->int {
        auto* C=Context(S); const int32 G=luaL_checkinteger(S,1)-1; if(C && C->ConnectionManager && G>=0 && G<GroupCount(C,false) && G<2 && G!=Active(C,false)) C->ConnectionManager->SendCastSpell(G==0?63645:63644,C->EntityManager->LocalPlayerGuid); return 0;
    });
    Register(L,"GetPreviewTalentPointsSpent",[](lua_State* S)->int { auto* C=Context(S); const bool Pet=Flag(S,2); const int32 G=Group(S,C,3,Pet); const uint32 Id=TabId(C,luaL_checkinteger(S,1)-1,Pet); lua_pushinteger(S,TreeSpent(C,Id,Pet,G,true)-TreeSpent(C,Id,Pet,G,false)); return 1; });
    Register(L,"GetGroupPreviewTalentPointsSpent",[](lua_State* S)->int { auto* C=Context(S); const bool Pet=Flag(S,1); const int32 G=Group(S,C,2,Pet); lua_pushinteger(S,Total(C,Pet,G,true)-Total(C,Pet,G,false)); return 1; });
    Register(L,"AddPreviewTalentPoints",[](lua_State* S)->int {
        auto* C=Context(S); const bool Pet=Flag(S,4); const int32 G=Group(S,C,5,Pet),Key=Slot(Pet,G),Tab=luaL_checkinteger(S,1)-1,Index=luaL_checkinteger(S,2)-1; const auto* N=NodeAt(C,Tab,Index,Pet); auto* V=View(C);
        if(!N || !V || Key<0 || !Ranks(C,Pet,G)) return 0; const int32 Base=Rank(C,N->TalentID,Pet,G),Before=Planned(C,N->TalentID,Pet,G),Delta=FMath::Clamp(int32(luaL_checkinteger(S,3)),-9,9);
        int32 Target=FMath::Clamp(Before+Delta,Base,MaxRank(*N)); if(Target>Before) Target=FMath::Min(Target,Before+FMath::Max(0,Budget(C,Pet)-Total(C,Pet,G,true))); if(Target==Before) return 0;
        V->Preview[Key].Add(N->TalentID,Target);
        if(!ValidPlan(C,Pet,G)) { if(Before>Base) V->Preview[Key].Add(N->TalentID,Before); else V->Preview[Key].Remove(N->TalentID); return 0; }
        if(Target==Base) V->Preview[Key].Remove(N->TalentID); PreviewEvent(C,Pet,{double(Index+1),double(Tab+1),double(G+1),double(Target-Before)}); return 0;
    });
    Register(L,"ResetPreviewTalentPoints",[](lua_State* S)->int {
        auto* C=Context(S); const bool Pet=Flag(S,2); const int32 G=Group(S,C,3,Pet),Key=Slot(Pet,G); const uint32 Id=TabId(C,luaL_checkinteger(S,1)-1,Pet); auto* V=View(C); if(!V || Key<0) return 0;
        bool Changed=false; if(const auto* Nodes=V->Nodes.Find(Id)) for(const uint32 N:*Nodes) Changed|=V->Preview[Key].Remove(N)>0; if(Changed) PreviewEvent(C,Pet); return 0;
    });
    Register(L,"ResetGroupPreviewTalentPoints",[](lua_State* S)->int {
        auto* C=Context(S); const bool Pet=Flag(S,1); const int32 G=Group(S,C,2,Pet),Key=Slot(Pet,G); auto* V=View(C); if(!V || Key<0 || V->Preview[Key].IsEmpty()) return 0; V->Preview[Key].Reset(); PreviewEvent(C,Pet); return 0;
    });
    Register(L,"LearnPreviewTalents",[](lua_State* S)->int {
        auto* C=Context(S); const bool Pet=Flag(S,1); const int32 G=Active(C,Pet),Key=Slot(Pet,G); const auto* V=View(C); if(!V || Key<0 || !ValidPlan(C,Pet,G)) return 0;
        TArray<const FTalentDbcEntry*> Pending; for(const auto& Pair:V->Preview[Key]) if(Pair.Value>Rank(C,Pair.Key,Pet,G)) if(const auto* N=FDbcStore::Get().Talents().GetById(Pair.Key)) Pending.Add(N);
        Pending.Sort([](const auto& A,const auto& B) { return A.Row==B.Row?A.Col==B.Col?A.TalentID<B.TalentID:A.Col<B.Col:A.Row<B.Row; });
        TArray<FWowTalentInfo> Requests; TSet<uint32> Added,Visiting; bool Valid=true;
        TFunction<void(const FTalentDbcEntry*)> Add=[&](const auto* N) { if(Added.Contains(N->TalentID)) return; if(Visiting.Contains(N->TalentID)) { Valid=false; return; } Visiting.Add(N->TalentID); for(const uint32 Id:N->Prerequisite) if(V->Preview[Key].Contains(Id)) if(const auto* P=FDbcStore::Get().Talents().GetById(Id)) Add(P); Visiting.Remove(N->TalentID); Added.Add(N->TalentID); const int32 R=Planned(C,N->TalentID,Pet,G); if(R>Rank(C,N->TalentID,Pet,G)) Requests.Add({N->TalentID,uint8(R-1)}); };
        for(const auto* N:Pending) Add(N); if(Valid && !Requests.IsEmpty()) C->ConnectionManager->SendLearnPreviewTalents(Requests,Pet); return 0;
    });
}
#else
void WowLuaApi::RegisterClassApi(lua_State*) {}
int WowLuaApi::SetTalentTooltip(lua_State*) { return 0; }
#endif
