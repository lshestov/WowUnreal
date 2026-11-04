#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WowRenderSettings.generated.h"

class UPrimitiveComponent;

/** Live world rendering preferences. Weak visual registrations never own collision/light data. */
UCLASS()
class WOWASSETS_API UWowRenderSettings : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    float GetParticleDensity() const {return Density;}
    float GetEnvironmentDetail() const {return Detail;}
    void SetParticleDensity(float Value);
    void SetEnvironmentDetail(float Value);
    void RegisterDoodad(UPrimitiveComponent* Visual,int32 FadeLevel);
    virtual void Deinitialize() override;
private:
    float Density=1,Detail=1;
    int32 RegistrationsSincePrune=0;
    TMap<TWeakObjectPtr<UPrimitiveComponent>,int32> Doodads;
    void ApplyDoodad(UPrimitiveComponent* Visual,int32 FadeLevel) const;
};
