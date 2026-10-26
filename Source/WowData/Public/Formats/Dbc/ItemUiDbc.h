#pragma once
#include "CoreMinimal.h"
class FMpqManager;

/** Client-side item labels, vendor token costs and durability prices from 12340 DBCs. */
class WOWDATA_API FItemUiDbc
{
public:
    struct FExtendedCost { uint32 Honor=0,Arena=0,Bracket=0,Items[5]={},Counts[5]={},Rating=0,Group=0; };
    bool Load(FMpqManager& Mpq);
    const FExtendedCost* ExtendedCost(uint32 Id) const { return ExtendedCosts.Find(Id); }
    const FString* ClassName(uint32 Class) const { return Classes.Find(Class); }
    const FString* SubclassName(uint32 Class,uint32 Subclass) const { return Subclasses.Find((uint64(Class)<<32)|Subclass); }
    uint32 FactionForTemplate(uint32 Id) const { return FactionTemplates.FindRef(Id); }
    uint32 BankBagPrice(uint32 Slot) const { return BankBagPrices.FindRef(Slot); }
    uint32 RepairCost(uint32 Level,uint32 Quality,uint32 Class,uint32 Subclass,uint32 Lost,float Discount=1) const;
private:
    TMap<uint32,FExtendedCost> ExtendedCosts;
    TMap<uint32,FString> Classes;
    TMap<uint64,FString> Subclasses;
    TMap<uint32,TArray<uint32>> DurabilityCosts;
    TMap<uint32,float> DurabilityQuality;
    TMap<uint32,uint32> FactionTemplates,BankBagPrices;
};
