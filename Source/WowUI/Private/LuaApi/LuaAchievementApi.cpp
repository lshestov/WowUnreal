#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "WowFrameManager.h"
#include "WowSavedVariables.h"
#include "WowAddonLoader.h"
#include "Formats/Dbc/DbcStore.h"

struct FWowAchievementUiView
{
    uint32 Revision=0,Data=0,Comparison=0; uint64 Player=0; bool Loaded=false;
    TArray<uint32> Tracked;
};
#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
using FAch=FAchievementUiDbc::FAchievement;
using FCrit=FAchievementUiDbc::FCriteria;
using FSnapshot=FWowAchievementState::FSnapshot;
const FAchievementUiDbc& Data() {return FDbcStore::Get().Achievements();}
FWowPacketHandler* Handler(FWowLuaContext* C) {return C && C->ConnectionManager?&C->ConnectionManager->PacketHandler:nullptr;}
FWowAchievementUiView& View(FWowLuaContext* C) {if(!C->AchievementUi) C->AchievementUi=MakeShared<FWowAchievementUiView>(); return *C->AchievementUi;}
const FSnapshot& Own(FWowLuaContext* C) {const auto* H=Handler(C); static const FSnapshot Empty; return H?H->Achievements.Own:Empty;}
void Text(lua_State* L,const FString& Value) {lua_pushstring(L,TCHAR_TO_UTF8(*Value));}
void Flag(lua_State* L,bool Value) {lua_pushboolean(L,Value);}
void Reg(lua_State* L,const char* Name,lua_CFunction F) {lua_pushcfunction(L,F); lua_setglobal(L,Name);}
bool Stats(uint32 Category)
{
    for(int I=0;I<16 && Category;++I) {if(Category==1) return true; const auto* C=Data().Category(Category); if(!C || C->Parent<0) break; Category=C->Parent;} return false;
}
bool Allowed(FWowLuaContext* C,const FAch& A)
{
    if(A.Flags&2) return false;
    const auto* H=Handler(C); const auto* P=H?H->EntityManager.GetLocalPlayer():nullptr; const uint8 Race=P?P->GetRaceId():0;
    if(A.Faction>=0 && Race) {const bool Alliance=Race==1 || Race==3 || Race==4 || Race==7 || Race==11; if(A.Faction!=(Alliance?1:0)) return false;}
    return A.Category!=81 || Own(C).Completed.Contains(A.Id);
}
const FAch* Next(FWowLuaContext* C,uint32 Id)
{
    const FAch* Out=nullptr; for(const auto& Pair:Data().Achievements()) if(Pair.Value.Previous==Id && Allowed(C,Pair.Value)) if(!Out || Pair.Value.Order<Out->Order || (Pair.Value.Order==Out->Order && Pair.Key<Out->Id)) Out=&Pair.Value;
    return Out;
}
TArray<const FAch*> Rows(FWowLuaContext* C,uint32 Category,bool All=false)
{
    TArray<const FAch*> Out; const auto& S=Own(C);
    // FrameXML uses -1/-2 for the aggregate achievement/statistics pages.
    const bool AchievementSummary=Category==MAX_uint32,StatisticsSummary=Category==MAX_uint32-1;
    for(const auto& Pair:Data().Achievements())
    {
        const auto& A=Pair.Value;
        const bool Matches=AchievementSummary?(!Stats(A.Category) && !(A.Flags&1)):StatisticsSummary?Stats(A.Category):A.Category==Category;
        if(!Matches || !Allowed(C,A)) continue;
        if(!All && !AchievementSummary && !StatisticsSummary && !Stats(Category))
        {
            if(A.Previous && !S.Completed.Contains(A.Previous)) continue;
            if(const auto* N=Next(C,A.Id); N && S.Completed.Contains(A.Id) && S.Completed.Contains(N->Id)) continue;
        }
        Out.Add(&A);
    }
    Out.Sort([&](const auto& A,const auto& B){const bool AC=S.Completed.Contains(A.Id),BC=S.Completed.Contains(B.Id); if(AC!=BC) return AC; return A.Order!=B.Order?A.Order<B.Order:A.Id<B.Id;}); return Out;
}
TArray<const FCrit*> Criteria(uint32 Id)
{
    TArray<const FCrit*> Out; const auto* A=Data().Achievement(Id); uint32 Source=A && A->Reference?A->Reference:Id;
    for(const auto& Pair:Data().Criteria()) if(Pair.Value.Achievement==Source) Out.Add(&Pair.Value);
    Out.Sort([](const auto& A,const auto& B){return A.Order!=B.Order?A.Order<B.Order:A.Id<B.Id;}); return Out;
}
bool Complete(const FSnapshot& S,const FCrit& C)
{return S.Completed.Contains(C.Achievement) || (C.Type==8?S.Completed.Contains(C.Asset):(S.Progress.Contains(C.Id) && S.Progress[C.Id].Counter>=FMath::Max(1u,C.Quantity)));}
FString CriterionName(const FCrit& C)
{
    if(!C.Name.IsEmpty()) return C.Name;
    if(C.Type==8) if(const auto* A=Data().Achievement(C.Asset)) return A->Name;
    if(const auto* A=Data().Achievement(C.Achievement)) return A->Name;
    return FString();
}
FString Icon(const FAch& A)
{const auto* I=FDbcStore::Get().SpellIcons().GetById(A.Icon); return I?I->TexturePath:TEXT("Interface\\Icons\\INV_Misc_QuestionMark");}
void Date(lua_State* L,bool Completed,uint32 Packed)
{
    if(!Completed) {for(int I=0;I<3;++I) lua_pushnil(L); return;}
    lua_pushinteger(L,((Packed>>20)&15)+1); lua_pushinteger(L,((Packed>>14)&63)+1); lua_pushinteger(L,(2000+(Packed>>24))%100);
}
int Info(lua_State* L,FWowLuaContext* C,const FAch* A)
{
    if(!A) return 0; const auto& S=Own(C); const bool Completed=S.Completed.Contains(A->Id);
    lua_pushinteger(L,A->Id); Text(L,A->Name); lua_pushinteger(L,A->Points); Flag(L,Completed); Date(L,Completed,S.Completed.FindRef(A->Id));
    Text(L,A->Description); lua_pushinteger(L,A->Flags); Text(L,Icon(*A)); Text(L,A->Reward); return 11;
}
uint32 Points(const FSnapshot& S)
{uint32 Sum=0; for(const auto& Pair:S.Completed) if(const auto* A=Data().Achievement(Pair.Key)) if(!(A->Flags&3)) Sum+=A->Points; return Sum;}
FString Number(uint64 Value) {return FString::Printf(TEXT("%llu"),Value);}
FString Statistic(const FSnapshot& S,uint32 Id)
{
    const auto* A=Data().Achievement(Id); if(!A) return TEXT("--"); const auto Values=Criteria(Id); uint64 Counter=0; uint32 Elapsed=0;
    for(const auto* C:Values) if(const auto* P=S.Progress.Find(C->Id))
    {if(A->Flags&8) Counter+=P->Counter; else Counter=FMath::Max(Counter,P->Counter); Elapsed=FMath::Max(Elapsed,P->CreationElapsed);}
    if((A->Flags&64) && Elapsed) return FString::Printf(TEXT("%.2f"),double(Counter)*86400./Elapsed);
    return Number(Counter);
}
void SaveTracking(lua_State* L,FWowLuaContext* C)
{lua_newtable(L); int I=0; for(uint32 Id:View(C).Tracked) {lua_pushinteger(L,Id); lua_rawseti(L,-2,++I);} lua_setglobal(L,"WowUnrealTrackedAchievements");}
void TrackingEvent(FWowLuaContext* C) {if(C && C->EventSystem) C->EventSystem->FireEvent(TEXT("TRACKED_ACHIEVEMENT_UPDATE"));}
FString Link(FWowLuaContext* C,const FAch& A)
{
    const auto* H=Handler(C); const auto& S=Own(C); const bool Completed=S.Completed.Contains(A.Id); const uint32 When=S.Completed.FindRef(A.Id);
    uint32 Mask[4]={}; const auto Values=Criteria(A.Id); for(int I=0;I<Values.Num() && I<128;++I) if(Complete(S,*Values[I])) Mask[I/32]|=1u<<(I%32);
    const uint64 Guid=H?H->EntityManager.LocalPlayerGuid:0;
    return FString::Printf(TEXT("|cffffff00|Hachievement:%u:%016llX:%u:%u:%u:%u:%u:%u:%u:%u|h[%s]|h|r"),A.Id,Guid,Completed?1u:0u,Completed?((When>>20)&15)+1:0,Completed?((When>>14)&63)+1:0,Completed?(2000+(When>>24))%100:0,Mask[0],Mask[1],Mask[2],Mask[3],*A.Name);
}
}
void WowLuaApi::RegisterAchievementApi(lua_State* L)
{
    Reg(L,"CanShowAchievementUI",[](lua_State* S)->int {Flag(S,!Data().Achievements().IsEmpty()); return 1;});
    Reg(L,"HasCompletedAnyAchievement",[](lua_State* S)->int {Flag(S,!Own(GetContext(S)).Completed.IsEmpty()); return 1;});
    Reg(L,"GetAchievementInfo",[](lua_State* S)->int {auto* C=GetContext(S); const FAch* A=nullptr; if(lua_isnumber(S,2)) {const auto List=Rows(C,luaL_checkinteger(S,1)); const int I=luaL_checkinteger(S,2)-1; if(List.IsValidIndex(I)) A=List[I];} else A=Data().Achievement(luaL_checkinteger(S,1)); return Info(S,C,A);});
    Reg(L,"GetAchievementInfoFromCriteria",[](lua_State* S)->int {const auto* C=Data().Criterion(luaL_checkinteger(S,1)); return Info(S,GetContext(S),C?Data().Achievement(C->Achievement):nullptr);});
    Reg(L,"GetAchievementCategory",[](lua_State* S)->int {const auto* A=Data().Achievement(luaL_checkinteger(S,1)); if(!A) return 0; lua_pushinteger(S,A->Category); return 1;});
    Reg(L,"GetCategoryInfo",[](lua_State* S)->int {const auto* C=Data().Category(luaL_checkinteger(S,1)); if(!C) return 0; Text(S,C->Name); lua_pushinteger(S,C->Parent==1?-1:C->Parent); lua_pushinteger(S,0); return 3;});
    for(int Kind=0;Kind<2;++Kind)
    {
        lua_pushboolean(L,Kind!=0); lua_pushcclosure(L,[](lua_State* S)->int {const bool Statistics=lua_toboolean(S,lua_upvalueindex(1))!=0; TArray<const FAchievementUiDbc::FCategory*> List; for(const auto& Pair:Data().Categories()) if(Pair.Key!=1 && Stats(Pair.Key)==Statistics) List.Add(&Pair.Value); List.Sort([](const auto& A,const auto& B){return A.Order!=B.Order?A.Order<B.Order:A.Id<B.Id;}); lua_newtable(S); int I=0; for(const auto* C:List) {lua_pushinteger(S,C->Id); lua_rawseti(S,-2,++I);} return 1;},1); lua_setglobal(L,Kind?"GetStatisticsCategoryList":"GetCategoryList");
    }
    Reg(L,"GetCategoryNumAchievements",[](lua_State* S)->int {auto* C=GetContext(S); const auto List=Rows(C,luaL_checkinteger(S,1),lua_toboolean(S,2)!=0); int Completed=0; for(const auto* A:List) Completed+=Own(C).Completed.Contains(A->Id)?1:0; lua_pushinteger(S,List.Num()); lua_pushinteger(S,Completed); lua_pushinteger(S,List.Num()-Completed); return 3;});
    Reg(L,"GetCategoryTotalNumAchievements",[](lua_State* S)->int {const auto List=Rows(GetContext(S),luaL_checkinteger(S,1),true); lua_pushinteger(S,List.Num()); return 1;});
    Reg(L,"GetNumCompletedAchievements",[](lua_State* S)->int {auto* C=GetContext(S); int Total=0,Completed=0; for(const auto& Pair:Data().Achievements()) if(!(Pair.Value.Flags&3) && Allowed(C,Pair.Value)) {++Total; Completed+=Own(C).Completed.Contains(Pair.Key)?1:0;} lua_pushinteger(S,Total); lua_pushinteger(S,Completed); return 2;});
    Reg(L,"GetTotalAchievementPoints",[](lua_State* S)->int {lua_pushinteger(S,Points(Own(GetContext(S)))); return 1;});
    Reg(L,"GetComparisonAchievementPoints",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); lua_pushinteger(S,H?Points(H->Achievements.Comparison):0); return 1;});
    Reg(L,"GetAchievementNumCriteria",[](lua_State* S)->int {lua_pushinteger(S,Criteria(luaL_checkinteger(S,1)).Num()); return 1;});
    Reg(L,"GetAchievementCriteriaInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); const FCrit* Criterion=nullptr; if(lua_isnumber(S,2)) {const auto List=Criteria(luaL_checkinteger(S,1)); const int I=luaL_checkinteger(S,2)-1; if(List.IsValidIndex(I)) Criterion=List[I];} else Criterion=Data().Criterion(luaL_checkinteger(S,1));
        if(!Criterion) return 0; const auto& R=*Criterion; const auto& State=Own(C); const auto* P=State.Progress.Find(R.Id); const uint64 Quantity=P?P->Counter:0;
        Text(S,CriterionName(R)); lua_pushinteger(S,R.Type); Flag(S,Complete(State,R)); lua_pushnumber(S,double(Quantity)); lua_pushnumber(S,FMath::Max(1u,R.Quantity));
        Text(S,Complete(State,R) && C && C->ConnectionManager?C->ConnectionManager->GetCharacterName():FString()); lua_pushinteger(S,R.Flags); lua_pushinteger(S,R.Asset); Text(S,Number(Quantity)); lua_pushinteger(S,R.Id); Flag(S,!P || !(P->Flags&1)); lua_pushnumber(S,R.Duration); lua_pushnumber(S,P?P->Elapsed+FMath::Max(0.,FPlatformTime::Seconds()-P->At):0); return 13;
    });
    Reg(L,"GetNextAchievement",[](lua_State* S)->int {auto* C=GetContext(S); const auto* N=Next(C,luaL_checkinteger(S,1)); if(!N) return 0; lua_pushinteger(S,N->Id); Flag(S,Own(C).Completed.Contains(N->Id)); return 2;});
    Reg(L,"GetPreviousAchievement",[](lua_State* S)->int {const auto* A=Data().Achievement(luaL_checkinteger(S,1)); if(!A || !A->Previous) return 0; lua_pushinteger(S,A->Previous); return 1;});
    Reg(L,"GetAchievementLink",[](lua_State* S)->int {const auto* A=Data().Achievement(luaL_checkinteger(S,1)); if(!A) return 0; Text(S,Link(GetContext(S),*A)); return 1;});
    Reg(L,"GetLatestCompletedAchievements",[](lua_State* S)->int {auto* C=GetContext(S); TArray<uint32> Ids; for(const auto& Pair:Own(C).Completed) if(const auto* A=Data().Achievement(Pair.Key); A && !(A->Flags&3)) Ids.Add(Pair.Key); Ids.Sort([&](uint32 A,uint32 B){const auto X=Own(C).Completed[A],Y=Own(C).Completed[B]; return X!=Y?X>Y:A<B;}); const int Count=FMath::Min(Ids.Num(),5); for(int I=0;I<Count;++I) lua_pushinteger(S,Ids[I]); return Count;});
    Reg(L,"GetStatistic",[](lua_State* S)->int {Text(S,Statistic(Own(GetContext(S)),luaL_checkinteger(S,1))); return 1;});
    Reg(L,"GetComparisonStatistic",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); if(H) Text(S,Statistic(H->Achievements.Comparison,luaL_checkinteger(S,1))); else lua_pushliteral(S,"--"); return 1;});
    Reg(L,"GetAchievementComparisonInfo",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); const uint32 Id=luaL_checkinteger(S,1); const bool Complete=H && H->Achievements.Comparison.Ready && H->Achievements.Comparison.Completed.Contains(Id); Flag(S,Complete); Date(S,Complete,H?H->Achievements.Comparison.Completed.FindRef(Id):0); return 4;});
    Reg(L,"GetComparisonCategoryNumAchievements",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); int Complete=0; if(H) for(const auto* A:Rows(C,luaL_checkinteger(S,1),lua_toboolean(S,2)!=0)) Complete+=H->Achievements.Comparison.Completed.Contains(A->Id)?1:0; lua_pushinteger(S,Complete); return 1;});
    Reg(L,"SetAchievementComparisonUnit",[](lua_State* S)->int {
        auto* C=GetContext(S); auto* H=Handler(C); const auto* Unit=ResolveUnitToken(S,1); if(!H || !Unit || !Unit->IsPlayer() || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame) return 0;
        TArray<uint8> B; B.Add(0); for(int I=0;I<8;++I) if(const uint8 Value=uint8(Unit->Guid>>(I*8))) {B[0]|=1<<I; B.Add(Value);}
        H->Achievements.CompareGuid=Unit->Guid; H->Achievements.Comparison={}; ++H->Achievements.ComparisonRevision; C->ConnectionManager->SendRawPacket(0x46B,B); return 0;
    });
    Reg(L,"ClearAchievementComparisonUnit",[](lua_State* S)->int {if(auto* H=Handler(GetContext(S))) {H->Achievements.CompareGuid=0; H->Achievements.Comparison={}; ++H->Achievements.ComparisonRevision;} return 0;});
    Reg(L,"GetNumTrackedAchievements",[](lua_State* S)->int {auto* C=GetContext(S); lua_pushinteger(S,C?View(C).Tracked.Num():0); return 1;});
    Reg(L,"GetTrackedAchievements",[](lua_State* S)->int {auto* C=GetContext(S); if(!C) return 0; for(uint32 Id:View(C).Tracked) lua_pushinteger(S,Id); return View(C).Tracked.Num();});
    Reg(L,"IsTrackedAchievement",[](lua_State* S)->int {auto* C=GetContext(S); Flag(S,C && View(C).Tracked.Contains(luaL_checkinteger(S,1))); return 1;});
    Reg(L,"AddTrackedAchievement",[](lua_State* S)->int {auto* C=GetContext(S); const uint32 Id=luaL_checkinteger(S,1); const auto* A=Data().Achievement(Id); if(C && A && !(A->Flags&3) && !Own(C).Completed.Contains(Id) && !View(C).Tracked.Contains(Id) && View(C).Tracked.Num()<10) {View(C).Tracked.Add(Id); SaveTracking(S,C); TrackingEvent(C);} return 0;});
    Reg(L,"RemoveTrackedAchievement",[](lua_State* S)->int {auto* C=GetContext(S); if(C && View(C).Tracked.Remove(luaL_checkinteger(S,1))) {SaveTracking(S,C); TrackingEvent(C);} return 0;});
}
int WowLuaApi::SetAchievementTooltip(lua_State* L,uint32 Id)
{
    auto* C=GetContext(L); const auto* A=Data().Achievement(Id); if(!C || !C->FrameManager || !A) return 0;
    lua_getfield(L,1,"__handle"); const int64 Handle=lua_tointeger(L,-1); lua_pop(L,1); auto* F=C->FrameManager; F->TooltipClearLines(Handle);
    F->TooltipAddLine(Handle,A->Name,1,.82f,0,false); F->TooltipAddLine(Handle,A->Description,1,1,1,true);
    if(!A->Reward.IsEmpty()) F->TooltipAddLine(Handle,A->Reward,0,1,0,true);
    F->SetFrameVisible(Handle,true); return 0;
}
void WowLuaApi::UpdateAchievementState(lua_State* L)
{
    auto* C=GetContext(L); auto* H=Handler(C); if(!H || !C->EventSystem) return; auto& V=View(C); auto& A=H->Achievements;
    const uint64 Player=H->EntityManager.LocalPlayerGuid;
    if(V.Player!=Player) {if(V.Player) {V.Tracked.Reset(); SaveTracking(L,C); TrackingEvent(C);} V.Player=Player; V.Loaded=false;}
    if(!V.Loaded && Player && C->ConnectionManager->GetState()==EWowSessionState::WorldInGame && !C->ConnectionManager->GetCharacterName().IsEmpty())
    {
        V.Loaded=true; FWowTocData Toc; Toc.SavedVariablesPerCharacter.Add(TEXT("WowUnrealTrackedAchievements")); FWowSavedVariables::RegisterAddon(TEXT("WowUnreal_Achievements"),Toc); FWowSavedVariables::LoadForAddon(L,TEXT("WowUnreal_Achievements"),Toc);
        lua_getglobal(L,"WowUnrealTrackedAchievements"); if(lua_istable(L,-1)) for(int I=1;I<=FMath::Min(10,int(lua_objlen(L,-1)));++I) {lua_rawgeti(L,-1,I); const uint32 Id=lua_tointeger(L,-1); lua_pop(L,1); if(const auto* Row=Data().Achievement(Id); Row && !(Row->Flags&3) && !V.Tracked.Contains(Id)) V.Tracked.Add(Id);} lua_pop(L,1); TrackingEvent(C);
    }
    if(V.Data!=A.DataRevision) {V.Data=A.DataRevision; if(A.Own.Ready) C->EventSystem->FireEvent(TEXT("RECEIVED_ACHIEVEMENT_LIST"));}
    if(V.Revision!=A.Revision)
    {
        V.Revision=A.Revision; bool Changed=false; for(int I=V.Tracked.Num()-1;I>=0;--I) if(A.Own.Completed.Contains(V.Tracked[I])) {V.Tracked.RemoveAt(I); Changed=true;}
        if(Changed) {SaveTracking(L,C); TrackingEvent(C);} C->EventSystem->FireEvent(TEXT("CRITERIA_UPDATE"));
        const auto Earned=MoveTemp(A.Earned); A.Earned.Reset(); for(uint32 Id:Earned) C->EventSystem->FireEventTyped(TEXT("ACHIEVEMENT_EARNED"),[Id](lua_State* S){lua_pushinteger(S,Id); return 1;});
    }
    if(V.Comparison!=A.ComparisonRevision) {V.Comparison=A.ComparisonRevision; if(A.Comparison.Ready) C->EventSystem->FireEvent(TEXT("INSPECT_ACHIEVEMENT_READY"));}
}
#else
void WowLuaApi::RegisterAchievementApi(lua_State*) {}
void WowLuaApi::UpdateAchievementState(lua_State*) {}
int WowLuaApi::SetAchievementTooltip(lua_State*,uint32) {return 0;}
#endif
