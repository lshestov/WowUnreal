#include "WowMinimapUi.h"
#include "LuaApi/LuaApiRegistry.h"
#include "WowConnectionManager.h"
#include "WowFrameManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Coord/WowCoordinate.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

namespace
{
using FTracking=WowMinimapUi::FTracking;
const FTracking Common[]={
    {0,1,0x1000,0,TEXT("MINIMAP_TRACKING_REPAIR"),TEXT("Repair"),TEXT("Repair")},
    {0,1,0x200,0,TEXT("MINIMAP_TRACKING_VENDOR_FOOD"),TEXT("Food & Drink"),TEXT("Food")},
    {0,1,0x400,0x10,TEXT("MINIMAP_TRACKING_VENDOR_POISON"),TEXT("Poisons"),TEXT("Poisons")},
    {0,1,0x100,0x1a,TEXT("MINIMAP_TRACKING_VENDOR_AMMO"),TEXT("Ammunition"),TEXT("Ammunition")},
    {0,1,0x800,0,TEXT("MINIMAP_TRACKING_VENDOR_REAGENT"),TEXT("Reagents"),TEXT("Reagents")},
    {0,1,0x10000,0,TEXT("MINIMAP_TRACKING_INNKEEPER"),TEXT("Innkeeper"),TEXT("Innkeeper")},
    {0,1,0x2000,0,TEXT("MINIMAP_TRACKING_FLIGHTMASTER"),TEXT("Flight Master"),TEXT("FlightMaster")},
    {0,1,0x400000,8,TEXT("MINIMAP_TRACKING_STABLEMASTER"),TEXT("Stable Master"),TEXT("StableMaster")},
    {0,1,0x100000,0,TEXT("MINIMAP_TRACKING_BATTLEMASTER"),TEXT("Battlemaster"),TEXT("BattleMaster")},
    {0,1,0x20,0,TEXT("MINIMAP_TRACKING_TRAINER_CLASS"),TEXT("Class Trainer"),TEXT("Class")},
    {0,1,0x40,0,TEXT("MINIMAP_TRACKING_TRAINER_PROFESSION"),TEXT("Profession Trainer"),TEXT("Profession")},
    {0,1,0x200000,0,TEXT("MINIMAP_TRACKING_AUCTIONEER"),TEXT("Auctioneer"),TEXT("Auctioneer")},
    {0,1,0x20000,0,TEXT("MINIMAP_TRACKING_BANKER"),TEXT("Banker"),TEXT("Banker")},
    {0,2,19,0,TEXT("MINIMAP_TRACKING_MAILBOX"),TEXT("Mailbox"),TEXT("Mailbox")},
    {0,3,0,0,TEXT("MINIMAP_TRACKING_TRIVIAL_QUESTS"),TEXT("Low Level Quests"),TEXT("TrivialQuests")}
};
bool IsTracking(const FSpellDbcEntry* S) {if(S) for(uint32 Aura:S->EffectApplyAuraName) if(Aura==44 || Aura==45 || Aura==151) return true; return false;}
int32 QuestIcon(uint8 Status,bool Trivial) {return Status==10?10:Status==8 || (Trivial && Status==2)?9:Status==7 || (Trivial && Status==4)?11:-1;}
}
FWowMinimapUiView& WowMinimapUi::View(FWowLuaContext* C) {if(!C->Minimap) C->Minimap=MakeShared<FWowMinimapUiView>(); return *C->Minimap;}
TArray<WowMinimapUi::FTracking> WowMinimapUi::Tracking(FWowLuaContext* C)
{
    TArray<FTracking> Rows; const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    if(C && C->ConnectionManager) for(uint32 Id:C->ConnectionManager->PacketHandler.KnownSpells) if(const auto* S=FDbcStore::Get().Spells().GetById(Id); IsTracking(S))
    {
        FTracking T; T.Spell=Id; T.Name=S->SpellName; if(const auto* Icon=FDbcStore::Get().SpellIcons().GetById(S->SpellIconID)) T.Icon=Icon->TexturePath; Rows.Add(MoveTemp(T));
    }
    Rows.Sort([](const auto& A,const auto& B){const int32 Order=A.Name.Compare(B.Name,ESearchCase::IgnoreCase); return Order==0?A.Spell<B.Spell:Order<0;});
    const uint32 Mask=P && P->GetClassId()<32?1u<<P->GetClassId():0;
    for(const auto& T:Common) if(!T.ClassMask || (T.ClassMask&Mask)) Rows.Add(T);
    return Rows;
}
uint32 WowMinimapUi::TrackingSpell(FWowLuaContext* C)
{
    const auto* P=C && C->EntityManager?C->EntityManager->GetLocalPlayer():nullptr;
    if(P && C->ConnectionManager) for(const auto& A:P->Auras) if(A.bActive && C->ConnectionManager->PacketHandler.KnownSpells.Contains(A.SpellId) && IsTracking(FDbcStore::Get().Spells().GetById(A.SpellId))) return A.SpellId;
    return 0;
}
int32 WowMinimapUi::CommonTracking(FWowLuaContext* C)
{
    if(!C) return -1; const FString Key=View(C).CommonKey; for(int32 I=0;I<UE_ARRAY_COUNT(Common);++I) if(Common[I].Key==Key) return I; return -1;
}
FVector WowMinimapUi::Position(FWowLuaContext* C,uint64 Guid)
{
    if(C && C->ResolveActor) if(auto* A=C->ResolveActor(Guid)) {FVector P=A->GetActorLocation(); if(const auto* Pawn=Cast<ACharacter>(A)) P.Z-=Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(); return FWowCoordinate::UEToWow(P);}
    const auto* E=C && C->EntityManager?C->EntityManager->Find(Guid):nullptr; return E?E->Movement.Position:FVector::ZeroVector;
}
FWowWmoLocation WowMinimapUi::Location(FWowLuaContext* C)
{
    if(C && C->ResolveActor && C->EntityManager && C->ConnectionManager) if(auto* A=C->ResolveActor(C->EntityManager->LocalPlayerGuid))
        return UWowWmoAreaComponent::Locate(A->GetWorld(),A->GetActorLocation(),C->ConnectionManager->PacketHandler.CurrentMapId);
    return FWowWmoLocation();
}
float WowMinimapUi::Radius(FWowLuaContext* C) {return Radius(C,C && View(C).Location.Minimap);}
float WowMinimapUi::Radius(FWowLuaContext* C,bool Indoors)
{
    const int32 Zoom=C && C->FrameManager?FMath::Clamp(C->FrameManager->MinimapZoom,0,5):0;
    static const float Outdoor[]={233.33333f,200.f,166.66667f,133.33333f,100.f,66.66667f},Indoor[]={150,120,90,60,40,25}; return Indoors?Indoor[Zoom]:Outdoor[Zoom];
}
FVector2D WowMinimapUi::Offset(FWowLuaContext* C,const FVector& Center,const FVector& P,float R,float Facing)
{
    FVector2D V((Center.Y-P.Y)/(R*2),(Center.X-P.X)/(R*2));
    if(C && View(C).Rotates) {const double S=FMath::Sin(Facing),Cos=FMath::Cos(Facing); V={V.X*Cos-V.Y*S,V.X*S+V.Y*Cos};} return V;
}
TArray<WowMinimapUi::FBlip> WowMinimapUi::Blips(FWowLuaContext* C)
{
    TArray<FBlip> Out; if(!C || !C->EntityManager || !C->ConnectionManager) return Out;
    const auto* Player=C->EntityManager->GetLocalPlayer(); if(!Player) return Out; auto& H=C->ConnectionManager->PacketHandler;
    const auto Center=Position(C,Player->Guid); const double R=Radius(C)*.8; const int32 CommonId=CommonTracking(C); const auto* Track=CommonId>=0?&Common[CommonId]:nullptr;
    const uint32 Creatures=Track?0:Player->GetField(PlayerField::BASE+0x036A),Resources=Track?0:Player->GetField(PlayerField::BASE+0x036B);
    for(const auto& Pair:C->EntityManager->GetAll())
    {
        const auto& E=*Pair.Value; if(E.Guid==Player->Guid || (!E.IsUnit() && E.GetEntityKind()!=EWowEntityKind::GameObject)) continue;
        const auto Pos=Position(C,E.Guid); if(FVector::DistSquared2D(Pos,Center)>R*R) continue;
        if(const auto& Where=View(C).Location; Where.Minimap && Where.Component.IsValid() && !H.GroupInfo.FindMember(E.Guid))
        {
            const auto* Meta=Where.Component.Get(); const auto Local=Meta->ToLocal(FWowCoordinate::WowToUE(Pos)); bool SameInterior=false;
            for(const auto& G:Meta->Groups) if(!(G.Flags&8) && G.Bounds.IsValid && G.Bounds.ExpandBy(.25).IsInsideOrOn(Local)) {SameInterior=true; break;}
            if(!SameInterior) continue;
        }
        int32 Icon=-1; FString Name;
        if(E.GetEntityKind()==EWowEntityKind::GameObject)
        {
            const auto& G=static_cast<const FWowGameObjectEntity&>(E); const uint32 Type=G.GetGameObjectType();
            if(G.GetGameObjectFlags()&0x10) continue;
            const auto* T=H.GameObjectTemplates.Find(E.Entry);
            if((Track && Track->Type==2 && Track->Flags==Type) || (Resources && (Type==3 || Type==25)))
            {
                if(!T) C->ConnectionManager->RequestGameObjectTemplate(E.Entry,E.Guid);
                if(Track || (T && (FDbcStore::Get().MapUi().ResourceLockMask(T->Data[0])&Resources))) {Icon=8; Name=T?T->Name:TEXT("Mailbox");}
            }
            if(Icon<0) Icon=QuestIcon(H.QuestGiverStatuses.FindRef(E.Guid),Track && Track->Type==3);
            if(T) Name=T->Name;
        }
        else
        {
            if(E.GetField(UnitField::HEALTH)==0) continue;
            const uint32 Faction=E.GetField(UnitField::FACTIONTEMPLATE); const int32 Reaction=FDbcStore::Get().ItemUi().DefaultReaction(Faction,Player->GetFactionTemplate());
            if(H.GroupInfo.FindMember(E.Guid)) Icon=H.GroupInfo.IsRaidGroup()?0:1;
            else if(E.GetField64(UnitField::SUMMONEDBY)==Player->Guid || E.GetField64(UnitField::CHARMEDBY)==Player->Guid) Icon=15;
            else if(Track && Track->Type==1 && (Track->Flags&E.GetField(UnitField::NPC_FLAGS)) && Reaction>=4) Icon=8;
            if(Icon<0) Icon=QuestIcon(H.QuestGiverStatuses.FindRef(E.Guid),Track && Track->Type==3);
            if(Icon<0 && (E.GetField(UnitField::NPC_FLAGS)&0x2000) && Reaction>=4) Icon=13;
            const uint32 Type=E.IsPlayer()?7:H.CreatureTypes.FindRef(E.Entry);
            const uint8 UnitFlags=E.GetFieldByte(UnitField::BYTES_1,2);
            if(Icon<0 && !Player->IsDead() && !(Player->GetPlayerFlags()&0x10) && !(UnitFlags&4) && ((E.GetField(UnitField::DYNAMIC_FLAGS)&2) || (Type>0 && Type<=32 && (Creatures&(1u<<(Type-1))))))
            {
                if(E.IsPlayer()) Icon=Reaction<4?(E.GetFieldByte(UnitField::BYTES_2,1)&1?2:3):4;
                else Icon=Reaction>=5?7:Reaction==4?6:5;
            }
            Name=E.IsPlayer()?H.PlayerNameCache.FindRef(E.Guid):H.CreatureNameCache.FindRef(E.Entry);
        }
        if(Icon>=0) {FBlip B; B.Guid=E.Guid; B.Icon=uint8(Icon); B.Position=Pos; B.Name=Name.IsEmpty()?TEXT("Unknown"):Name; Out.Add(MoveTemp(B));}
        if(Out.Num()==512) break;
    }
    Out.Sort([](const auto& A,const auto& B){return A.Icon==B.Icon?A.Guid<B.Guid:A.Icon<B.Icon;}); return Out;
}
