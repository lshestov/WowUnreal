#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "WowQaCharacterCommandlet.generated.h"

/** Creates/deletes only an isolated, coordinator-owned QA character, without entering the world. */
UCLASS()
class UWowQaCharacterCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    virtual int32 Main(const FString& Params) override;
};
