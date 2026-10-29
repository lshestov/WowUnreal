#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "WowSpellMissile.generated.h"

UCLASS()
class WOWUNREAL_API AWowSpellMissile : public AActor
{
    GENERATED_BODY()

public:
    AWowSpellMissile();

    /** Initialize the missile with start/end positions and spell school color */
    void Initialize(const FVector& StartLocation, const FVector& TargetLocation, const FLinearColor& SpellColor, float TravelTime = 1.0f);
    static uint32 TravelMilliseconds(float Distance,float CasterReach,float Speed);
    void BindTarget(uint32 Spell,uint64 Caster,uint64 Target,AActor* Actor,const FVector& Offset);
    uint32 GetSpellId() const {return SpellId;}
    uint64 GetTargetGuid() const {return TargetGuid;}
    uint64 GetCasterGuid() const {return CasterGuid;}
    float GetTravelTime() const {return TotalTravelTime;}
    FVector GetDestination() const {return EndPos;}

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

protected:
    /** Sphere mesh component for the missile */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> SphereMesh;

    /** Dynamic material instance for changing color */
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

    /** Point light component for ambient glow */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<UPointLightComponent> GlowLight;

private:
    FVector StartPos;
    FVector EndPos;
    float TotalTravelTime;
    float CurrentTime;
    double FlightStartedAt=0;
    bool bIsInitialized;
    uint32 SpellId=0;
    uint64 CasterGuid=0,TargetGuid=0;
    TWeakObjectPtr<AActor> TargetActor;
    FVector TargetOffset=FVector::ZeroVector;

    void UpdatePosition();
    void OnReachedTarget();
};
