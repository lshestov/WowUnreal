#include "WowGameplayController.h"
#include "WowPlayerCharacter.h"
#include "WowConnectionManager.h"
#include "WowSpellMissile.h"
#include "WowM2EmitterComponent.h"
#include "WowDoodadManager.h"
#include "WowAnimationController.h"
#include "WowCharacterBuilder.h"
#include "WowSkeletalMeshBuilder.h"
#include "WowEquipmentManager.h"
#include "Formats/Dbc/DbcStore.h"
#include "Formats/M2Types.h"
#include "Coord/WowCoordinate.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

const FSpellVisualDbcEntry* AWowGameplayController::SpellVisual(uint32 Spell) const
{
    const auto& D=FDbcStore::Get(); const auto* S=D.Spells().GetById(Spell);
    return S?D.SpellVisuals().GetById(S->SpellVisual[0]):nullptr;
}
AActor* AWowGameplayController::SpellActor(uint64 Guid) const
{
    if(!ConnectionManager || !Guid) return nullptr;
    return Guid==ConnectionManager->PacketHandler.EntityManager.LocalPlayerGuid?GetPawn():SpawnedEntityActors.FindRef(Guid).Get();
}
namespace
{
UWowAnimationController* SpellAnimation(AActor* Actor)
{
    if(auto* Player=Cast<AWowPlayerCharacter>(Actor)) return Player->AnimationController;
    return IsValid(Actor)?FWowCharacterBuilder::GetAnimationController(Actor):nullptr;
}
FVector EffectOffset(const FVector& V) {return FVector(-V.Y,V.X,V.Z)*FWowCoordinate::SCALE;}
}
FVector AWowGameplayController::SpellAttachment(AActor* Actor,uint32 Attachment,const FVector& Offset) const
{
    if(!IsValid(Actor)) return FVector::ZeroVector;
    if(auto* Animation=SpellAnimation(Actor)) if(auto Model=Animation->GetRuntimeModel()) if(auto* Mesh=Animation->GetModelMesh())
    {
        const auto* A=Model->Attachments.FindByPredicate([&](const auto& V){return V.Id==Attachment;});
        if(A && Model->Bones.IsValidIndex(A->Bone))
        {
            const FName Bone=FWowSkeletalMeshBuilder::GetBoneName(A->Bone);
            if(Mesh->GetBoneIndex(Bone)>=0) return Mesh->GetSocketTransform(Bone).TransformPosition(
                FWowEquipmentManager::GetAttachmentOffset(*Model,static_cast<FWowEquipmentManager::EAttachmentPoint>(Attachment))+EffectOffset(Offset));
        }
    }
    return Actor->GetActorTransform().TransformPosition(EffectOffset(Offset))+FVector(0,0,Attachment==19?0:100);
}
UWowM2EmitterComponent* AWowGameplayController::SpawnSpellModel(AActor* Actor,uint32 EffectId,uint32 Attachment,float Duration,bool WorldSpace,const FVector& Position,const FString& ModelPath)
{
    if(!IsValid(Actor) || (!EffectId && ModelPath.IsEmpty())) return nullptr;
    CacheWorldResources(); if(!CachedMpq || !CachedAssetCache) return nullptr;
    const auto* Effect=FDbcStore::Get().SpellVisualEffectNames().GetById(EffectId); if(!Effect && ModelPath.IsEmpty()) return nullptr;
    const FString Path=ModelPath.IsEmpty()?Effect->FilePath:ModelPath;
    const auto Model=FWowDoodadManager::GetOrParseM2(Path,CachedMpq); if(!Model) return nullptr;
    auto* Component=NewObject<UWowM2EmitterComponent>(Actor); Actor->AddInstanceComponent(Component);
    bool Attached=false;
    if(!WorldSpace) if(auto* Animation=SpellAnimation(Actor)) if(auto CharacterModel=Animation->GetRuntimeModel()) if(auto* Mesh=Animation->GetModelMesh())
    {
        const auto* A=CharacterModel->Attachments.FindByPredicate([&](const auto& V){return V.Id==Attachment;});
        if(A && CharacterModel->Bones.IsValidIndex(A->Bone))
        {
            const FName Bone=FWowSkeletalMeshBuilder::GetBoneName(A->Bone);
            if(Mesh->GetBoneIndex(Bone)>=0)
            {
                Component->SetupAttachment(Mesh,Bone);
                Component->SetRelativeLocation(FWowEquipmentManager::GetAttachmentOffset(*CharacterModel,static_cast<FWowEquipmentManager::EAttachmentPoint>(Attachment))); Attached=true;
            }
        }
    }
    if(!Attached)
    {
        if(!WorldSpace) {Component->SetupAttachment(Actor->GetRootComponent()); Component->SetRelativeLocation(Attachment==19?FVector::ZeroVector:FVector(0,0,100));}
        else {Component->SetAbsolute(true,true,true); Component->SetWorldLocation(Position);}
    }
    const float Scale=Effect && FMath::IsFinite(Effect->Scale) && Effect->Scale>0?FMath::Clamp(Effect->Scale,.01f,10.f):1.f;
    Component->SetRelativeScale3D(FVector(Scale)); Component->ComponentTags.Add(*Path); Component->RegisterComponent();
    if(!Component->Initialize(Model,CachedMpq,CachedAssetCache,Duration>0?Duration:UWowM2EmitterComponent::OneShotSeconds(*Model))) {Component->DestroyComponent(); return nullptr;}
    return Component;
}
TArray<TWeakObjectPtr<UWowM2EmitterComponent>> AWowGameplayController::SpawnSpellKit(AActor* Actor,uint32 KitId,float Duration,const TCHAR* Phase,uint32 Spell,uint64 Caster,uint8 Counter,bool WorldSpace,const FVector& Position)
{
    TArray<TWeakObjectPtr<UWowM2EmitterComponent>> Result;
    const auto* Kit=FDbcStore::Get().SpellVisualKits().GetById(KitId); if(!Kit || !IsValid(Actor)) return Result;
    struct FSlot {uint32 Attachment,Effect;};
    const FSlot Slots[]={{20,Kit->HeadEffectID},{34,Kit->ChestEffectID},{19,Kit->BaseEffectID},{21,Kit->LeftHandEffectID},{22,Kit->RightHandEffectID},{17,Kit->BreathEffectID},{15,Kit->LeftWeaponEffectID},{16,Kit->RightWeaponEffectID},{23,Kit->SpecialEffectIDs[0]},{24,Kit->SpecialEffectIDs[1]},{25,Kit->SpecialEffectIDs[2]}};
    for(const auto& Slot:Slots) if(auto* Effect=SpawnSpellModel(Actor,Slot.Effect,Slot.Attachment,Duration,WorldSpace,Position)) Result.Add(Effect);
    if(auto* Effect=SpawnSpellModel(Actor,Kit->WorldEffectID,19,Duration,true,WorldSpace?Position:SpellAttachment(Actor,19))) Result.Add(Effect);
    for(const auto& Weak:Result) if(auto* E=Weak.Get()) {E->ComponentTags.Add(*FString::Printf(TEXT("spell:%u"),Spell)); E->ComponentTags.Add(*FString::Printf(TEXT("phase:%s"),Phase));}
    if(Result.Num()) UE_LOG(LogTemp,Log,TEXT("Authored phase=%s spell=%u caster=%llu counter=%u kit=%u components=%d"),Phase,Spell,Caster,Counter,KitId,Result.Num());
    return Result;
}
void AWowGameplayController::SpellImpact(const FWowSpellLaunch& Launch,uint64 Target,bool Hit,const FVector& Position,bool Ground)
{
    const auto* V=SpellVisual(Launch.SpellId); if(!V || !Hit) return;
    AActor* Actor=Ground?this:SpellActor(Target); if(!IsValid(Actor)) return;
    SpawnSpellKit(Actor,Ground?V->ImpactAreaKit:V->ImpactKit,0,TEXT("impact"),Launch.SpellId,Launch.CasterGuid,Launch.Counter,Ground,Position);
    if(!Ground) SpawnSpellKit(Actor,V->TargetImpactKit,0,TEXT("target-impact"),Launch.SpellId,Launch.CasterGuid,Launch.Counter);
}
void AWowGameplayController::SpawnSpellMissile(const FWowSpellLaunch& Launch)
{
    const auto* V=SpellVisual(Launch.SpellId); const auto* S=FDbcStore::Get().Spells().GetById(Launch.SpellId);
    AActor* Caster=SpellActor(Launch.CastObjectGuid); if(!Caster) Caster=SpellActor(Launch.CasterGuid);
    if(!V || !S || !Caster || !ConnectionManager) return;
    const auto& Entities=ConnectionManager->PacketHandler.EntityManager;
    const auto* CE=Entities.Find(Launch.CasterGuid);
    const float Reach=CE?FMath::Max(0.f,CE->GetFieldFloat(UnitField::COMBATREACH)):0;
    const FVector Start=SpellAttachment(Caster,FSpellVisualDbcEntry::EffectAttachment(V->MissileAttachment,(V->Flags&0x200)!=0),FVector(V->MissileCastOffsetX,V->MissileCastOffsetY,V->MissileCastOffsetZ));
    const FVector ImpactOffset(V->MissileImpactOffsetX,V->MissileImpactOffsetY,V->MissileImpactOffsetZ);
    const TWeakObjectPtr<AWowGameplayController> Self(this);
    const auto Flight=[&](uint64 Target,bool Hit,uint8 Miss,uint8 Reflect,bool Ground,const FVector& GroundPosition)
    {
        AActor* TargetActor=Ground?nullptr:SpellActor(Target); if(!Ground && !TargetActor) return;
        const FVector End=Ground?GroundPosition:SpellAttachment(TargetActor,FSpellVisualDbcEntry::EffectAttachment(V->MissileDestinationAttachment,(V->Flags&0x200)!=0),ImpactOffset);
        const auto* TE=Entities.Find(Target);
        const float Distance=CE && (TE || Ground)?FVector::Distance(CE->Movement.Position,Ground?FWowCoordinate::UEToWow(End):TE->Movement.Position):FVector::Distance(Start,End)/FWowCoordinate::SCALE;
        const uint32 FlightMs=Target==Launch.CasterGuid && !Ground?0:Ground?(S->Speed>0?uint32(FMath::Clamp(FMath::FloorToDouble(Distance/S->Speed*1000.),0.,60000.)):0):AWowSpellMissile::TravelMilliseconds(Distance,Reach,S->Speed);
        const float Elapsed=Launch.ReceivedAt>0?FMath::Max(0.f,float(FPlatformTime::Seconds()-Launch.ReceivedAt)):0;
        if(!FlightMs) {SpellImpact(Launch,Target,Hit,End,Ground); return;}
        auto* Missile=GetWorld()->SpawnActor<AWowSpellMissile>(); if(!Missile) return;
        Missile->Initialize(Start,End,FlightMs/1000.f,Elapsed);
        Missile->BindTarget(Launch.SpellId,Launch.CasterGuid,Target,TargetActor,TargetActor?End-TargetActor->GetActorLocation():FVector::ZeroVector);
        if(V->HasMissile && V->MissileModel>0) if(auto* E=SpawnSpellModel(Missile,V->MissileModel,19,FlightMs/1000.f))
        {E->ComponentTags.Add(*FString::Printf(TEXT("spell:%u"),Launch.SpellId)); E->ComponentTags.Add(TEXT("phase:missile"));}
        Missile->Arrival=[Self,Launch,Target,Hit,Miss,Reflect,Ground,FlightMs](AWowSpellMissile& Out)
        {
            if(!Self.IsValid()) return;
            if(Miss!=11) {Self->SpellImpact(Launch,Target,Hit,Out.GetDestination(),Ground); return;}
            auto* ReturnTarget=Self->SpellActor(Launch.CasterGuid); auto* Reflector=Self->SpellActor(Target);
            const auto* Visual=Self->SpellVisual(Launch.SpellId); if(!ReturnTarget || !Reflector || !Visual) return;
            const FVector End=Self->SpellAttachment(ReturnTarget,FSpellVisualDbcEntry::EffectAttachment(Visual->MissileDestinationAttachment,(Visual->Flags&0x200)!=0));
            auto* Back=Self->GetWorld()->SpawnActor<AWowSpellMissile>(); if(!Back) return;
            Back->Initialize(Out.GetDestination(),End,FMath::Max(.001f,(FlightMs>>1)/1000.f));
            Back->BindTarget(Launch.SpellId,Target,Launch.CasterGuid,ReturnTarget,End-ReturnTarget->GetActorLocation());
            if(Visual->HasMissile && Visual->MissileModel>0) Self->SpawnSpellModel(Back,Visual->MissileModel,19,(FlightMs>>1)/1000.f);
            Back->Arrival=[Self,Launch,Reflect](AWowSpellMissile& In){if(Self.IsValid()) Self->SpellImpact(Launch,Launch.CasterGuid,Reflect==0,In.GetDestination());};
        };
        UE_LOG(LogTemp,Log,TEXT("Spawned spell missile spell=%u caster=%llu target=%llu counter=%u serverTime=%u flightMs=%u hit=%d miss=%u ground=%d"),Launch.SpellId,Launch.CasterGuid,Target,Launch.Counter,Launch.ServerTimeMs,FlightMs,Hit,Miss,Ground);
    };
    TSet<uint64> Seen;
    for(uint64 Target:Launch.HitTargets) if(Target && !Seen.Contains(Target)) {Seen.Add(Target); Flight(Target,true,0,0,false,FVector::ZeroVector);}
    for(const auto& Miss:Launch.MissedTargets) if(Miss.Guid && !Seen.Contains(Miss.Guid)) {Seen.Add(Miss.Guid); Flight(Miss.Guid,false,Miss.Reason,Miss.ReflectReason,false,FVector::ZeroVector);}
    if(Launch.HasDestination && !Launch.DestinationTransport)
    {
        const FVector Ground=FWowCoordinate::WowToUE(Launch.Destination);
        SpawnSpellKit(this,V->InstantAreaKit,0,TEXT("instant-area"),Launch.SpellId,Launch.CasterGuid,Launch.Counter,true,Ground);
        Flight(0,true,0,0,true,Ground);
    }
    SpawnSpellKit(Caster,V->CasterImpactKit,0,TEXT("caster-impact"),Launch.SpellId,Launch.CasterGuid,Launch.Counter);
}
void AWowGameplayController::UpdateSpellEffects()
{
    if(!ConnectionManager) return;
    const double Now=FPlatformTime::Seconds(); const auto& Handler=ConnectionManager->PacketHandler;
    for(auto It=AuthoredCastEffects.CreateIterator();It;++It)
    {
        const auto* Cast=Handler.Casts.Find(It.Key()); const auto& State=It.Value();
        const bool Active=Cast && !Cast->Channel && Cast->Spell==State.Spell && Cast->Counter==State.Counter && Cast->Ends>Now && SpellActor(It.Key());
        for(const auto& Weak:State.Components) if(auto* E=Weak.Get()) {if(Active) E->ExtendEmission(Cast->Ends+.25); else E->StopEmitting();}
        if(!Active) It.RemoveCurrent();
    }
    TSet<FString> Active;
    const auto Keep=[&](const FString& Key,uint64 Unit,uint64 Caster,uint32 Spell,uint8 Counter,bool Channel,uint32 Kit,uint32 Done,double End)
    {
        if(!Kit || End<=Now || !SpellActor(Unit)) return; Active.Add(Key);
        auto* State=PersistentSpellEffects.Find(Key);
        if(!State)
        {
            if(PersistentSpellEffects.Num()>=256) return;
            FVisualState New; New.Unit=Unit; New.Caster=Caster; New.Spell=Spell; New.Counter=Counter; New.Channel=Channel; New.DoneKit=Done;
            New.Components=SpawnSpellKit(SpellActor(Unit),Kit,FMath::Min(60.f,float(End-Now)+.25f),Channel?TEXT("channel"):TEXT("aura"),Spell,Caster,Counter);
            State=&PersistentSpellEffects.Add(Key,MoveTemp(New));
        }
        for(const auto& Weak:State->Components) if(auto* E=Weak.Get()) E->ExtendEmission(FMath::Min(Now+60,End+.25));
    };
    for(const auto& Pair:Handler.Casts) if(Pair.Value.Channel)
    {
        const auto& Cast=Pair.Value; const auto* V=SpellVisual(Cast.Spell); if(!V) continue;
        Keep(FString::Printf(TEXT("channel:%llu:%u:%u"),Pair.Key,Cast.Spell,Cast.Counter),Pair.Key,Pair.Key,Cast.Spell,Cast.Counter,true,V->ChannelKit,0,Cast.Ends);
    }
    const auto Auras=[&](uint64 Guid)
    {
        const auto* Entity=Handler.EntityManager.Find(Guid); if(!Entity) return;
        for(int32 Slot=0;Slot<Entity->Auras.Num();++Slot)
        {
            const auto& A=Entity->Auras[Slot]; if(!A.bActive) continue; const auto* V=SpellVisual(A.SpellId); if(!V) continue;
            const double End=A.DurationMs?A.UpdatedAt+A.RemainingMs/1000.:Now+60;
            Keep(FString::Printf(TEXT("aura:%llu:%d:%u:%llu"),Guid,Slot,A.SpellId,A.CasterGuid),Guid,A.CasterGuid,A.SpellId,0,false,V->StateKit,V->StateDoneKit,End);
        }
    };
    for(const auto& Pair:Handler.EntityManager.GetAll()) if(Pair.Value->IsDynamicObject())
    {
        const auto* Dynamic=static_cast<const FWowDynamicObjectEntity*>(Pair.Value.Get()); const auto* V=SpellVisual(Dynamic->GetSpellId());
        if(!V || !V->PersistentAreaKit) continue;
        const FString Key=FString::Printf(TEXT("area:%llu:%u"),Pair.Key,Dynamic->GetSpellId()); Active.Add(Key);
        auto* State=PersistentSpellEffects.Find(Key);
        if(!State && PersistentSpellEffects.Num()<256)
        {
            FVisualState New; New.Unit=Pair.Key; New.Caster=Dynamic->GetCasterGuid(); New.Spell=Dynamic->GetSpellId(); New.Updated=Now;
            New.Components=SpawnSpellKit(this,V->PersistentAreaKit,60,TEXT("persistent-area"),New.Spell,New.Caster,0,true,FWowCoordinate::WowToUE(Dynamic->Movement.Position));
            State=&PersistentSpellEffects.Add(Key,MoveTemp(New));
        }
        const auto* Kit=FDbcStore::Get().SpellVisualKits().GetById(V->PersistentAreaKit); int32 Proc=-1;
        if(Kit) for(int32 I=0;I<4;++I) if(Kit->CharProc[I]==9) {Proc=I; break;}
        if(State && Proc>=0)
        {
            const float Rate=FMath::Clamp(Kit->CharParams[1][Proc],0.f,60.f); State->Accumulator+=FMath::Min(.06666667,Now-State->Updated)*Rate; State->Updated=Now;
            const FString Path=FDbcStore::Get().SpellVisualKits().AreaModel(int32(Kit->CharParams[0][Proc]));
            for(int32 N=0;N<2 && State->Accumulator>=1;++N,State->Accumulator-=1)
            {
                const float Angle=FMath::FRand()*2*PI,Radius=FMath::FRand()*FMath::Clamp(Dynamic->GetRadius(),0.f,100.f)*FWowCoordinate::SCALE;
                FVector Position=FWowCoordinate::WowToUE(Dynamic->Movement.Position)+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0);
                FHitResult Hit; FCollisionQueryParams Params; Params.AddIgnoredActor(GetPawn());
                if(GetWorld()->LineTraceSingleByChannel(Hit,Position+FVector(0,0,1666),Position-FVector(0,0,3333),ECC_WorldStatic,Params)) Position=Hit.ImpactPoint;
                if(auto* E=SpawnSpellModel(this,0,19,0,true,Position,Path)) {E->ComponentTags.Add(*FString::Printf(TEXT("spell:%u"),State->Spell)); E->ComponentTags.Add(TEXT("phase:persistent-area")); State->Components.Add(E);}
            }
            State->Components.RemoveAll([](const auto& E){return !E.IsValid();});
        }
        else if(State) for(const auto& Weak:State->Components) if(auto* E=Weak.Get()) E->ExtendEmission(Now+60);
    }
    Auras(Handler.EntityManager.LocalPlayerGuid); for(const auto& Pair:SpawnedEntityActors) Auras(Pair.Key);
    for(auto It=PersistentSpellEffects.CreateIterator();It;++It) if(!Active.Contains(It.Key()))
    {
        const auto& State=It.Value(); for(const auto& Weak:State.Components) if(auto* E=Weak.Get()) E->StopEmitting();
        if(SpellActor(State.Unit)) SpawnSpellKit(SpellActor(State.Unit),State.DoneKit,0,TEXT("state-done"),State.Spell,State.Caster,State.Counter);
        It.RemoveCurrent();
    }
    for(auto It=LastVisualLaunches.CreateIterator();It;++It) if(Now-It.Value().ReceivedAt>65) It.RemoveCurrent();
}

