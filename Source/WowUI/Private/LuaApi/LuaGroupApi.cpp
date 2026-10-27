#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"

struct FWowGroupUiView
{
    struct FUnit
    {
        uint64 Guid=0,Pet=0;
        uint32 Health=0,MaxHealth=0,Power=0,MaxPower=0,Bytes=0,Level=0,Flags=0,Pvp=0,Auras=0;
        bool Online=false,Visible=false;
    };
    uint32 GroupRevision=MAX_uint32;
    uint64 SelectedGuid=0;
    TMap<FString,FUnit> Units;
    bool ReadyActive=false;
    double ReadyEnd=0;
    TMap<uint64,uint8> Responses;
};

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void Reg(lua_State* L,const char* Name,lua_CFunction Function) {lua_pushcfunction(L,Function); lua_setglobal(L,Name);}
void Flag(lua_State* L,bool Value) {if(Value) lua_pushinteger(L,1); else lua_pushnil(L);}
UWowConnectionManager* Connection(lua_State* L) {const auto* C=WowLuaApi::GetContext(L); return C?C->ConnectionManager:nullptr;}
FWowGroupUiView& View(FWowLuaContext* C) {if(!C->GroupUi) C->GroupUi=MakeShared<FWowGroupUiView>(); return *C->GroupUi;}
uint64 Local(const FWowLuaContext* C) {return C && C->EntityManager?C->EntityManager->LocalPlayerGuid:0;}
bool Leader(const FWowLuaContext* C) {return C && C->ConnectionManager && C->ConnectionManager->PacketHandler.GroupInfo.LocalPlayerIsLeader(Local(C));}
bool Officer(const FWowLuaContext* C) {return Leader(C) || (C && C->ConnectionManager && C->ConnectionManager->PacketHandler.GroupInfo.LocalPlayerIsAssistant());}
template<typename T> void Add(TArray<uint8>& Bytes,T Value) {Bytes.Append(reinterpret_cast<const uint8*>(&Value),sizeof(Value));}
void Text(TArray<uint8>& Bytes,const FString& Value) {FTCHARToUTF8 Utf8(*Value); Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length()); Bytes.Add(0);}
bool Send(FWowLuaContext* C,uint32 Opcode,const TArray<uint8>& Bytes={})
{if(!C || !C->ConnectionManager || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame) return false; C->ConnectionManager->SendRawPacket(Opcode,Bytes); return true;}
uint64 GuidArg(lua_State* L,int32 Index=1)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->ConnectionManager) return 0;
    if(const auto* E=WowLuaApi::ResolveUnitToken(L,Index)) return E->Guid;
    const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,Index,""));
    for(const auto& Member:C->ConnectionManager->PacketHandler.GroupInfo.Members) if(Member.Name.Equals(Name,ESearchCase::IgnoreCase)) return Member.Guid;
    return C->ConnectionManager->GetCharacterName().Equals(Name,ESearchCase::IgnoreCase)?Local(C):0;
}
uint64 RaidGuid(const FWowLuaContext* C,int32 Index)
{
    if(!C || !C->ConnectionManager) return 0; const auto& G=C->ConnectionManager->PacketHandler.GroupInfo;
    if(!G.IsRaidGroup() || Index<1 || Index>G.MemberCount) return 0;
    return Index==1?Local(C):G.Members.IsValidIndex(Index-2)?G.Members[Index-2].Guid:0;
}
FString Name(const FWowLuaContext* C,uint64 Guid)
{
    if(!C || !C->ConnectionManager) return FString(); if(Guid==Local(C) && !C->ConnectionManager->GetCharacterName().IsEmpty()) return C->ConnectionManager->GetCharacterName();
    const auto& H=C->ConnectionManager->PacketHandler; if(const auto* Member=H.GroupInfo.FindMember(Guid)) return Member->Name;
    return H.PlayerNameCache.FindRef(Guid);
}
void SetLeader(lua_State* L,bool Assistant,bool Apply)
{
    auto* C=WowLuaApi::GetContext(L); if(!Leader(C)) return; const uint64 Guid=GuidArg(L);
    if(Assistant && !C->ConnectionManager->PacketHandler.GroupInfo.IsRaidGroup()) return;
    if(!Guid || !C->ConnectionManager->PacketHandler.GroupInfo.FindMember(Guid)) return;
    TArray<uint8> Bytes; Add(Bytes,Guid); if(Assistant) Add(Bytes,uint8(Apply)); Send(C,Assistant?0x028F:0x0078,Bytes);
}
const char* const LootNames[]={"freeforall","roundrobin","master","group","needbeforegreed"};
int LootIndex(lua_State* L,int32 Index=1) {const FString Value=UTF8_TO_TCHAR(luaL_checkstring(L,Index)); for(int32 I=0;I<5;++I) if(Value==UTF8_TO_TCHAR(LootNames[I])) return I; return -1;}
void Loot(FWowLuaContext* C,uint32 Method,uint64 Master,uint32 Threshold)
{
    if(!Leader(C) || Method>4 || Threshold<2 || Threshold>6 || C->ConnectionManager->PacketHandler.GroupInfo.IsLfgGroup()) return;
    TArray<uint8> Bytes; Add(Bytes,Method); Add(Bytes,Master); Add(Bytes,Threshold); Send(C,0x007A,Bytes);
}
int Assignment(const char* Value) {return FCStringAnsi::Stricmp(Value,"MAINTANK")==0?0:FCStringAnsi::Stricmp(Value,"MAINASSIST")==0?1:-1;}
void SetAssignment(lua_State* L,bool Apply)
{
    auto* C=WowLuaApi::GetContext(L); const int32 Type=Assignment(luaL_checkstring(L,1)); if(Type<0) {luaL_argerror(L,1,"invalid party assignment"); return;}
    const uint64 Guid=GuidArg(L,2); if(!Officer(C) || !Guid) return;
    TArray<uint8> Bytes; Add(Bytes,uint8(Type)); Add(Bytes,uint8(Apply)); Add(Bytes,Guid); Send(C,0x038E,Bytes);
}
}

void WowLuaApi::RegisterGroupApi(lua_State* L)
{
    Reg(L,"InviteUnit",[](lua_State* S)->int {
        auto* C=GetContext(S); const uint64 Guid=GuidArg(S); const FString Value=Guid?Name(C,Guid):UTF8_TO_TCHAR(luaL_checkstring(S,1));
        if(Value.IsEmpty() || Value.Len()>63) return 0; TArray<uint8> Bytes; Text(Bytes,Value); Add(Bytes,uint32(0)); Send(C,0x006E,Bytes); return 0;
    });
    Reg(L,"AcceptGroup",[](lua_State* S)->int {auto* C=GetContext(S); if(C && C->ConnectionManager && !C->ConnectionManager->PacketHandler.PendingGroupInviter.IsEmpty()) {TArray<uint8> Bytes; Add(Bytes,uint32(0)); if(Send(C,0x0072,Bytes)) C->ConnectionManager->PacketHandler.PendingGroupInviter.Empty();} return 0;});
    Reg(L,"DeclineGroup",[](lua_State* S)->int {auto* C=GetContext(S); if(C && C->ConnectionManager && !C->ConnectionManager->PacketHandler.PendingGroupInviter.IsEmpty()) {if(Send(C,0x0073)) C->ConnectionManager->PacketHandler.PendingGroupInviter.Empty();} return 0;});
    Reg(L,"LeaveParty",[](lua_State* S)->int {auto* C=GetContext(S); if(C && C->ConnectionManager && !C->ConnectionManager->PacketHandler.GroupInfo.IsEmpty()) Send(C,0x007B); return 0;});
    Reg(L,"UninviteUnit",[](lua_State* S)->int {
        auto* C=GetContext(S); const uint64 Guid=GuidArg(S); if(!Officer(C) || !Guid || Guid==Local(C)) return 0;
        TArray<uint8> Bytes; Add(Bytes,Guid); Text(Bytes,UTF8_TO_TCHAR(luaL_optstring(S,2,""))); Send(C,0x0076,Bytes); return 0;
    });
    Reg(L,"PromoteToLeader",[](lua_State* S)->int {SetLeader(S,false,true); return 0;});
    Reg(L,"PromoteToAssistant",[](lua_State* S)->int {SetLeader(S,true,true); return 0;});
    Reg(L,"DemoteAssistant",[](lua_State* S)->int {SetLeader(S,true,false); return 0;});
    Reg(L,"ConvertToRaid",[](lua_State* S)->int {auto* C=GetContext(S); if(Leader(C)) {const auto& G=C->ConnectionManager->PacketHandler.GroupInfo; if(!G.IsRaidGroup() && G.MemberCount>=2 && !G.IsLfgGroup()) Send(C,0x028E);} return 0;});
    Reg(L,"GetPartyMember",[](lua_State* S)->int {const auto* N=Connection(S); const int32 I=luaL_checkinteger(S,1); if(I<1 || I>4) return luaL_argerror(S,1,"party index must be 1..4"); Flag(S,N && N->PacketHandler.GroupInfo.PartyMember(I-1)); return 1;});
    Reg(L,"UnitInParty",[](lua_State* S)->int {auto* C=GetContext(S); const uint64 Guid=GuidArg(S); bool Member=Guid && Guid==Local(C); if(C && C->ConnectionManager) for(int32 I=0;I<4;++I) if(const auto* M=C->ConnectionManager->PacketHandler.GroupInfo.PartyMember(I); M && M->Guid==Guid) Member=true; Flag(S,Member); return 1;});
    Reg(L,"GetPartyLeaderIndex",[](lua_State* S)->int {auto* C=GetContext(S); int32 Index=0; if(C && C->ConnectionManager) {const auto& G=C->ConnectionManager->PacketHandler.GroupInfo; for(int32 I=0;I<4;++I) if(const auto* M=G.PartyMember(I); M && M->Guid==G.LeaderGuid) Index=I+1;} lua_pushinteger(S,Index); return 1;});
    Reg(L,"GetRealNumPartyMembers",[](lua_State* S)->int {const auto* N=Connection(S); const auto* G=N?&N->PacketHandler.GroupInfo:nullptr; lua_pushinteger(S,G?G->PartyCount():0); return 1;});
    Reg(L,"GetRealNumRaidMembers",[](lua_State* S)->int {const auto* N=Connection(S); const auto* G=N?&N->PacketHandler.GroupInfo:nullptr; lua_pushinteger(S,G && G->IsRaidGroup()?G->MemberCount:0); return 1;});
    Reg(L,"IsRealPartyLeader",[](lua_State* S)->int {const auto* C=GetContext(S); Flag(S,Leader(C) && C->ConnectionManager->PacketHandler.GroupInfo.IsPartyGroup()); return 1;});
    Reg(L,"IsRealRaidLeader",[](lua_State* S)->int {const auto* C=GetContext(S); Flag(S,Leader(C) && C->ConnectionManager->PacketHandler.GroupInfo.IsRaidGroup()); return 1;});
    Reg(L,"UnitGroupRolesAssigned",[](lua_State* S)->int {auto* C=GetContext(S); const uint64 Guid=GuidArg(S); uint8 Roles=0; if(C && C->ConnectionManager) {const auto& G=C->ConnectionManager->PacketHandler.GroupInfo; if(Guid==Local(C)) Roles=G.SelfRoles; else if(const auto* M=G.FindMember(Guid)) Roles=M->Roles;} Flag(S,Roles&2); Flag(S,Roles&4); Flag(S,Roles&8); return 3;});
    Reg(L,"UnitInRange",[](lua_State* S)->int {auto* C=GetContext(S); const auto* E=ResolveUnitToken(S); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const bool Group=E && C && C->ConnectionManager && (E->Guid==Local(C) || C->ConnectionManager->PacketHandler.GroupInfo.FindMember(E->Guid) || E->GetField64(UnitField::SUMMONEDBY)==Local(C)); Flag(S,Group && P && C->EntityManager->Find(E->Guid) && FVector::DistSquared(P->Movement.Position,E->Movement.Position)<=1600); return 1;});
    Reg(L,"SetRaidRosterSelection",[](lua_State* S)->int {auto* C=GetContext(S); if(C) View(C).SelectedGuid=RaidGuid(C,luaL_checkinteger(S,1)); return 0;});
    Reg(L,"GetRaidRosterSelection",[](lua_State* S)->int {auto* C=GetContext(S); int32 Selected=0; if(C && C->ConnectionManager) for(int32 I=1;I<=C->ConnectionManager->PacketHandler.GroupInfo.MemberCount;++I) if(RaidGuid(C,I) && RaidGuid(C,I)==View(C).SelectedGuid) Selected=I; lua_pushinteger(S,Selected); return 1;});
    Reg(L,"GetRaidRosterInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); const uint64 Guid=RaidGuid(C,luaL_checkinteger(S,1)); if(!Guid) {for(int32 I=0;I<14;++I) {if(I==1) lua_pushinteger(S,0); else if(I==2 || I==3) lua_pushinteger(S,1); else lua_pushnil(S);} return 14;}
        auto& H=C->ConnectionManager->PacketHandler; const auto& G=H.GroupInfo; const auto* M=G.FindMember(Guid); const auto* E=H.FindUiUnit(Guid);
        const auto* Remote=H.RemoteGroupMembers.Find(Guid); const bool Self=Guid==Local(C),Online=Self || (Remote?(Remote->Status&1)!=0:M && (M->Status&1));
        const uint8 Flags=Self?G.SelfFlags:M?M->Flags:0,Roles=Self?G.SelfRoles:M?M->Roles:0;
        lua_pushstring(S,TCHAR_TO_UTF8(*Name(C,Guid))); lua_pushinteger(S,Guid==G.LeaderGuid?2:(Flags&1)?1:0); lua_pushinteger(S,(Self?G.SelfSubgroup:M?M->Group:0)+1);
        lua_pushinteger(S,Online && E?E->GetLevel():0);
        const uint8 Class=E?E->GetFieldByte(UnitField::BYTES_0,1):M?M->Class:0;
        if((Class>=1 && Class<=9) || Class==11)
        {lua_getglobal(S,"GetClassInfo"); lua_pushinteger(S,Class); if(lua_pcall(S,1,2,0)!=0) {lua_pop(S,1); lua_pushnil(S); lua_pushnil(S);}}
        else {lua_pushnil(S); lua_pushnil(S);}
        const uint32 Zone=Self?ResolveAreaId(C):Remote?Remote->Zone:0; const auto* Area=FDbcStore::Get().AreaTable().GetById(Zone);
        if(!Online) {lua_getglobal(S,"PLAYER_OFFLINE");} else if(Area) lua_pushstring(S,TCHAR_TO_UTF8(*Area->Name)); else lua_pushnil(S);
        Flag(S,Online); Flag(S,Online && ((E && E->GetMaxHealth()>0 && E->GetHealth()<=0) || (Remote && (Remote->Status&12))));
        if(Flags&2) lua_pushstring(S,"MAINTANK"); else if(Flags&4) lua_pushstring(S,"MAINASSIST"); else lua_pushnil(S);
        Flag(S,G.LootMethod==2 && G.MasterLooter==Guid); Flag(S,Roles&2); Flag(S,Roles&4); Flag(S,Roles&8); return 14;
    });
    Reg(L,"SetRaidSubgroup",[](lua_State* S)->int {auto* C=GetContext(S); const uint64 Guid=RaidGuid(C,luaL_checkinteger(S,1)); const int32 Group=luaL_checkinteger(S,2); if(Officer(C) && Guid && Group>=1 && Group<=8) {TArray<uint8> Bytes; Text(Bytes,Name(C,Guid)); Add(Bytes,uint8(Group-1)); Send(C,0x027E,Bytes);} return 0;});
    Reg(L,"SwapRaidSubgroup",[](lua_State* S)->int {auto* C=GetContext(S); const uint64 A=RaidGuid(C,luaL_checkinteger(S,1)),B=RaidGuid(C,luaL_checkinteger(S,2)); if(Officer(C) && A && B && A!=B) {TArray<uint8> Bytes; Text(Bytes,Name(C,A)); Text(Bytes,Name(C,B)); Send(C,0x0280,Bytes);} return 0;});
    Reg(L,"SetPartyAssignment",[](lua_State* S)->int {SetAssignment(S,true); return 0;});
    Reg(L,"ClearPartyAssignment",[](lua_State* S)->int {SetAssignment(S,false); return 0;});
    Reg(L,"GetPartyAssignment",[](lua_State* S)->int {
        auto* C=GetContext(S); const int32 Type=Assignment(luaL_checkstring(S,1)); if(Type<0) return luaL_argerror(S,1,"invalid party assignment");
        const uint8 Bit=Type==0?2:4; if(!C || !C->ConnectionManager) {lua_pushnil(S); return 1;} const auto& G=C->ConnectionManager->PacketHandler.GroupInfo;
        if(!lua_isnoneornil(S,2)) {const uint64 Guid=GuidArg(S,2); const auto* M=G.FindMember(Guid); Flag(S,Guid && ((Guid==Local(C)?G.SelfFlags:M?M->Flags:0)&Bit)); return 1;}
        int32 Count=0; if(G.SelfFlags&Bit) {lua_pushstring(S,G.IsRaidGroup()?"raid1":"player"); ++Count;}
        for(int32 I=0;I<G.Members.Num();++I) if(G.Members[I].Flags&Bit) {const FString Token=FString(G.IsRaidGroup()?TEXT("raid"):TEXT("party"))+FString::FromInt(I+(G.IsRaidGroup()?2:1)); lua_pushstring(S,TCHAR_TO_UTF8(*Token)); ++Count;} return Count;
    });
    Reg(L,"GetLootMethod",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* N=Connection(S); const auto* G=N?&N->PacketHandler.GroupInfo:nullptr; lua_pushstring(S,LootNames[G?FMath::Min<int32>(G->LootMethod,4):0]);
        int32 Party=-1,Raid=-1; if(G && G->LootMethod==2 && G->MasterLooter) {if(G->IsRaidGroup()) {for(int32 I=1;I<=G->MemberCount;++I) if(RaidGuid(C,I)==G->MasterLooter) Raid=I;} else {if(G->MasterLooter==Local(C)) Party=0; for(int32 I=0;I<G->Members.Num();++I) if(G->Members[I].Guid==G->MasterLooter) Party=I+1;}}
        if(Party>=0) lua_pushinteger(S,Party); else lua_pushnil(S); if(Raid>=0) lua_pushinteger(S,Raid); else lua_pushnil(S); return 3;
    });
    Reg(L,"GetLootThreshold",[](lua_State* S)->int {const auto* N=Connection(S); lua_pushinteger(S,N?N->PacketHandler.GroupInfo.LootThreshold:2); return 1;});
    Reg(L,"SetLootMethod",[](lua_State* S)->int {auto* C=GetContext(S); const int32 Method=LootIndex(S); if(C && C->ConnectionManager && Method>=0) {const auto& G=C->ConnectionManager->PacketHandler.GroupInfo; const uint64 Master=Method==2?GuidArg(S,2):0; if(Method!=2 || Master) Loot(C,uint32(Method),Master,G.LootThreshold);} return 0;});
    Reg(L,"SetLootThreshold",[](lua_State* S)->int {auto* C=GetContext(S); const int32 Quality=luaL_checkinteger(S,1); if(C && C->ConnectionManager && Quality>=2 && Quality<=6) {const auto& G=C->ConnectionManager->PacketHandler.GroupInfo; Loot(C,G.LootMethod,G.MasterLooter,uint32(Quality));} return 0;});
}

void WowLuaApi::UpdateGroupState(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->ConnectionManager || !C->EntityManager || !C->EventSystem) return;
    auto& V=View(C); auto& H=C->ConnectionManager->PacketHandler; const auto& G=H.GroupInfo; auto* Events=C->EventSystem;
    if(C->FocusGuid && !H.FindUiUnit(C->FocusGuid)) {C->FocusGuid=0; Events->FireEvent(TEXT("PLAYER_FOCUS_CHANGED"));}
    if(V.GroupRevision!=H.GroupRevision)
    {
        const bool Notify=V.GroupRevision!=MAX_uint32 || !G.IsEmpty(); V.GroupRevision=H.GroupRevision;
        if(!G.IsRaidGroup() || (V.SelectedGuid!=Local(C) && !G.FindMember(V.SelectedGuid))) V.SelectedGuid=0;
        if(Notify) {Events->FireEvent(TEXT("PARTY_MEMBERS_CHANGED")); Events->FireEvent(TEXT("RAID_ROSTER_UPDATE")); Events->FireEvent(TEXT("PARTY_LEADER_CHANGED")); Events->FireEvent(TEXT("PARTY_LOOT_METHOD_CHANGED")); Events->FireEvent(TEXT("RAID_TARGET_UPDATE"));}
    }
    TMap<FString,FWowGroupUiView::FUnit> Units;
    const auto Update=[&](const FString& Token,uint64 Guid,uint16 Status)
    {
        const auto* E=H.FindUiUnit(Guid); if(!E) return;
        FWowGroupUiView::FUnit U; U.Guid=Guid; U.Health=E->GetHealth(); U.MaxHealth=E->GetMaxHealth(); U.Bytes=E->GetField(UnitField::BYTES_0); U.Level=E->GetLevel();
        const uint8 Power=E->GetFieldByte(UnitField::BYTES_0,3); U.Power=Power<7?E->GetField(UnitField::POWER1+Power):0; U.MaxPower=Power<7?E->GetField(UnitField::MAXPOWER1+Power):0;
        U.Flags=HashCombineFast(E->GetField(UnitField::FLAGS),E->GetField(PlayerField::FLAGS)); U.Pvp=E->GetField(UnitField::BYTES_2); U.Pet=E->GetField64(UnitField::SUMMON);
        U.Online=(Status&1)!=0; U.Visible=C->EntityManager->Find(Guid)!=nullptr;
        for(const auto& Aura:E->Auras) if(Aura.bActive) U.Auras=HashCombineFast(U.Auras,HashCombineFast(Aura.SpellId,uint32(Aura.Flags)|(uint32(Aura.Charges)<<8)));
        const auto* Old=V.Units.Find(Token); const bool First=!Old || Old->Guid!=Guid;
        const auto Event=[&](const TCHAR* Name) {Events->FireEvent(Name,{Token});};
        if(First || Old->Bytes!=U.Bytes) {Event(TEXT("UNIT_NAME_UPDATE")); Event(TEXT("UNIT_MODEL_CHANGED")); Event(TEXT("UNIT_DISPLAYPOWER"));}
        if(First || Old->Health!=U.Health || Old->Online!=U.Online) Event(TEXT("UNIT_HEALTH"));
        if(First || Old->MaxHealth!=U.MaxHealth) Event(TEXT("UNIT_MAXHEALTH"));
        static const TCHAR* Powers[]={TEXT("MANA"),TEXT("RAGE"),TEXT("FOCUS"),TEXT("ENERGY"),TEXT("HAPPINESS"),TEXT("RUNES"),TEXT("RUNIC_POWER")};
        if(Power<7)
        {
            if(First || Old->Power!=U.Power || Old->Bytes!=U.Bytes) Events->FireEvent(FString(TEXT("UNIT_"))+Powers[Power],{Token});
            if(First || Old->MaxPower!=U.MaxPower || Old->Bytes!=U.Bytes) Events->FireEvent(FString(TEXT("UNIT_MAX"))+Powers[Power],{Token});
        }
        if(First || Old->Level!=U.Level) Event(TEXT("UNIT_LEVEL"));
        if(First || Old->Flags!=U.Flags || Old->Pvp!=U.Pvp) Event(TEXT("UNIT_FLAGS"));
        if(First || Old->Auras!=U.Auras) Event(TEXT("UNIT_AURA"));
        if(First || Old->Pet!=U.Pet) Event(TEXT("UNIT_PET"));
        Units.Add(Token,U);
    };
    int32 PartyIndex=0;
    for(int32 I=0;I<G.Members.Num();++I)
    {
        const auto& M=G.Members[I]; const auto* Remote=H.RemoteGroupMembers.Find(M.Guid); const uint16 Status=Remote?Remote->Status:M.Status;
        if(M.Group==G.SelfSubgroup && PartyIndex<4) {++PartyIndex; const FString Token=TEXT("party")+FString::FromInt(PartyIndex); Update(Token,M.Guid,Status); if(const auto* E=H.FindUiUnit(M.Guid); E && E->GetField64(UnitField::SUMMON)) Update(TEXT("partypet")+FString::FromInt(PartyIndex),E->GetField64(UnitField::SUMMON),Status);}
        if(G.IsRaidGroup())
        {
            Update(TEXT("raid")+FString::FromInt(I+2),M.Guid,Status);
            if(const auto* E=H.FindUiUnit(M.Guid); E && E->GetField64(UnitField::SUMMON)) Update(TEXT("raidpet")+FString::FromInt(I+2),E->GetField64(UnitField::SUMMON),Status);
        }
    }
    if(G.IsRaidGroup())
    {
        Update(TEXT("raid1"),Local(C),1);
        if(const auto* P=C->EntityManager->GetLocalPlayer(); P && P->GetField64(UnitField::SUMMON)) Update(TEXT("raidpet1"),P->GetField64(UnitField::SUMMON),1);
    }
    if(C->FocusGuid)
    {
        uint16 Status=C->EntityManager->Find(C->FocusGuid)?1:0;
        if(const auto* Remote=H.RemoteGroupMembers.Find(C->FocusGuid)) Status=Remote->Status;
        Update(TEXT("focus"),C->FocusGuid,Status);
    }
    V.Units=MoveTemp(Units);
    const bool TimedOut=H.ReadyCheck.bActive && H.ReadyCheck.GetTimeLeftSeconds()<=0;
    if(TimedOut) {if(H.ReadyCheck.InitiatorGuid==Local(C)) C->ConnectionManager->SendFinishReadyCheck(); H.ReadyCheck.Clear();}
    const auto& Ready=H.ReadyCheck;
    if(Ready.bActive && (!V.ReadyActive || V.ReadyEnd!=Ready.EndTimeSeconds))
    {if(V.ReadyActive) Events->FireEventBoolean(TEXT("READY_CHECK_FINISHED"),true); Events->FireEventStringNumber(TEXT("READY_CHECK"),Name(C,Ready.InitiatorGuid),Ready.GetTimeLeftSeconds()); V.Responses.Reset();}
    for(const auto& Response:Ready.Responses) if(!V.Responses.Contains(Response.Key) || V.Responses[Response.Key]!=Response.Value)
    {
        TArray<FString> Tokens; if(Response.Key==Local(C)) Tokens.Add(TEXT("player"));
        for(const auto& Pair:V.Units) if(Pair.Value.Guid==Response.Key) Tokens.Add(Pair.Key);
        for(const auto& Unit:Tokens) Events->FireEventTyped(TEXT("READY_CHECK_CONFIRM"),[&](lua_State* S){lua_pushstring(S,TCHAR_TO_UTF8(*Unit)); lua_pushinteger(S,Response.Value==1?1:0); return 2;});
    }
    if(V.ReadyActive && !Ready.bActive) Events->FireEventBoolean(TEXT("READY_CHECK_FINISHED"),G.IsEmpty());
    V.ReadyActive=Ready.bActive; V.ReadyEnd=Ready.EndTimeSeconds; V.Responses=Ready.Responses;
}
#else
void WowLuaApi::RegisterGroupApi(lua_State*) {}
void WowLuaApi::UpdateGroupState(lua_State*) {}
#endif
