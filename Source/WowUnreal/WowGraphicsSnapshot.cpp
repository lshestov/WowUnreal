#include "WowGraphicsSnapshot.h"
#include "WowRenderSettings.h"
#include "WowM2EmitterComponent.h"
#include "WowM2LightSubsystem.h"
#include "WowWorldManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "SceneView.h"
#include "ConvexVolume.h"
#include "GameFramework/PlayerController.h"
#include "ProceduralMeshComponent.h"

TSharedRef<FJsonObject> WowGraphicsSnapshot(UWorld* World,APlayerController* Controller)
{
    auto Row=MakeShared<FJsonObject>(); if(!World || !Controller) return Row;
    FVector Camera; FRotator Rotation; Controller->GetPlayerViewPoint(Camera,Rotation);
    const auto VectorJson=[](const FVector& V) {
        return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};
    };
    Row->SetArrayField(TEXT("cameraUE"),VectorJson(Camera));
    Row->SetArrayField(TEXT("cameraRotation"),VectorJson(FVector(Rotation.Pitch,Rotation.Yaw,Rotation.Roll)));
    FConvexVolume Frustum; bool HasFrustum=false;
    if(const auto* Player=Controller->GetLocalPlayer();Player && Player->ViewportClient && Player->ViewportClient->Viewport)
    {
        FSceneViewProjectionData Projection;
        HasFrustum=Player->GetProjectionData(Player->ViewportClient->Viewport,Projection);
        if(HasFrustum) GetViewFrustumBounds(Frustum,Projection.ComputeViewProjectionMatrix(),true);
    }
    if(const auto* Settings=World->GetSubsystem<UWowRenderSettings>())
    {
        Row->SetNumberField(TEXT("particleDensity"),Settings->GetParticleDensity());
        Row->SetNumberField(TEXT("environmentDetail"),Settings->GetEnvironmentDetail());
    }
    int32 Resident=0,DistanceEligible=0,FrustumEligible=0,Batches=0,Particles=0,Emitters=0,CollisionBodies=0,CollisionTriangles=0,WmoGroups=0;
    for(TActorIterator<AActor> It(World);It;++It)
    {
        if(It->IsActorBeingDestroyed()) continue;
        TInlineComponentArray<UActorComponent*> Components(*It);
        for(auto* C:Components)
        {
            if(!IsValid(C) || !C->IsRegistered()) continue;
            if(const auto* Effect=Cast<UWowM2EmitterComponent>(C)) {Particles+=Effect->GetParticleCount(); Emitters+=Effect->GetEmitterCount();}
            if(auto* Collision=Cast<UProceduralMeshComponent>(C);Collision && Collision->ComponentHasTag(TEXT("WowGroundSurface")))
            {
                CollisionBodies+=Collision->BodyInstance.IsValidBodyInstance();
                for(int32 S=0;S<Collision->GetNumSections();++S) if(const auto* Section=Collision->GetProcMeshSection(S)) CollisionTriangles+=Section->ProcIndexBuffer.Num()/3;
            }
            const auto* Mesh=Cast<UStaticMeshComponent>(C); if(!Mesh || !Mesh->GetStaticMesh()) continue;
            if(Mesh->GetName().StartsWith(TEXT("WmoGroup_"))) ++WmoGroups;
            const auto* Instances=Cast<UInstancedStaticMeshComponent>(Mesh);
            if(!Instances || !Instances->ComponentHasTag(TEXT("WowDistanceDoodad"))) continue;
            ++Batches; Resident+=Instances->GetInstanceCount();
            for(int32 I=0;I<Instances->GetInstanceCount();++I)
            {
                FTransform Transform; if(!Instances->GetInstanceTransform(I,Transform,true)) continue;
                const auto Bounds=Instances->GetStaticMesh()->GetBounds().TransformBy(Transform);
                const float Limit=Instances->InstanceEndCullDistance;
                if(Limit>0 && FVector::DistSquared(Camera,Bounds.Origin)>FMath::Square(double(Limit))) continue;
                ++DistanceEligible;
                if(HasFrustum && Frustum.IntersectBox(Bounds.Origin,Bounds.BoxExtent)) ++FrustumEligible;
            }
        }
        if(const auto* Manager=Cast<AWowWorldManager>(*It))
        {
            Row->SetNumberField(TEXT("loadedTiles"),Manager->GetLoadedTileCount());
            Row->SetNumberField(TEXT("missingTerrainCoverage"),Manager->GetMissingTerrainCoverage());
            Row->SetBoolField(TEXT("streamingIdle"),Manager->IsStreamingWorkIdle());
        }
    }
    Row->SetNumberField(TEXT("residentDoodadInstances"),Resident); Row->SetNumberField(TEXT("doodadBatches"),Batches);
    Row->SetNumberField(TEXT("distanceEligibleDoodadInstances"),DistanceEligible);
    Row->SetBoolField(TEXT("hasFrustum"),HasFrustum); Row->SetNumberField(TEXT("frustumEligibleDoodadInstances"),FrustumEligible);
    Row->SetStringField(TEXT("visibilityCountMethod"),TEXT("instance bounds inside native distance and camera frustum, before GPU occlusion; not a pixel-visible count"));
    Row->SetNumberField(TEXT("liveParticles"),Particles); Row->SetNumberField(TEXT("particleEmitters"),Emitters);
    Row->SetNumberField(TEXT("collisionBodies"),CollisionBodies); Row->SetNumberField(TEXT("collisionTriangles"),CollisionTriangles);
    Row->SetNumberField(TEXT("wmoGroups"),WmoGroups);
    if(const auto* Lights=World->GetSubsystem<UWowM2LightSubsystem>())
    {Row->SetNumberField(TEXT("lightSources"),Lights->GetSourceCount()); Row->SetNumberField(TEXT("activeLights"),Lights->GetActiveCount()); Row->SetNumberField(TEXT("pendingLights"),Lights->GetPendingCount());}
    return Row;
}
