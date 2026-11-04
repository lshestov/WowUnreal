#include "WowRenderSettings.h"
#include "WowRenderPolicy.h"
#include "Components/PrimitiveComponent.h"
#include "Components/InstancedStaticMeshComponent.h"

void UWowRenderSettings::SetParticleDensity(float Value)
{Density=WowRenderPolicy::ParticleDensity(Value);}

void UWowRenderSettings::ApplyDoodad(UPrimitiveComponent* Visual,int32 FadeLevel) const
{
    Visual->bNeverDistanceCull=false;
    const float Distance=WowRenderPolicy::DoodadCullDistanceCm(FadeLevel,Detail);
    if(auto* Instances=Cast<UInstancedStaticMeshComponent>(Visual))
        Instances->SetCullDistances(0,FMath::RoundToInt(Distance));
    else Visual->SetCullDistance(Distance);
}
void UWowRenderSettings::SetEnvironmentDetail(float Value)
{
    Detail=WowRenderPolicy::EnvironmentDetail(Value);
    for(auto It=Doodads.CreateIterator();It;++It)
        if(auto* Visual=It.Key().Get();IsValid(Visual)) ApplyDoodad(Visual,It.Value());
        else It.RemoveCurrent();
}
void UWowRenderSettings::RegisterDoodad(UPrimitiveComponent* Visual,int32 FadeLevel)
{
    if(!IsValid(Visual) || Visual->GetWorld()!=GetWorld()) return;
    Visual->ComponentTags.AddUnique(TEXT("WowDistanceDoodad"));
    Doodads.Add(TWeakObjectPtr<UPrimitiveComponent>(Visual),FadeLevel); ApplyDoodad(Visual,FadeLevel);
    if(++RegistrationsSincePrune>=128)
    {
        for(auto It=Doodads.CreateIterator();It;++It) if(!It.Key().IsValid()) It.RemoveCurrent();
        RegistrationsSincePrune=0;
    }
}
void UWowRenderSettings::Deinitialize()
{Doodads.Reset(); Super::Deinitialize();}
