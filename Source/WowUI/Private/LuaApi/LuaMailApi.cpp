#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"
#include "WowEventSystem.h"
#include "Formats/Dbc/DbcStore.h"
#include "GameFramework/Actor.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void Register(lua_State* L,const char* Name,lua_CFunction Fn) { lua_pushcfunction(L,Fn); lua_setglobal(L,Name); }
FWowPacketHandler* Handler(FWowLuaContext* C) { return C && C->ConnectionManager?&C->ConnectionManager->PacketHandler:nullptr; }
FWowMailMessage* Row(FWowLuaContext* C,int32 Index) { auto* H=Handler(C); return H && H->MailInbox.IsValidIndex(Index-1)?&H->MailInbox[Index-1]:nullptr; }
const FWowMailAttachment* Attachment(const FWowMailMessage* M,int32 Index)
{
    if(!M) return nullptr; if(Index<1) return M->Attachments.IsEmpty()?nullptr:&M->Attachments[0];
    return M->Attachments.FindByPredicate([Index](const auto& A){return int32(A.AttachmentIndex)==Index-1;});
}
FString Global(lua_State* L,const char* Name,const TCHAR* Fallback)
{
    lua_getglobal(L,Name); const FString Value=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):Fallback; lua_pop(L,1); return Value;
}
void MailError(FWowLuaContext* C,const TCHAR* Key) { if(C && C->EventSystem) C->EventSystem->FireEvent(TEXT("UI_ERROR_MESSAGE"),{C->EventSystem->ResolveText(Key)}); }
void Event(FWowLuaContext* C,const TCHAR* Name) { if(C && C->EventSystem) C->EventSystem->FireEvent(Name); }
FString Sender(lua_State* L,FWowLuaContext* C,uint32 Type,uint64 Guid,uint32 Entry,uint32 Paper=0)
{
    auto* H=Handler(C); if(!H) return TEXT("");
    if((Type==0 || Type==5) && Paper==61) return Global(L,"GM_EMAIL_NAME",TEXT("Customer Support"));
    if((Type==0 || Type==5) && Guid)
    {
        if(Guid==H->EntityManager.LocalPlayerGuid) return C->ConnectionManager->GetCharacterName();
        if(const auto* Name=H->PlayerNameCache.Find(Guid)) return *Name;
        if(!H->RequestedMailNames.Contains(Guid)) { H->RequestedMailNames.Add(Guid); C->ConnectionManager->SendNameQuery(int64(Guid)); }
        return Global(L,"RETRIEVING_ITEM_INFO",TEXT("Retrieving information..."));
    }
    if(Type==2) return Global(L,"AUCTION_HOUSE",TEXT("Auction House"));
    if(const auto* Name=H->CreatureNameCache.Find(Entry)) return *Name;
    if(Type==3 && Entry && !H->RequestedMailCreatures.Contains(Entry)) { H->RequestedMailCreatures.Add(Entry); C->ConnectionManager->SendCreatureQuery(Entry,0); }
    if(Type==5) return Global(L,"CALENDAR",TEXT("Calendar"));
    return FString::Printf(TEXT("Sender %u"),Entry);
}
TArray<const FItemUiDbc::FStationery*> Stationeries(FWowLuaContext* C)
{
    TArray<const FItemUiDbc::FStationery*> Rows;
    for(const auto& S:FDbcStore::Get().ItemUi().Stationeries())
    {
        bool Owned=false; if(C && C->EntityManager) for(const auto* Item:C->EntityManager->GetInventoryItems()) if(Item->Entry==S.Item) {Owned=true; break;}
        if((S.Flags&1) || Owned) { Rows.Add(&S); WowLuaApi::QueryItemTemplate(C,S.Item); }
    }
    return Rows;
}
const FItemUiDbc::FStationery* Stationery(uint32 Id)
{
    return FDbcStore::Get().ItemUi().Stationeries().FindByPredicate([Id](const auto& S){return S.Id==Id;});
}
const FItemUiDbc::FPackage* Package(uint32 Id)
{
    return FDbcStore::Get().ItemUi().Packages().FindByPredicate([Id](const auto& S){return S.Id==Id;});
}
TArray<FString> AuctionFields(const FWowMailMessage* M)
{
    TArray<FString> Fields; if(M && M->MessageType==2) M->Subject.ParseIntoArray(Fields,TEXT(":"),false); if(Fields.Num()<3) Fields.Empty(); return Fields;
}
TArray<FString> Invoice(const FWowMailMessage* M)
{
    auto Fields=AuctionFields(M);
    if(!Fields.IsEmpty()) { const int32 Type=FCString::Atoi(*Fields[2]); if(Type!=1 && Type!=2 && Type!=6) Fields.Empty(); }
    return Fields;
}
FString Subject(lua_State* L,FWowLuaContext* C,const FWowMailMessage& M)
{
    const auto Fields=AuctionFields(&M);
    if(!Fields.IsEmpty()) if(const auto* T=WowLuaApi::QueryItemTemplate(C,FCString::Atoi(*Fields[0])))
    {
        static const char* Keys[]={"AUCTION_OUTBID_MAIL_SUBJECT","AUCTION_WON_MAIL_SUBJECT","AUCTION_SOLD_MAIL_SUBJECT","AUCTION_EXPIRED_MAIL_SUBJECT","AUCTION_REMOVED_MAIL_SUBJECT","AUCTION_REMOVED_MAIL_SUBJECT","AUCTION_INVOICE_MAIL_SUBJECT"};
        const int32 Type=FCString::Atoi(*Fields[2]);
        return Type>=0 && Type<UE_ARRAY_COUNT(Keys)?Global(L,Keys[Type],TEXT("%s")).Replace(TEXT("%s"),*T->Name):T->Name;
    }
    if(M.Checked&8) return Global(L,"COD_PAYMENT",TEXT("COD Payment: %s")).Replace(TEXT("%s"),*M.Subject);
    if(M.MailTemplateId) if(const auto* Title=FDbcStore::Get().ItemUi().MailSubject(M.MailTemplateId)) return *Title;
    return M.Subject;
}
bool CanDelete(const FWowMailMessage& M) { return (M.Checked&0x20A)!=0 || M.MessageType!=0 || (M.Attachments.IsEmpty() && !M.Money); }
int HeaderInfo(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const auto* M=Row(C,luaL_checkinteger(L,1));
    if(!M) { for(int32 I=1;I<=14;++I) if(I>=5 && I<=7) lua_pushinteger(L,0); else lua_pushnil(L); return 14; }
    FString Icon;
    if(const auto* P=Package(M->Package)) Icon=TEXT("Interface\\Icons\\")+P->Icon;
    else if(M->Attachments.Num()>1)
    {
        if(const auto* Art=FDbcStore::Get().ItemDisplayInfo().GetById(7913)) Icon=TEXT("Interface\\Icons\\")+Art->Icon1;
    }
    else if(M->Attachments.Num()==1) Icon=WowLuaApi::ItemIconForUi(C,M->Attachments[0].ItemEntry);
    if(Icon.IsEmpty()) lua_pushnil(L); else lua_pushstring(L,TCHAR_TO_UTF8(*Icon));
    const auto* S=Stationery(M->Stationery); if(S && S->Item) lua_pushstring(L,TCHAR_TO_UTF8(*WowLuaApi::ItemIconForUi(C,S->Item))); else lua_pushnil(L);
    lua_pushstring(L,TCHAR_TO_UTF8(*Sender(L,C,M->MessageType,M->SenderGuid,M->SenderEntry,M->Stationery))); lua_pushstring(L,TCHAR_TO_UTF8(*Subject(L,C,*M)));
    lua_pushinteger(L,M->Money); lua_pushinteger(L,M->COD);
    lua_pushnumber(L,FMath::Max(0.,double(M->DaysLeft)-(M->ReceivedAt>0?FPlatformTime::Seconds()-M->ReceivedAt:0)/86400.));
    if(M->Attachments.Num()) lua_pushinteger(L,M->Attachments.Num()); else lua_pushnil(L);
    for(uint32 Mask:{1u,0x202u,4u}) { if(M->Checked&Mask) lua_pushboolean(L,1); else lua_pushnil(L); }
    if(!M->MessageType && M->Stationery!=61) lua_pushboolean(L,1); else lua_pushnil(L);
    if(M->Stationery==61) lua_pushboolean(L,1); else lua_pushnil(L);
    if(M->Attachments.Num()==1) lua_pushinteger(L,M->Attachments[0].Count); else lua_pushnil(L); return 14;
}
int InboxItem(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const auto* A=Attachment(Row(C,luaL_checkinteger(L,1)),luaL_optinteger(L,2,0)); const auto* T=A?WowLuaApi::QueryItemTemplate(C,A->ItemEntry):nullptr;
    if(!A) { lua_pushnil(L); lua_pushnil(L); lua_pushinteger(L,0); lua_pushinteger(L,0); lua_pushnil(L); return 5; }
    lua_pushstring(L,T?TCHAR_TO_UTF8(*T->Name):"Retrieving item information..."); lua_pushstring(L,TCHAR_TO_UTF8(*WowLuaApi::ItemIconForUi(C,A->ItemEntry)));
    lua_pushinteger(L,A->Count); lua_pushinteger(L,T && T->InventoryType?int32(T->Quality):-1); lua_pushboolean(L,!T || WowLuaApi::CanUseItem(C,*T)); return 5;
}
int Action(lua_State* L,uint32 Type)
{
    auto* C=WowLuaApi::GetContext(L); const auto* M=Row(C,luaL_checkinteger(L,1)); if(!M) return 0;
    const auto* A=Type==2?Attachment(M,luaL_optinteger(L,2,0)):nullptr;
    if(Type==2 && !A) return 0;
    C->ConnectionManager->SendMailAction(M->MessageId,Type,A?A->ItemGuidLow:0); return 0;
}
FString UnitLabel(lua_State* L,const char* Api)
{
    const int32 Top=lua_gettop(L); lua_getglobal(L,Api); lua_pushstring(L,"player"); FString Value;
    if(lua_pcall(L,1,1,0)==0 && lua_isstring(L,-1)) Value=UTF8_TO_TCHAR(lua_tostring(L,-1)); lua_settop(L,Top); return Value;
}
int InboxText(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); auto* M=Row(C,luaL_checkinteger(L,1));
    if(!M) { for(int32 I=0;I<4;++I) lua_pushnil(L); return 4; }
    const bool Changed=C->ConnectionManager->MarkMailRead(M->MessageId); FString Body=M->Body;
    const bool HasInvoice=!Invoice(M).IsEmpty();
    if(Body.IsEmpty() && M->MailTemplateId) if(const auto* Template=FDbcStore::Get().ItemUi().MailBody(M->MailTemplateId)) Body=*Template;
    if(M->MessageType!=0 || M->MailTemplateId)
    {
        Body.ReplaceInline(TEXT("$B"),TEXT("\n")); Body.ReplaceInline(TEXT("$b"),TEXT("\n")); Body.ReplaceInline(TEXT("$n"),*C->ConnectionManager->GetCharacterName());
        Body.ReplaceInline(TEXT("$r"),*UnitLabel(L,"UnitRace")); Body.ReplaceInline(TEXT("$c"),*UnitLabel(L,"UnitClass"));
        for(int32 I=0;I<32;++I)
        {
            const int32 Start=Body.Find(TEXT("$g")); if(Start<0) break; const int32 End=Body.Find(TEXT(";"),ESearchCase::CaseSensitive,ESearchDir::FromStart,Start);
            if(End<0) break; TArray<FString> Choices; Body.Mid(Start+2,End-Start-2).ParseIntoArray(Choices,TEXT(":"),false);
            const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const int32 Choice=P && P->GetGenderId()?1:0;
            Body=Body.Left(Start)+(Choices.IsValidIndex(Choice)?Choices[Choice]:FString())+Body.Mid(End+1);
        }
    }
    if(Body.IsEmpty() || M->MessageType==2) lua_pushnil(L); else lua_pushstring(L,TCHAR_TO_UTF8(*Body));
    if(const auto* Paper=Stationery(M->Stationery)) lua_pushstring(L,TCHAR_TO_UTF8(*Paper->Texture)); else lua_pushnil(L);
    const bool Permanent=(!M->Body.IsEmpty() || M->MailTemplateId) && !(M->Checked&4) && M->Stationery!=61 && M->MessageType!=5 && (M->MessageType!=2 || HasInvoice);
    if(Permanent) lua_pushboolean(L,1); else lua_pushnil(L);
    if(HasInvoice) lua_pushboolean(L,1); else lua_pushnil(L);
    if(Changed) Event(C,TEXT("MAIL_INBOX_UPDATE")); return 4;
}
int InvoiceInfo(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const auto* M=Row(C,luaL_checkinteger(L,1)); const auto Fields=Invoice(M);
    if(!M || Fields.IsEmpty() || M->Body.IsEmpty()) { lua_pushnil(L); lua_pushnil(L); lua_pushnil(L); for(int32 I=0;I<4;++I) lua_pushinteger(L,0); return 7; }
    TArray<FString> Values; M->Body.ParseIntoArray(Values,TEXT(":"),false); const auto Value=[&](int32 I){return Values.IsValidIndex(I)?FCString::Strtoui64(*Values[I],nullptr,10):0ull;};
    const int32 Type=FCString::Atoi(*Fields[2]); const auto* T=WowLuaApi::QueryItemTemplate(C,FCString::Atoi(*Fields[0]));
    lua_pushstring(L,Type==2?"seller":Type==6?"seller_temp_invoice":"buyer"); lua_pushstring(L,T?TCHAR_TO_UTF8(*T->Name):"Retrieving item information...");
    const uint64 Guid=Values.Num()?FCString::Strtoui64(*Values[0],nullptr,16):0;
    lua_pushstring(L,TCHAR_TO_UTF8(*Sender(L,C,0,Guid,0)));
    for(int32 I=1;I<=5;++I) lua_pushnumber(L,double(Value(I)));
    const uint32 Time=uint32(Value(6)); lua_pushinteger(L,(Time>>6)&31); lua_pushinteger(L,Time&63); return 10;
}
int SendItem(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const auto* Item=C && C->ConnectionManager?C->ConnectionManager->GetOutgoingMailItem(luaL_optinteger(L,1,1)-1):nullptr;
    const auto* T=Item?WowLuaApi::QueryItemTemplate(C,Item->Entry):nullptr;
    if(T) lua_pushstring(L,TCHAR_TO_UTF8(*T->Name)); else lua_pushnil(L);
    if(Item) lua_pushstring(L,TCHAR_TO_UTF8(*WowLuaApi::ItemIconForUi(C,Item->Entry))); else lua_pushnil(L);
    lua_pushinteger(L,Item?Item->GetStackCount():0); lua_pushinteger(L,T && T->InventoryType?int32(T->Quality):Item?-1:0); return 4;
}
}

void WowLuaApi::RegisterMailApi(lua_State* L)
{
    Register(L,"GetInboxNumItems",[](lua_State* S)->int { auto* H=Handler(GetContext(S)); lua_pushinteger(S,H?H->MailInbox.Num():0); lua_pushinteger(S,H?H->MailInboxTotalCount:0); return 2; });
    Register(L,"GetInboxHeaderInfo",HeaderInfo); Register(L,"GetInboxItem",InboxItem); Register(L,"GetInboxText",InboxText); Register(L,"GetInboxInvoiceInfo",InvoiceInfo);
    Register(L,"GetInboxItemLink",[](lua_State* S)->int { auto* C=GetContext(S); const auto* A=Attachment(Row(C,luaL_checkinteger(S,1)),luaL_optinteger(S,2,0)); if(A) lua_pushstring(S,TCHAR_TO_UTF8(*ItemLinkForUi(C,A->ItemEntry))); else lua_pushnil(S); return 1; });
    Register(L,"TakeInboxMoney",[](lua_State* S)->int {return Action(S,1);}); Register(L,"TakeInboxItem",[](lua_State* S)->int {return Action(S,2);});
    Register(L,"ReturnInboxItem",[](lua_State* S)->int {return Action(S,3);}); Register(L,"DeleteInboxItem",[](lua_State* S)->int {return Action(S,4);}); Register(L,"TakeInboxTextItem",[](lua_State* S)->int {return Action(S,5);});
    Register(L,"InboxItemCanDelete",[](lua_State* S)->int { const auto* M=Row(GetContext(S),luaL_checkinteger(S,1)); lua_pushboolean(S,M && CanDelete(*M)); return 1; });
    Register(L,"AutoLootMailItem",[](lua_State* S)->int { auto* C=GetContext(S); const auto* M=Row(C,luaL_checkinteger(S,1)); if(M) C->ConnectionManager->StartMailAutoLoot(M->MessageId); return 0; });
    Register(L,"CheckInbox",[](lua_State* S)->int { auto* C=GetContext(S); auto* H=Handler(C); if(H && H->CurrentMailboxGuid) { if(FPlatformTime::Seconds()>=H->NextMailQuery) C->ConnectionManager->SendGetMailList(int64(H->CurrentMailboxGuid)); else Event(C,TEXT("MAIL_INBOX_UPDATE")); } return 0; });
    Register(L,"CloseMail",[](lua_State* S)->int { auto* C=GetContext(S); auto* H=Handler(C); if(H && H->CurrentMailboxGuid) { C->ConnectionManager->CloseMail(); Event(C,TEXT("MAIL_CLOSED")); } return 0; });
    Register(L,"HasNewMail",[](lua_State* S)->int { auto* H=Handler(GetContext(S)); lua_pushboolean(S,H && H->HasUnreadMail); return 1; });
    Register(L,"GetLatestThreeSenders",[](lua_State* S)->int { auto* C=GetContext(S); auto* H=Handler(C); int32 Count=0; if(H) for(const auto& R:H->RecentMailSenders) if(R.TimeLeft<=0 && Count<3) {lua_pushstring(S,TCHAR_TO_UTF8(*Sender(S,C,R.Type,R.Guid,R.Entry,R.Stationery))); ++Count;} return Count; });
    Register(L,"GetSendMailItem",SendItem);
    Register(L,"GetSendMailItemLink",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Item=C && C->ConnectionManager?C->ConnectionManager->GetOutgoingMailItem(luaL_optinteger(S,1,1)-1):nullptr; if(Item) lua_pushstring(S,TCHAR_TO_UTF8(*ItemLinkForUi(C,Item->Entry))); else lua_pushnil(S); return 1; });
    Register(L,"ClickSendMailItemButton",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) C->ConnectionManager->ClickOutgoingMailAttachment(lua_isnumber(S,1)?int32(lua_tointeger(S,1))-1:-1,lua_toboolean(S,2)!=0); return 0; });
    Register(L,"ClearSendMail",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) C->ConnectionManager->ClearOutgoingMail(); return 0; });
    Register(L,"SendMail",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) C->ConnectionManager->SendMail(UTF8_TO_TCHAR(luaL_checkstring(S,1)),UTF8_TO_TCHAR(luaL_checkstring(S,2)),UTF8_TO_TCHAR(luaL_optstring(S,3,""))); return 0; });
    Register(L,"RespondMailLockSendItem",[](lua_State* S)->int { auto* C=GetContext(S); if(C && C->ConnectionManager) C->ConnectionManager->RespondOutgoingMailLock(luaL_checkinteger(S,1)-1,lua_toboolean(S,2)!=0); return 0; });
    Register(L,"SetSendMailShowing",[](lua_State* S)->int { if(auto* H=Handler(GetContext(S))) H->MailComposer.Showing=lua_toboolean(S,1)!=0; return 0; });
    Register(L,"GetSendMailMoney",[](lua_State* S)->int { auto* H=Handler(GetContext(S)); lua_pushinteger(S,H?H->MailComposer.Money:0); return 1; });
    Register(L,"GetSendMailCOD",[](lua_State* S)->int { auto* H=Handler(GetContext(S)); lua_pushinteger(S,H?H->MailComposer.COD:0); return 1; });
    Register(L,"SetSendMailMoney",[](lua_State* S)->int {
        auto* C=GetContext(S); auto* H=Handler(C); const double Value=luaL_checknumber(S,1); if(!H || !FMath::IsFinite(Value) || Value<0 || Value>MAX_int32 || H->MailComposer.Sending) return 0;
        const auto* P=H->EntityManager.GetLocalPlayer(); if(!P || P->GetCoinage()<Value) { MailError(C,TEXT("ERR_NOT_ENOUGH_MONEY")); return 0; }
        const uint32 Amount=uint32(Value); if(H->MailComposer.Money!=Amount) { H->MailComposer.Money=Amount; Event(C,TEXT("SEND_MAIL_MONEY_CHANGED")); }
        lua_pushboolean(S,1); return 1;
    });
    Register(L,"SetSendMailCOD",[](lua_State* S)->int {
        auto* C=GetContext(S); auto* H=Handler(C); const double Value=luaL_checknumber(S,1); if(!H || !FMath::IsFinite(Value) || Value<0 || Value>MAX_int32 || H->MailComposer.Sending) return 0;
        bool HasItem=false; for(uint64 Guid:H->MailComposer.Items) HasItem|=Guid!=0;
        if(HasItem && H->MailComposer.COD!=uint32(Value)) {H->MailComposer.COD=uint32(Value); Event(C,TEXT("SEND_MAIL_COD_CHANGED"));} return 0;
    });
    Register(L,"GetSendMailPrice",[](lua_State* S)->int {
        auto* C=GetContext(S); auto* H=Handler(C); uint32 Count=0; uint64 Cost=30;
        if(H) { for(int32 I=0;I<12;++I) Count+=C->ConnectionManager->GetOutgoingMailItem(I)!=nullptr; Cost=30*FMath::Max(1u,Count)+uint64(H->MailComposer.Money);
            if(const auto* P=Package(H->MailComposer.Package)) Cost+=P->Cost;
            if(const auto* T=Stationery(H->MailComposer.Stationery))
            { bool Owned=false; for(const auto* Item:H->EntityManager.GetInventoryItems()) if(Item->Entry==T->Item) {Owned=true; break;} if(!Owned) if(const auto* Item=QueryItemTemplate(C,T->Item)) Cost+=Item->BuyPrice; }
        }
        lua_pushnumber(S,double(Cost)); return 1;
    });
    Register(L,"GetNumStationeries",[](lua_State* S)->int { lua_pushinteger(S,Stationeries(GetContext(S)).Num()); return 1; });
    Register(L,"GetStationeryInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto Rows=Stationeries(C); const int32 Index=luaL_checkinteger(S,1)-1; const auto* R=Rows.IsValidIndex(Index)?Rows[Index]:nullptr; const auto* T=R?QueryItemTemplate(C,R->Item):nullptr;
        if(!T) {lua_pushnil(S); lua_pushnil(S); lua_pushnil(S); return 3;}
        lua_pushstring(S,TCHAR_TO_UTF8(*T->Name)); lua_pushstring(S,TCHAR_TO_UTF8(*ItemIconForUi(C,R->Item))); bool Owned=false;
        if(C && C->EntityManager) for(const auto* Item:C->EntityManager->GetInventoryItems()) if(Item->Entry==R->Item) {Owned=true; break;}
        if(Owned) lua_pushnil(S); else lua_pushinteger(S,T->BuyPrice); return 3;
    });
    Register(L,"SelectStationery",[](lua_State* S)->int { auto* C=GetContext(S); if(auto* H=Handler(C)) { const auto Rows=Stationeries(C); const int32 Index=luaL_checkinteger(S,1)-1; H->MailComposer.Stationery=Rows.IsValidIndex(Index)?Rows[Index]->Id:0; Event(C,TEXT("MAIL_SEND_INFO_UPDATE")); } return 0; });
    Register(L,"GetSelectedStationeryTexture",[](lua_State* S)->int { auto* H=Handler(GetContext(S)); const auto* R=H?Stationery(H->MailComposer.Stationery):nullptr; if(R) lua_pushstring(S,TCHAR_TO_UTF8(*R->Texture)); else lua_pushnil(S); return 1; });
    Register(L,"GetNumPackages",[](lua_State* S)->int { lua_pushinteger(S,FDbcStore::Get().ItemUi().Packages().Num()); return 1; });
    Register(L,"GetPackageInfo",[](lua_State* S)->int { const auto& Rows=FDbcStore::Get().ItemUi().Packages(); const int32 Index=luaL_checkinteger(S,1)-1;
        if(!Rows.IsValidIndex(Index)) { lua_pushnil(S); lua_pushnil(S); lua_pushinteger(S,0); return 3; }
        const auto& R=Rows[Index]; lua_pushstring(S,TCHAR_TO_UTF8(*R.Name)); lua_pushstring(S,TCHAR_TO_UTF8(*(TEXT("Interface\\Icons\\")+R.Icon))); lua_pushinteger(S,R.Cost); return 3;
    });
    Register(L,"SelectPackage",[](lua_State* S)->int { auto* C=GetContext(S); if(auto* H=Handler(C)) { const auto& Rows=FDbcStore::Get().ItemUi().Packages(); const int32 Index=luaL_checkinteger(S,1)-1; H->MailComposer.Package=Rows.IsValidIndex(Index)?Rows[Index].Id:0; Event(C,TEXT("MAIL_SEND_INFO_UPDATE")); } return 0; });
    Register(L,"CanComplainInboxItem",[](lua_State* S)->int { auto* C=GetContext(S); const auto* M=Row(C,luaL_checkinteger(S,1)); const auto* H=Handler(C); lua_pushboolean(S,M && H && !M->MessageType && M->Stationery!=61 && M->SenderGuid && M->SenderGuid!=H->EntityManager.LocalPlayerGuid); return 1; });
    Register(L,"ComplainInboxItem",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* M=Row(C,luaL_checkinteger(S,1)); auto* H=Handler(C);
        if(M && H && H->CurrentMailboxGuid && !M->MessageType && M->Stationery!=61 && M->SenderGuid!=H->EntityManager.LocalPlayerGuid)
        { TArray<uint8> Data; Data.Add(0); const auto Put=[&](const auto Value){const int32 At=Data.AddUninitialized(sizeof(Value)); FMemory::Memcpy(Data.GetData()+At,&Value,sizeof(Value));}; Put(M->SenderGuid); Put(uint32(0)); Put(M->MessageId); Put(uint32(0)); C->ConnectionManager->SendRawPacket(0x03C7,Data); }
        return 0;
    });
}

void WowLuaApi::UpdateMail(lua_State* L)
{
    auto* C=GetContext(L); auto* H=Handler(C); if(!H) return; C->ConnectionManager->TickMailState();
    const uint64 Guid=H->CurrentMailboxGuid; if(!Guid) return;
    const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    bool Close=!P || !P->GetHealth() || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame;
    if(P && Guid!=P->Guid)
    {
        if(!C->EntityManager->Find(Guid)) Close=true;
        if(C->ResolveActor) { const auto* A=C->ResolveActor(P->Guid); const auto* B=C->ResolveActor(Guid); if(IsValid(A) && IsValid(B) && FVector::DistSquared(A->GetActorLocation(),B->GetActorLocation())>FMath::Square(500.f)) Close=true; }
    }
    if(Close) { C->ConnectionManager->CloseMail(); Event(C,TEXT("MAIL_CLOSED")); }
}
#else
void WowLuaApi::RegisterMailApi(lua_State*) {}
void WowLuaApi::UpdateMail(lua_State*) {}
#endif
