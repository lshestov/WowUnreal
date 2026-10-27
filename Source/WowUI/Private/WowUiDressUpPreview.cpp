#include "WowUiDressUpPreview.h"
#include "LuaApi/LuaApiRegistry.h"
#include "WowCharacterBuilder.h"
#include "WowConnectionManager.h"
#include "WowEntityManager.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"

FWowUiDressUpPreview::~FWowUiDressUpPreview() { ReleaseActor(); }
void FWowUiDressUpPreview::ReleaseActor() { if(Actor.IsValid()) Actor->Destroy(); Actor.Reset(); Dirty=true; }
void FWowUiDressUpPreview::SetUnit(uint64 UnitGuid) { Guid=UnitGuid; Undressed=false; Overrides.Empty(); Dirty=true; if(!Guid) ReleaseActor(); }
int32 FWowUiDressUpPreview::SlotForType(uint32 Type)
{
    // Zero-based equipment slots. Nonvisual accessories are accepted by IsDressableItem
    // but do not change a character mesh.
    static const int32 Slots[]={-1,0,-1,2,3,4,5,6,7,8,9,-1,-1,15,16,17,14,15,-1,18,4,15,16,16,-1,17,17,-1,-1};
    return Type<UE_ARRAY_COUNT(Slots)?Slots[Type]:-1;
}
void FWowUiDressUpPreview::TryOn(uint32 Id,uint32 Type)
{
    const int32 Slot=SlotForType(Type); if(Slot<0) return;
    Overrides.Add(Slot,Id); if(Type==17) Overrides.Add(16,0); Dirty=true;
}
void FWowUiDressUpPreview::Dress(bool Dressed) { Undressed=!Dressed; Overrides.Empty(); Dirty=true; }
bool FWowUiDressUpPreview::Rebuild(UWorld* World,FWowLuaContext& Context,FMpqManager* Mpq,FWowAssetCache* Cache)
{
    if(Actor.IsValid() && Actor->GetWorld()!=World) ReleaseActor();
    if(!World || !Mpq || !Cache || !Context.EntityManager || !Context.ConnectionManager) return false;
    const auto* Player=Context.EntityManager->FindPlayer(Guid); if(!Player) return false;
    uint32 CurrentHash=HashCombine(GetTypeHash(Player->GetPlayerBytes()),GetTypeHash(Player->GetPlayerBytes2()));
    CurrentHash=HashCombine(CurrentHash,GetTypeHash(Player->GetRaceId()|(Player->GetGenderId()<<8)));
    for(int32 Slot=0;Slot<19;++Slot) {CurrentHash=HashCombine(CurrentHash,GetTypeHash(Player->GetEquipmentItemGuid(Slot))); CurrentHash=HashCombine(CurrentHash,GetTypeHash(Player->GetField(PlayerField::VISIBLE_ITEM_BASE+Slot*2)));}
    const auto* Guild=Context.ConnectionManager->PacketHandler.Guilds.Find(Player->GetField(PlayerField::GUILD_ID));
    if(Guild) for(uint32 Value:Guild->Emblem) CurrentHash=HashCombineFast(CurrentHash,Value);
    if(CurrentHash!=SourceHash) Dirty=true;
    if(!Dirty) return Actor.IsValid();
    auto* Connection=Context.ConnectionManager;
    TMap<int32,uint32> Items;
    if(!Undressed) for(int32 Slot=0;Slot<19;++Slot)
    {
        uint32 Entry=Player->GetField(PlayerField::VISIBLE_ITEM_BASE+Slot*2);
        if(const auto* Item=Context.EntityManager->FindItem(Player->GetEquipmentItemGuid(Slot))) Entry=Item->Entry;
        if(Entry) Items.Add(Slot,Entry);
    }
    for(const auto& Override:Overrides) Items.Add(Override.Key,Override.Value);
    bool Ready=true;
    for(const auto& Item:Items) if(Item.Value && !Connection->PacketHandler.ItemTemplates.Contains(Item.Value)) { Connection->RequestItemTemplate(Item.Value); Ready=false; }
    if(!Ready) return false;
    // Trying an offhand item replaces a two-hander; trying a two-hander cleared
    // the offhand above. Other slots and previous preview choices are retained.
    if(Overrides.FindRef(16)) if(const auto* Main=Connection->PacketHandler.ItemTemplates.Find(Items.FindRef(15)); Main && Main->InventoryType==17) Items.Remove(15);
    FWowCharacterBuilder::FCharacterParams Params;
    const uint8 Race=Player->GetRaceId(),Gender=Player->GetGenderId();
    Params.Race=static_cast<FWowCharacterBuilder::ERace>(Race); Params.Gender=static_cast<FWowCharacterBuilder::EGender>(Gender);
    auto& Custom=Params.Customization; Custom.RaceId=Race; Custom.Gender=Gender;
    const uint32 Bytes=Player->GetPlayerBytes(); Custom.SkinColor=Bytes&255; Custom.FaceVariation=(Bytes>>8)&255; Custom.HairStyle=(Bytes>>16)&255; Custom.HairColor=(Bytes>>24)&255; Custom.FacialHairStyle=Player->GetPlayerBytes2()&255;
    auto& Body=Params.BodyEquipment; Body.RaceId=Race; Body.Gender=Gender;
    for(const auto& Item:Items) if(const auto* T=Connection->PacketHandler.ItemTemplates.Find(Item.Value))
    {
        const uint32 Display=T->DisplayId; if(!Display) continue;
        using Attach=FWowEquipmentManager::EAttachmentPoint;
        switch(T->InventoryType)
        {
            case 1: Params.Equipment.Add({Display,Attach::Helmet}); break;
            case 3: Params.Equipment.Add({Display,Attach::LeftShoulder}); Params.Equipment.Add({Display,Attach::RightShoulder}); break;
            case 4: Body.ShirtDisplayId=Display; break;
            case 5: case 20: Body.ChestDisplayId=Display; Body.bIsRobeEquipped=T->InventoryType==20; break;
            case 6: Body.BeltDisplayId=Display; break;
            case 7: Body.PantsDisplayId=Display; break;
            case 8: Body.BootsDisplayId=Display; break;
            case 9: Body.BracersDisplayId=Display; break;
            case 10: Body.GlovesDisplayId=Display; break;
            case 16: Body.CapeDisplayId=Display; break;
            case 19: Body.TabardDisplayId=Display; break;
            case 13: case 14: case 15: case 17: case 21: case 22: case 23: case 25: case 26:
                Params.Equipment.Add({Display,Item.Key==16?Attach::LeftHand:Attach::RightHand}); break;
        }
    }
    if(Guild && Items.FindRef(18)==5976) {Body.UseGuildEmblem=true; Body.GuildEmblem={Guild->Emblem[0],Guild->Emblem[1],Guild->Emblem[2],Guild->Emblem[3],Guild->Emblem[4]};}
    if(HasTabardPreview) {Body.TabardDisplayId=TabardDisplay; Body.UseGuildEmblem=true; Body.GuildEmblem=Emblem;}
    FVector Location(0,0,-100000); if(Context.ResolveActor) if(auto* Live=Context.ResolveActor(Guid)) Location=Live->GetActorLocation()-FVector(0,0,10000);
    auto* NewActor=FWowCharacterBuilder::SpawnCharacterWithEquipment(World,Mpq,Cache,Params,Location);
    if(!NewActor) return false;
    NewActor->SetFlags(RF_Transient); NewActor->SetActorEnableCollision(false);
    TInlineComponentArray<UMeshComponent*> Meshes; NewActor->GetComponents(Meshes);
    for(auto* Mesh:Meshes) { Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetVisibleInSceneCaptureOnly(true); Mesh->SetCastShadow(false); }
    if(Actor.IsValid()) Actor->Destroy(); Actor=NewActor; Dirty=false; SourceHash=CurrentHash;
    UE_LOG(LogTemp,Log,TEXT("UiDressUp: rebuilt guid=%llu undressed=%d overrides=%d meshes=%d chest=%u boots=%u"),Guid,Undressed,Overrides.Num(),Meshes.Num(),Body.ChestDisplayId,Body.BootsDisplayId);
    return true;
}
