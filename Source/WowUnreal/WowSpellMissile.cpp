#include "WowSpellMissile.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Components/PointLightComponent.h"

AWowSpellMissile::AWowSpellMissile()
{
    PrimaryActorTick.bCanEverTick = true;

    // Create sphere mesh component
    SphereMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SphereMesh"));
    RootComponent = SphereMesh;

    // Load sphere mesh
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshAsset(TEXT("/Engine/BasicShapes/Sphere"));
    if (SphereMeshAsset.Succeeded())
    {
        SphereMesh->SetStaticMesh(SphereMeshAsset.Object.Get());
    }

    // Scale down the sphere for better visibility
    SphereMesh->SetWorldScale3D(FVector(0.5f, 0.5f, 0.5f));

    // Create point light component for ambient glow
    GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
    GlowLight->SetupAttachment(SphereMesh);
    GlowLight->SetIntensity(3000.0f);
    GlowLight->SetAttenuationRadius(200.0f);
    GlowLight->SetCastShadows(false); // Disable shadows for performance

    // Initialize variables
    StartPos = FVector::ZeroVector;
    EndPos = FVector::ZeroVector;
    TotalTravelTime = 1.0f;
    CurrentTime = 0.0f;
    bIsInitialized = false;

    // Disable collision
    SphereMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AWowSpellMissile::Initialize(const FVector& StartLocation, const FVector& TargetLocation, const FLinearColor& SpellColor, float TravelTime)
{
    StartPos = StartLocation;
    EndPos = TargetLocation;
    TotalTravelTime = FMath::IsFinite(TravelTime)?FMath::Max(.001f,TravelTime):.001f;
    CurrentTime = 0.0f;
    bIsInitialized = true;
    FlightStartedAt=FPlatformTime::Seconds();

    // Set initial position
    SetActorLocation(StartPos);

    // Set glow light color to match spell school
    if (GlowLight)
    {
        GlowLight->SetLightColor(SpellColor);
    }

#if WITH_EDITOR
    // Create proper emissive material at runtime for glow effect
    UMaterial* MissileMat = NewObject<UMaterial>(GetTransientPackage());
    MissileMat->SetShadingModel(MSM_Unlit);
    MissileMat->BlendMode = BLEND_Additive; // Additive for glow effect

    auto* ColorParam = NewObject<UMaterialExpressionConstant3Vector>(MissileMat);
    ColorParam->Constant = FLinearColor(SpellColor.R, SpellColor.G, SpellColor.B); // The spell school color
    MissileMat->GetExpressionCollection().AddExpression(ColorParam);

    auto* Multiply = NewObject<UMaterialExpressionMultiply>(MissileMat);
    Multiply->ConstB = 5.0f; // Intensity multiplier for glow
    MissileMat->GetExpressionCollection().AddExpression(Multiply);
    Multiply->A.Connect(0, ColorParam);

    MissileMat->GetEditorOnlyData()->EmissiveColor.Connect(0, Multiply);
    MissileMat->GetEditorOnlyData()->Opacity.Connect(0, ColorParam); // Use color brightness as opacity

    MissileMat->PreEditChange(nullptr);
    MissileMat->PostEditChange();

    SphereMesh->SetMaterial(0, MissileMat);
#else
    // Fallback for runtime without editor - use basic material with dynamic instance
    UMaterial* FallbackMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
    if (FallbackMaterial)
    {
        DynamicMaterial = UMaterialInstanceDynamic::Create(FallbackMaterial, this);
        if (DynamicMaterial)
        {
            DynamicMaterial->SetVectorParameterValue(FName("Color"), SpellColor);
            SphereMesh->SetMaterial(0, DynamicMaterial);
        }
    }
#endif
}

void AWowSpellMissile::BeginPlay()
{
    Super::BeginPlay();
}

uint32 AWowSpellMissile::TravelMilliseconds(float Distance,float CasterReach,float Speed)
{
    if(!FMath::IsFinite(Distance) || !FMath::IsFinite(CasterReach) || !FMath::IsFinite(Speed) || Distance<0 || Speed<=0) return 0;
    // AzerothCore uses distance from the caster's combat reach, with a five
    // yard lower bound, then truncates to whole milliseconds.
    return uint32(FMath::Clamp(FMath::FloorToDouble(FMath::Max(5.f,Distance-FMath::Max(0.f,CasterReach))/double(Speed)*1000.),0.,60000.));
}
void AWowSpellMissile::BindTarget(uint32 Spell,uint64 Caster,uint64 Target,AActor* Actor,const FVector& Offset)
{
    SpellId=Spell; CasterGuid=Caster; TargetGuid=Target; TargetActor=Actor; TargetOffset=Offset;
}

void AWowSpellMissile::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!bIsInitialized)
    {
        return;
    }

    if(TargetActor.IsValid()) EndPos=TargetActor->GetActorLocation()+TargetOffset;
    // A newly spawned actor can tick with a delta that began before launch.
    // Measure from the packet-driven spawn instead of charging that old time.
    CurrentTime=FMath::Max(0.f,float(FPlatformTime::Seconds()-FlightStartedAt));

    if (CurrentTime >= TotalTravelTime)
    {
        // Reached target
        OnReachedTarget();
    }
    else
    {
        // Update position
        UpdatePosition();
    }
}

void AWowSpellMissile::UpdatePosition()
{
    if (!bIsInitialized)
    {
        return;
    }

    // Simple linear interpolation
    float Alpha = CurrentTime / TotalTravelTime;
    Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

    FVector NewPosition = FMath::Lerp(StartPos, EndPos, Alpha);
    SetActorLocation(NewPosition);
}

void AWowSpellMissile::OnReachedTarget()
{
    UpdatePosition();
    UE_LOG(LogTemp, Log, TEXT("Spell missile reached target spell=%u caster=%llu target=%llu time=%.3f elapsed=%.3f"),SpellId,CasterGuid,TargetGuid,TotalTravelTime,CurrentTime);

    // Destroy the missile
    Destroy();
}
