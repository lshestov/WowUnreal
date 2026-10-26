#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowSavedVariables.h"
#include "WowUiText.h"
#include "Formats/Dbc/DbcStore.h"
#include "GameFramework/GameUserSettings.h"
#include "UnrealClient.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void RegisterPanel(lua_State* L,const char* Name,lua_CFunction Fn) { lua_pushcfunction(L,Fn); lua_setglobal(L,Name); }
const FDbcStore::FWorldMapArea* CurrentMapArea(FWowLuaContext* C)
{
    const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; if(!P || !C->ConnectionManager) return nullptr;
    const auto* Selected=C->SelectedRows.Find(TEXT("mapArea")); const FDbcStore::FWorldMapArea* Found=nullptr; double Smallest=DBL_MAX;
    for(const auto& A:FDbcStore::Get().WorldMapAreas())
    {
        if(Selected && A.Id==uint32(*Selected)) return &A;
        const auto Position=A.Project(P->Movement.Position); const double Size=FMath::Abs((A.X1-A.X2)*(A.Y1-A.Y2));
        if(A.MapId==C->ConnectionManager->PacketHandler.CurrentMapId && Position.X>=0 && Position.X<=1 && Position.Y>=0 && Position.Y<=1 && Size<Smallest) { Found=&A; Smallest=Size; }
    } return Found;
}
struct FSkillRow { uint32 Id=0,Rank=0,Max=0; int32 Temporary=0,Permanent=0; FString Name,Description; bool Abandonable=false; };
TArray<FSkillRow> Skills(FWowLuaContext* C)
{
    TArray<FSkillRow> Rows; const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; if(!P) return Rows;
    for(int32 I=0; I<128; ++I)
    {
        const uint16 Base=PlayerField::SKILL_INFO+I*3; const uint32 Id=P->GetField(Base)&0xffff;
        if(const auto* Skill=FDbcStore::Get().SpellBook().Skill(Id))
        {
            FSkillRow Row; Row.Id=Id; Row.Name=Skill->Name; Row.Description=Skill->Description; Row.Rank=P->GetField(Base+1)&0xffff; Row.Max=P->GetField(Base+1)>>16;
            Row.Temporary=int16(P->GetField(Base+2)&0xffff); Row.Permanent=int16(P->GetField(Base+2)>>16); Row.Abandonable=Skill->Category==11; Rows.Add(MoveTemp(Row));
        }
    }
    Rows.Sort([](const auto& A,const auto& B){return A.Name<B.Name;}); return Rows;
}
int32 Selection(lua_State* L,const TCHAR* Name,bool Set)
{
    auto* C=WowLuaApi::GetContext(L); if(!C) return 0;
    auto& Value=C->SelectedRows.FindOrAdd(FName(Name)); if(Set) { Value=FMath::Max(int32(luaL_optinteger(L,1,0)),0); return 0; }
    lua_pushinteger(L,Value); return 1;
}
const FWowQuestLogEntry* SelectedQuest(FWowLuaContext* C,int32 Index=0)
{
    if(!C || !C->ConnectionManager) return nullptr;
    if(!Index) Index=C->SelectedRows.FindRef(TEXT("quest"));
    const auto& Log=C->ConnectionManager->PacketHandler.QuestLog;
    if(!Log.IsValidIndex(Index-1)) return nullptr;
    C->ConnectionManager->RequestQuestTemplate(Log[Index-1].QuestId); return &Log[Index-1];
}
const FWowQuestTemplate* QuestData(FWowLuaContext* C,const FWowQuestLogEntry* Quest)
{
    return C && C->ConnectionManager && Quest?C->ConnectionManager->PacketHandler.QuestTemplates.Find(Quest->QuestId):nullptr;
}
FString QuestText(FWowLuaContext* C,const FString& Text)
{
    const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const auto* Race=P?FDbcStore::Get().ChrRaces().GetById(P->GetRaceId()):nullptr;
    return WowQuestText(Text,C && C->ConnectionManager?C->ConnectionManager->GetCharacterName():FString(),P?P->GetClassId():0,Race?Race->Name:FString(),P && P->GetGenderId()==1);
}
uint16 QuestField(FWowLuaContext* C,uint32 Id)
{
    const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    if(P) for(int32 I=0; I<25; ++I) { const uint16 Field=PlayerField::BASE+0x000A+I*5; if(P->GetField(Field)==Id) return Field; }
    return 0;
}
int32 CountItem(FWowLuaContext* C,uint32 Id)
{
    int32 Count=0; if(!C || !C->EntityManager) return Count;
    for(const auto& Pair:C->EntityManager->GetAll()) if(Pair.Value->IsItem() && Pair.Value->Entry==Id)
    { const auto* Item=static_cast<const FWowItemEntity*>(Pair.Value.Get()); if(Item->GetOwnerGuid()==C->EntityManager->LocalPlayerGuid) Count+=FMath::Max(Item->GetStackCount(),1); }
    return Count;
}
TArray<int32> QuestWatches(lua_State* L)
{
    TArray<int32> Rows; auto* C=WowLuaApi::GetContext(L); if(!C || !C->ConnectionManager) return Rows;
    const auto& Log=C->ConnectionManager->PacketHandler.QuestLog;
    for(int32 I=0;I<Log.Num();++I) if(WowLuaApi::GetUiPreference(L,FString::Printf(TEXT("questWatch%u"),Log[I].QuestId),1)) Rows.Add(I+1);
    return Rows;
}
int QuestRewardInfo(lua_State* L,bool bChoice)
{
    auto* C=WowLuaApi::GetContext(L); const auto* Q=QuestData(C,SelectedQuest(C)); const int32 Index=luaL_checkinteger(L,1)-1;
    const auto* Items=Q?(bChoice?&Q->Choices:&Q->Rewards):nullptr;
    if(!Items || !Items->IsValidIndex(Index)) { lua_pushnil(L); return 1; }
    const auto& Reward=(*Items)[Index]; C->ConnectionManager->RequestItemTemplate(Reward.ItemId);
    const auto* Item=C->ConnectionManager->PacketHandler.ItemTemplates.Find(Reward.ItemId);
    const auto* Display=Item?FDbcStore::Get().ItemDisplayInfo().GetById(Item->DisplayId):nullptr;
    const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    lua_pushstring(L,Item?TCHAR_TO_UTF8(*Item->Name):"Retrieving item information...");
    lua_pushstring(L,Display?TCHAR_TO_UTF8(*(TEXT("Interface\\Icons\\")+Display->Icon1)):"Interface\\Icons\\INV_Misc_QuestionMark");
    lua_pushinteger(L,Reward.Count); lua_pushinteger(L,Item?Item->Quality:1); lua_pushboolean(L,Item && P && uint32(P->GetLevel())>=Item->RequiredLevel); return 5;
}
}
int32 WowLuaApi::GetUiPreference(lua_State* L,const FString& Key,int32 Default)
{
    lua_getglobal(L,"IdleWowUIState");
    if(!lua_istable(L,-1)) { lua_pop(L,1); return Default; }
    lua_getfield(L,-1,TCHAR_TO_UTF8(*Key)); const int32 Value=lua_isnumber(L,-1)?int32(lua_tointeger(L,-1)):Default; lua_pop(L,2); return Value;
}
void WowLuaApi::SetUiPreference(lua_State* L,const FString& Key,int32 Value)
{
    lua_getglobal(L,"IdleWowUIState");
    if(!lua_istable(L,-1)) { lua_pop(L,1); lua_newtable(L); lua_pushvalue(L,-1); lua_setglobal(L,"IdleWowUIState"); }
    lua_pushinteger(L,Value); lua_setfield(L,-2,TCHAR_TO_UTF8(*Key)); lua_pop(L,1);
}
void WowLuaApi::RegisterPanelApi(lua_State* L)
{
    RegisterPanel(L,"GetAutoCompleteResults",[](lua_State* S)->int {
        auto* C=GetContext(S); const FString Prefix=UTF8_TO_TCHAR(luaL_optstring(S,1,"")); const int32 Max=FMath::Clamp(int32(luaL_optinteger(S,4,10)),1,100); TArray<FString> Names;
        if(C && C->ConnectionManager)
        {
            Names.AddUnique(C->ConnectionManager->GetCharacterName());
            for(const auto& F:C->ConnectionManager->PacketHandler.FriendsList) Names.AddUnique(F.Name);
            for(const auto& G:C->ConnectionManager->PacketHandler.GuildRoster) Names.AddUnique(G.Name);
            for(const auto& G:C->ConnectionManager->PacketHandler.GroupInfo.Members) Names.AddUnique(G.Name);
            for(const auto& Pair:C->ConnectionManager->PacketHandler.PlayerNameCache) Names.AddUnique(Pair.Value);
        }
        Names.Sort(); int32 Count=0; for(const auto& Name:Names) if(!Name.IsEmpty() && Name.StartsWith(Prefix,ESearchCase::IgnoreCase)) { lua_pushstring(S,TCHAR_TO_UTF8(*Name)); if(++Count>=Max) break; }
        return Count;
    });
    // The private realm has no Battle.net presence service. A character whisper
    // must follow the normal realm path instead of being treated as a BN whisper.
    RegisterPanel(L,"GetAutoCompletePresenceID",[](lua_State* S)->int { lua_pushnil(S); return 1; });
    RegisterPanel(L,"GetMapInfo",[](lua_State* S)->int { const auto* A=CurrentMapArea(GetContext(S)); if(A) lua_pushstring(S,TCHAR_TO_UTF8(*A->Name)); else lua_pushnil(S); lua_pushinteger(S,0); return 2; });
    RegisterPanel(L,"SetMapToCurrentZone",[](lua_State* S)->int { if(auto* C=GetContext(S)) C->SelectedRows.Remove(TEXT("mapArea")); return 0; });
    RegisterPanel(L,"GetCurrentMapAreaID",[](lua_State* S)->int { const auto* A=CurrentMapArea(GetContext(S)); lua_pushinteger(S,A?A->Id:0); return 1; });
    RegisterPanel(L,"GetCurrentMapZone",[](lua_State* S)->int { const auto* A=CurrentMapArea(GetContext(S)); lua_pushinteger(S,A?A->AreaId:0); return 1; });
    RegisterPanel(L,"GetCurrentMapDungeonLevel",[](lua_State* S)->int { lua_pushinteger(S,0); return 1; });
    RegisterPanel(L,"UnitPVPRank",[](lua_State* S)->int { const auto* P=ResolveUnitToken(S); lua_pushinteger(S,P && P->IsPlayer()?P->GetFieldByte(PlayerField::BASE+0x0007,2):0); return 1; });
    RegisterPanel(L,"GetDungeonDifficulty",[](lua_State* S)->int { auto* C=GetContext(S); lua_pushinteger(S,C && C->ConnectionManager?C->ConnectionManager->PacketHandler.DungeonDifficulty+1:1); return 1; });
    RegisterPanel(L,"GetRaidDifficulty",[](lua_State* S)->int { auto* C=GetContext(S); lua_pushinteger(S,C && C->ConnectionManager?C->ConnectionManager->PacketHandler.RaidDifficulty+1:1); return 1; });
    RegisterPanel(L,"SetDungeonDifficulty",[](lua_State* S)->int { auto* C=GetContext(S); const uint32 Mode=luaL_checkinteger(S,1)-1; if(C && C->ConnectionManager && Mode<2) { TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Mode),4); C->ConnectionManager->SendRawPacket(0x329,Data); } return 0; });
    RegisterPanel(L,"SetRaidDifficulty",[](lua_State* S)->int { auto* C=GetContext(S); const uint32 Mode=luaL_checkinteger(S,1)-1; if(C && C->ConnectionManager && Mode<4) { TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Mode),4); C->ConnectionManager->SendRawPacket(0x4EB,Data); } return 0; });
    RegisterPanel(L,"RequestRaidInfo",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) C->ConnectionManager->SendRawPacket(0x2CD,{}); return 0; });
    RegisterPanel(L,"GetNumSavedInstances",[](lua_State* S)->int { auto* C=GetContext(S); lua_pushinteger(S,C && C->ConnectionManager?C->ConnectionManager->PacketHandler.SavedInstances.Num():0); return 1; });
    RegisterPanel(L,"GetSavedInstanceInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); const int32 I=luaL_checkinteger(S,1)-1; if(!C || !C->ConnectionManager || !C->ConnectionManager->PacketHandler.SavedInstances.IsValidIndex(I)) { lua_pushnil(S); return 1; }
        const auto& V=C->ConnectionManager->PacketHandler.SavedInstances[I]; const auto* Map=FDbcStore::Get().Maps().GetById(V.Map);
        lua_pushstring(S,Map?TCHAR_TO_UTF8(*Map->Name):""); lua_pushinteger(S,uint32(V.Id)); lua_pushinteger(S,V.Reset); lua_pushinteger(S,V.Difficulty+1); lua_pushboolean(S,V.Locked); lua_pushboolean(S,V.Extended); lua_pushinteger(S,uint32(V.Id>>32)); lua_pushboolean(S,Map && Map->MapType==2); lua_pushinteger(S,Map?Map->MaxPlayers:0); lua_pushstring(S,""); return 10;
    });
    RegisterPanel(L,"GetCurrentArenaSeason",[](lua_State* S)->int { auto* C=GetContext(S); lua_pushinteger(S,C && C->ConnectionManager?C->ConnectionManager->PacketHandler.WorldStates.FindRef(3901):0); return 1; });
    RegisterPanel(L,"PetCanBeDismissed",[](lua_State* S)->int { auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; lua_pushboolean(S,P && P->GetField64(UnitField::SUMMON)!=0 && !P->GetField64(UnitField::CHARM)); return 1; });
    RegisterPanel(L,"GetPVPRankInfo",[](lua_State* S)->int {
        const int32 Rank=luaL_checkinteger(S,1); auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const auto* Race=P?FDbcStore::Get().ChrRaces().GetById(P->GetRaceId()):nullptr;
        if(Rank<=0) lua_pushnil(S); else { const FString Name=FString::Printf(TEXT("PVP_RANK_%d_%d%s"),Rank,Race && Race->FactionGroup==1?1:0,P && P->GetGenderId()==1?TEXT("_FEMALE"):TEXT("")); lua_getglobal(S,TCHAR_TO_UTF8(*Name)); } lua_pushinteger(S,Rank>=5?Rank-4:0); return 2;
    });
    RegisterPanel(L,"ReloadUI",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ReloadUI) C->ReloadUI(); return 0; });
    RegisterPanel(L,"Logout",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) { FWowSavedVariables::SaveAll(S); C->ConnectionManager->RequestLogout(); } return 0; });
    RegisterPanel(L,"CancelLogout",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) C->ConnectionManager->CancelLogout(); return 0; });
    RegisterPanel(L,"GetPossessInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const int32 Index=luaL_checkinteger(S,1)-1;
        const auto* Bar=C && C->ConnectionManager?&C->ConnectionManager->PacketHandler.PetActionBar:nullptr;
        const bool Used=P && P->GetField64(UnitField::CHARM) && Bar && Bar->ActionSlots.IsValidIndex(Index) && Bar->ActionSlots[Index].ActionId;
        if(Used) { const auto* Spell=FDbcStore::Get().Spells().GetById(Bar->ActionSlots[Index].ActionId); const auto* Icon=Spell?FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID):nullptr; lua_pushstring(S,Icon?TCHAR_TO_UTF8(*Icon->TexturePath):""); lua_pushstring(S,Spell?TCHAR_TO_UTF8(*Spell->SpellName):""); } else { lua_pushnil(S); lua_pushnil(S); }
        lua_pushboolean(S,Used); lua_pushboolean(S,0); return 4;
    });
    RegisterPanel(L,"GetGuildRosterMOTD",[](lua_State* S)->int { auto* C=GetContext(S); lua_pushstring(S,C && C->ConnectionManager?TCHAR_TO_UTF8(*C->ConnectionManager->PacketHandler.GuildMotd):""); return 1; });
    RegisterPanel(L,"UnitSex",[](lua_State* S)->int { const auto* E=ResolveUnitToken(S); lua_pushinteger(S,E?E->GetFieldByte(UnitField::BYTES_0,2)+2:0); return 1; });
    RegisterPanel(L,"GetDailyQuestsCompleted",[](lua_State* S)->int { auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; int32 Count=0; if(P) for(int32 I=0;I<25;++I) Count+=P->GetField(PlayerField::BASE+0x046C+I)!=0; lua_pushinteger(S,Count); return 1; });
    RegisterPanel(L,"GetMaxDailyQuests",[](lua_State* S)->int { lua_pushinteger(S,25); return 1; });
    RegisterPanel(L,"GetAbandonQuestName",[](lua_State* S)->int { auto* C=GetContext(S); const auto* T=QuestData(C,SelectedQuest(C)); if(T) lua_pushstring(S,TCHAR_TO_UTF8(*T->Title)); else lua_pushnil(S); return 1; });
    RegisterPanel(L,"GetQuestLogPushable",[](lua_State* S)->int { auto* C=GetContext(S); const auto* T=QuestData(C,SelectedQuest(C)); lua_pushboolean(S,T && (T->Flags&8)); return 1; });
    RegisterPanel(L,"GetTrackedAchievements",[](lua_State* S)->int {
        TArray<int32> Tracked; lua_getglobal(S,"IdleWowUIState");
        if(lua_istable(S,-1)) { lua_pushnil(S); while(lua_next(S,-2)) { if(lua_type(S,-2)==LUA_TSTRING && lua_tonumber(S,-1)!=0) { const FString Key=UTF8_TO_TCHAR(lua_tostring(S,-2)); if(Key.StartsWith(TEXT("achievementWatch"))) { const int32 Id=FCString::Atoi(*Key.Mid(16)); if(Id>0) Tracked.AddUnique(Id); } } lua_pop(S,1); } } lua_pop(S,1);
        Tracked.Sort(); for(int32 Id:Tracked) lua_pushinteger(S,Id); return Tracked.Num();
    });
    RegisterPanel(L,"AddTrackedAchievement",[](lua_State* S)->int { const int32 Id=luaL_checkinteger(S,1); if(Id>0) SetUiPreference(S,FString::Printf(TEXT("achievementWatch%d"),Id),1); if(auto* C=GetContext(S); C && C->EventSystem) C->EventSystem->FireEvent(TEXT("TRACKED_ACHIEVEMENT_UPDATE")); return 0; });
    RegisterPanel(L,"RemoveTrackedAchievement",[](lua_State* S)->int { SetUiPreference(S,FString::Printf(TEXT("achievementWatch%d"),int32(luaL_checkinteger(S,1))),0); if(auto* C=GetContext(S); C && C->EventSystem) C->EventSystem->FireEvent(TEXT("TRACKED_ACHIEVEMENT_UPDATE")); return 0; });
    RegisterPanel(L,"IsTrackedAchievement",[](lua_State* S)->int { lua_pushboolean(S,GetUiPreference(S,FString::Printf(TEXT("achievementWatch%d"),int32(luaL_checkinteger(S,1))))!=0); return 1; });
    RegisterPanel(L,"GetQuestLogIsAutoComplete",[](lua_State* S)->int { auto* C=GetContext(S); const auto* T=QuestData(C,SelectedQuest(C,luaL_optinteger(S,1,0))); lua_pushboolean(S,T && (T->Flags&0x10000)); return 1; });
    RegisterPanel(L,"GetNumQuestWatches",[](lua_State* S)->int { lua_pushinteger(S,QuestWatches(S).Num()); return 1; });
    RegisterPanel(L,"GetQuestIndexForWatch",[](lua_State* S)->int { const auto Rows=QuestWatches(S); const int32 I=luaL_checkinteger(S,1)-1; if(Rows.IsValidIndex(I)) lua_pushinteger(S,Rows[I]); else lua_pushnil(S); return 1; });
    RegisterPanel(L,"GetQuestWatchIndex",[](lua_State* S)->int { const int32 I=QuestWatches(S).Find(luaL_checkinteger(S,1)); if(I>=0) lua_pushinteger(S,I+1); else lua_pushnil(S); return 1; });
    RegisterPanel(L,"IsQuestWatched",[](lua_State* S)->int { lua_pushboolean(S,QuestWatches(S).Contains(luaL_checkinteger(S,1))); return 1; });
    RegisterPanel(L,"AddQuestWatch",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Q=SelectedQuest(C,luaL_checkinteger(S,1)); if(Q) { SetUiPreference(S,FString::Printf(TEXT("questWatch%u"),Q->QuestId),1); if(C->EventSystem) C->EventSystem->FireEvent(TEXT("QUEST_WATCH_UPDATE")); } return 0; });
    RegisterPanel(L,"RemoveQuestWatch",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Q=SelectedQuest(C,luaL_checkinteger(S,1)); if(Q) { SetUiPreference(S,FString::Printf(TEXT("questWatch%u"),Q->QuestId),0); if(C->EventSystem) C->EventSystem->FireEvent(TEXT("QUEST_WATCH_UPDATE")); } return 0; });
    RegisterPanel(L,"GetNumQuestLogEntries",[](lua_State* S)->int { auto* C=GetContext(S); const int32 Count=C && C->ConnectionManager?C->ConnectionManager->PacketHandler.QuestLog.Num():0; lua_pushinteger(S,Count); lua_pushinteger(S,Count); return 2; });
    RegisterPanel(L,"SelectQuestLogEntry",[](lua_State* S)->int { Selection(S,TEXT("quest"),true); SelectedQuest(GetContext(S)); return 0; });
    RegisterPanel(L,"GetQuestLogSelection",[](lua_State* S)->int { return Selection(S,TEXT("quest"),false); });
    RegisterPanel(L,"ToggleQuestLog",[](lua_State* S)->int { lua_getglobal(S,"ToggleFrame"); lua_getglobal(S,"QuestLogFrame"); lua_call(S,1,0); return 0; });
    RegisterPanel(L,"IsCurrentQuestFailed",[](lua_State* S)->int { const auto* Q=SelectedQuest(GetContext(S)); lua_pushboolean(S,Q && (Q->State&2)); return 1; });
    RegisterPanel(L,"GetQuestLogGroupNum",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Q=QuestData(C,SelectedQuest(C)); lua_pushinteger(S,Q?Q->Group:0); return 1; });
    RegisterPanel(L,"GetQuestGreenRange",[](lua_State* S)->int { auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const int32 Level=P?P->GetLevel():1; const int32 Gray=Level<=5?0:Level<=39?Level-5-Level/10:Level<=59?Level-1-Level/5:Level-9; lua_pushinteger(S,Level-Gray-1); return 1; });
    RegisterPanel(L,"GetQuestLogTitle",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* Q=SelectedQuest(C,luaL_checkinteger(S,1)); const auto* T=QuestData(C,Q); if(!Q) { lua_pushnil(S); return 1; }
        lua_pushstring(S,TCHAR_TO_UTF8(*(T?T->Title:TEXT("Retrieving quest information...")))); const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
        lua_pushinteger(S,T && T->Level>0?T->Level:P?P->GetLevel():0); lua_pushnil(S); lua_pushinteger(S,T?T->Group:0); lua_pushnil(S); lua_pushnil(S);
        if(Q->State&2) lua_pushinteger(S,-1); else if(Q->State&1) lua_pushinteger(S,1); else lua_pushnil(S);
        lua_pushboolean(S,T && (T->Flags&0x1000)!=0); lua_pushinteger(S,Q->QuestId); return 9;
    });
    RegisterPanel(L,"GetQuestLogQuestText",[](lua_State* S)->int { auto* C=GetContext(S); const auto* T=QuestData(C,SelectedQuest(C)); lua_pushstring(S,TCHAR_TO_UTF8(*QuestText(C,T?T->Details:FString()))); lua_pushstring(S,TCHAR_TO_UTF8(*QuestText(C,T?T->Objectives:FString()))); return 2; });
    RegisterPanel(L,"GetQuestLogRewardMoney",[](lua_State* S)->int { auto* C=GetContext(S); const auto* T=QuestData(C,SelectedQuest(C)); lua_pushinteger(S,T?FMath::Max(T->Money,0):0); return 1; });
    RegisterPanel(L,"GetNumQuestLogRewards",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Q=QuestData(C,SelectedQuest(C)); lua_pushinteger(S,Q?Q->Rewards.Num():0); return 1; });
    RegisterPanel(L,"GetNumQuestLogChoices",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Q=QuestData(C,SelectedQuest(C)); lua_pushinteger(S,Q?Q->Choices.Num():0); return 1; });
    RegisterPanel(L,"GetQuestLogRewardInfo",[](lua_State* S)->int { return QuestRewardInfo(S,false); });
    RegisterPanel(L,"GetQuestLogChoiceInfo",[](lua_State* S)->int { return QuestRewardInfo(S,true); });
    RegisterPanel(L,"GetQuestLogRewardHonor",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Q=QuestData(C,SelectedQuest(C)); lua_pushinteger(S,Q?Q->Honor:0); return 1; });
    RegisterPanel(L,"GetQuestLogRewardTalents",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Q=QuestData(C,SelectedQuest(C)); lua_pushinteger(S,Q?Q->Talents:0); return 1; });
    RegisterPanel(L,"GetQuestLogRewardArenaPoints",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Q=QuestData(C,SelectedQuest(C)); lua_pushinteger(S,Q?Q->ArenaPoints:0); return 1; });
    RegisterPanel(L,"GetQuestLogRewardTitle",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* Q=QuestData(C,SelectedQuest(C)); const auto* T=Q?FDbcStore::Get().TitleById(Q->TitleId):nullptr; const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
        if(T && P) { FString Name=P->GetGenderId()==1?T->Female:T->Male; Name=Name.Replace(TEXT("%s"),*C->ConnectionManager->GetCharacterName()); lua_pushstring(S,TCHAR_TO_UTF8(*Name)); } else lua_pushnil(S); return 1;
    });
    RegisterPanel(L,"GetQuestLogRewardSpell",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* Q=QuestData(C,SelectedQuest(C)); const uint32 Id=Q?(Q->RewardSpell?Q->RewardSpell:Q->RewardSpellCast):0; const auto* Spell=FDbcStore::Get().Spells().GetById(Id);
        if(!Spell) { lua_pushnil(S); return 1; } const auto* Icon=FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID);
        lua_pushstring(S,Icon?TCHAR_TO_UTF8(*Icon->TexturePath):""); lua_pushstring(S,TCHAR_TO_UTF8(*Spell->SpellName)); lua_pushboolean(S,Spell->Effect[0]==36); lua_pushboolean(S,Q->RewardSpell!=0); return 4;
    });
    RegisterPanel(L,"GetQuestLogRewardXP",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* Q=QuestData(C,SelectedQuest(C)); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; uint32 XP=0;
        if(Q && P && P->GetLevel()<80) { const int32 Level=Q->Level<0?P->GetLevel():Q->Level; XP=FMath::Clamp(2*(Level-int32(P->GetLevel()))+20,1,10)*FDbcStore::Get().QuestXP(Level,Q->XPDifficulty)/10; const uint32 Step=XP<=100?5:XP<=500?10:XP<=1000?25:50; XP=((XP+Step/2)/Step)*Step; } lua_pushinteger(S,XP); return 1;
    });
    RegisterPanel(L,"ProcessQuestLogRewardFactions",[](lua_State* S)->int { SelectedQuest(GetContext(S)); return 0; });
    RegisterPanel(L,"GetNumQuestLogRewardFactions",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Q=QuestData(C,SelectedQuest(C)); int32 Count=0; if(Q) for(uint32 Id:Q->RewardFactionId) Count+=Id!=0; lua_pushinteger(S,Count); return 1; });
    RegisterPanel(L,"GetQuestLogRewardFactionInfo",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Q=QuestData(C,SelectedQuest(C)); const int32 Wanted=luaL_checkinteger(S,1); int32 Row=0; if(Q) for(int32 I=0;I<5;++I) if(Q->RewardFactionId[I] && ++Row==Wanted) { lua_pushinteger(S,Q->RewardFactionId[I]); lua_pushinteger(S,Q->RewardFactionOverride[I]?Q->RewardFactionOverride[I]:100*FDbcStore::Get().QuestReputation(Q->RewardFactionValue[I])); return 2; } lua_pushnil(S); return 1; });
    RegisterPanel(L,"GetQuestLogRequiredMoney",[](lua_State* S)->int { auto* C=GetContext(S); const auto* T=QuestData(C,SelectedQuest(C,luaL_optinteger(S,1,0))); lua_pushinteger(S,T?FMath::Max(-T->Money,0):0); return 1; });
    RegisterPanel(L,"GetQuestLogCompletionText",[](lua_State* S)->int { auto* C=GetContext(S); const auto* T=QuestData(C,SelectedQuest(C)); lua_pushstring(S,TCHAR_TO_UTF8(*QuestText(C,T?T->CompletedText:FString()))); return 1; });
    RegisterPanel(L,"GetQuestLogSpecialItemInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* Q=QuestData(C,SelectedQuest(C,luaL_checkinteger(S,1))); if(!Q || !Q->SourceItemId || !CountItem(C,Q->SourceItemId)) { lua_pushnil(S); return 1; }
        C->ConnectionManager->RequestItemTemplate(Q->SourceItemId); const auto* Item=C->ConnectionManager->PacketHandler.ItemTemplates.Find(Q->SourceItemId); if(!Item || !Item->UseSpellId) { lua_pushnil(S); return 1; }
        const auto* Display=FDbcStore::Get().ItemDisplayInfo().GetById(Item->DisplayId); const FString Link=FString::Printf(TEXT("|Hitem:%u:0:0:0:0:0:0:0|h[%s]|h"),Item->Id,*Item->Name);
        lua_pushstring(S,TCHAR_TO_UTF8(*Link)); lua_pushstring(S,Display?TCHAR_TO_UTF8(*(TEXT("Interface\\Icons\\")+Display->Icon1)):""); lua_pushinteger(S,CountItem(C,Item->Id)); return 3;
    });
    RegisterPanel(L,"GetQuestLogSpecialItemCooldown",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Q=QuestData(C,SelectedQuest(C,luaL_checkinteger(S,1))); const auto* Item=Q?C->ConnectionManager->PacketHandler.ItemTemplates.Find(Q->SourceItemId):nullptr; return PushSpellCooldown(S,Item?Item->UseSpellId:0); });
    RegisterPanel(L,"GetNumQuestLeaderBoards",[](lua_State* S)->int { auto* C=GetContext(S); const auto* T=QuestData(C,SelectedQuest(C,luaL_optinteger(S,1,0))); int32 Count=0; if(T) { for(uint32 Id:T->CreatureId) Count+=Id!=0; for(uint32 Id:T->ItemId) Count+=Id!=0; } lua_pushinteger(S,Count); return 1; });
    RegisterPanel(L,"GetQuestLogLeaderBoard",[](lua_State* S)->int {
        auto* C=GetContext(S); const int32 Wanted=luaL_checkinteger(S,1); const auto* Q=SelectedQuest(C,luaL_optinteger(S,2,0)); const auto* T=QuestData(C,Q); if(!Q || !T) { lua_pushnil(S); return 1; }
        int32 Row=0; FString Text,Type; uint32 Current=0,Required=0;
        const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const uint16 Field=QuestField(C,Q->QuestId); const uint64 Counters=P && Field?P->GetField64(Field+2):0;
        for(int32 I=0; I<4; ++I) if(T->CreatureId[I] && ++Row==Wanted)
        {
            Current=(Counters>>(I*16))&0xffff; Required=T->CreatureCount[I]; Type=(T->CreatureId[I]&0x80000000)?TEXT("object"):TEXT("monster"); Text=T->ObjectiveText[I];
            if(Text.IsEmpty()) { const auto* Name=C->ConnectionManager->PacketHandler.CreatureNameCache.Find(T->CreatureId[I]); Text=Name?*Name:FString::Printf(TEXT("Objective %d"),I+1); }
        }
        for(int32 I=0; I<6; ++I) if(T->ItemId[I] && ++Row==Wanted)
        {
            Current=CountItem(C,T->ItemId[I]); Required=T->ItemCount[I]; Type=TEXT("item"); C->ConnectionManager->RequestItemTemplate(T->ItemId[I]); const auto* Item=C->ConnectionManager->PacketHandler.ItemTemplates.Find(T->ItemId[I]); Text=Item?Item->Name:TEXT("Retrieving item information...");
        }
        if(!Required) { lua_pushnil(S); return 1; }
        Text+=FString::Printf(TEXT(": %u/%u"),Current,Required); lua_pushstring(S,TCHAR_TO_UTF8(*Text)); lua_pushstring(S,TCHAR_TO_UTF8(*Type)); lua_pushboolean(S,Current>=Required); return 3;
    });
    RegisterPanel(L,"GetQuestLogTimeLeft",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Q=SelectedQuest(C,luaL_optinteger(S,1,0)); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const uint16 F=Q?QuestField(C,Q->QuestId):0; const uint32 Expiry=P && F?P->GetField(F+4):0; if(Expiry) lua_pushnumber(S,FMath::Max(double(Expiry-FDateTime::UtcNow().ToUnixTimestamp()),0.0)); else lua_pushnil(S); return 1; });
    RegisterPanel(L,"GetQuestTimers",[](lua_State* S)->int { auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; int32 Count=0; if(C && C->ConnectionManager && P) { const auto& Log=C->ConnectionManager->PacketHandler.QuestLog; for(int32 I=0; I<Log.Num(); ++I) { const uint16 F=QuestField(C,Log[I].QuestId); if(F && P->GetField(F+4)) { lua_pushinteger(S,I+1); ++Count; } } } return Count; });
    RegisterPanel(L,"GetNumSkillLines",[](lua_State* S)->int { lua_pushinteger(S,Skills(GetContext(S)).Num()); return 1; });
    RegisterPanel(L,"GetSkillLineInfo",[](lua_State* S)->int {
        const auto Rows=Skills(GetContext(S)); const int32 I=luaL_checkinteger(S,1)-1;
        if(!Rows.IsValidIndex(I)) { lua_pushstring(S,""); lua_pushnil(S); lua_pushnil(S); for(int32 N=0;N<10;++N) lua_pushinteger(S,0); return 13; } const auto& R=Rows[I];
        lua_pushstring(S,TCHAR_TO_UTF8(*R.Name)); lua_pushnil(S); lua_pushnil(S); lua_pushinteger(S,R.Rank); lua_pushinteger(S,R.Temporary); lua_pushinteger(S,R.Permanent); lua_pushinteger(S,R.Max); lua_pushboolean(S,R.Abandonable);
        lua_pushnil(S); lua_pushnil(S); lua_pushinteger(S,0); lua_pushinteger(S,0); lua_pushstring(S,TCHAR_TO_UTF8(*R.Description)); return 13;
    });
    RegisterPanel(L,"GetSkillLineDescription",[](lua_State* S)->int { const auto Rows=Skills(GetContext(S)); const int32 I=luaL_checkinteger(S,1)-1; lua_pushstring(S,TCHAR_TO_UTF8(*(Rows.IsValidIndex(I)?Rows[I].Description:FString()))); return 1; });
    RegisterPanel(L,"SetSelectedSkill",[](lua_State* S)->int { return Selection(S,TEXT("skill"),true); });
    RegisterPanel(L,"GetSelectedSkill",[](lua_State* S)->int { return Selection(S,TEXT("skill"),false); });
    RegisterPanel(L,"GetAdjustedSkillPoints",[](lua_State* S)->int { const auto Rows=Skills(GetContext(S)); int32 Points=0; for(const auto& Row:Rows) Points+=Row.Temporary; lua_pushinteger(S,Points); return 1; });
    RegisterPanel(L,"SetGuildRosterSelection",[](lua_State* S)->int { return Selection(S,TEXT("guild"),true); });
    RegisterPanel(L,"GetGuildRosterSelection",[](lua_State* S)->int { return Selection(S,TEXT("guild"),false); });
    RegisterPanel(L,"GetAccountExpansionLevel",[](lua_State* S)->int { lua_pushinteger(S,2); return 1; });
    RegisterPanel(L,"HasKey",[](lua_State* S)->int { auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; bool Found=false; if(P) for(int32 I=0; I<32; ++I) Found|=P->GetField64(PlayerField::BASE+0x015C+I*2)!=0; lua_pushboolean(S,Found); return 1; });
    RegisterPanel(L,"IsPossessBarVisible",[](lua_State* S)->int { const auto* E=ResolveUnitToken(S); lua_pushboolean(S,E && E->IsUnit() && E->GetField64(UnitField::CHARM)!=0); return 1; });
    RegisterPanel(L,"PetCanBeAbandoned",[](lua_State* S)->int { auto* C=GetContext(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; lua_pushboolean(S,P && P->GetClassId()==3 && P->GetField64(UnitField::SUMMON)!=0); return 1; });
    RegisterPanel(L,"SetAbandonQuest",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) C->SelectedRows.FindOrAdd(TEXT("abandonQuest"))=C->SelectedRows.FindRef(TEXT("quest")); return 0; });
    RegisterPanel(L,"AbandonQuest",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) { const auto& Q=C->ConnectionManager->PacketHandler.QuestLog; const int32 I=C->SelectedRows.FindRef(TEXT("abandonQuest"))-1; if(Q.IsValidIndex(I)) C->ConnectionManager->SendQuestAbandon(Q[I].QuestId); } return 0; });
    RegisterPanel(L,"SetActionBarToggles",[](lua_State* S)->int { for(int32 I=1;I<=4;++I) SetUiPreference(S,FString::Printf(TEXT("actionbar%d"),I),!lua_isnil(S,I) && lua_toboolean(S,I) && (!lua_isnumber(S,I) || lua_tonumber(S,I)!=0)); return 0; });
    RegisterPanel(L,"GetActionBarToggles",[](lua_State* S)->int { for(int32 I=1;I<=4;++I) { if(GetUiPreference(S,FString::Printf(TEXT("actionbar%d"),I))) lua_pushinteger(S,1); else lua_pushnil(S); } return 4; });
    RegisterPanel(L,"Quit",[](lua_State* S)->int { FWowSavedVariables::SaveAll(S); FPlatformMisc::RequestExit(false); return 0; });
    RegisterPanel(L,"Screenshot",[](lua_State* S)->int { FScreenshotRequest::RequestScreenshot(false); return 0; });
    RegisterPanel(L,"IsPlayerResolutionAvailable",[](lua_State* S)->int { const auto* Settings=UGameUserSettings::GetGameUserSettings(); lua_pushboolean(S,Settings && Settings->GetScreenResolution().X>=640); return 1; });
}
#else
void WowLuaApi::RegisterPanelApi(lua_State*) {}
int32 WowLuaApi::GetUiPreference(lua_State*,const FString&,int32 Default) { return Default; }
void WowLuaApi::SetUiPreference(lua_State*,const FString&,int32) {}
#endif
