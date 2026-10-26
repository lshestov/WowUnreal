#pragma once
#include "CoreMinimal.h"

struct FWowCombatValue
{
    enum class EKind : uint8 { Number,Boolean,String,Localized } Kind=EKind::Number;
    double Number=0;
    FString String;
    static FWowCombatValue N(double V) { FWowCombatValue R; R.Number=V; return R; }
    static FWowCombatValue B(bool V) { auto R=N(V?1:0); R.Kind=EKind::Boolean; return R; }
    static FWowCombatValue S(const FString& V) { FWowCombatValue R; R.Kind=EKind::String; R.String=V; return R; }
    static FWowCombatValue L(const FString& V) { auto R=S(V); R.Kind=EKind::Localized; return R; }
};
struct FWowCombatEntry
{
    uint64 Serial=0,Source=0,Destination=0;
    double Timestamp=0;
    FString Event;
    uint32 Spell=0,School=1;
    uint32 SourceFlags=0,DestinationFlags=0;
    bool FlagsCaptured=false;
    TArray<FWowCombatValue> Values;
};

/** Receipt-time protocol events. A fixed ring retains history across Lua reloads. */
class WOWNETWORK_API FWowCombatHistory
{
public:
    static constexpr int32 Capacity=1024;
    const FWowCombatEntry& Append(FWowCombatEntry Entry)
    {
        Entry.Serial=Next++;
        if(!Entry.Timestamp) { const auto Now=FDateTime::UtcNow(); Entry.Timestamp=Now.ToUnixTimestamp()+Now.GetMillisecond()/1000.; }
        if(Entries.Num()<Capacity) Entries.SetNum(Capacity);
        Entries[(Entry.Serial-1)%Capacity]=MoveTemp(Entry); Count=FMath::Min(Count+1,Capacity);
        return Entries[(Next-2)%Capacity];
    }
    const FWowCombatEntry* Find(uint64 Serial) const { return Serial>=First() && Serial<Next?&Entries[(Serial-1)%Capacity]:nullptr; }
    uint64 First() const { return Next-Count; }
    uint64 End() const { return Next; }
    int32 Num() const { return Count; }
    void Reset() { Entries.Reset(); Count=0; Next=1; }
private:
    TArray<FWowCombatEntry> Entries;
    int32 Count=0;
    uint64 Next=1;
};
