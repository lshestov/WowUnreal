#include "WowSpellMissile.h"
#include "WowM2EmitterComponent.h"

AWowSpellMissile::AWowSpellMissile()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("FlightRoot"));
}
void AWowSpellMissile::Initialize(const FVector& Start,const FVector& End,float TravelTime,float Elapsed)
{
    StartPos=Start; EndPos=End;
    TotalTravelTime=FMath::IsFinite(TravelTime)?FMath::Max(.001f,TravelTime):.001f;
    FlightStartedAt=FPlatformTime::Seconds()-FMath::Max(0.f,Elapsed); bIsInitialized=true;
    SetActorLocation(StartPos); SetActorRotation((EndPos-StartPos).Rotation()+FRotator(0,-90,0));
}
uint32 AWowSpellMissile::TravelMilliseconds(float Distance,float CasterReach,float Speed)
{
    if(!FMath::IsFinite(Distance) || !FMath::IsFinite(CasterReach) || !FMath::IsFinite(Speed) || Distance<0 || Speed<=0) return 0;
    return uint32(FMath::Clamp(FMath::FloorToDouble(FMath::Max(5.f,Distance-FMath::Max(0.f,CasterReach))/double(Speed)*1000.),0.,60000.));
}
void AWowSpellMissile::BindTarget(uint32 Spell,uint64 Caster,uint64 Target,AActor* Actor,const FVector& Offset)
{
    SpellId=Spell; CasterGuid=Caster; TargetGuid=Target; TargetActor=Actor; TargetOffset=Offset;
}
void AWowSpellMissile::Tick(float Delta)
{
    Super::Tick(Delta); if(!bIsInitialized) return;
    if(TargetActor.IsValid()) EndPos=TargetActor->GetActorLocation()+TargetOffset;
    const float Elapsed=FMath::Max(0.f,float(FPlatformTime::Seconds()-FlightStartedAt));
    const FVector Position=FMath::Lerp(StartPos,EndPos,FMath::Clamp(Elapsed/TotalTravelTime,0.f,1.f));
    const FVector Direction=Position-GetActorLocation();
    if(!Direction.IsNearlyZero()) SetActorRotation(Direction.Rotation()+FRotator(0,-90,0));
    SetActorLocation(Position);
    if(Elapsed<TotalTravelTime) return;
    bIsInitialized=false;
    UE_LOG(LogTemp,Log,TEXT("Spell missile reached target spell=%u caster=%llu target=%llu time=%.3f elapsed=%.3f"),SpellId,CasterGuid,TargetGuid,TotalTravelTime,Elapsed);
    TInlineComponentArray<UWowM2EmitterComponent*> Effects(this); for(auto* Effect:Effects) Effect->StopEmitting();
    if(Arrival) Arrival(*this); Arrival=nullptr;
    SetActorTickEnabled(false); SetLifeSpan(11.f);
}
