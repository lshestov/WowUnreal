#include "LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"
#include "WowEventSystem.h"
#include "WowFrameManager.h"
#include "WowMacros.h"
#include "Formats/Dbc/DbcStore.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"

#if __has_include("lua.h")
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
namespace
{
void RegisterItem(lua_State* L,const char* Name,lua_CFunction Function) { lua_pushcfunction(L,Function); lua_setglobal(L,Name); }
uint32 ItemId(lua_State* L,int32 Index)
{
    if(lua_isnumber(L,Index)) return uint32(FMath::Max(lua_tonumber(L,Index),0.0));
    FString Text=UTF8_TO_TCHAR(luaL_optstring(L,Index,""));
    int32 Start=Text.Find(TEXT("item:"));
    if(Start>=0) return FCString::Strtoui64(*Text.Mid(Start+5),nullptr,10);
    if(Text.IsNumeric()) return FCString::Strtoui64(*Text,nullptr,10);
    auto* C=WowLuaApi::GetContext(L); if(C && C->ConnectionManager) for(const auto& Pair:C->ConnectionManager->PacketHandler.ItemTemplates) if(Pair.Value.Name.Equals(Text,ESearchCase::IgnoreCase)) return Pair.Key;
    return 0;
}
const FWowItemTemplate* Template(FWowLuaContext* C,uint32 Id)
{
    if(!C || !C->ConnectionManager || !Id) return nullptr;
    C->ConnectionManager->RequestItemTemplate(Id);
    return C->ConnectionManager->PacketHandler.ItemTemplates.Find(Id);
}
FString Icon(const FWowItemTemplate* T)
{
    if(T) if(const auto* Display=FDbcStore::Get().ItemDisplayInfo().GetById(T->DisplayId); Display && !Display->Icon1.IsEmpty())
        return TEXT("Interface\\Icons\\")+Display->Icon1;
    return TEXT("Interface\\Icons\\INV_Misc_QuestionMark");
}
TArray<uint32> CurrencyRows(FWowLuaContext* C)
{
    TArray<uint32> Rows; const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; if(!P) return Rows;
    const uint64 Known=P->GetField64(PlayerField::BASE+0x01E4);
    for(const auto& Currency:FDbcStore::Get().Currencies()) if(Known&(1ULL<<(Currency.Bit-1))) { Rows.AddUnique(Currency.Item); Template(C,Currency.Item); }
    Rows.Sort(); return Rows;
}
int32 CurrencyCount(FWowLuaContext* C,uint32 Id)
{
    const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; if(!P) return 0;
    if(Id==43307 || Id==43308) return P->GetField(PlayerField::BASE+(Id==43307?0x046A:0x0469));
    int32 Count=0; for(int32 I=0;I<32;++I) if(const auto* Item=C->EntityManager->FindItem(P->GetField64(PlayerField::BASE+0x019C+I*2)); Item && Item->Entry==Id) Count+=Item->GetStackCount();
    return Count;
}
int CurrencyInfo(lua_State* L,uint32 Id,bool Backpack)
{
    auto* C=WowLuaApi::GetContext(L); const auto* T=Template(C,Id);
    lua_pushstring(L,TCHAR_TO_UTF8(*(T?T->Name:FString(TEXT("Retrieving item information...")))));
    if(!Backpack)
    {
        lua_pushboolean(L,0); lua_pushboolean(L,1);
        lua_pushboolean(L,WowLuaApi::GetUiPreference(L,FString::Printf(TEXT("currencyUnused%u"),Id))!=0);
        lua_pushboolean(L,WowLuaApi::GetUiPreference(L,FString::Printf(TEXT("currencyWatch%u"),Id))!=0);
    }
    lua_pushinteger(L,CurrencyCount(C,Id)); lua_pushinteger(L,Id==43307?1:Id==43308?2:0); lua_pushstring(L,TCHAR_TO_UTF8(*Icon(T))); lua_pushinteger(L,Id);
    return Backpack?5:9;
}
FLinearColor Quality(uint32 Value)
{
    static const FLinearColor Colors[]={FLinearColor(.62f,.62f,.62f),FLinearColor::White,FLinearColor(.12f,1,0),FLinearColor(0,.44f,.87f),FLinearColor(.64f,.21f,.93f),FLinearColor(1,.5f,0),FLinearColor(.9f,.8f,.5f),FLinearColor(.9f,.8f,.5f)};
    return Colors[FMath::Min<uint32>(Value,UE_ARRAY_COUNT(Colors)-1)];
}
FString Link(const FWowItemTemplate* T,uint32 Id)
{
    const FColor Color=Quality(T?T->Quality:1).ToFColor(false);
    return FString::Printf(TEXT("|cff%02x%02x%02x|Hitem:%u:0:0:0:0:0:0:0:0:0|h[%s]|h|r"),Color.R,Color.G,Color.B,Id,T?*T->Name:*FString::Printf(TEXT("Item %u"),Id));
}
const TCHAR* EquipType(uint32 Id)
{
    static const TCHAR* Names[]={TEXT(""),TEXT("INVTYPE_HEAD"),TEXT("INVTYPE_NECK"),TEXT("INVTYPE_SHOULDER"),TEXT("INVTYPE_BODY"),TEXT("INVTYPE_CHEST"),TEXT("INVTYPE_WAIST"),TEXT("INVTYPE_LEGS"),TEXT("INVTYPE_FEET"),TEXT("INVTYPE_WRIST"),TEXT("INVTYPE_HAND"),TEXT("INVTYPE_FINGER"),TEXT("INVTYPE_TRINKET"),TEXT("INVTYPE_WEAPON"),TEXT("INVTYPE_SHIELD"),TEXT("INVTYPE_RANGED"),TEXT("INVTYPE_CLOAK"),TEXT("INVTYPE_2HWEAPON"),TEXT("INVTYPE_BAG"),TEXT("INVTYPE_TABARD"),TEXT("INVTYPE_ROBE"),TEXT("INVTYPE_WEAPONMAINHAND"),TEXT("INVTYPE_WEAPONOFFHAND"),TEXT("INVTYPE_HOLDABLE"),TEXT("INVTYPE_AMMO"),TEXT("INVTYPE_THROWN"),TEXT("INVTYPE_RANGEDRIGHT"),TEXT("INVTYPE_QUIVER"),TEXT("INVTYPE_RELIC")};
    return Id<UE_ARRAY_COUNT(Names)?Names[Id]:TEXT("");
}
uint64 InventoryGuid(FWowLuaContext* C,int32 Slot)
{
    auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    if(!P) return 0;
    return Slot>=1 && Slot<=150?P->GetField64(PlayerField::INV_SLOT_HEAD+(Slot-1)*2):0;
}
int32 Slots(FWowLuaContext* C,int32 Bag)
{
    if(Bag==0) return 16;
    if(Bag==-1) return 28;
    auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    if(!P) return 0;
    uint64 Guid=Bag>=1 && Bag<=4?P->GetBagGuid(Bag):Bag>=5 && Bag<=11?P->GetBankBagGuid(Bag-5):0;
    const auto* Container=C->EntityManager->FindContainer(Guid);
    return Container?FMath::Clamp(Container->GetNumSlots(),0,36):0;
}
FWowItemEntity* BagItem(FWowLuaContext* C,int32 Bag,int32 Slot)
{
    auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    if(!P || Slot<1 || Slot>Slots(C,Bag)) return nullptr;
    uint64 Guid=0;
    if(Bag==0) Guid=P->GetBackpackItemGuid(Slot-1);
    else if(Bag==-1) Guid=P->GetBankItemGuid(Slot-1);
    else
    {
        uint64 BagGuid=Bag<=4?P->GetBagGuid(Bag):P->GetBankBagGuid(Bag-5);
        if(const auto* Container=C->EntityManager->FindContainer(BagGuid)) Guid=Container->GetItemGuidAtSlot(Slot-1);
    }
    return C->EntityManager->FindItem(Guid);
}
FWowItemEntity* Equipped(FWowLuaContext* C,int32 Slot);
FWowItemEntity* PurchaseItem(lua_State* L,FWowLuaContext* C,int32 EquippedArgument)
{
    const int32 Bag=luaL_checkinteger(L,1),Slot=luaL_checkinteger(L,2);
    return lua_toboolean(L,EquippedArgument)?Equipped(C,Slot):BagItem(C,Bag,Slot);
}
const FWowItemRefundInfo* RefundInfo(FWowLuaContext* C,FWowItemEntity* Item)
{
    if(!Item || !C || !C->ConnectionManager || !(Item->GetField(ItemField::FLAGS)&0x1000)) return nullptr;
    C->ConnectionManager->RequestItemRefundInfo(Item->Guid);
    for(int32 I:{0,2,3,6}) if(Item->GetField(ItemField::ENCHANTMENT_1_1+I*3)) return nullptr;
    const auto& H=C->ConnectionManager->PacketHandler;
    return H.ItemRefundSeconds(Item->Guid)>0?H.ItemRefunds.Find(Item->Guid):nullptr;
}
FWowItemEntity* Equipped(FWowLuaContext* C,int32 Slot)
{
    return C && C->EntityManager?C->EntityManager->FindItem(InventoryGuid(C,Slot)):nullptr;
}
int Info(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const uint32 Id=ItemId(L,1); const auto* T=Template(C,Id);
    if(!T) { lua_pushnil(L); return 1; }
    static const TCHAR* Classes[]={TEXT("Consumable"),TEXT("Container"),TEXT("Weapon"),TEXT("Gem"),TEXT("Armor"),TEXT("Reagent"),TEXT("Projectile"),TEXT("Trade Goods"),TEXT("Generic"),TEXT("Recipe"),TEXT("Money"),TEXT("Quiver"),TEXT("Quest"),TEXT("Key"),TEXT("Permanent"),TEXT("Miscellaneous"),TEXT("Glyph")};
    lua_pushstring(L,TCHAR_TO_UTF8(*T->Name)); lua_pushstring(L,TCHAR_TO_UTF8(*Link(T,Id)));
    lua_pushinteger(L,T->Quality); lua_pushinteger(L,T->ItemLevel); lua_pushinteger(L,T->RequiredLevel);
    const auto* Class=FDbcStore::Get().ItemUi().ClassName(T->Class); const auto* Subclass=FDbcStore::Get().ItemUi().SubclassName(T->Class,T->Subclass);
    lua_pushstring(L,Class?TCHAR_TO_UTF8(**Class):TCHAR_TO_UTF8((T->Class<UE_ARRAY_COUNT(Classes)?Classes[T->Class]:TEXT("Miscellaneous"))));
    lua_pushstring(L,Subclass?TCHAR_TO_UTF8(**Subclass):""); lua_pushinteger(L,T->MaxStack); lua_pushstring(L,TCHAR_TO_UTF8(EquipType(T->InventoryType)));
    lua_pushstring(L,TCHAR_TO_UTF8(*Icon(T))); lua_pushinteger(L,T->SellPrice); return 11;
}
int BagInfo(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); auto* Item=BagItem(C,luaL_checkinteger(L,1),luaL_checkinteger(L,2));
    if(!Item) { lua_pushnil(L); return 1; }
    const auto* T=Template(C,Item->Entry);
    const auto* Held=C->ConnectionManager?C->ConnectionManager->GetCursorItem():nullptr;
    lua_pushstring(L,TCHAR_TO_UTF8(*Icon(T))); lua_pushinteger(L,FMath::Max(Item->GetStackCount(),1)); lua_pushboolean(L,Held && Held->Guid==Item->Guid);
    if(T) lua_pushinteger(L,T->Quality); else lua_pushnil(L);
    lua_pushboolean(L,0); lua_pushboolean(L,0); lua_pushstring(L,TCHAR_TO_UTF8(*Link(T,Item->Entry))); return 7;
}
int BagLink(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); auto* Item=BagItem(C,luaL_checkinteger(L,1),luaL_checkinteger(L,2));
    if(Item) lua_pushstring(L,TCHAR_TO_UTF8(*Link(Template(C,Item->Entry),Item->Entry))); else lua_pushnil(L); return 1;
}
int InventoryTexture(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const int32 Slot=luaL_checkinteger(L,lua_gettop(L)>=2?2:1);
    auto* Item=Equipped(C,Slot);
    if(Item) lua_pushstring(L,TCHAR_TO_UTF8(*Icon(Template(C,Item->Entry)))); else lua_pushnil(L); return 1;
}
int InventoryLink(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); auto* Item=Equipped(C,luaL_checkinteger(L,lua_gettop(L)>=2?2:1));
    if(Item) lua_pushstring(L,TCHAR_TO_UTF8(*Link(Template(C,Item->Entry),Item->Entry))); else lua_pushnil(L); return 1;
}
void Texture(lua_State* L,int32 Index,const FString& Path)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->FrameManager) return;
    FString Name;
    if(lua_istable(L,Index)) { lua_getfield(L,Index,"__name"); Name=UTF8_TO_TCHAR(lua_tostring(L,-1)?lua_tostring(L,-1):""); lua_pop(L,1); }
    else Name=UTF8_TO_TCHAR(luaL_optstring(L,Index,""));
    auto* Image=C->FrameManager->GetNamedTexture(Name); auto* Loaded=C->FrameManager->LoadUITexture(Path);
    if(Image && Loaded) { Image->SetBrushFromTexture(Loaded,false); Image->SetRenderOpacity(1); }
    if(lua_istable(L,Index)) { lua_pushstring(L,TCHAR_TO_UTF8(*Path)); lua_setfield(L,Index,"__texture"); }
}
int BagPortrait(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const int32 Bag=luaL_checkinteger(L,2);
    FString Path=TEXT("Interface\\Buttons\\Button-Backpack-Up");
    if(Bag>=1 && Bag<=4) if(auto* Item=Equipped(C,Bag+19)) Path=Icon(Template(C,Item->Entry));
    Texture(L,1,Path); return 0;
}
int FreeSlots(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const int32 Bag=luaL_checkinteger(L,1); int32 Free=0;
    for(int32 I=1; I<=Slots(C,Bag); ++I) if(!BagItem(C,Bag,I)) ++Free;
    lua_pushinteger(L,Free); lua_pushinteger(L,0); return 2;
}
bool Position(FWowLuaContext* C,int32 Bag,int32 Slot,uint8& WireBag,uint8& WireSlot)
{
    if(Slot<1 || Slot>Slots(C,Bag)) return false;
    WireBag=Bag==0 || Bag==-1?255:Bag<=4?Bag+18:Bag+62;
    WireSlot=Slot-1+(Bag==0?23:Bag==-1?39:0); return true;
}
int Pickup(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->ConnectionManager) return 0;
    const int32 Bag=luaL_checkinteger(L,1),Slot=luaL_checkinteger(L,2); uint8 B,S;
    if(!Position(C,Bag,Slot,B,S)) return 0;
    auto* Connection=C->ConnectionManager;
    if(Connection->IsRepairMode()) { if(auto* Item=BagItem(C,Bag,Slot)) Connection->SendRepairItems(Item->Guid); return 0; }
    if(Connection->HasCursorItemPayload() || Connection->HasCursorMerchantPayload()) Connection->PlaceCursorIntoContainerSlot(B,S);
    else if(auto* Item=BagItem(C,Bag,Slot)) Connection->PickupBagItemCursor(B,S,Item->Entry,Link(Template(C,Item->Entry),Item->Entry));
    return 0;
}
int PickupEquipment(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const int32 Slot=luaL_checkinteger(L,1);
    if(!C || !C->ConnectionManager || Slot<1 || Slot>23) return 0;
    auto* Connection=C->ConnectionManager;
    if(Connection->IsRepairMode()) { if(auto* Item=Equipped(C,Slot)) Connection->SendRepairItems(Item->Guid); return 0; }
    if(Connection->HasCursorItemPayload() || Connection->HasCursorMerchantPayload()) Connection->PlaceCursorIntoContainerSlot(255,Slot-1);
    else if(auto* Item=Equipped(C,Slot)) Connection->PickupInventoryItemCursor(Slot-1,Item->Entry,Link(Template(C,Item->Entry),Item->Entry));
    return 0;
}
int Use(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->ConnectionManager) return 0;
    const int32 Bag=luaL_checkinteger(L,1),Slot=luaL_checkinteger(L,2); uint8 B,S;
    if(!Position(C,Bag,Slot,B,S)) return 0;
    auto* Item=BagItem(C,Bag,Slot); const auto* T=Item?Template(C,Item->Entry):nullptr;
    if(Item && C->ConnectionManager->IsRepairMode()) { C->ConnectionManager->SendRepairItems(Item->Guid); return 0; }
    if(Item && C->ConnectionManager->PacketHandler.Merchant.Open)
    {
        auto& H=C->ConnectionManager->PacketHandler;
        if(Item->GetField(ItemField::FLAGS)&0x1000) { C->ConnectionManager->RequestItemRefundInfo(Item->Guid); if(!H.ItemRefunds.Contains(Item->Guid) || H.PlayedTimeReceived<=0) return 0; }
        C->ConnectionManager->SendSellItem(H.Merchant.Guid,Item->Guid,0); return 0;
    }
    if(T && T->InventoryType) C->ConnectionManager->SendAutoEquipItem(B,S);
    else if(T && T->UseSpellId) C->ConnectionManager->SendUseItem(B,S,Item->Guid,T->UseSpellId);
    return 0;
}
int Split(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->ConnectionManager) return 0;
    const int32 Bag=luaL_checkinteger(L,1),Slot=luaL_checkinteger(L,2),Count=luaL_checkinteger(L,3); uint8 B,S;
    auto* Item=BagItem(C,Bag,Slot);
    if(!Position(C,Bag,Slot,B,S) || !Item || Count<1 || Count>Item->GetStackCount()) return 0;
    C->ConnectionManager->PickupBagItemCursor(B,S,Item->Entry,Link(Template(C,Item->Entry),Item->Entry),Count==Item->GetStackCount()?0:Count); return 0;
}
int64 Handle(lua_State* L)
{
    lua_getfield(L,1,"__handle"); const int64 Value=lua_tointeger(L,-1); lua_pop(L,1); return Value;
}
const TCHAR* StatToken(uint32 Id)
{
    static const TCHAR* Names[]={TEXT("MANA"),TEXT("HEALTH"),TEXT(""),TEXT("AGILITY"),TEXT("STRENGTH"),TEXT("INTELLECT"),TEXT("SPIRIT"),TEXT("STAMINA"),TEXT(""),TEXT(""),TEXT(""),TEXT(""),
        TEXT("DEFENSE_SKILL_RATING"),TEXT("DODGE_RATING"),TEXT("PARRY_RATING"),TEXT("BLOCK_RATING"),TEXT("HIT_MELEE_RATING"),TEXT("HIT_RANGED_RATING"),TEXT("HIT_SPELL_RATING"),
        TEXT("CRIT_MELEE_RATING"),TEXT("CRIT_RANGED_RATING"),TEXT("CRIT_SPELL_RATING"),TEXT("HIT_TAKEN_MELEE_RATING"),TEXT("HIT_TAKEN_RANGED_RATING"),TEXT("HIT_TAKEN_SPELL_RATING"),
        TEXT("CRIT_TAKEN_MELEE_RATING"),TEXT("CRIT_TAKEN_RANGED_RATING"),TEXT("CRIT_TAKEN_SPELL_RATING"),TEXT("HASTE_MELEE_RATING"),TEXT("HASTE_RANGED_RATING"),TEXT("HASTE_SPELL_RATING"),
        TEXT("HIT_RATING"),TEXT("CRIT_RATING"),TEXT("HIT_TAKEN_RATING"),TEXT("CRIT_TAKEN_RATING"),TEXT("RESILIENCE_RATING"),TEXT("HASTE_RATING"),TEXT("EXPERTISE_RATING"),
        TEXT("ATTACK_POWER"),TEXT("RANGED_ATTACK_POWER"),TEXT("FERAL_ATTACK_POWER"),TEXT("SPELL_HEALING_DONE"),TEXT("SPELL_DAMAGE_DONE"),TEXT("MANA_REGENERATION"),
        TEXT("ARMOR_PENETRATION_RATING"),TEXT("SPELL_POWER"),TEXT("HEALTH_REGEN"),TEXT("SPELL_PENETRATION"),TEXT("BLOCK_VALUE")};
    return Id<UE_ARRAY_COUNT(Names)?Names[Id]:TEXT("");
}
FString StatKey(uint32 Id) { const TCHAR* Token=StatToken(Id); return *Token?FString::Printf(TEXT("ITEM_MOD_%s_SHORT"),Token):FString(); }
FString Localized(lua_State* L,const FString& Key,const FString& Fallback)
{
    lua_getglobal(L,TCHAR_TO_UTF8(*Key)); const FString Text=lua_isstring(L,-1)?UTF8_TO_TCHAR(lua_tostring(L,-1)):Fallback; lua_pop(L,1); return Text;
}
FString StatLabel(lua_State* L,const FString& Key)
{
    if(Key==TEXT("RESISTANCE0_NAME")) return Localized(L,Key,TEXT("Armor"));
    FString Fallback=Key; Fallback.RemoveFromStart(TEXT("ITEM_MOD_")); Fallback.RemoveFromEnd(TEXT("_SHORT"));
    return Localized(L,Key,Fallback.Replace(TEXT("_"),TEXT(" ")).ToLower());
}
TMap<FString,float> ItemStats(const FWowItemTemplate* T)
{
    TMap<FString,float> Values; if(!T) return Values;
    if(T->Armor) Values.Add(TEXT("RESISTANCE0_NAME"),T->Armor);
    if(T->DamageMax>0 && T->Delay) Values.Add(TEXT("ITEM_MOD_DAMAGE_PER_SECOND_SHORT"),(T->DamageMin+T->DamageMax)*500.f/T->Delay);
    for(const auto& Stat:T->Stats) { const auto Key=StatKey(Stat.Key); if(!Key.IsEmpty()) Values.FindOrAdd(Key)+=Stat.Value; }
    return Values;
}
TMap<FString,float> ItemDelta(const FWowItemTemplate* Incoming,const FWowItemTemplate* EquippedItem)
{
    auto Values=ItemStats(Incoming); for(const auto& Stat:ItemStats(EquippedItem)) Values.FindOrAdd(Stat.Key)-=Stat.Value; return Values;
}
int PushStats(lua_State* L,const TMap<FString,float>& Values,int32 OptionalTable)
{
    if(lua_istable(L,OptionalTable)) lua_pushvalue(L,OptionalTable); else lua_newtable(L);
    for(const auto& Stat:Values) if(!FMath::IsNearlyZero(Stat.Value)) { lua_pushnumber(L,Stat.Value); lua_setfield(L,-2,TCHAR_TO_UTF8(*Stat.Key)); }
    return 1;
}
void ItemTooltip(lua_State* L,FWowLuaContext* C,int64 H,const FWowItemTemplate* T,uint32 Id,FWowItemEntity* Item=nullptr,bool Compare=false)
{
    auto* F=C->FrameManager; F->TooltipClearLines(H);
    auto* State=F->GetTooltipState(H); State->ItemLink=Link(T,Id); State->ItemId=Id; State->ItemName=T?T->Name:TEXT("Retrieving item information...");
    if(Compare) F->TooltipAddLine(H,Localized(L,TEXT("CURRENTLY_EQUIPPED"),TEXT("Currently Equipped")),1,.82f,0,false);
    const FLinearColor Color=Quality(T?T->Quality:1);
    F->TooltipAddLine(H,T?T->Name:TEXT("Retrieving item information..."),Color.R,Color.G,Color.B,false);
    if(T)
    {
        if(T->DamageMax>0)
        {
            F->TooltipAddDoubleLine(H,FString::Printf(TEXT("%.0f - %.0f Damage"),T->DamageMin,T->DamageMax),FString::Printf(TEXT("Speed %.2f"),T->Delay/1000.f),1,1,1,1,1,1);
            if(T->Delay) F->TooltipAddLine(H,FString::Printf(TEXT("(%.1f damage per second)"),(T->DamageMin+T->DamageMax)*500.f/T->Delay),1,1,1,false);
        }
        if(T->Armor) F->TooltipAddLine(H,FString::Printf(TEXT("%u %s"),T->Armor,*StatLabel(L,TEXT("RESISTANCE0_NAME"))),1,1,1,false);
        for(const auto& Stat:T->Stats) if(const auto Key=StatKey(Stat.Key); !Key.IsEmpty()) F->TooltipAddLine(H,FString::Printf(TEXT("%+d %s"),Stat.Value,*StatLabel(L,Key)),1,1,1,false);
        if(T->RequiredLevel>1) F->TooltipAddLine(H,FString::Printf(TEXT("Requires Level %u"),T->RequiredLevel),1,1,1,false);
        if(!T->Description.IsEmpty()) F->TooltipAddLine(H,T->Description,1,.82f,0,true);
        if(T->UseSpellId) { const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const auto Text=FDbcStore::Get().Spells().Description(T->UseSpellId,P?P->GetLevel():1); if(!Text.IsEmpty()) F->TooltipAddLine(H,TEXT("Use: ")+Text,0,1,0,true); }
        if(Item && Item->GetField(ItemField::MAX_DURABILITY)) F->TooltipAddLine(H,FString::Printf(TEXT("Durability %u / %u"),Item->GetField(ItemField::DURABILITY),Item->GetField(ItemField::MAX_DURABILITY)),1,1,1,false);
    }
    F->SetFrameVisible(H,true);
}
}
const FWowItemTemplate* WowLuaApi::QueryItemTemplate(FWowLuaContext* C,uint32 Id) { return Template(C,Id); }
FString WowLuaApi::ItemLinkForUi(FWowLuaContext* C,uint32 Id) { return Link(Template(C,Id),Id); }
FString WowLuaApi::ItemIconForUi(FWowLuaContext* C,uint32 Id,uint32 Display)
{
    if(const auto* D=FDbcStore::Get().ItemDisplayInfo().GetById(Display); D && !D->Icon1.IsEmpty()) return TEXT("Interface\\Icons\\")+D->Icon1;
    return Icon(Template(C,Id));
}
int WowLuaApi::SetMerchantItemTooltip(lua_State* L)
{
    auto* C=GetContext(L); const int32 Index=luaL_checkinteger(L,2)-1;
    if(!C || !C->FrameManager || !C->ConnectionManager) return 0;
    const auto& M=C->ConnectionManager->PacketHandler.Merchant;
    if(M.Open && M.Items.IsValidIndex(Index)) { const auto Id=M.Items[Index].ItemId; ItemTooltip(L,C,Handle(L),Template(C,Id),Id); }
    else { C->FrameManager->TooltipClearLines(Handle(L)); C->FrameManager->SetFrameVisible(Handle(L),false); }
    return 0;
}
int WowLuaApi::SetBuybackItemTooltip(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->FrameManager) return 0;
    if(auto* Item=BuybackItemForUi(C,luaL_checkinteger(L,2))) ItemTooltip(L,C,Handle(L),Template(C,Item->Entry),Item->Entry,Item);
    else { C->FrameManager->TooltipClearLines(Handle(L)); C->FrameManager->SetFrameVisible(Handle(L),false); }
    return 0;
}
int WowLuaApi::SetBagItemTooltip(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->FrameManager) return 0;
    auto* Item=BagItem(C,luaL_checkinteger(L,2),luaL_checkinteger(L,3)); const int64 H=Handle(L);
    if(Item) ItemTooltip(L,C,H,Template(C,Item->Entry),Item->Entry,Item); else C->FrameManager->SetFrameVisible(H,false);
    lua_pushboolean(L,0); lua_pushinteger(L,Item?ItemRepairCost(C,*Item):0); return 2;
}
int WowLuaApi::SetInventoryItemTooltip(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->FrameManager) return 0;
    auto* Item=Equipped(C,luaL_checkinteger(L,3)); const int64 H=Handle(L);
    if(Item) ItemTooltip(L,C,H,Template(C,Item->Entry),Item->Entry,Item); else C->FrameManager->SetFrameVisible(H,false);
    lua_pushboolean(L,Item!=nullptr); lua_pushboolean(L,0); lua_pushinteger(L,Item?ItemRepairCost(C,*Item):0); return 3;
}
int WowLuaApi::SetHyperlinkTooltip(lua_State* L)
{
    auto* C=GetContext(L); const uint32 Id=ItemId(L,2);
    if(C && C->FrameManager && Id) ItemTooltip(L,C,Handle(L),Template(C,Id),Id);
    return 0;
}
void WowLuaApi::UpdateItemComparisons(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->FrameManager) return;
    const int32 Top=lua_gettop(L);
    const auto Call=[&](int32 Args,int32 Results)->bool
    {
        if(lua_pcall(L,Args,Results,0)==0) return true;
        UE_LOG(LogWowLuaFrame,Warning,TEXT("Item comparison: %s"),UTF8_TO_TCHAR(lua_tostring(L,-1))); lua_pop(L,1); return false;
    };
    lua_getglobal(L,"GameTooltip"); if(!lua_istable(L,-1)) { lua_settop(L,Top); return; }
    const int32 Tip=lua_gettop(L); lua_getfield(L,Tip,"__handle"); const int64 H=lua_tointeger(L,-1); lua_pop(L,1);
    const auto* State=C->FrameManager->GetTooltipState(H);
    bool Compare=false,Shift=false;
    if(State && State->ItemId && C->FrameManager->IsFrameVisible(H))
    {
        lua_getglobal(L,"IsModifiedClick"); lua_pushstring(L,"COMPAREITEMS");
        if(Call(1,1)) { Shift=lua_toboolean(L,-1)!=0; lua_pop(L,1); }
        Compare=Shift;
        lua_getglobal(L,"GetCVar"); lua_pushstring(L,"alwaysCompareItems");
        if(Call(1,1)) { Compare|=FCStringAnsi::Atoi(lua_tostring(L,-1)?lua_tostring(L,-1):"")!=0; lua_pop(L,1); }
    }
    if(Compare)
    {
        lua_getglobal(L,"GameTooltip_ShowCompareItem");
        if(lua_isfunction(L,-1)) { lua_pushvalue(L,Tip); lua_pushboolean(L,Shift); Call(2,0); }
        else lua_pop(L,1);
    }
    else
    {
        lua_getfield(L,Tip,"shoppingTooltips");
        if(lua_istable(L,-1))
        {
            const int32 Shopping=lua_gettop(L); lua_pushnil(L);
            while(lua_next(L,Shopping))
            {
                if(lua_istable(L,-1))
                {
                    lua_getfield(L,-1,"__handle"); const int64 Other=lua_tointeger(L,-1); lua_pop(L,1);
                    if(C->FrameManager->IsFrameVisible(Other)) C->FrameManager->SetFrameVisible(Other,false);
                }
                lua_pop(L,1);
            }
        }
    }
    lua_pushboolean(L,Compare); lua_setfield(L,Tip,"comparing"); lua_settop(L,Top);
}
int WowLuaApi::SetHyperlinkCompareItem(lua_State* L)
{
    auto* C=GetContext(L); const uint32 Id=ItemId(L,2); const int32 Index=luaL_optinteger(L,3,1)-1;
    const int64 H=Handle(L);
    const auto Fail=[&] { if(C && C->FrameManager) { C->FrameManager->TooltipClearLines(H); C->FrameManager->SetFrameVisible(H,false); } lua_pushnil(L); return 1; };
    const auto* Incoming=Template(C,Id);
    if(!C || !C->FrameManager || !Incoming || Index<0 || Index>1) return Fail();
    // 3.3.5 equipment masks. Containers are outside the 19 equipped-item slots.
    static const uint32 Masks[]={0,1,2,4,8,16,32,64,128,256,512,3072,12288,98304,65536,131072,16384,98304,7864320,262144,16,98304,98304,65536,0,131072,131072,0,131072};
    const uint32 Mask=Incoming->InventoryType<UE_ARRAY_COUNT(Masks)?Masks[Incoming->InventoryType]:0;
    TArray<FWowItemEntity*> Candidates;
    auto* Main=Equipped(C,16); auto* Off=Equipped(C,17);
    if(Mask&0x18000)
    {
        if((Mask&0x18000)==0x18000 || Incoming->InventoryType==17)
        {
            if(Main && Off && Main!=Off) { Candidates.Add(Off); Candidates.Add(Main); }
            else if(Main || Off) Candidates.Add(Main?Main:Off);
        }
        else if(Mask&0x8000) { if(Main) Candidates.Add(Main); }
        else if(Off) Candidates.Add(Off);
        else if(Main) { const auto* T=Template(C,Main->Entry); if(T && T->InventoryType==17) Candidates.Add(Main); }
    }
    else for(int32 Slot=0;Slot<19;++Slot) if(Mask&(1u<<Slot)) if(auto* Item=Equipped(C,Slot+1)) Candidates.Add(Item);
    if(!Candidates.IsValidIndex(Index)) return Fail();
    auto* Item=Candidates[Index]; const auto* Current=Template(C,Item->Entry); if(!Current) return Fail();
    ItemTooltip(L,C,H,Current,Item->Entry,Item,true);
    if(lua_istable(L,5))
    {
        lua_getfield(L,5,"__handle"); const auto* Source=C->FrameManager->GetTooltipState(lua_tointeger(L,-1)); lua_pop(L,1);
        if(Source && Source->ItemId==Id)
        {
            const auto Delta=ItemDelta(Incoming,Current); TArray<FString> Keys; Delta.GetKeys(Keys); Keys.Sort();
            bool Header=false;
            for(const auto& Key:Keys) if(const float Value=Delta[Key]; !FMath::IsNearlyZero(Value))
            {
                if(!Header)
                {
                    C->FrameManager->TooltipAddLine(H,TEXT(" "),1,1,1,false);
                    C->FrameManager->TooltipAddLine(H,Localized(L,TEXT("ITEM_DELTA_DESCRIPTION"),TEXT("If you replace this item, the following stat changes will occur:")),1,.82f,0,true); Header=true;
                }
                const bool Positive=Value>0;
                const FString Amount=Key==TEXT("ITEM_MOD_DAMAGE_PER_SECOND_SHORT")?FString::Printf(TEXT("%+.1f"),Value):FString::Printf(TEXT("%+d"),FMath::RoundToInt(Value));
                C->FrameManager->TooltipAddLine(H,Amount+TEXT(" ")+StatLabel(L,Key),Positive?0:1,Positive?1:.1f,0,false);
            }
        }
    }
    lua_pushboolean(L,1); return 1;
}
int WowLuaApi::TryOnItem(lua_State* L)
{
    auto* C=GetContext(L); const uint32 Id=ItemId(L,2); const auto* T=Template(C,Id);
    if(C && C->FrameManager && C->FrameManager->OnModelTryOn && T && T->InventoryType) C->FrameManager->OnModelTryOn(Handle(L),Id,T->InventoryType);
    return 0;
}
int WowLuaApi::SetActionTooltip(lua_State* L)
{
    auto* C=GetContext(L); const int32 Slot=luaL_checkinteger(L,2)-1; if(!C || !C->ConnectionManager || !C->FrameManager || !C->ConnectionManager->PacketHandler.ActionButtons.IsValidIndex(Slot)) return 0;
    const uint32 Packed=C->ConnectionManager->PacketHandler.ActionButtons[Slot],Id=Packed&0xffffff;
    if((Packed>>24)==64) { lua_pushboolean(L,FWowMacros::SetActionTooltip(L,Handle(L),Id)); return 1; }
    if((Packed>>24)==128) ItemTooltip(L,C,Handle(L),Template(C,Id),Id);
    else { const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; C->FrameManager->TooltipSetSpell(Handle(L),Id,P?P->GetLevel():1); }
    lua_pushboolean(L,Id!=0); return 1;
}
void WowLuaApi::RegisterItemApi(lua_State* L)
{
    RegisterItem(L,"GetContainerItemPurchaseInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); auto* Item=PurchaseItem(S,C,3); const auto* Info=RefundInfo(C,Item); if(!Info) return 0;
        int32 Count=0; for(uint32 Id:Info->Items) Count+=Id!=0;
        lua_pushinteger(S,Info->Money); lua_pushinteger(S,Info->Honor); lua_pushinteger(S,Info->Arena); lua_pushinteger(S,Count); lua_pushinteger(S,C->ConnectionManager->PacketHandler.ItemRefundSeconds(Item->Guid)); return 5;
    });
    RegisterItem(L,"GetContainerItemPurchaseItem",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto* Info=RefundInfo(C,PurchaseItem(S,C,4)); int32 Wanted=luaL_checkinteger(S,3);
        if(Info) for(int32 I=0;I<5;++I) if(Info->Items[I] && --Wanted==0) { lua_pushstring(S,TCHAR_TO_UTF8(*ItemIconForUi(C,Info->Items[I]))); lua_pushinteger(S,Info->Counts[I]); lua_pushstring(S,TCHAR_TO_UTF8(*ItemLinkForUi(C,Info->Items[I]))); return 3; } return 0;
    });
    RegisterItem(L,"ContainerRefundItemPurchase",[](lua_State* S)->int { auto* C=GetContext(S); auto* Item=PurchaseItem(S,C,3); if(RefundInfo(C,Item)) C->ConnectionManager->SendItemRefund(Item->Guid); return 0; });
    RegisterItem(L,"CursorCanGoInSlot",[](lua_State* S)->int {
        auto* C=GetContext(S); const int32 Slot=luaL_checkinteger(S,1)-1; uint32 Id=0;
        if(C && C->ConnectionManager) { if(const auto* Item=C->ConnectionManager->GetCursorItem()) Id=Item->Entry; else Id=C->ConnectionManager->GetCursorMerchantItemId(); }
        const auto* T=Template(C,Id);
        static const uint32 Masks[]={0,1,2,4,8,16,32,64,128,256,512,3072,12288,98304,65536,131072,16384,98304,7864320,262144,16,32768,65536,65536,0,131072,131072,7864320,131072};
        lua_pushboolean(S,T && Slot>=0 && Slot<23 && T->InventoryType<UE_ARRAY_COUNT(Masks) && (Masks[T->InventoryType]&(1u<<Slot)) && CanUseItem(C,*T)); return 1;
    });
    RegisterItem(L,"GetItemCount",[](lua_State* S)->int { auto* C=GetContext(S); const uint32 Id=ItemId(S,1); uint64 Count=0; if(C && C->EntityManager && Id) for(const auto* Item:C->EntityManager->GetInventoryItems(lua_toboolean(S,2)!=0)) if(Item->Entry==Id) Count+=FMath::Max(1,Item->GetStackCount()); lua_pushnumber(S,double(Count)); return 1; });
    RegisterItem(L,"IsDressableItem",[](lua_State* S)->int { const auto* T=Template(GetContext(S),ItemId(S,1)); if(T && T->InventoryType) lua_pushboolean(S,1); else lua_pushnil(S); return 1; });
    RegisterItem(L,"GetItemStats",[](lua_State* S)->int { const auto* T=Template(GetContext(S),ItemId(S,1)); return T?PushStats(S,ItemStats(T),2):0; });
    RegisterItem(L,"GetItemStatDelta",[](lua_State* S)->int { auto* C=GetContext(S); const auto* A=Template(C,ItemId(S,1)); const auto* B=Template(C,ItemId(S,2)); return A && B?PushStats(S,ItemDelta(A,B),3):0; });
    RegisterItem(L,"IsItemAction",[](lua_State* S)->int { auto* C=GetContext(S); const int32 I=luaL_checkinteger(S,1)-1; const auto* Actions=C && C->ConnectionManager?&C->ConnectionManager->PacketHandler.ActionButtons:nullptr; lua_pushboolean(S,Actions && Actions->IsValidIndex(I) && ((*Actions)[I]>>24)==128); return 1; });
    RegisterItem(L,"GetActionCount",[](lua_State* S)->int {
        auto* C=GetContext(S); const int32 I=luaL_checkinteger(S,1)-1; int32 Count=0;
        if(C && C->ConnectionManager && C->EntityManager && C->ConnectionManager->PacketHandler.ActionButtons.IsValidIndex(I))
        { const uint32 Action=C->ConnectionManager->PacketHandler.ActionButtons[I]; if((Action>>24)==128) for(const auto* Item:C->EntityManager->GetInventoryItems()) if(Item->Entry==(Action&0xffffff)) Count+=FMath::Max(Item->GetStackCount(),1); }
        lua_pushinteger(S,Count); return 1;
    });
    RegisterItem(L,"UseInventoryItem",[](lua_State* S)->int { auto* C=GetContext(S); const int32 Slot=luaL_checkinteger(S,1); auto* Item=Equipped(C,Slot); const auto* T=Item?Template(C,Item->Entry):nullptr; if(C && C->ConnectionManager && T && T->UseSpellId) C->ConnectionManager->SendUseItem(255,Slot-1,Item->Guid,T->UseSpellId); return 0; });
    RegisterItem(L,"GetItemInfo",Info); RegisterItem(L,"GetContainerItemInfo",BagInfo); RegisterItem(L,"GetContainerItemLink",BagLink);
    RegisterItem(L,"GetInventoryItemTexture",InventoryTexture); RegisterItem(L,"GetInventoryItemLink",InventoryLink);
    RegisterItem(L,"GetContainerFreeSlots",FreeSlots); RegisterItem(L,"SetBagPortraitTexture",BagPortrait);
    RegisterItem(L,"GetInventoryItemCooldown",[](lua_State* S)->int { auto* C=GetContext(S); auto* Item=Equipped(C,luaL_checkinteger(S,2)); const auto* T=Item?Template(C,Item->Entry):nullptr; return PushSpellCooldown(S,T?T->UseSpellId:0); });
    RegisterItem(L,"GetActionTexture",[](lua_State* S)->int {
        auto* C=GetContext(S); const int32 Slot=luaL_checkinteger(S,1)-1;
        if(!C || !C->ConnectionManager || !C->ConnectionManager->PacketHandler.ActionButtons.IsValidIndex(Slot)) { lua_pushnil(S); return 1; }
        const uint32 Packed=C->ConnectionManager->PacketHandler.ActionButtons[Slot],Id=Packed&0xffffff;
        if(!Id) { lua_pushnil(S); return 1; }
        if((Packed>>24)==128) { lua_pushstring(S,TCHAR_TO_UTF8(*Icon(Template(C,Id)))); return 1; }
        const auto* Spell=FDbcStore::Get().Spells().GetById(Id); const auto* Image=Spell?FDbcStore::Get().SpellIcons().GetById(Spell->SpellIconID):nullptr;
        if(Image) lua_pushstring(S,TCHAR_TO_UTF8(*Image->TexturePath)); else lua_pushnil(S); return 1;
    });
    RegisterItem(L,"GetActionCooldown",[](lua_State* S)->int {
        auto* C=GetContext(S); const int32 Slot=luaL_checkinteger(S,1)-1;
        uint32 Id=0;
        if(C && C->ConnectionManager && C->ConnectionManager->PacketHandler.ActionButtons.IsValidIndex(Slot))
        {
            const uint32 Packed=C->ConnectionManager->PacketHandler.ActionButtons[Slot]; Id=Packed&0xffffff;
            if((Packed>>24)==128) { const auto* T=Template(C,Id); Id=T?T->UseSpellId:0; }
        }
        return PushSpellCooldown(S,Id);
    });
    RegisterItem(L,"GetContainerItemCooldown",[](lua_State* S)->int { auto* C=GetContext(S); auto* Item=BagItem(C,luaL_checkinteger(S,1),luaL_checkinteger(S,2)); const auto* T=Item?Template(C,Item->Entry):nullptr; return PushSpellCooldown(S,T?T->UseSpellId:0); });
    RegisterItem(L,"GetCurrencyListSize",[](lua_State* S)->int { lua_pushinteger(S,CurrencyRows(GetContext(S)).Num()); return 1; });
    RegisterItem(L,"GetCurrencyListInfo",[](lua_State* S)->int { const auto Rows=CurrencyRows(GetContext(S)); const int32 I=luaL_checkinteger(S,1)-1; if(Rows.IsValidIndex(I)) return CurrencyInfo(S,Rows[I],false); lua_pushnil(S); return 1; });
    RegisterItem(L,"GetBackpackCurrencyInfo",[](lua_State* S)->int {
        int32 Wanted=luaL_checkinteger(S,1); for(uint32 Id:CurrencyRows(GetContext(S))) if(GetUiPreference(S,FString::Printf(TEXT("currencyWatch%u"),Id)) && --Wanted==0) return CurrencyInfo(S,Id,true);
        lua_pushnil(S); return 1;
    });
    RegisterItem(L,"SetCurrencyBackpack",[](lua_State* S)->int {
        auto* C=GetContext(S); const auto Rows=CurrencyRows(C); const int32 I=luaL_checkinteger(S,1)-1; if(!Rows.IsValidIndex(I)) return 0;
        const bool Watch=lua_toboolean(S,2) && (!lua_isnumber(S,2) || lua_tonumber(S,2)!=0); int32 Count=0; for(uint32 Id:Rows) Count+=GetUiPreference(S,FString::Printf(TEXT("currencyWatch%u"),Id))!=0;
        if(!Watch || Count<3) SetUiPreference(S,FString::Printf(TEXT("currencyWatch%u"),Rows[I]),Watch);
        if(C && C->EventSystem) C->EventSystem->FireEvent(TEXT("CURRENCY_DISPLAY_UPDATE")); return 0;
    });
    RegisterItem(L,"SetCurrencyUnused",[](lua_State* S)->int { auto* C=GetContext(S); const auto Rows=CurrencyRows(C); const int32 I=luaL_checkinteger(S,1)-1; if(Rows.IsValidIndex(I)) { SetUiPreference(S,FString::Printf(TEXT("currencyUnused%u"),Rows[I]),lua_toboolean(S,2) && (!lua_isnumber(S,2) || lua_tonumber(S,2)!=0)); if(C && C->EventSystem) C->EventSystem->FireEvent(TEXT("CURRENCY_DISPLAY_UPDATE")); } return 0; });
    RegisterItem(L,"PickupContainerItem",Pickup); RegisterItem(L,"PickupInventoryItem",PickupEquipment); RegisterItem(L,"UseContainerItem",Use); RegisterItem(L,"SplitContainerItem",Split);
    RegisterItem(L,"IsInventoryItemLocked",[](lua_State* S)->int { auto* C=GetContext(S); const auto* Item=Equipped(C,luaL_checkinteger(S,1)); const auto* Held=C && C->ConnectionManager?C->ConnectionManager->GetCursorItem():nullptr; lua_pushboolean(S,Item && Held && Item->Guid==Held->Guid); return 1; });
    RegisterItem(L,"PutItemInBackpack",[](lua_State* S)->int { auto* C=GetContext(S); lua_pushboolean(S,C && C->ConnectionManager && C->ConnectionManager->PlaceCursorIntoFirstFreeContainerSlot(255)); return 1; });
    RegisterItem(L,"PutItemInBag",[](lua_State* S)->int { auto* C=GetContext(S); const int32 Slot=luaL_checkinteger(S,1); lua_pushboolean(S,C && C->ConnectionManager && Slot>=20 && Slot<=23 && C->ConnectionManager->PlaceCursorIntoFirstFreeContainerSlot(Slot-1)); return 1; });
    RegisterItem(L,"GetContainerItemID",[](lua_State* S)->int { auto* Item=BagItem(GetContext(S),luaL_checkinteger(S,1),luaL_checkinteger(S,2)); if(Item) lua_pushinteger(S,Item->Entry); else lua_pushnil(S); return 1; });
    RegisterItem(L,"GetInventoryItemID",[](lua_State* S)->int { auto* Item=Equipped(GetContext(S),luaL_checkinteger(S,2)); if(Item) lua_pushinteger(S,Item->Entry); else lua_pushnil(S); return 1; });
    RegisterItem(L,"GetBagName",[](lua_State* S)->int { auto* C=GetContext(S); const int32 Bag=luaL_checkinteger(S,1); if(Bag==0) lua_pushstring(S,"Backpack"); else { auto* Item=Equipped(C,Bag+19); const auto* T=Item?Template(C,Item->Entry):nullptr; if(T) lua_pushstring(S,TCHAR_TO_UTF8(*T->Name)); else lua_pushnil(S); } return 1; });
    RegisterItem(L,"GetContainerNumSlots",[](lua_State* S)->int { lua_pushinteger(S,Slots(GetContext(S),luaL_checkinteger(S,1))); return 1; });
    RegisterItem(L,"GetContainerItemQuestInfo",[](lua_State* S)->int {
        auto* C=GetContext(S); auto* Item=BagItem(C,luaL_checkinteger(S,1),luaL_checkinteger(S,2)); const auto* T=Item?Template(C,Item->Entry):nullptr;
        lua_pushboolean(S,T && T->Class==12); lua_pushnil(S); lua_pushboolean(S,0); return 3;
    });
    RegisterItem(L,"GetInventoryItemDurability",[](lua_State* S)->int {
        auto* Item=Equipped(GetContext(S),luaL_checkinteger(S,1));
        if(!Item || !Item->GetField(ItemField::MAX_DURABILITY)) { lua_pushnil(S); return 1; }
        lua_pushinteger(S,Item->GetField(ItemField::DURABILITY)); lua_pushinteger(S,Item->GetField(ItemField::MAX_DURABILITY)); return 2;
    });
    RegisterItem(L,"GetInventoryItemCount",[](lua_State* S)->int { auto* Item=Equipped(GetContext(S),luaL_checkinteger(S,2)); lua_pushinteger(S,Item?FMath::Max(Item->GetStackCount(),1):0); return 1; });
    RegisterItem(L,"GetInventoryItemQuality",[](lua_State* S)->int { auto* C=GetContext(S); auto* Item=Equipped(C,luaL_checkinteger(S,2)); const auto* T=Item?Template(C,Item->Entry):nullptr; if(T) lua_pushinteger(S,T->Quality); else lua_pushnil(S); return 1; });
    RegisterItem(L,"GetInventoryItemBroken",[](lua_State* S)->int { auto* Item=Equipped(GetContext(S),luaL_checkinteger(S,2)); lua_pushboolean(S,Item && Item->GetField(ItemField::MAX_DURABILITY)>0 && Item->GetField(ItemField::DURABILITY)==0); return 1; });
    RegisterItem(L,"GetItemIcon",[](lua_State* S)->int { lua_pushstring(S,TCHAR_TO_UTF8(*Icon(Template(GetContext(S),ItemId(S,1))))); return 1; });
    RegisterItem(L,"GetItemQualityColor",[](lua_State* S)->int { const auto C=Quality(luaL_checkinteger(S,1)); const FColor Byte=C.ToFColor(false); lua_pushnumber(S,C.R); lua_pushnumber(S,C.G); lua_pushnumber(S,C.B); lua_pushstring(S,TCHAR_TO_UTF8(*FString::Printf(TEXT("ff%02x%02x%02x"),Byte.R,Byte.G,Byte.B))); return 4; });
}
#else
void WowLuaApi::RegisterItemApi(lua_State*) {}
int WowLuaApi::SetBagItemTooltip(lua_State*) { return 0; }
int WowLuaApi::SetInventoryItemTooltip(lua_State*) { return 0; }
int WowLuaApi::SetHyperlinkTooltip(lua_State*) { return 0; }
int WowLuaApi::SetHyperlinkCompareItem(lua_State*) { return 0; }
void WowLuaApi::UpdateItemComparisons(lua_State*) {}
int WowLuaApi::TryOnItem(lua_State*) { return 0; }
const FWowItemTemplate* WowLuaApi::QueryItemTemplate(FWowLuaContext*,uint32) { return nullptr; }
FString WowLuaApi::ItemLinkForUi(FWowLuaContext*,uint32) { return {}; }
FString WowLuaApi::ItemIconForUi(FWowLuaContext*,uint32,uint32) { return {}; }
int WowLuaApi::SetMerchantItemTooltip(lua_State*) { return 0; }
int WowLuaApi::SetBuybackItemTooltip(lua_State*) { return 0; }
int WowLuaApi::SetActionTooltip(lua_State*) { return 0; }
#endif
