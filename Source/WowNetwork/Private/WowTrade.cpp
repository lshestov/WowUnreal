#include "WowConnectionManager.h"
#include "Net/WowWorldSocket.h"
#include "Formats/Dbc/DbcStore.h"

bool UWowConnectionManager::BeginItemSpell(uint32 Spell,uint64 Source,uint8 Bag,uint8 Slot)
{
    const auto* Row=FDbcStore::Get().Spells().GetById(Spell);
    if(!Row || !(Row->Targets&0x4010) || (!Source && !PacketHandler.KnownSpells.Contains(Spell))) return false;
    CancelGlyphUse(); ClearCursorPayload(); PendingItemSpell={Spell,Source,Bag,Slot}; OnGlyphTargetingChanged.Broadcast(); return true;
}
bool UWowConnectionManager::HasPendingItemSpell() const
{
    const auto* Source=PendingItemSpell.Source?ResolveCursorSource(PendingItemSpell.Bag,PendingItemSpell.Slot):nullptr;
    return PendingItemSpell.Spell && (!PendingItemSpell.Source || (Source && Source->Guid==PendingItemSpell.Source && !IsItemInTrade(Source->Guid) && !IsItemInOutgoingMail(Source->Guid)));
}
void UWowConnectionManager::CancelItemSpell()
{
    if(!PendingItemSpell.Spell) return;
    PendingItemSpell={}; OnGlyphTargetingChanged.Broadcast();
}
bool UWowConnectionManager::TargetItemSpell(uint64 ItemGuid,bool Trade)
{
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !HasPendingItemSpell()) return false;
    const auto& T=PacketHandler.CurrentTrade;
    if(Trade ? (!T.bTradeOpen || !T.TargetItems.IsValidIndex(6) || !T.TargetItems[6].ItemId || T.PendingKind) : (!PacketHandler.EntityManager.FindItem(ItemGuid) || IsItemInTrade(ItemGuid) || IsItemInOutgoingMail(ItemGuid))) return false;
    const auto Use=PendingItemSpell; const uint32 Mask=Trade?0x1000:0x10; const uint64 Target=Trade?6:ItemGuid;
    TArray<uint8> Data;
    if(Use.Source) {Data=BuildUseItemPacket(Use.Bag,Use.Slot,Use.Source,Use.Spell); Data.SetNum(20);}
    else {Data.Add(0); Data.Append(reinterpret_cast<const uint8*>(&Use.Spell),4); Data.Add(0);}
    Data.Append(reinterpret_cast<const uint8*>(&Mask),4); uint8 GuidMask=0; TArray<uint8> Bytes;
    for(int32 I=0;I<8;++I) if(const uint8 Byte=Target>>(I*8)) {GuidMask|=1<<I; Bytes.Add(Byte);}
    Data.Add(GuidMask); Data.Append(Bytes);
    if(Trade) SendUnacceptTrade(); WorldSocket->SendPacket(Use.Source?WowOpcode::CMSG_USE_ITEM:WowOpcode::CMSG_CAST_SPELL,Data);
    CancelItemSpell(); return true;
}

bool UWowConnectionManager::IsItemInTrade(uint64 Guid) const
{
    if(!Guid) return false; const auto& T=PacketHandler.CurrentTrade;
    if(!T.bTradeOpen) return false;
    if(T.PendingKind==5 && T.PendingGuid==Guid) return true;
    if(T.Split.Index>=0 && T.Split.SourceGuid==Guid) return true;
    for(uint64 Item:T.LocalGuids) if(Item==Guid) return true;
    return false;
}
const FWowItemEntity* UWowConnectionManager::GetLocalTradeItem(int32 Index) const
{
    const auto& T=PacketHandler.CurrentTrade;
    return T.bTradeOpen && Index>=0 && Index<7?PacketHandler.EntityManager.FindItem(T.LocalGuids[Index]):nullptr;
}
bool UWowConnectionManager::ClickTradeItem(int32 Index,bool Remove)
{
    auto& T=PacketHandler.CurrentTrade;
    if(!WorldSocket || State!=EWowSessionState::WorldInGame || !T.bTradeOpen || T.PendingKind || Index<0 || Index>=7) return false;
    const uint64 Previous=T.LocalGuids[Index]; const auto* Item=GetCursorItem();
    if(Remove || !Item)
    {
        if(!Previous) return false;
        SendUnacceptTrade(); T.bTargetAccepted=false;
        T.LocalGuids[Index]=0; if(T.PlayerItems.IsValidIndex(Index)) T.PlayerItems[Index]=FWowTradeItem(); ++T.StateIndex[0];
        WorldSocket->SendPacket(0x011E,{uint8(Index)}); PacketHandler.OnTradeUpdated.Broadcast(T);
        if(!Remove) { uint8 Bag=0,Slot=0; if(FindCarriedItemPosition(Previous,Bag,Slot)) if(const auto* Old=PacketHandler.EntityManager.FindItem(Previous)) PickupBagItemCursor(Bag,Slot,Old->Entry); }
        return true;
    }
    if(IsItemInTrade(Item->Guid) || IsItemInOutgoingMail(Item->Guid)) return false;
    RequestItemTemplate(Item->Entry); const auto* Template=PacketHandler.ItemTemplates.Find(Item->Entry);
    if(!Template) {T.PendingKind=5; T.PendingSlot=Index; T.PendingGuid=Item->Guid; T.PendingDeadline=FPlatformTime::Seconds()+10; return true;}
    const auto Reject=[&](const TCHAR* Key){PacketHandler.OnTradeClientError.Broadcast(FString(Key)); return false;};
    if(Index<6)
    {
        if(Template->Bonding==4) return Reject(TEXT("ERR_TRADE_QUEST_ITEM"));
        if((Item->GetField(ItemField::FLAGS)&1) && !(Item->GetField(ItemField::FLAGS)&0x100)) return Reject(TEXT("ERR_TRADE_BOUND_ITEM"));
        if(const auto* Bag=PacketHandler.EntityManager.FindContainer(Item->Guid)) for(int32 I=0;I<Bag->GetNumSlots();++I) if(Bag->GetItemGuidAtSlot(I)) return Reject(TEXT("ERR_TRADE_BAG"));
        if(CursorPayload.bFromInventorySlot && CursorPayload.SourceInventorySlot>=19 && CursorPayload.SourceInventorySlot<23) return Reject(TEXT("ERR_TRADE_EQUIPPED_BAG"));
    }
    if(CursorPayload.SplitCount)
    {
        const auto* P=PacketHandler.EntityManager.GetLocalPlayer(); bool Found=false; uint8 DestinationBag=255,DestinationSlot=0;
        for(int32 I=0;P && I<16;++I) if(!P->GetBackpackItemGuid(uint8(I))) {DestinationSlot=uint8(I+23); Found=true; break;}
        for(uint8 I=1;P && !Found && I<=4;++I) if(const auto* B=PacketHandler.EntityManager.FindContainer(P->GetBagGuid(I)))
            for(int32 N=0;N<B->GetNumSlots();++N) if(!B->GetItemGuidAtSlot(N)) {DestinationBag=I+18; DestinationSlot=uint8(N); Found=true; break;}
        if(!Found) return Reject(TEXT("ERR_INV_FULL"));
        SendUnacceptTrade(); T.bTargetAccepted=false;
        T.Split.Index=Index; T.Split.SourceGuid=Item->Guid; T.Split.Entry=Item->Entry; T.Split.SourceCount=uint32(Item->GetStackCount()); T.Split.Count=CursorPayload.SplitCount; T.Split.Bag=DestinationBag; T.Split.Slot=DestinationSlot;
        T.PendingKind=4; T.PendingDeadline=FPlatformTime::Seconds()+10;
        SendSplitItem(CursorPayload.SourceBag,CursorPayload.SourceSlot,DestinationBag,DestinationSlot,CursorPayload.SplitCount);
        ClearCursorPayload(); PacketHandler.OnTradeUpdated.Broadcast(T); return true;
    }
    const uint8 Bag=CursorPayload.bFromInventorySlot?255:CursorPayload.SourceBag;
    const uint8 Slot=CursorPayload.bFromInventorySlot?CursorPayload.SourceInventorySlot:CursorPayload.SourceSlot;
    T.ReturnCursorGuid=Previous;
    if(Index==6) T.BindWarningShown=false;
    SendUnacceptTrade(); T.bTargetAccepted=false;
    // The 12340 client owns its offer view. AzerothCore only sends the extended
    // item/money update to the other party; inventory and balances stay on server.
    T.PlayerItems.SetNum(7); auto& Row=T.PlayerItems[Index]; Row=FWowTradeItem(); Row.Slot=uint8(Index);
    Row.ItemId=Item->Entry; Row.DisplayId=Template->DisplayId; Row.Count=Item->GetStackCount(); Row.bWrapped=(Item->GetField(ItemField::FLAGS)&8)!=0;
    Row.GiftCreatorGuid=Item->GetField64(ItemField::GIFT_CREATOR); Row.CreatorGuid=Item->GetField64(ItemField::CREATOR);
    Row.PermanentEnchantId=Item->GetField(ItemField::ENCHANTMENT_1_1); Row.GemEnchantId1=Item->GetField(ItemField::ENCHANTMENT_3_1); Row.GemEnchantId2=Item->GetField(ItemField::ENCHANTMENT_4_1); Row.GemEnchantId3=Item->GetField(ItemField::ENCHANTMENT_5_1);
    Row.Charges=Item->GetField(ItemField::SPELL_CHARGES); Row.SuffixFactor=Item->GetField(ItemField::PROPERTY_SEED); Row.RandomPropertyId=int32(Item->GetField(ItemField::RANDOM_PROPERTIES_ID));
    Row.MaxDurability=Item->GetField(ItemField::MAX_DURABILITY); Row.Durability=Item->GetField(ItemField::DURABILITY);
    T.LocalGuids[Index]=Item->Guid; ++T.StateIndex[0];
    WorldSocket->SendPacket(0x011D,{uint8(Index),Bag,Slot}); ClearCursorPayload(); PacketHandler.OnTradeUpdated.Broadcast(T); return true;
}
void UWowConnectionManager::ResetTradeState()
{
    CancelItemSpell();
    PacketHandler.CurrentTrade=FWowTradeState(); OnCursorChanged.Broadcast();
}
void UWowConnectionManager::TickTradeState()
{
    auto& T=PacketHandler.CurrentTrade; if(!T.bTradeOpen) return;
    const auto* Player=PacketHandler.EntityManager.GetLocalPlayer();
    if(State!=EWowSessionState::WorldInGame || !Player || !Player->GetHealth() || (T.PendingKind && FPlatformTime::Seconds()>=T.PendingDeadline)) { SendCancelTrade(); return; }
    if(T.PendingKind==5)
    {
        const auto* Item=GetCursorItem();
        if(!Item || Item->Guid!=T.PendingGuid) {T.PendingKind=0; T.PendingSlot=-1; T.PendingGuid=0;}
        else if(PacketHandler.ItemTemplates.Contains(Item->Entry)) {const int32 Index=T.PendingSlot; T.PendingKind=0; T.PendingSlot=-1; T.PendingGuid=0; ClickTradeItem(Index);}
    }
    if(T.UseCursorOnOpen && !T.PendingKind) {T.UseCursorOnOpen=false; if(HasCursorItemPayload()) ClickTradeItem(0);}
    if(T.Split.Index>=0)
    {
        const auto Pending=T.Split; const auto* Source=PacketHandler.EntityManager.FindItem(Pending.SourceGuid); const auto* Item=ResolveCursorSource(Pending.Bag,Pending.Slot);
        if(Source && Pending.SourceCount>Pending.Count && uint32(Source->GetStackCount())==Pending.SourceCount-Pending.Count && Item && Item->Guid!=Pending.SourceGuid && Item->Entry==Pending.Entry && uint32(Item->GetStackCount())==Pending.Count)
        {
            T.Split=FWowTradeState::FSplit(); T.PendingKind=0;
            PickupBagItemCursor(Pending.Bag,Pending.Slot,Item->Entry); ClickTradeItem(Pending.Index);
        }
    }
    if(!T.PendingKind && T.ReturnCursorGuid)
    {
        const uint64 Guid=T.ReturnCursorGuid; T.ReturnCursorGuid=0; uint8 Bag=0,Slot=0;
        if(!HasCursorPayload() && FindCarriedItemPosition(Guid,Bag,Slot)) if(const auto* Item=PacketHandler.EntityManager.FindItem(Guid)) PickupBagItemCursor(Bag,Slot,Item->Entry);
    }
    if(!T.bLocalAccepted) for(uint64 Guid:T.LocalGuids) if(Guid && !PacketHandler.EntityManager.FindItem(Guid)) {SendCancelTrade(); return;}
}
uint32 UWowConnectionManager::GetCursorMoney() const
{
    return CursorPayload.Type==ECursorPayloadType::Money?uint32(CursorPayload.PrimaryId):0;
}
void UWowConnectionManager::PickupCursorMoney(int32 Amount,bool FromTrade)
{
    const auto* Player=PacketHandler.EntityManager.GetLocalPlayer(); auto& T=PacketHandler.CurrentTrade;
    if(Amount<=0 || !Player || uint32(Amount)>Player->GetCoinage() || (FromTrade && (!T.bTradeOpen || T.PendingKind || uint32(Amount)>T.PlayerMoney))) return;
    if(FromTrade) SendSetTradeGold(int32(T.PlayerMoney)-Amount);
    ClearCursorPayload(); CursorPayload.Type=ECursorPayloadType::Money; CursorPayload.PrimaryId=Amount; OnCursorChanged.Broadcast();
}
void UWowConnectionManager::AddCursorTradeMoney()
{
    const uint32 Amount=GetCursorMoney(); const auto& T=PacketHandler.CurrentTrade; const auto* P=PacketHandler.EntityManager.GetLocalPlayer();
    if(!Amount || !T.bTradeOpen || T.PendingKind || !P || uint64(T.PlayerMoney)+Amount>P->GetCoinage()) return;
    SendSetTradeGold(int32(T.PlayerMoney+Amount)); ClearCursorPayload();
}
