#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowFrameManager.h"
#include "WowEventSystem.h"
#include "WowGuildEmblem.h"

struct FWowTabardUiView {FWowGuildEmblem Draft,Saved,Submitted; int64 Model=0; bool Open=false,Pending=false,CanSave=false; uint32 Result=0;};
#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
FWowPacketHandler* Handler(FWowLuaContext* C) {return C && C->ConnectionManager?&C->ConnectionManager->PacketHandler:nullptr;}
FWowTabardUiView& View(FWowLuaContext* C) {if(!C->TabardUi) C->TabardUi=MakeShared<FWowTabardUiView>(); return *C->TabardUi;}
FWowGuildEmblem Emblem(FWowLuaContext* C)
{
    FWowGuildEmblem Value; const auto* H=Handler(C); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    if(const auto* G=H && P?H->Guilds.Find(P->GetField(PlayerField::GUILD_ID)):nullptr) {Value.Style=G->Emblem[0]; Value.Color=G->Emblem[1]; Value.BorderStyle=G->Emblem[2]; Value.BorderColor=G->Emblem[3]; Value.Background=G->Emblem[4];} return Value.Valid()?Value:FWowGuildEmblem();
}
bool CanSave(FWowLuaContext* C)
{
    const auto* H=Handler(C); const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    return H && P && C->ConnectionManager->GetState()==EWowSessionState::WorldInGame && H->Tabard.Open && !H->Tabard.Pending && P->GetField(PlayerField::GUILD_ID) && P->GetField(PlayerField::GUILD_RANK)==0 && P->GetField(PlayerField::COINAGE)>=100000 && !(View(C).Draft==View(C).Saved);
}
void Preview(FWowLuaContext* C) {if(!C) return; auto& V=View(C); const auto* T=WowLuaApi::QueryItemTemplate(C,5976); if(T && C->TabardPreview && V.Model) C->TabardPreview(V.Model,V.Draft,T->DisplayId);}
void Changed(FWowLuaContext* C) {Preview(C); if(C && C->EventSystem) {View(C).CanSave=CanSave(C); C->EventSystem->FireEvent(TEXT("TABARD_CANSAVE_CHANGED"));}}
void Reg(lua_State* L,const char* Name,lua_CFunction F) {lua_pushcfunction(L,F); lua_setglobal(L,Name);}
}
void WowLuaApi::RegisterTabardApi(lua_State* L)
{
    Reg(L,"GetTabardCreationCost",[](lua_State* S)->int {lua_pushinteger(S,100000); return 1;});
    Reg(L,"CloseTabardCreation",[](lua_State* S)->int {auto* H=Handler(GetContext(S)); if(H && H->Tabard.Open) {H->Tabard.Open=false; ++H->Tabard.Revision;} return 0;});
}
int WowLuaApi::TabardModelMethod(lua_State* L,int32 Method)
{
    auto* C=GetContext(L); auto* H=Handler(C); if(!C || !C->FrameManager || !H) return 0; lua_getfield(L,1,"__handle"); const int64 Model=lua_tointeger(L,-1); lua_pop(L,1);
    if(Model!=C->FrameManager->FindFrame(TEXT("TabardModel"))) return 0; auto& V=View(C); V.Model=Model;
    if(Method==0) {V.Saved=V.Draft=Emblem(C); Preview(C);}
    else if(Method==1)
    {
        const int32 Index=luaL_checkinteger(L,2)-1,Direction=luaL_checkinteger(L,3); uint32* Values[]={&V.Draft.Style,&V.Draft.Color,&V.Draft.BorderStyle,&V.Draft.BorderColor,&V.Draft.Background}; constexpr int32 Counts[]={170,17,10,17,51};
        if(Index>=0 && Index<5 && Direction) {*Values[Index]=uint32((int32(*Values[Index])+(Direction>0?1:-1)+Counts[Index])%Counts[Index]); Changed(C);}
    }
    else if(Method==2 || Method==3)
    {const FString Path=V.Draft.Textures(Method==2)[1]; lua_getfield(L,2,"SetTexture"); lua_pushvalue(L,2); lua_pushstring(L,TCHAR_TO_UTF8(*Path)); lua_call(L,2,0);}
    else if(Method==4) {lua_pushboolean(L,CanSave(C)); return 1;}
    else if(Method==5 && CanSave(C))
    {
        TArray<uint8> B; const auto Add=[&](auto Value){B.Append(reinterpret_cast<const uint8*>(&Value),sizeof(Value));}; Add(H->Tabard.Vendor); Add(V.Draft.Style); Add(V.Draft.Color); Add(V.Draft.BorderStyle); Add(V.Draft.BorderColor); Add(V.Draft.Background);
        V.Submitted=V.Draft; H->Tabard.Pending=true; H->Tabard.SentAt=FPlatformTime::Seconds(); ++H->Tabard.Revision; C->ConnectionManager->SendRawPacket(0x1F1,B); if(C->EventSystem) C->EventSystem->FireEvent(TEXT("TABARD_SAVE_PENDING"));
    }
    return 0;
}
void WowLuaApi::UpdateTabardState(lua_State* L)
{
    auto* C=GetContext(L); auto* H=Handler(C); if(!H || !C->EventSystem) return; auto& V=View(C); auto& T=H->Tabard;
    if(T.Open)
    {
        const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const auto* N=C->EntityManager?C->EntityManager->Find(T.Vendor):nullptr;
        if(!P || !N || FVector::DistSquared(P->Movement.Position,N->Movement.Position)>100) {T.Open=false; ++T.Revision;}
    }
    if(T.Open && !V.Open)
    {
        if(!QueryItemTemplate(C,5976)) return; V.Open=true; V.Result=T.ResultRevision; C->EventSystem->FireEvent(TEXT("OPEN_TABARD_FRAME"));
    }
    else if(!T.Open && V.Open) {V.Open=false; C->EventSystem->FireEvent(TEXT("CLOSE_TABARD_FRAME"));}
    if(V.Pending!=T.Pending) {V.Pending=T.Pending; C->EventSystem->FireEvent(TEXT("TABARD_CANSAVE_CHANGED"));}
    const bool Save=CanSave(C); if(V.CanSave!=Save) {V.CanSave=Save; C->EventSystem->FireEvent(TEXT("TABARD_CANSAVE_CHANGED"));}
    if(V.Result!=T.ResultRevision)
    {
        V.Result=T.ResultRevision; if(!T.HasResult) return; if(T.Error==0) V.Saved=V.Submitted;
        static const char* Keys[]={"ERR_GUILDEMBLEM_SUCCESS","ERR_GUILDEMBLEM_INVALID_TABARD_COLORS","ERR_GUILDEMBLEM_NOGUILD","ERR_GUILDEMBLEM_NOTGUILDMASTER","ERR_GUILDEMBLEM_NOTENOUGHMONEY","ERR_GUILDEMBLEM_INVALIDVENDOR","ERR_GUILDEMBLEM_COLORSPRESENT"};
        const int Top=lua_gettop(L); lua_getglobal(L,Keys[FMath::Min<uint32>(T.Error,6)]); const FString Text=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):TEXT(""); lua_settop(L,Top);
        C->EventSystem->FireEvent(T.Error?TEXT("UI_ERROR_MESSAGE"):TEXT("UI_INFO_MESSAGE"),{Text}); C->EventSystem->FireEvent(TEXT("TABARD_CANSAVE_CHANGED"));
    }
}
#else
void WowLuaApi::RegisterTabardApi(lua_State*) {}
void WowLuaApi::UpdateTabardState(lua_State*) {}
int WowLuaApi::TabardModelMethod(lua_State*,int32) {return 0;}
#endif
