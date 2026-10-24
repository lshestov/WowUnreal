#include "WowGameplayController.h"
#include "WowConnectionManager.h"
#include "WowWorldCollision.h"
#include "Coord/WowCoordinate.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Engine/World.h"

void AWowGameplayController::UpdateEntityGrounding()
{
    if (!ConnectionManager || !GetWorld() || SpawnedEntityActors.IsEmpty()) return;
    const double Started = FPlatformTime::Seconds();
    TArray<uint64> Guids;
    SpawnedEntityActors.GetKeys(Guids);
    for (int32 Probe = 0; Probe < FMath::Min(16, Guids.Num()); ++Probe)
    {
        NpcGroundCursor %= Guids.Num();
        const uint64 Guid = Guids[NpcGroundCursor++];
        const auto* Entity = ConnectionManager->PacketHandler.EntityManager.Find(Guid);
        AActor* Actor = SpawnedEntityActors.FindRef(Guid);
        if (!Entity || Entity->IsPlayer() || !IsValid(Actor) || DeadEntityGuids.Contains(Guid)) continue;
        // Disable gravity, hover, flying, swimming and airborne routes are intentional.
        if ((Entity->Movement.MoveFlags & (0x00000400 | 0x40000000 | 0x02000000 | 0x00200000 | 0x00003000))
            || (Entity->Movement.bHasActiveSpline && (Entity->Movement.SplineFlags & (0x00002000 | 0x00000800 | 0x00000200))))
        {
            if (NpcGroundOffsets.Remove(Guid))
            {
                FVector Visual = Actor->GetActorLocation();
                Visual.Z = FWowCoordinate::WowToUE(Entity->Movement.Position).Z;
                Actor->SetActorLocation(Visual);
            }
            continue;
        }
        auto* Mesh = Actor->FindComponentByClass<USkeletalMeshComponent>();
        if (!Mesh || !Mesh->GetSkeletalMeshAsset()) continue;
        if (!NpcReferenceFeet.Contains(Guid))
        {
            // Use only vertices in the actual visible geosets, in reference pose.
            float Lowest = 0;
            const auto* Render = Mesh->GetSkeletalMeshAsset()->GetResourceForRendering();
            if (Render && !Render->LODRenderData.IsEmpty())
            {
                const auto& Positions = Render->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;
                if (Positions.GetVertexData() && Positions.GetNumVertices())
                {
                    Lowest = TNumericLimits<float>::Max();
                    for (uint32 Vertex = 0; Vertex < Positions.GetNumVertices(); ++Vertex)
                        Lowest = FMath::Min(Lowest, Positions.VertexPosition(Vertex).Z);
                }
            }
            NpcReferenceFeet.Add(Guid, Lowest * Mesh->GetComponentScale().Z + Mesh->GetRelativeLocation().Z);
        }
        const FVector Server = FWowCoordinate::WowToUE(Entity->Movement.Position);
        const float FootOffset = NpcReferenceFeet.FindRef(Guid);
        const FVector ExpectedFeet(Actor->GetActorLocation().X, Actor->GetActorLocation().Y, Server.Z + FootOffset);
        FHitResult Floor;
        if (!FWowWorldCollision::FindFloor(GetWorld(), ExpectedFeet, Actor, Floor, 75, 1000)) continue;
        const double RootZ = Floor.ImpactPoint.Z + 2.0 - FootOffset;
        NpcGroundOffsets.Add(Guid, RootZ - Server.Z);
        FVector Visual = Actor->GetActorLocation();
        Visual.Z = RootZ;
        if (!FMath::IsNearlyEqual(Actor->GetActorLocation().Z, Visual.Z, 0.5)) Actor->SetActorLocation(Visual);
    }
    NpcGroundingMilliseconds = (FPlatformTime::Seconds() - Started) * 1000;
}
