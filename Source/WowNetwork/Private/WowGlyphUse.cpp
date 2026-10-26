#include "WowConnectionManager.h"
#include "WowUpdateFields.h"
#include "Formats/Dbc/DbcStore.h"

bool UWowConnectionManager::HasPendingGlyph() const
{
    const auto* Player=PacketHandler.EntityManager.GetLocalPlayer();
    const auto* Item=PacketHandler.EntityManager.FindItem(PendingGlyph.ItemGuid);
    if(!PendingGlyph.ItemGuid || !Player || PendingGlyph.Group!=PacketHandler.ActiveTalentGroup || !Item || Item->GetStackCount()<=0) return false;
    uint64 Guid=0;
    if(PendingGlyph.Bag==255 && PendingGlyph.Slot>=23 && PendingGlyph.Slot<39) Guid=Player->GetBackpackItemGuid(PendingGlyph.Slot-23);
    else if(PendingGlyph.Bag>=19 && PendingGlyph.Bag<=22)
        if(const auto* Bag=PacketHandler.EntityManager.FindContainer(Player->GetBagGuid(PendingGlyph.Bag-18)); Bag && PendingGlyph.Slot<Bag->GetNumSlots()) Guid=Bag->GetItemGuidAtSlot(PendingGlyph.Slot);
    return Guid==PendingGlyph.ItemGuid;
}
bool UWowConnectionManager::BeginGlyphUse(uint8 Bag,uint8 Slot,uint64 ItemGuid,uint32 SpellId)
{
    CancelGlyphUse(); const auto* Spell=FDbcStore::Get().Spells().GetById(SpellId); if(!Spell) return false;
    for(int32 I=0;I<3;++I) if(Spell->Effect[I]==74 && Spell->EffectMiscValue[I]>0 && FDbcStore::Get().Glyph(Spell->EffectMiscValue[I]))
    {
        PendingGlyph={ItemGuid,SpellId,uint32(Spell->EffectMiscValue[I]),Bag,Slot,PacketHandler.ActiveTalentGroup};
        if(!HasPendingGlyph()) { PendingGlyph={}; return false; }
        OnGlyphTargetingChanged.Broadcast(); return true;
    }
    return false;
}
bool UWowConnectionManager::GlyphMatchesSocket(uint32 Socket) const
{
    if(Socket>=6 || !HasPendingGlyph()) return false;
    const auto* Player=PacketHandler.EntityManager.GetLocalPlayer();
    const auto* Slot=FDbcStore::Get().GlyphSlot(Player->GetField(PlayerField::GLYPH_SLOTS_1+Socket));
    const auto* Glyph=FDbcStore::Get().Glyph(PendingGlyph.Property);
    return Slot && Glyph && Slot->Type==Glyph->Type;
}
bool UWowConnectionManager::PlaceGlyph(uint32 Socket)
{
    const auto* Player=PacketHandler.EntityManager.GetLocalPlayer();
    if(!WorldSocket.IsValid() || GetState()!=EWowSessionState::WorldInGame || !Player || Socket>=6 || !(Player->GetField(PlayerField::GLYPHS_ENABLED)&(1u<<Socket)) || !GlyphMatchesSocket(Socket)) return false;
    const auto Use=PendingGlyph; SendUseItem(Use.Bag,Use.Slot,Use.ItemGuid,Use.Spell,0,Socket); CancelGlyphUse(); return true;
}
void UWowConnectionManager::CancelGlyphUse()
{
    if(!PendingGlyph.ItemGuid) return;
    PendingGlyph={}; OnGlyphTargetingChanged.Broadcast();
}
void UWowConnectionManager::SendRemoveGlyph(uint32 Socket)
{
    const auto* Player=PacketHandler.EntityManager.GetLocalPlayer(); const auto& Groups=PacketHandler.TalentGroups; const int32 Group=PacketHandler.ActiveTalentGroup;
    if(Socket>=6 || !Player || !Groups.IsValidIndex(Group) || !Groups[Group].Glyphs[Socket]) return;
    TArray<uint8> Data; Data.Append(reinterpret_cast<const uint8*>(&Socket),4); SendRawPacket(0x48A,Data);
}
