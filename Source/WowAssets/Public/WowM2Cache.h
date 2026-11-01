#pragma once
#include "CoreMinimal.h"
#include "Formats/M2Types.h"

struct FWowM2CacheStats {uint64 Bytes=0, Budget=0, Evictions=0; int32 Entries=0;};

/** Parsed immutable payload LRU. Callers hold their existing cache lock.
 * Eviction releases only cache ownership; active animation/effect/light shared refs remain valid. */
class FWowM2Cache
{
public:
    static constexpr int32 MaxEntries=2048;
    explicit FWowM2Cache(uint64 InBudget=128ull*1024*1024):Budget(InBudget){}
    TSharedPtr<FM2Data>* Find(const FString& Path)
    {auto* E=Entries.Find(Path); if(!E) return nullptr; E->Used=++Clock; return &E->Model;}
    void Add(const FString& Path,const TSharedPtr<FM2Data>& Model)
    {
        if(!Model) return;
        if(auto* E=Entries.Find(Path)){Bytes-=E->Bytes; Entries.Remove(Path);}
        const uint64 Size=Model->GetAllocatedSize()+Path.GetAllocatedSize()+sizeof(FEntry);
        if(Size>Budget){++Evictions; return;}
        FEntry E; E.Model=Model; E.Bytes=Size; E.Used=++Clock; Entries.Add(Path,MoveTemp(E)); Bytes+=Size;
        while(Bytes>Budget || Entries.Num()>MaxEntries)
        {
            FString Oldest; uint64 Age=MAX_uint64;
            for(const auto& P:Entries) if(P.Value.Used<Age){Age=P.Value.Used; Oldest=P.Key;}
            Bytes-=Entries.FindChecked(Oldest).Bytes; Entries.Remove(Oldest); ++Evictions;
        }
    }
    FWowM2CacheStats GetStats() const {return {Bytes,Budget,Evictions,Entries.Num()};}
private:
    struct FEntry {TSharedPtr<FM2Data> Model; uint64 Bytes=0,Used=0;};
    TMap<FString,FEntry> Entries;
    uint64 Bytes=0,Budget=0,Clock=0,Evictions=0;
};
