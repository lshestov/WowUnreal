#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WowSpellMissile.generated.h"

/** Packet-owned flight clock; the visible model is loaded from the MPQs. */
UCLASS()
class WOWUNREAL_API AWowSpellMissile : public AActor
{
    GENERATED_BODY()
public:
    AWowSpellMissile();
    void Initialize(const FVector& Start,const FVector& End,float TravelTime,float Elapsed=0);
    static uint32 TravelMilliseconds(float Distance,float CasterReach,float Speed);
    void BindTarget(uint32 Spell,uint64 Caster,uint64 Target,AActor* Actor,const FVector& Offset);
    uint32 GetSpellId() const {return SpellId;}
    uint64 GetTargetGuid() const {return TargetGuid;}
    uint64 GetCasterGuid() const {return CasterGuid;}
    float GetTravelTime() const {return TotalTravelTime;}
    FVector GetDestination() const {return EndPos;}
    bool IsFlying() const {return bIsInitialized;}
    TFunction<void(AWowSpellMissile&)> Arrival;
    virtual void Tick(float DeltaTime) override;
private:
    FVector StartPos=FVector::ZeroVector,EndPos=FVector::ZeroVector;
    float TotalTravelTime=0;
    double FlightStartedAt=0;
    bool bIsInitialized=false;
    uint32 SpellId=0;
    uint64 CasterGuid=0,TargetGuid=0;
    TWeakObjectPtr<AActor> TargetActor;
    FVector TargetOffset=FVector::ZeroVector;
};
