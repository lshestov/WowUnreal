#pragma once
#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "WowPcmSoundWave.generated.h"

/** An independent playback cursor over immutable, runtime decoded PCM. */
UCLASS()
class WOWWORLD_API UWowPcmSoundWave : public USoundWaveProcedural
{
    GENERATED_BODY()
public:
    TSharedPtr<const TArray<uint8>,ESPMode::ThreadSafe> Pcm;
    bool bRepeat=false;
    virtual int32 GeneratePCMData(uint8* Destination,int32 SamplesNeeded) override;
private:
    int32 Offset=0;
};
