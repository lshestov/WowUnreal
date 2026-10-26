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
    return Start>=0 ? FCString::Strtoui64(*Text.Mid(Start+5),nullptr,10) : FCString::Strtoui64(*Text,nullptr,10);
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
    if(Slot>=1 && Slot<=19) return P->GetEquipmentItemGuid(Slot-1);
    if(Slot>=20 && Slot<=23) return P->GetBagGuid(Slot-19);
    return 0;
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
    lua_pushstring(L,TCHAR_TO_UTF8((T->Class<UE_ARRAY_COUNT(Classes)?Classes[T->Class]:TEXT("Miscellaneous"))));
    lua_pushstring(L,""); lua_pushinteger(L,T->MaxStack); lua_pushstring(L,TCHAR_TO_UTF8(EquipType(T->InventoryType)));
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
    if(Connection->HasCursorItemPayload()) Connection->PlaceCursorIntoContainerSlot(B,S);
    else if(auto* Item=BagItem(C,Bag,Slot)) Connection->PickupBagItemCursor(B,S,Item->Entry,Link(Template(C,Item->Entry),Item->Entry));
    return 0;
}
int PickupEquipment(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); const int32 Slot=luaL_checkinteger(L,1);
    if(!C || !C->ConnectionManager || Slot<1 || Slot>23) return 0;
    auto* Connection=C->ConnectionManager;
    if(Connection->HasCursorItemPayload()) Connection->PlaceCursorIntoContainerSlot(255,Slot-1);
    else if(auto* Item=Equipped(C,Slot)) Connection->PickupInventoryItemCursor(Slot-1,Item->Entry,Link(Template(C,Item->Entry),Item->Entry));
    return 0;
}
int Use(lua_State* L)
{
    auto* C=WowLuaApi::GetContext(L); if(!C || !C->ConnectionManager) return 0;
    const int32 Bag=luaL_checkinteger(L,1),Slot=luaL_checkinteger(L,2); uint8 B,S;
    if(!Position(C,Bag,Slot,B,S)) return 0;
    auto* Item=BagItem(C,Bag,Slot); const auto* T=Item?Template(C,Item->Entry):nullptr;
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
void ItemTooltip(FWowLuaContext* C,int64 H,const FWowItemTemplate* T,uint32 Id,FWowItemEntity* Item=nullptr)
{
    auto* F=C->FrameManager; F->TooltipClearLines(H);
    auto* State=F->GetTooltipState(H); State->ItemLink=Link(T,Id); State->SpellId=0; State->UnitToken.Empty();
    const FLinearColor Color=Quality(T?T->Quality:1);
    F->TooltipAddLine(H,T?T->Name:TEXT("Retrieving item information..."),Color.R,Color.G,Color.B,false);
    if(T)
    {
        if(T->DamageMax>0) F->TooltipAddDoubleLine(H,FString::Printf(TEXT("%.0f - %.0f Damage"),T->DamageMin,T->DamageMax),FString::Printf(TEXT("Speed %.2f"),T->Delay/1000.f),1,1,1,1,1,1);
        if(T->Armor) F->TooltipAddLine(H,FString::Printf(TEXT("%u Armor"),T->Armor),1,1,1,false);
        static const TCHAR* StatNames[]={TEXT("Mana"),TEXT("Health"),TEXT(""),TEXT("Agility"),TEXT("Strength"),TEXT("Intellect"),TEXT("Spirit"),TEXT("Stamina")};
        for(const auto& Stat:T->Stats) if(Stat.Key<UE_ARRAY_COUNT(StatNames) && *StatNames[Stat.Key]) F->TooltipAddLine(H,FString::Printf(TEXT("%+d %s"),Stat.Value,StatNames[Stat.Key]),1,1,1,false);
        if(T->RequiredLevel>1) F->TooltipAddLine(H,FString::Printf(TEXT("Requires Level %u"),T->RequiredLevel),1,1,1,false);
        if(!T->Description.IsEmpty()) F->TooltipAddLine(H,T->Description,1,.82f,0,true);
        if(T->UseSpellId) { const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; const auto Text=FDbcStore::Get().Spells().Description(T->UseSpellId,P?P->GetLevel():1); if(!Text.IsEmpty()) F->TooltipAddLine(H,TEXT("Use: ")+Text,0,1,0,true); }
        if(Item && Item->GetField(ItemField::MAX_DURABILITY)) F->TooltipAddLine(H,FString::Printf(TEXT("Durability %u / %u"),Item->GetField(ItemField::DURABILITY),Item->GetField(ItemField::MAX_DURABILITY)),1,1,1,false);
    }
    F->SetFrameVisible(H,true);
}
}
int WowLuaApi::SetBagItemTooltip(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->FrameManager) return 0;
    auto* Item=BagItem(C,luaL_checkinteger(L,2),luaL_checkinteger(L,3)); const int64 H=Handle(L);
    if(Item) ItemTooltip(C,H,Template(C,Item->Entry),Item->Entry,Item); else C->FrameManager->SetFrameVisible(H,false);
    lua_pushboolean(L,0); lua_pushinteger(L,0); return 2;
}
int WowLuaApi::SetInventoryItemTooltip(lua_State* L)
{
    auto* C=GetContext(L); if(!C || !C->FrameManager) return 0;
    auto* Item=Equipped(C,luaL_checkinteger(L,3)); const int64 H=Handle(L);
    if(Item) ItemTooltip(C,H,Template(C,Item->Entry),Item->Entry,Item); else C->FrameManager->SetFrameVisible(H,false);
    lua_pushboolean(L,Item!=nullptr); lua_pushboolean(L,0); lua_pushinteger(L,0); return 3;
}
int WowLuaApi::SetHyperlinkTooltip(lua_State* L)
{
    auto* C=GetContext(L); const uint32 Id=ItemId(L,2);
    if(C && C->FrameManager && Id) ItemTooltip(C,Handle(L),Template(C,Id),Id);
    return 0;
}
int WowLuaApi::SetActionTooltip(lua_State* L)
{
    auto* C=GetContext(L); const int32 Slot=luaL_checkinteger(L,2)-1; if(!C || !C->ConnectionManager || !C->FrameManager || !C->ConnectionManager->PacketHandler.ActionButtons.IsValidIndex(Slot)) return 0;
    const uint32 Packed=C->ConnectionManager->PacketHandler.ActionButtons[Slot],Id=Packed&0xffffff;
    if((Packed>>24)==64) { lua_pushboolean(L,FWowMacros::SetActionTooltip(L,Handle(L),Id)); return 1; }
    if((Packed>>24)==128) ItemTooltip(C,Handle(L),Template(C,Id),Id);
    else { const auto* P=C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr; C->FrameManager->TooltipSetSpell(Handle(L),Id,P?P->GetLevel():1); }
    lua_pushboolean(L,Id!=0); return 1;
}
void WowLuaApi::RegisterItemApi(lua_State* L)
{
    RegisterItem(L,"IsItemAction",[](lua_State* S)->int { auto* C=GetContext(S); const int32 I=luaL_checkinteger(S,1)-1; const auto* Actions=C && C->ConnectionManager?&C->ConnectionManager->PacketHandler.ActionButtons:nullptr; lua_pushboolean(S,Actions && Actions->IsValidIndex(I) && ((*Actions)[I]>>24)==128); return 1; });
    RegisterItem(L,"GetActionCount",[](lua_State* S)->int {
        auto* C=GetContext(S); const int32 I=luaL_checkinteger(S,1)-1; int32 Count=0;
        if(C && C->ConnectionManager && C->EntityManager && C->ConnectionManager->PacketHandler.ActionButtons.IsValidIndex(I))
        { const uint32 Action=C->ConnectionManager->PacketHandler.ActionButtons[I]; if((Action>>24)==128) for(const auto& Pair:C->EntityManager->GetAll()) if(Pair.Value->IsItem() && Pair.Value->Entry==(Action&0xffffff)) { const auto* Item=static_cast<const FWowItemEntity*>(Pair.Value.Get()); if(Item->GetOwnerGuid()==C->EntityManager->LocalPlayerGuid) Count+=FMath::Max(Item->GetStackCount(),1); } }
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
int WowLuaApi::SetActionTooltip(lua_State*) { return 0; }
#endif
