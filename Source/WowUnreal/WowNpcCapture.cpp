#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowAnimationController.h"
#include "WowCharacterBuilder.h"
#include "WowM2AnimInstance.h"
#include "WowWorldCollision.h"
#include "Coord/WowCoordinate.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshRenderData.h"

DEFINE_LOG_CATEGORY_STATIC(LogWowNpcCapture, Log, All);

bool AWowGameplayController::ConfigureNpcCapture(uint32 DisplayId)
{
    if (!ConnectionManager) return false;
    double Nearest = TNumericLimits<double>::Max();
    const bool bPreferMoving = FParse::Param(FCommandLine::Get(), TEXT("capturenpcmoving"));
    for (const auto& Pair : SpawnedEntityActors)
    {
        const auto* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Pair.Key);
        AActor* Actor = Pair.Value;
        if (!Entity || Entity->IsPlayer() || !IsValid(Actor)) continue;
        const uint32 Display = Entity->GetDisplayId() ? Entity->GetDisplayId() : Entity->GetNativeDisplayId();
        if (Display != DisplayId) continue;
        const double Distance = (GetPawn() ? FVector::DistSquared(Actor->GetActorLocation(), GetPawn()->GetActorLocation()) : 0)
            - (bPreferMoving && Entity->Movement.bHasActiveSpline ? 1.e12 : 0);
        if (Distance < Nearest) { Nearest = Distance; NpcCaptureTarget = Actor; }
    }
    if (!NpcCaptureTarget.IsValid()) return false;
    NpcCaptureCamera = GetWorld()->SpawnActor<ACameraActor>();
    if (!NpcCaptureCamera.IsValid()) return false;
    NpcCaptureCamera->GetCameraComponent()->SetFieldOfView(50);
    UpdateNpcCaptureCamera();
    SetViewTarget(NpcCaptureCamera.Get());
    return true;
}

bool AWowGameplayController::IsNpcCaptureMoving() const
{
    if (!NpcCaptureTarget.IsValid() || !ConnectionManager) return false;
    for (const auto& Pair : SpawnedEntityActors)
    {
        if (Pair.Value != NpcCaptureTarget.Get()) continue;
        const auto* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Pair.Key);
        const auto* Mesh = Pair.Value->FindComponentByClass<USkeletalMeshComponent>();
        const auto* Animation = Mesh ? Cast<UWowM2AnimInstance>(Mesh->GetAnimInstance()) : nullptr;
        return Entity && Entity->Movement.GetSplineSpeed() > 0 && Animation
            && (Animation->GetM2AnimationId() == 4 || Animation->GetM2AnimationId() == 5)
            && Animation->GetM2Time() > 0.15f;
    }
    return false;
}

void AWowGameplayController::UpdateNpcCaptureCamera()
{
    AActor* Target = NpcCaptureTarget.Get();
    ACameraActor* Camera = NpcCaptureCamera.Get();
    if (!Target || !Camera) return;
    auto* Mesh = Target->FindComponentByClass<USkeletalMeshComponent>();
    if (!Mesh) return;
    const FVector Center = Mesh->Bounds.Origin;
    const double Radius = FMath::Max3(Mesh->Bounds.BoxExtent.X, Mesh->Bounds.BoxExtent.Y, Mesh->Bounds.BoxExtent.Z);
    const FVector Direction = Target->GetActorQuat().RotateVector(FVector(1, 0.45, 0)).GetSafeNormal();
    const FVector CameraLocation = Center + Direction * FMath::Max(150.0, Radius * 3.6) + FVector(0, 0, Radius * 0.35);
    Camera->SetActorLocationAndRotation(CameraLocation, (Center - CameraLocation).Rotation());
}

void AWowGameplayController::LogNpcCapture() const
{
    const AActor* Target = NpcCaptureTarget.Get();
    if (!Target || !ConnectionManager) return;
    const FWowEntity* Entity = nullptr;
    for (const auto& Pair : SpawnedEntityActors)
        if (Pair.Value == Target) { Entity = ConnectionManager->PacketHandler.EntityManager.Find(Pair.Key); break; }
    auto* Mesh = Target->FindComponentByClass<USkeletalMeshComponent>();
    const auto* Model = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!Entity || !Model) return;
    const auto* Render = Model->GetResourceForRendering();
    const auto* Animation = Cast<UWowM2AnimInstance>(Mesh->GetAnimInstance());
    int32 AnimatedBones = 0;
    double PoseChecksum = 0;
    const auto& Pose = Mesh->GetBoneSpaceTransforms();
    const auto& Ref = Model->GetRefSkeleton().GetRefBonePose();
    for (int32 Bone = 0; Bone < FMath::Min(Pose.Num(), Ref.Num()); ++Bone)
    {
        if (!Pose[Bone].Equals(Ref[Bone], 0.001)) ++AnimatedBones;
        const FQuat& R = Pose[Bone].GetRotation();
        PoseChecksum += (Bone + 1) * (R.X + 3 * R.Y + 7 * R.Z);
    }
    TSet<FString> Textures;
    for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
    {
        UTexture* Texture = nullptr;
        if (const auto* Material = Mesh->GetMaterial(Slot))
            if (Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseTexture")), Texture) && Texture)
                Textures.Add(Texture->GetName());
    }
    const double PositionError = FVector::Distance(Target->GetActorLocation(), FWowCoordinate::WowToUE(Entity->Movement.Position));
    UE_LOG(LogWowNpcCapture, Log, TEXT("NpcCapture: guid=%llu entry=%u display=%u positionError=%.3f vertices=%u textures=%d animId=%d time=%.3f rate=%.3f animatedBones=%d poseChecksum=%.6f splineSpeed=%.3f"),
        Entity->Guid, Entity->Entry, Entity->GetDisplayId(), PositionError,
        Render && !Render->LODRenderData.IsEmpty() ? Render->LODRenderData[0].GetNumVertices() : 0,
        Textures.Num(), Animation ? Animation->GetM2AnimationId() : -1, Animation ? Animation->GetM2Time() : 0,
        Animation ? Animation->GetM2PlayRate() : 0, AnimatedBones, PoseChecksum, Entity->Movement.GetSplineSpeed());
    for (const FString& Texture : Textures) UE_LOG(LogWowNpcCapture, Log, TEXT("NpcCaptureTexture: %s"), *Texture);
    const float FootOffset = NpcReferenceFeet.FindRef(Entity->Guid);
    const FVector Feet = Target->GetActorLocation() + FVector(0, 0, FootOffset);
    FHitResult Ground;
    const bool GroundReady = FWowWorldCollision::FindFloor(GetWorld(), Feet, Target, Ground);
    UE_LOG(LogWowNpcCapture, Log, TEXT("NpcGroundCapture: feetZ=%.3f floorZ=%.3f gap=%.3f ready=%d surface=%s"),
        Feet.Z, GroundReady ? Ground.ImpactPoint.Z : 0, GroundReady ? Feet.Z - Ground.ImpactPoint.Z : 0,
        GroundReady ? 1 : 0, GroundReady ? *GetNameSafe(Ground.GetComponent()) : TEXT("none"));
    UE_LOG(LogWowNpcCapture, Log, TEXT("NpcGroundingCost: %.3f ms"), NpcGroundingMilliseconds);
}
