#include "WowPcmSoundWave.h"

int32 UWowPcmSoundWave::GeneratePCMData(uint8* Destination,int32 SamplesNeeded)
{
    if(!Pcm || Pcm->IsEmpty() || SamplesNeeded<=0) return 0;
    const int32 Wanted=SamplesNeeded*sizeof(int16);
    int32 Written=0;
    while(Written<Wanted)
    {
        const int32 Count=FMath::Min(Wanted-Written,Pcm->Num()-Offset);
        if(Count>0) FMemory::Memcpy(Destination+Written,Pcm->GetData()+Offset,Count);
        Offset+=Count; Written+=Count;
        if(Offset==Pcm->Num()) { if(bRepeat) Offset=0; else break; }
    }
    // The mixer requests a complete render quantum even across the end of a
    // finite sound. Returning zero makes it repeatedly request the same buffer.
    if(Written<Wanted) FMemory::Memzero(Destination+Written,Wanted-Written);
    return Wanted;
}
