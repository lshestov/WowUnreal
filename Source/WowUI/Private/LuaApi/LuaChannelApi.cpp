#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"
#include "WowSavedVariables.h"
#include "Formats/Dbc/DbcStore.h"

struct FWowChannelUiView
{
    FString Selected,Membership;
    uint32 Revision=0,Zone=0;
    double ZoneChangedAt=0;
    int32 Names=-1,GroupCount=-1;
    TSet<uint64> RequestedNames;
};
#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
using FDefinition=FItemUiDbc::FChatChannel;
struct FDisplay { FString Name,Key; int32 Header=-1,Category=0,Count=0; uint32 Id=0; bool Group=false; };
const char* Categories[]={"CHANNEL_CATEGORY_GROUP","CHANNEL_CATEGORY_WORLD","CHANNEL_CATEGORY_CUSTOM"};
void Register(lua_State* L,const char* Name,lua_CFunction Fn) {lua_pushcfunction(L,Fn); lua_setglobal(L,Name);}
UWowConnectionManager* Connection(lua_State* L) {const auto* C=WowLuaApi::GetContext(L); return C?C->ConnectionManager:nullptr;}
FWowChannelUiView& View(lua_State* L) {auto* C=WowLuaApi::GetContext(L); if(!C->Channels) C->Channels=MakeShared<FWowChannelUiView>(); return *C->Channels;}
FString String(lua_State* L,const char* Key,const FString& Fallback=FString()) {lua_getglobal(L,Key); const FString Value=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):Fallback; lua_pop(L,1); return Value;}
void Flag(lua_State* L,bool Value) {if(Value) lua_pushinteger(L,1); else lua_pushnil(L);}
const FDefinition* Definition(uint32 Id) {return FDbcStore::Get().ItemUi().ChatChannels().FindByPredicate([&](const FDefinition& C){return C.Id==Id;});}
const FDefinition* Definition(const FString& Name) {return FDbcStore::Get().ItemUi().ChatChannels().FindByPredicate([&](const FDefinition& C){return C.Name.Equals(Name,ESearchCase::IgnoreCase);});}
FString Short(const FWowChannel& C) {const auto* D=Definition(C.Id); return D?D->Name:C.Name;}
FWowChannel* Resolve(lua_State* L,int32 Index=1)
{
    auto* N=Connection(L); if(!N) return nullptr;
    if(lua_isnumber(L,Index)) {const int32 Number=lua_tointeger(L,Index); return N->PacketHandler.Channels.FindByPredicate([&](const FWowChannel& C){return C.Number==Number;});}
    const FString Name=UTF8_TO_TCHAR(luaL_optstring(L,Index,"")); if(auto* C=N->PacketHandler.FindChannel(Name)) return C;
    if(const auto* D=Definition(Name)) return N->PacketHandler.Channels.FindByPredicate([&](const FWowChannel& C){return C.Id==D->Id;});
    return nullptr;
}
FString WindowKey(int32 Window,const FString& Name) {return FString::Printf(TEXT("channelFilter_%d_%s"),Window,*Name.ToLower());}
bool Included(lua_State* L,int32 Window,const FWowChannel& Channel) {return WowLuaApi::GetUiPreference(L,WindowKey(Window,Short(Channel)),Window==1?1:0)!=0;}
void Refresh(lua_State* L,bool Windows=false)
{
    if(const auto* C=WowLuaApi::GetContext(L); C && C->EventSystem) {if(Windows) C->EventSystem->FireEvent(TEXT("UPDATE_CHAT_WINDOWS")); C->EventSystem->FireEvent(TEXT("CHANNEL_UI_UPDATE"));}
}
TArray<FDisplay> Rows(lua_State* L)
{
    TArray<FDisplay> Result; const auto* N=Connection(L);
    for(int32 Category=0;Category<3;++Category)
    {
        FDisplay Header; Header.Header=Header.Category=Category; Header.Name=String(L,Categories[Category]); Header.Key=FString::Printf(TEXT("#%d"),Category);
        if(Category==0) Header.Count=N && N->PacketHandler.GroupInfo.MemberCount>1?1:0;
        if(Category==1) Header.Count=FDbcStore::Get().ItemUi().ChatChannels().Num();
        if(Category==2 && N) for(const auto& C:N->PacketHandler.Channels) if(!C.Id) ++Header.Count;
        Result.Add(Header);
        if(WowLuaApi::GetUiPreference(L,FString::Printf(TEXT("channelCollapsed%d"),Category))) continue;
        if(Category==0 && N && N->PacketHandler.GroupInfo.MemberCount>1)
        {FDisplay R; R.Group=true; R.Key=TEXT("@group"); R.Name=String(L,N->PacketHandler.GroupInfo.GroupType?"CHAT_MSG_RAID":"CHAT_MSG_PARTY"); Result.Add(R);}
        if(Category==1) for(const auto& D:FDbcStore::Get().ItemUi().ChatChannels()) {FDisplay R; R.Category=1; R.Id=D.Id; R.Name=D.Name; R.Key=FString::Printf(TEXT("@%u"),D.Id); Result.Add(R);}
        if(Category==2 && N) {TArray<FWowChannel> Sorted=N->PacketHandler.Channels; Sorted.Sort([](const FWowChannel& A,const FWowChannel& B){return A.Number<B.Number;}); for(const auto& C:Sorted) if(!C.Id) {FDisplay R; R.Category=2; R.Name=C.Name; R.Key=C.Name.ToLower(); Result.Add(R);}}
    }
    return Result;
}
FWowChannel* Channel(lua_State* L,const FDisplay& Display)
{
    auto* N=Connection(L); if(!N || Display.Header>=0 || Display.Group) return nullptr;
    return Display.Id?N->PacketHandler.Channels.FindByPredicate([&](const FWowChannel& C){return C.Id==Display.Id;}):N->PacketHandler.FindChannel(Display.Name);
}
int32 Selected(lua_State* L) {const auto All=Rows(L); const auto& Key=View(L).Selected; for(int32 I=0;I<All.Num();++I) if(All[I].Key==Key && All[I].Header<0) return I+1; return 0;}
void Watch(lua_State* L,const FString& Name)
{
    auto* N=Connection(L); if(!N || N->GetState()!=EWowSessionState::WorldInGame) return;
    const FTCHARToUTF8 Text(*Name); TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(Text.Get()),Text.Length()); Data.Add(0); N->SendRawPacket(0x03EF,Data);
}
TArray<FWowChannelMember> Members(lua_State* L,const FDisplay& Display)
{
    TArray<FWowChannelMember> Result; auto* N=Connection(L); if(!N) return Result;
    if(Display.Group)
    {
        const auto& G=N->PacketHandler.GroupInfo; const uint64 Local=N->PacketHandler.EntityManager.LocalPlayerGuid;
        Result.Add({Local,uint8(G.LeaderGuid==Local?1:0)}); for(const auto& M:G.Members) Result.Add({M.Guid,uint8(G.LeaderGuid==M.Guid?1:0)});
    }
    else if(auto* C=Channel(L,Display)) {N->RequestChannelRoster(C->Name); if(C->RosterKnown) Result=C->Members;}
    const auto Name=[&](uint64 Guid){const auto Local=N->GetCharacterName(); return Guid==N->PacketHandler.EntityManager.LocalPlayerGuid && !Local.IsEmpty()?Local:N->PacketHandler.PlayerNameCache.FindRef(Guid);};
    Result.Sort([&](const FWowChannelMember& A,const FWowChannelMember& B){const auto AN=Name(A.Guid),BN=Name(B.Guid); return AN==BN?A.Guid<B.Guid:AN.Compare(BN,ESearchCase::IgnoreCase)<0;}); return Result;
}
const FAreaTableDbcEntry* Zone(lua_State* L)
{
    const auto* C=WowLuaApi::GetContext(L); const uint32 Area=WowLuaApi::ResolveAreaId(C);
    const auto* Row=FDbcStore::Get().AreaTable().GetById(Area); for(int32 Depth=0;Row && Row->ParentAreaID && Depth<8;++Depth) Row=FDbcStore::Get().AreaTable().GetById(Row->ParentAreaID); return Row;
}
int Join(lua_State* L)
{
    auto* N=Connection(L); if(!N) return 0;
    FString Name=UTF8_TO_TCHAR(luaL_checkstring(L,1)); const FString Password=UTF8_TO_TCHAR(luaL_optstring(L,2,"")); const int32 Window=luaL_optinteger(L,3,1); uint32 Id=0; int32 Number=0;
    if(const auto* D=Definition(Name))
    {
        const auto* Area=Zone(L); if(!Area) return 0;
        if((D->Flags&0x10) && !(Area->Flags&8)) return 0;
        Id=D->Id; Name=D->Pattern; Name.ReplaceInline(TEXT("%s"),(D->Flags&0x10)?TEXT("City"):*Area->Name); Number=Id==1?1:Id==2?2:Id==22?3:0;
    }
    if(!N->JoinChannel(Name,Password,Id,Number,Window)) return 0;
    if(Id==1 || Id==2 || Id==22) WowLuaApi::SetUiPreference(L,FString::Printf(TEXT("builtinChannelEnabled%u"),Id),1);
    lua_pushinteger(L,Id); lua_pushstring(L,TCHAR_TO_UTF8(*Name)); return 2;
}
int ConfigureWindow(lua_State* L,bool Add)
{
    const int32 Window=luaL_checkinteger(L,1); if(Window<1 || Window>10) return 0;
    const FString Name=UTF8_TO_TCHAR(luaL_checkstring(L,2)); WowLuaApi::SetUiPreference(L,WindowKey(Window,Name),Add?1:0); Refresh(L,true); return 0;
}
int Moderation(lua_State* L,uint32 Opcode)
{
    auto* N=Connection(L); auto* C=Resolve(L); if(N && C) N->ChannelCommand(Opcode,C->Name,UTF8_TO_TCHAR(luaL_optstring(L,2,""))); return 0;
}
void UpdateBuiltin(lua_State* L)
{
    auto* N=Connection(L); if(!N || N->GetState()!=EWowSessionState::WorldInGame) return;
    const auto* Area=Zone(L); if(!Area) return;
    auto& V=View(L); const double Now=FPlatformTime::Seconds();
    if(V.Zone!=Area->ID) {V.Zone=Area->ID; V.ZoneChangedAt=Now;}
    // Allow the server's automatic zone-channel replacement to arrive first.
    if(Now-V.ZoneChangedAt<.5) return;
    for(uint32 Id:{1u,2u,22u})
    {
        const auto* D=Definition(Id); if(!D) continue;
        auto* Row=N->PacketHandler.Channels.FindByPredicate([&](const FWowChannel& C){return C.Id==Id;});
        const bool Allowed=WowLuaApi::GetUiPreference(L,FString::Printf(TEXT("builtinChannelEnabled%u"),Id),1) && !(Area->Flags&0x10000) && (!(D->Flags&0x10) || (Area->Flags&8));
        FString Name=D->Pattern; Name.ReplaceInline(TEXT("%s"),(D->Flags&0x10)?TEXT("City"):*Area->Name);
        if(Row) {if(!Allowed || !Row->Name.Equals(Name,ESearchCase::IgnoreCase)) N->LeaveChannel(Row->Name); continue;}
        if(Allowed) N->JoinChannel(Name,{},Id,Id==1?1:Id==2?2:3,1);
    }
}
}
void WowLuaApi::RegisterChannelApi(lua_State* L)
{
    Register(L,"JoinChannelByName",Join); Register(L,"JoinTemporaryChannel",Join); Register(L,"JoinPermanentChannel",Join);
    Register(L,"LeaveChannelByName",[](lua_State* S)->int {if(auto* N=Connection(S)) if(const auto* C=Resolve(S)) {if(C->Id==1 || C->Id==2 || C->Id==22) SetUiPreference(S,FString::Printf(TEXT("builtinChannelEnabled%u"),C->Id),0); N->LeaveChannel(C->Name);} return 0;});
    Register(L,"GetChannelList",[](lua_State* S)->int {const auto* N=Connection(S); if(!N) return 0; TArray<FWowChannel> All=N->PacketHandler.Channels; All.Sort([](const FWowChannel& A,const FWowChannel& B){return A.Number<B.Number;}); for(const auto& C:All) {lua_pushinteger(S,C.Number); lua_pushstring(S,TCHAR_TO_UTF8(*Short(C)));} return All.Num()*2;});
    Register(L,"GetChannelName",[](lua_State* S)->int {const auto* C=Resolve(S); lua_pushinteger(S,C?C->Number:0); lua_pushstring(S,C?TCHAR_TO_UTF8(*C->Name):nullptr); lua_pushinteger(S,0); return 3;});
    Register(L,"GetChatWindowChannels",[](lua_State* S)->int {const auto* N=Connection(S); const int32 Window=luaL_checkinteger(S,1); if(!N || Window<1 || Window>10) return 0; int32 Count=0; for(const auto& C:N->PacketHandler.Channels) if(Included(S,Window,C)) {lua_pushstring(S,TCHAR_TO_UTF8(*Short(C))); lua_pushinteger(S,C.Id); Count+=2;} return Count;});
    Register(L,"AddChatWindowChannel",[](lua_State* S)->int{return ConfigureWindow(S,true);}); Register(L,"RemoveChatWindowChannel",[](lua_State* S)->int{return ConfigureWindow(S,false);});
    Register(L,"EnumerateServerChannels",[](lua_State* S)->int {const auto& All=FDbcStore::Get().ItemUi().ChatChannels(); for(const auto& D:All) lua_pushstring(S,TCHAR_TO_UTF8(*D.Name)); return All.Num();});
    Register(L,"GetNumDisplayChannels",[](lua_State* S)->int {lua_pushinteger(S,Rows(S).Num()); return 1;});
    Register(L,"GetChannelDisplayInfo",[](lua_State* S)->int {
        const auto All=Rows(S); const int32 I=luaL_checkinteger(S,1)-1; if(!All.IsValidIndex(I)) return 0;
        const auto& R=All[I]; const auto* C=Channel(S,R); lua_pushstring(S,TCHAR_TO_UTF8(*R.Name)); Flag(S,R.Header>=0); Flag(S,R.Header>=0 && GetUiPreference(S,FString::Printf(TEXT("channelCollapsed%d"),R.Header))!=0);
        if(C) lua_pushinteger(S,C->Number); else lua_pushnil(S);
        int32 Count=C?int32(C->Count):R.Count; if(R.Group && Connection(S)) Count=Connection(S)->PacketHandler.GroupInfo.MemberCount;
        if(Count>0) lua_pushinteger(S,Count); else lua_pushnil(S); Flag(S,C || R.Group); lua_pushstring(S,Categories[R.Category]); lua_pushnil(S); lua_pushnil(S); return 9;
    });
    Register(L,"GetSelectedDisplayChannel",[](lua_State* S)->int {const int32 I=Selected(S); if(I) lua_pushinteger(S,I); else lua_pushnil(S); return 1;});
    Register(L,"SetSelectedDisplayChannel",[](lua_State* S)->int {const auto All=Rows(S); const int32 I=luaL_checkinteger(S,1)-1; if(!All.IsValidIndex(I) || All[I].Header>=0) return 0; if(View(S).Selected!=All[I].Key) {View(S).Selected=All[I].Key; const auto* C=Channel(S,All[I]); Watch(S,C?C->Name:FString()); if(C) Connection(S)->RequestChannelRoster(C->Name,true); Refresh(S);} return 0;});
    Register(L,"GetNumChannelMembers",[](lua_State* S)->int {const auto All=Rows(S); const int32 I=luaL_checkinteger(S,1)-1; if(!All.IsValidIndex(I) || All[I].Header>=0) {lua_pushnil(S); return 1;} const auto M=Members(S,All[I]); if(M.Num()) lua_pushinteger(S,M.Num()); else lua_pushnil(S); return 1;});
    Register(L,"GetChannelRosterInfo",[](lua_State* S)->int {
        const auto All=Rows(S); const int32 I=luaL_checkinteger(S,1)-1,Index=luaL_checkinteger(S,2)-1; if(!All.IsValidIndex(I) || All[I].Header>=0) return 0;
        const auto List=Members(S,All[I]); auto* N=Connection(S); if(!N || !List.IsValidIndex(Index)) return 0;
        const auto& M=List[Index]; const FString Local=N->GetCharacterName(); const FString Name=M.Guid==N->PacketHandler.EntityManager.LocalPlayerGuid && !Local.IsEmpty()?Local:N->PacketHandler.PlayerNameCache.FindRef(M.Guid);
        if(Name.IsEmpty() && !View(S).RequestedNames.Contains(M.Guid)) {View(S).RequestedNames.Add(M.Guid); N->SendNameQuery(int64(M.Guid));}
        lua_pushstring(S,TCHAR_TO_UTF8(*(Name.IsEmpty()?String(S,"UNKNOWN",TEXT("Unknown")):Name))); Flag(S,(M.Flags&1)!=0); Flag(S,(M.Flags&2)!=0); Flag(S,(M.Flags&8)!=0); lua_pushnil(S); lua_pushnil(S); return 6;
    });
    Register(L,"CollapseChannelHeader",[](lua_State* S)->int {const auto All=Rows(S); const int32 I=luaL_checkinteger(S,1)-1; if(All.IsValidIndex(I) && All[I].Header>=0) {SetUiPreference(S,FString::Printf(TEXT("channelCollapsed%d"),All[I].Header),1); Refresh(S);} return 0;});
    Register(L,"ExpandChannelHeader",[](lua_State* S)->int {const auto All=Rows(S); const int32 I=luaL_checkinteger(S,1)-1; if(All.IsValidIndex(I) && All[I].Header>=0) {SetUiPreference(S,FString::Printf(TEXT("channelCollapsed%d"),All[I].Header),0); Refresh(S);} return 0;});
    Register(L,"SetChannelWatch",[](lua_State* S)->int {const auto* C=Resolve(S); Watch(S,C?C->Name:FString()); return 0;}); Register(L,"ClearChannelWatch",[](lua_State* S)->int {Watch(S,{}); return 0;});
    Register(L,"GetActiveVoiceChannel",[](lua_State* S)->int {lua_pushnil(S); return 1;}); Register(L,"SetActiveVoiceChannel",[](lua_State* S)->int {return 0;});
    Register(L,"IsVoiceChatAllowed",[](lua_State* S)->int {lua_pushnil(S); return 1;});
    Register(L,"ChannelInvite",[](lua_State* S)->int{return Moderation(S,0x00A3);}); Register(L,"ChannelKick",[](lua_State* S)->int{return Moderation(S,0x00A4);});
    Register(L,"ChannelBan",[](lua_State* S)->int{return Moderation(S,0x00A5);}); Register(L,"ChannelUnban",[](lua_State* S)->int{return Moderation(S,0x00A6);});
    Register(L,"ChannelMute",[](lua_State* S)->int{return Moderation(S,0x00A1);}); Register(L,"ChannelUnmute",[](lua_State* S)->int{return Moderation(S,0x00A2);});
    Register(L,"ChannelModerator",[](lua_State* S)->int{return Moderation(S,0x009F);}); Register(L,"ChannelUnmoderator",[](lua_State* S)->int{return Moderation(S,0x00A0);});
    Register(L,"SetChannelPassword",[](lua_State* S)->int{return Moderation(S,0x009C);}); Register(L,"SetChannelOwner",[](lua_State* S)->int{return Moderation(S,0x009D);}); Register(L,"GetChannelOwner",[](lua_State* S)->int{return Moderation(S,0x009E);});
    Register(L,"ChannelToggleAnnouncements",[](lua_State* S)->int{return Moderation(S,0x00A7);});
    Register(L,"IsDisplayChannelOwner",[](lua_State* S)->int {const auto All=Rows(S); const int32 I=luaL_optinteger(S,1,Selected(S))-1; bool Owner=false; if(All.IsValidIndex(I)) for(const auto& M:Members(S,All[I])) if(Connection(S) && M.Guid==Connection(S)->PacketHandler.EntityManager.LocalPlayerGuid) Owner=(M.Flags&1)!=0; Flag(S,Owner); return 1;});
    Register(L,"IsDisplayChannelModerator",[](lua_State* S)->int {const auto All=Rows(S); const int32 I=luaL_optinteger(S,1,Selected(S))-1; bool Moderator=false; if(All.IsValidIndex(I)) for(const auto& M:Members(S,All[I])) if(Connection(S) && M.Guid==Connection(S)->PacketHandler.EntityManager.LocalPlayerGuid) Moderator=(M.Flags&3)!=0; Flag(S,Moderator); return 1;});
}
void WowLuaApi::UpdateChannels(lua_State* L)
{
    auto* C=GetContext(L); auto* N=Connection(L); if(!C || !N || !C->EventSystem) return;
    auto& H=N->PacketHandler; auto& V=View(L); const double Now=FPlatformTime::Seconds();
    for(auto It=H.ChannelJoins.CreateIterator();It;++It) if(Now>It.Value().Deadline) {It.RemoveCurrent(); ++H.ChannelRevision;}
    for(auto& Channel:H.Channels) if((Channel.RosterPending || Channel.Leaving) && Now-Channel.RequestAt>15) {Channel.RosterPending=Channel.Leaving=false; ++H.ChannelRevision;}
    UpdateBuiltin(L);
    if(V.Revision==H.ChannelRevision && V.Names==H.PlayerNameCache.Num() && V.GroupCount==H.GroupInfo.MemberCount && H.ChannelNotices.IsEmpty()) return;
    V.Revision=H.ChannelRevision; V.Names=H.PlayerNameCache.Num(); V.GroupCount=H.GroupInfo.MemberCount;
    FString Membership; for(const auto& Channel:H.Channels) Membership+=FString::Printf(TEXT("%d:%s;"),Channel.Number,*Channel.Name);
    const bool Changed=Membership!=V.Membership; V.Membership=Membership;
    auto Notices=MoveTemp(H.ChannelNotices); H.ChannelNotices.Reset();
    bool Joined=false; for(const auto& Notice:Notices) {Joined|=Notice.Type==2; if(Notice.Type==2 && Notice.Window!=1) SetUiPreference(L,WindowKey(Notice.Window,Definition(Notice.Id)?Definition(Notice.Id)->Name:Notice.Channel),1);}
    if(Changed && Joined) C->EventSystem->FireEvent(TEXT("UPDATE_CHAT_WINDOWS"));
    static const TCHAR* Tokens[]={TEXT(""),TEXT(""),TEXT("YOU_JOINED"),TEXT("YOU_LEFT"),TEXT("WRONG_PASSWORD"),TEXT("NOT_MEMBER"),TEXT("NOT_MODERATOR"),TEXT("PASSWORD_CHANGED"),TEXT("OWNER_CHANGED"),TEXT("PLAYER_NOT_FOUND"),TEXT("NOT_OWNER"),TEXT("CHANNEL_OWNER"),TEXT("MODE_CHANGE"),TEXT("ANNOUNCEMENTS_ON"),TEXT("ANNOUNCEMENTS_OFF"),TEXT("MODERATION_ON"),TEXT("MODERATION_OFF"),TEXT("MUTED"),TEXT("PLAYER_KICKED"),TEXT("BANNED"),TEXT("PLAYER_BANNED"),TEXT("PLAYER_UNBANNED"),TEXT("PLAYER_NOT_BANNED"),TEXT("PLAYER_ALREADY_MEMBER"),TEXT("INVITE"),TEXT("INVITE_WRONG_FACTION"),TEXT("WRONG_FACTION"),TEXT("INVALID_NAME"),TEXT("NOT_MODERATED"),TEXT("PLAYER_INVITED"),TEXT("PLAYER_INVITE_BANNED"),TEXT("THROTTLED"),TEXT("NOT_IN_AREA"),TEXT("NOT_IN_LFG"),TEXT("VOICE_ON"),TEXT("VOICE_OFF")};
    for(const auto& Notice:Notices)
    {
        const auto Person=[&](uint64 Guid){const FString Local=N->GetCharacterName(); return Guid==H.EntityManager.LocalPlayerGuid && !Local.IsEmpty()?Local:H.PlayerNameCache.FindRef(Guid);};
        bool Wait=false; for(uint64 Guid:{Notice.Actor,Notice.Target}) if(Guid && Person(Guid).IsEmpty() && Now-Notice.ReceivedAt<5) {Wait=true; if(!V.RequestedNames.Contains(Guid)) {V.RequestedNames.Add(Guid); N->SendNameQuery(int64(Guid));}}
        if(Wait) {H.ChannelNotices.Add(Notice); continue;}
        const FString Actor=Person(Notice.Actor),Target=Person(Notice.Target);
        const FString Name=Notice.Player.IsEmpty()?(Notice.Target?Target:Actor):Notice.Player; const FString Display=Notice.Number?FString::Printf(TEXT("%d. %s"),Notice.Number,*Notice.Channel):Notice.Channel;
        const bool User=Notice.Type>=7 && Notice.Type!=10 && Notice.Type!=17 && Notice.Type!=19 && Notice.Type!=26 && Notice.Type!=31 && Notice.Type!=32 && Notice.Type!=33;
        const FString Event=Notice.Type==0?TEXT("CHAT_MSG_CHANNEL_JOIN"):Notice.Type==1?TEXT("CHAT_MSG_CHANNEL_LEAVE"):User?TEXT("CHAT_MSG_CHANNEL_NOTICE_USER"):TEXT("CHAT_MSG_CHANNEL_NOTICE");
        const auto Emit=[&](const FString& Token){C->EventSystem->FireEvent(Event,{Token,Name,TEXT(""),Display,Notice.Target?Actor:FString(),TEXT(""),FString::FromInt(Notice.Id),FString::FromInt(Notice.Number),Notice.Channel,TEXT(""),TEXT("0"),FString::Printf(TEXT("0x%016llX"),Notice.Target?Notice.Target:Notice.Actor)});};
        if(Notice.Type==12)
        {
            if((Notice.Flags^Notice.OldFlags)&2) Emit(Notice.Flags&2?TEXT("SET_MODERATOR"):TEXT("UNSET_MODERATOR"));
            if((Notice.Flags^Notice.OldFlags)&4) Emit(Notice.Flags&4?TEXT("SET_VOICE"):TEXT("UNSET_VOICE"));
            if((Notice.Flags^Notice.OldFlags)&16) Emit(Notice.Flags&16?TEXT("SET_SPEAK"):TEXT("UNSET_SPEAK"));
            const auto All=Rows(L); for(int32 I=0;I<All.Num();++I) if(const auto* Row=Channel(L,All[I]); Row && Row->Name.Equals(Notice.Channel,ESearchCase::IgnoreCase)) C->EventSystem->FireEventNumbers(TEXT("CHANNEL_FLAGS_UPDATED"),{double(I+1)});
        }
        else Emit(Tokens[Notice.Type]);
        if(Notice.Type==4) C->EventSystem->FireEvent(TEXT("CHANNEL_PASSWORD_REQUEST"),{Notice.Channel});
    }
    if(Changed && !Joined) C->EventSystem->FireEvent(TEXT("UPDATE_CHAT_WINDOWS"));
    Refresh(L); const int32 Index=Selected(L);
    if(Index) {C->EventSystem->FireEventNumbers(TEXT("CHANNEL_ROSTER_UPDATE"),{double(Index)}); const auto All=Rows(L); if(const auto* SelectedChannel=Channel(L,All[Index-1])) C->EventSystem->FireEventNumbers(TEXT("CHANNEL_COUNT_UPDATE"),{double(Index),double(SelectedChannel->Count)});}
}
#else
void WowLuaApi::RegisterChannelApi(lua_State*) {}
void WowLuaApi::UpdateChannels(lua_State*) {}
#endif
