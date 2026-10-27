#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEventSystem.h"

struct FWowLootUiView
{
    FWowLootState Loot;
    uint32 MasterRevision=MAX_uint32,ErrorRevision=0,EventSequence=0,NoticeSequence=0;
    TMap<int32,uint64> SlotSignatures;
    TSet<uint32> Started,Cancelled;
    TMap<uint32,int32> ConfirmRoll;
    uint64 ConfirmCorpse=0;
    int32 ConfirmSlot=0;
    uint64 AutoGuid=0;
    bool AutoCoinSent=false,CloseSent=false;
    TSet<int32> AutoSlots;
};

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void Reg(lua_State* L,const char* Name,lua_CFunction Fn) {lua_pushcfunction(L,Fn); lua_setglobal(L,Name);}
FWowLootUiView& View(FWowLuaContext* C) {if(!C->LootUi) C->LootUi=MakeShared<FWowLootUiView>(); return *C->LootUi;}
FWowPacketHandler* Handler(FWowLuaContext* C) {return C && C->ConnectionManager?&C->ConnectionManager->PacketHandler:nullptr;}
uint64 Local(FWowLuaContext* C) {return C && C->EntityManager?C->EntityManager->LocalPlayerGuid:0;}
bool Send(FWowLuaContext* C,uint32 Op,const TArray<uint8>& Bytes)
{if(!C || !C->ConnectionManager || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame) return false; C->ConnectionManager->SendRawPacket(Op,Bytes); return true;}
template<typename T> void Add(TArray<uint8>& Bytes,T Value) {Bytes.Append(reinterpret_cast<const uint8*>(&Value),sizeof(Value));}
const FWowLootItem* Slot(FWowLuaContext* C,int32 Index)
{
    const auto* H=Handler(C); if(!H || !H->Loot.Open) return nullptr;
    const int32 Row=Index-1-int32(H->Loot.HasMoneySlot);
    return H->Loot.Items.IsValidIndex(Row) && !H->Loot.Items[Row].bLooted?&H->Loot.Items[Row]:nullptr;
}
bool Coin(FWowLuaContext* C,int32 Index) {const auto* H=Handler(C); return H && H->Loot.Open && H->Loot.HasMoneySlot && H->Loot.Gold && Index==1;}
FWowLootRoll* Roll(FWowLuaContext* C,int32 Id) {auto* H=Handler(C); return H && Id>0?H->LootRolls.Find(uint32(Id)):nullptr;}
FString Global(lua_State* L,const char* Key)
{lua_getglobal(L,Key); const FString Value=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):TEXT(""); lua_pop(L,1); return Value;}
FString Link(FWowLuaContext* C,uint32 Item,int32 Property,uint32 Suffix)
{
    if(!WowLuaApi::QueryItemTemplate(C,Item)) return FString();
    return WowLuaApi::ItemLinkForUi(C,Item).Replace(TEXT(":0:0:0:0:0:0:0:0:0|h"),*FString::Printf(TEXT(":0:0:0:0:0:%d:%u:0:0|h"),Property,Suffix));
}
FString Name(FWowLuaContext* C,uint64 Guid)
{
    const auto* H=Handler(C); if(!H) return FString();
    if(Guid==Local(C)) return C->ConnectionManager->GetCharacterName();
    if(const auto* M=H->GroupInfo.FindMember(Guid)) return M->Name;
    return H->PlayerNameCache.FindRef(Guid);
}
FString Format(lua_State* L,const FString& Pattern,const TArray<FString>& Args)
{
    const int32 Top=lua_gettop(L); lua_getglobal(L,"format"); lua_pushstring(L,TCHAR_TO_UTF8(*Pattern));
    for(const auto& A:Args) lua_pushstring(L,TCHAR_TO_UTF8(*A));
    FString Text; if(lua_pcall(L,Args.Num()+1,1,0)==0 && lua_isstring(L,-1)) Text=UTF8_TO_TCHAR(lua_tostring(L,-1));
    lua_settop(L,Top); return Text;
}
FString MoneyText(lua_State* L,uint32 Money)
{
    FString Text; const uint32 Values[]={Money/10000,(Money/100)%100,Money%100}; const char* Keys[]={"GOLD_AMOUNT","SILVER_AMOUNT","COPPER_AMOUNT"};
    for(int32 N=0;N<3;++N) if(Values[N]) {if(!Text.IsEmpty()) Text+=TEXT(" "); Text+=Format(L,Global(L,Keys[N]),{FString::FromInt(Values[N])});}
    return Text;
}
bool AutoLoot(lua_State* L)
{
    const int32 Top=lua_gettop(L); lua_getglobal(L,"GetCVar"); lua_pushstring(L,"autoLootDefault");
    const bool Enabled=lua_pcall(L,1,1,0)==0 && lua_isstring(L,-1) && FCStringAnsi::Strcmp(lua_tostring(L,-1),"1")==0;
    lua_settop(L,Top); lua_getglobal(L,"IsModifiedClick"); lua_pushstring(L,"AUTOLOOTTOGGLE");
    const bool Toggle=lua_pcall(L,1,1,0)==0 && lua_toboolean(L,-1)!=0; lua_settop(L,Top); return Enabled!=Toggle;
}
void Chat(FWowLuaContext* C,const TCHAR* Event,const FString& Text,uint64 Player=0)
{
    if(!Text.IsEmpty() && C->EventSystem) C->EventSystem->FireEvent(Event,{Text,Name(C,Player),TEXT(""),TEXT(""),TEXT(""),TEXT(""),TEXT("0"),TEXT("0"),TEXT(""),TEXT(""),TEXT("0"),FString::Printf(TEXT("0x%016llX"),Player)});
}
void Close(FWowLuaContext* C)
{
    const auto* H=Handler(C); if(!H || !H->Loot.Open || View(C).CloseSent || C->ConnectionManager->GetState()!=EWowSessionState::WorldInGame) return;
    C->ConnectionManager->SendLootRelease(H->Loot.Guid); View(C).CloseSent=true;
}
void Vote(FWowLuaContext* C,uint32 Id,int32 Choice,bool Confirm)
{
    auto* R=Roll(C,Id); if(!R || R->Finished || R->MillisecondsLeft()<=0 || R->Choice>=0 || R->PendingChoice>=0 || Choice<0 || Choice>3 || !(R->VoteMask&(1<<Choice))) return;
    if(!Confirm && Choice>0)
    {
        const auto* T=WowLuaApi::QueryItemTemplate(C,R->ItemId); if(!T) return;
        if(T->Bonding==1 || Choice==3)
        {
            View(C).ConfirmRoll.Add(Id,Choice);
            if(C->EventSystem) C->EventSystem->FireEventTyped(TEXT("CONFIRM_LOOT_ROLL"),[=](lua_State* S){lua_pushinteger(S,Id); lua_pushinteger(S,Choice); return 2;});
            return;
        }
    }
    TArray<uint8> Bytes; Add(Bytes,R->Guid); Add(Bytes,R->Slot); Add(Bytes,uint8(Choice));
    if(Send(C,0x02A0,Bytes)) R->PendingChoice=Choice;
}
void Collect(FWowLuaContext* C,int32 Index,bool Confirm)
{
    auto* H=Handler(C); if(!H) return;
    if(Coin(C,Index)) {C->ConnectionManager->SendLootMoney(); return;}
    const auto* I=Slot(C,Index); if(!I) return;
    if(I->SlotType==2 && H->GroupInfo.LootMethod==2 && H->GroupInfo.MasterLooter==Local(C))
    {if(C->EventSystem) C->EventSystem->FireEvent(TEXT("OPEN_MASTER_LOOT_LIST")); return;}
    if(!I->CanLoot()) return;
    const auto* T=WowLuaApi::QueryItemTemplate(C,I->ItemId); if(!T) return;
    if(!Confirm && T->Bonding==1 && I->SlotType!=4)
    {auto& V=View(C); V.ConfirmCorpse=H->Loot.Guid; V.ConfirmSlot=Index; if(C->EventSystem) C->EventSystem->FireEventNumbers(TEXT("LOOT_BIND_CONFIRM"),{double(Index)}); return;}
    C->ConnectionManager->SendLootItem(I->Index);
}
int Tooltip(lua_State* L,const FString& ItemLink)
{
    if(ItemLink.IsEmpty()) return 0;
    lua_pushstring(L,TCHAR_TO_UTF8(*ItemLink)); lua_replace(L,2); lua_settop(L,2);
    return WowLuaApi::SetHyperlinkTooltip(L);
}
}
void WowLuaApi::RegisterLootApi(lua_State* L)
{
    Reg(L,"GetNumLootItems",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); lua_pushinteger(S,H && H->Loot.Open?H->Loot.Items.Num()+int32(H->Loot.HasMoneySlot):0); return 1;});
    Reg(L,"IsFishingLoot",[](lua_State* S)->int {const auto* H=Handler(GetContext(S)); lua_pushboolean(S,H && H->Loot.Open && H->Loot.Type==3); return 1;});
    Reg(L,"LootSlotIsCoin",[](lua_State* S)->int {lua_pushboolean(S,Coin(GetContext(S),luaL_checkinteger(S,1))); return 1;});
    Reg(L,"LootSlotIsItem",[](lua_State* S)->int {lua_pushboolean(S,Slot(GetContext(S),luaL_checkinteger(S,1))!=nullptr); return 1;});
    Reg(L,"GetLootSlotType",[](lua_State* S)->int {auto* C=GetContext(S); const int32 I=luaL_checkinteger(S,1); lua_pushinteger(S,Coin(C,I)?2:Slot(C,I)?1:0); return 1;});
    Reg(L,"GetLootSlotInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); const int32 Index=luaL_checkinteger(S,1); const auto* I=Slot(C,Index);
        if(Coin(C,Index))
        {
            const uint32 Money=Handler(C)->Loot.Gold;
            const FString Icon=Money>=10000?TEXT("Interface\\MoneyFrame\\UI-GoldIcon"):Money>=100?TEXT("Interface\\MoneyFrame\\UI-SilverIcon"):TEXT("Interface\\MoneyFrame\\UI-CopperIcon");
            const FString Text=MoneyText(S,Money);
            lua_pushstring(S,TCHAR_TO_UTF8(*Icon)); lua_pushstring(S,TCHAR_TO_UTF8(*Text)); lua_pushinteger(S,1); lua_pushinteger(S,1); lua_pushboolean(S,0); return 5;
        }
        if(!I) {lua_pushnil(S); return 1;} const auto* T=QueryItemTemplate(C,I->ItemId);
        lua_pushstring(S,TCHAR_TO_UTF8(*ItemIconForUi(C,I->ItemId,I->DisplayId))); lua_pushstring(S,TCHAR_TO_UTF8(*(T?T->Name:Global(S,"RETRIEVING_ITEM_INFO"))));
        lua_pushinteger(S,I->Count); lua_pushinteger(S,T?T->Quality:I->Quality); lua_pushboolean(S,I->SlotType==1 || I->SlotType==3); return 5;
    });
    Reg(L,"GetLootSlotLink",[](lua_State* S)->int {auto* C=GetContext(S); const auto* I=Slot(C,luaL_checkinteger(S,1)); const FString Text=I?Link(C,I->ItemId,int32(I->RandomProperty),I->RandomSuffix):FString(); if(Text.IsEmpty()) lua_pushnil(S); else lua_pushstring(S,TCHAR_TO_UTF8(*Text)); return 1;});
    Reg(L,"LootSlot",[](lua_State* S)->int {Collect(GetContext(S),luaL_checkinteger(S,1),false); return 0;});
    Reg(L,"ConfirmLootSlot",[](lua_State* S)->int {auto* C=GetContext(S); auto* H=Handler(C); const int32 I=luaL_checkinteger(S,1); if(H && View(C).ConfirmCorpse==H->Loot.Guid && View(C).ConfirmSlot==I) {Collect(C,I,true); View(C).ConfirmSlot=0;} return 0;});
    Reg(L,"CloseLoot",[](lua_State* S)->int {Close(GetContext(S)); return 0;});
    Reg(L,"GetLootRollTimeLeft",[](lua_State* S)->int {const auto* R=Roll(GetContext(S),luaL_checkinteger(S,1)); lua_pushnumber(S,R?R->MillisecondsLeft():0); return 1;});
    Reg(L,"GetLootRollItemLink",[](lua_State* S)->int {auto* C=GetContext(S); const auto* R=Roll(C,luaL_checkinteger(S,1)); const FString Text=R?Link(C,R->ItemId,R->RandomProperty,R->RandomSuffix):FString(); if(Text.IsEmpty()) lua_pushnil(S); else lua_pushstring(S,TCHAR_TO_UTF8(*Text)); return 1;});
    Reg(L,"GetLootRollItemInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* R=Roll(C,luaL_checkinteger(S,1)); const auto* T=R?QueryItemTemplate(C,R->ItemId):nullptr;
        if(!R || !T) {lua_pushnil(S); return 1;}
        lua_pushstring(S,TCHAR_TO_UTF8(*ItemIconForUi(C,R->ItemId))); lua_pushstring(S,TCHAR_TO_UTF8(*T->Name)); lua_pushinteger(S,R->Count); lua_pushinteger(S,T->Quality); lua_pushboolean(S,T->Bonding==1);
        lua_pushboolean(S,(R->VoteMask&2)!=0); lua_pushboolean(S,(R->VoteMask&4)!=0); lua_pushboolean(S,(R->VoteMask&8)!=0);
        lua_pushinteger(S,Handler(C)->GroupInfo.LootMethod==4?1:5); lua_pushinteger(S,2); lua_pushinteger(S,T->RequiredDisenchantSkill?4:3); lua_pushinteger(S,T->RequiredDisenchantSkill); return 12;
    });
    Reg(L,"RollOnLoot",[](lua_State* S)->int {Vote(GetContext(S),luaL_checkinteger(S,1),luaL_checkinteger(S,2),false); return 0;});
    Reg(L,"ConfirmLootRoll",[](lua_State* S)->int {auto* C=GetContext(S); if(!C) return 0; const uint32 Id=luaL_checkinteger(S,1); const int32 Choice=luaL_checkinteger(S,2); const auto* Expected=View(C).ConfirmRoll.Find(Id); if(Expected && *Expected==Choice) {Vote(C,Id,Choice,true); View(C).ConfirmRoll.Remove(Id);} return 0;});
    Reg(L,"GetMasterLootCandidate",[](lua_State* S)->int {auto* C=GetContext(S); const auto* H=Handler(C); const int32 I=luaL_checkinteger(S,1)-1; const FString Text=H && H->Loot.Open && H->MasterLootGuid==H->Loot.Guid && H->MasterLootCandidates.IsValidIndex(I)?Name(C,H->MasterLootCandidates[I]):FString(); if(Text.IsEmpty()) lua_pushnil(S); else lua_pushstring(S,TCHAR_TO_UTF8(*Text)); return 1;});
    Reg(L,"GiveMasterLoot",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* H=Handler(C); const auto* I=Slot(C,luaL_checkinteger(S,1)); const int32 Target=luaL_checkinteger(S,2)-1;
        if(!H || !I || I->SlotType!=2 || H->GroupInfo.LootMethod!=2 || H->GroupInfo.MasterLooter!=Local(C) || H->MasterLootGuid!=H->Loot.Guid || !H->MasterLootCandidates.IsValidIndex(Target)) return 0;
        TArray<uint8> Bytes; Add(Bytes,H->Loot.Guid); Add(Bytes,I->Index); Add(Bytes,H->MasterLootCandidates[Target]); Send(C,0x02A3,Bytes); return 0;
    });
}
int WowLuaApi::SetLootItemTooltip(lua_State* L)
{auto* C=GetContext(L); const auto* I=Slot(C,luaL_checkinteger(L,2)); return Tooltip(L,I?Link(C,I->ItemId,int32(I->RandomProperty),I->RandomSuffix):FString());}
int WowLuaApi::SetLootRollItemTooltip(lua_State* L)
{auto* C=GetContext(L); const auto* R=Roll(C,luaL_checkinteger(L,2)); return Tooltip(L,R?Link(C,R->ItemId,R->RandomProperty,R->RandomSuffix):FString());}

void WowLuaApi::UpdateLootState(lua_State* L)
{
    auto* C=GetContext(L); auto* H=Handler(C); if(!H || !C->EventSystem) return;
    auto& V=View(C); auto* E=C->EventSystem; const auto& Loot=H->Loot;
    const bool Opened=Loot.Open && (!V.Loot.Open || V.Loot.Guid!=Loot.Guid || V.Loot.OpenSerial!=Loot.OpenSerial);
    if(V.Loot.Open && (!Loot.Open || V.Loot.Guid!=Loot.Guid)) {E->FireEvent(TEXT("LOOT_CLOSED")); V.ConfirmSlot=0;}
    if(Opened)
    {
        V.AutoGuid=AutoLoot(L)?Loot.Guid:0; V.AutoCoinSent=false; V.AutoSlots.Reset(); V.CloseSent=false; V.ConfirmSlot=0;
        E->FireEventNumbers(TEXT("LOOT_OPENED"),{V.AutoGuid?1.:0.});
    }
    if(Loot.Open)
    {
        if(!Opened && V.Loot.Gold && !Loot.Gold && Loot.HasMoneySlot) E->FireEventNumbers(TEXT("LOOT_SLOT_CLEARED"),{1});
        TMap<int32,uint64> Signatures;
        for(int32 Row=0;Row<Loot.Items.Num();++Row)
        {
            const auto& I=Loot.Items[Row]; const int32 Index=Row+1+int32(Loot.HasMoneySlot);
            const auto* T=QueryItemTemplate(C,I.ItemId); const uint64 Signature=(uint64(T?GetTypeHash(T->Name):0)<<32)|uint64(I.SlotType)|(uint64(I.bLooted)<<8);
            Signatures.Add(Index,Signature); const auto* Old=V.SlotSignatures.Find(Index);
            if(!Opened && Old && *Old!=Signature) E->FireEventNumbers(I.bLooted?TEXT("LOOT_SLOT_CLEARED"):TEXT("LOOT_SLOT_CHANGED"),{double(Index)});
        }
        V.SlotSignatures=MoveTemp(Signatures);
    }
    else V.SlotSignatures.Reset();
    V.Loot=Loot;
    if(Loot.Open)
    {
        bool Done=!Loot.Gold;
        if(V.AutoGuid==Loot.Guid && Loot.Gold && !V.AutoCoinSent) {C->ConnectionManager->SendLootMoney(); V.AutoCoinSent=true;}
        for(int32 Row=0;Row<Loot.Items.Num();++Row)
        {
            const auto& I=Loot.Items[Row]; if(I.bLooted) continue;
            const bool Automatic=V.AutoGuid==Loot.Guid;
            if(!Automatic || I.CanLoot()) Done=false;
            const int32 Index=Row+1+int32(Loot.HasMoneySlot);
            if(Automatic && I.CanLoot() && !V.AutoSlots.Contains(Index) && QueryItemTemplate(C,I.ItemId)) {Collect(C,Index,true); V.AutoSlots.Add(Index);}
        }
        if(Done) Close(C);
    }
    if(V.MasterRevision!=H->MasterLootRevision) {V.MasterRevision=H->MasterLootRevision; if(Loot.Open) E->FireEvent(TEXT("UPDATE_MASTER_LOOT_LIST"));}
    if(V.ErrorRevision!=H->LootErrorRevision)
    {
        V.ErrorRevision=H->LootErrorRevision;
        const char* Key="ERR_LOOT_CANT_LOOT_THAT_NOW";
        switch(H->LastLootError) {case 0:Key="ERR_LOOT_DIDNT_KILL";break; case 4:Key="ERR_LOOT_TOO_FAR";break;case 5:Key="ERR_LOOT_BAD_FACING";break;case 6:Key="ERR_LOOT_LOCKED";break;case 8:Key="ERR_LOOT_NOTSTANDING";break;case 9:Key="ERR_LOOT_STUNNED";break;case 10:Key="ERR_LOOT_PLAYER_NOT_FOUND";break;case 12:Key="ERR_LOOT_MASTER_INV_FULL";break;case 13:Key="ERR_LOOT_MASTER_UNIQUE_ITEM";break;case 14:Key="ERR_LOOT_MASTER_OTHER";break;}
        const FString Text=Global(L,Key); if(H->LastLootErrorGuid && !Text.IsEmpty()) E->FireEvent(TEXT("UI_ERROR_MESSAGE"),{Text});
    }
    int32 Active=0;
    for(uint32 Id:V.Started)
    {
        const auto* R=H->LootRolls.Find(Id); const bool Cancel=!R || R->Finished || R->MillisecondsLeft()<=0 || R->Choice>=0;
        if(Cancel && !V.Cancelled.Contains(Id)) {E->FireEventNumbers(TEXT("CANCEL_LOOT_ROLL"),{double(Id)}); V.Cancelled.Add(Id); V.ConfirmRoll.Remove(Id);}
        if(!Cancel) ++Active;
    }
    TArray<uint32> Ids; H->LootRolls.GetKeys(Ids); Ids.Sort();
    for(uint32 Id:Ids)
    {
        const auto& R=H->LootRolls[Id]; if(Active>=4 || R.Finished || R.Choice>=0 || R.MillisecondsLeft()<=0 || V.Started.Contains(Id) || !QueryItemTemplate(C,R.ItemId)) continue;
        V.Started.Add(Id); ++Active;
        E->FireEventTyped(TEXT("START_LOOT_ROLL"),[&](lua_State* S){lua_pushinteger(S,Id); lua_pushnumber(S,R.MillisecondsLeft()); return 2;});
    }
    for(const auto& Event:H->LootRollEvents)
    {
        if(Event.Sequence<=V.EventSequence) continue; const auto* R=H->LootRolls.Find(Event.RollId); if(!R) {V.EventSequence=Event.Sequence; continue;}
        const FString ItemLink=Link(C,R->ItemId,R->RandomProperty,R->RandomSuffix),Player=Name(C,Event.Player); if(ItemLink.IsEmpty() || (Event.Kind!=2 && Player.IsEmpty())) break;
        const bool Self=Event.Player==Local(C); FString Key,Text;
        if(Event.Kind==2) {Key=TEXT("LOOT_ROLL_ALL_PASSED"); Text=Format(L,Global(L,TCHAR_TO_UTF8(*Key)),{ItemLink});}
        else if(Event.Kind==1) {Key=Self?TEXT("LOOT_ROLL_YOU_WON"):TEXT("LOOT_ROLL_WON"); Text=Format(L,Global(L,TCHAR_TO_UTF8(*Key)),Self?TArray<FString>{ItemLink}:TArray<FString>{Player,ItemLink});}
        else if(Event.Number>=1 && Event.Number<=100)
        {
            const char* K=Event.Choice==1?"LOOT_ROLL_ROLLED_NEED":Event.Choice==3?"LOOT_ROLL_ROLLED_DE":"LOOT_ROLL_ROLLED_GREED";
            Text=Format(L,Global(L,K),{FString::FromInt(Event.Number),ItemLink,Player});
        }
        else
        {
            Key=Event.Choice==1?TEXT("LOOT_ROLL_NEED"):Event.Choice==2?TEXT("LOOT_ROLL_GREED"):Event.Choice==3?TEXT("LOOT_ROLL_DISENCHANT"):TEXT("LOOT_ROLL_PASSED");
            if(Self) Key+=TEXT("_SELF"); if(Event.Auto && Event.Choice==0) Key+=TEXT("_AUTO");
            Text=Format(L,Global(L,TCHAR_TO_UTF8(*Key)),Self?TArray<FString>{ItemLink}:TArray<FString>{Player,ItemLink});
        }
        Chat(C,TEXT("CHAT_MSG_LOOT"),Text,Event.Player); V.EventSequence=Event.Sequence;
    }
    for(const auto& Notice:H->LootNotifications)
    {
        if(Notice.Sequence<=V.NoticeSequence) continue;
        const bool Self=Notice.Player==Local(C); FString Text;
        if(Notice.Money) Text=Format(L,Global(L,Notice.Sole?"YOU_LOOT_MONEY":"LOOT_MONEY_SPLIT"),{MoneyText(L,Notice.Count)});
        else
        {
            const FString ItemLink=Link(C,Notice.Item,Notice.RandomProperty,Notice.RandomSuffix),Player=Name(C,Notice.Player);
            if(ItemLink.IsEmpty() || (!Self && Player.IsEmpty())) break;
            FString Key=Notice.Created && Self?TEXT("LOOT_ITEM_CREATED_SELF"):Notice.Received?(Self?TEXT("LOOT_ITEM_PUSHED_SELF"):TEXT("LOOT_ITEM_PUSHED")):(Self?TEXT("LOOT_ITEM_SELF"):TEXT("LOOT_ITEM"));
            TArray<FString> Args; if(!Self) Args.Add(Player); Args.Add(ItemLink); if(Notice.Count>1) {Key+=TEXT("_MULTIPLE"); Args.Add(FString::FromInt(Notice.Count));}
            Text=Format(L,Global(L,TCHAR_TO_UTF8(*Key)),Args);
        }
        Chat(C,Notice.Money?TEXT("CHAT_MSG_MONEY"):TEXT("CHAT_MSG_LOOT"),Text,Notice.Player); V.NoticeSequence=Notice.Sequence;
    }
    // Bound per-VM bookkeeping when the network cache prunes old completed rolls.
    if(V.Started.Num()>512) for(auto I=V.Started.CreateIterator();I;++I) if(!H->LootRolls.Contains(*I)) {V.Cancelled.Remove(*I); I.RemoveCurrent();}
}
#else
void WowLuaApi::RegisterLootApi(lua_State*) {}
void WowLuaApi::UpdateLootState(lua_State*) {}
int WowLuaApi::SetLootItemTooltip(lua_State*) {return 0;}
int WowLuaApi::SetLootRollItemTooltip(lua_State*) {return 0;}
#endif
