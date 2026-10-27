#pragma once
#include "CoreMinimal.h"
class FMpqManager;
class WOWDATA_API FAchievementUiDbc
{
public:
    struct FAchievement
    {
        uint32 Id=0,Previous=0,Category=0,Points=0,Order=0,Flags=0,Icon=0,Count=0,Reference=0;
        int32 Faction=-1,Map=-1; FString Name,Description,Reward;
    };
    struct FCategory {uint32 Id=0,Order=0; int32 Parent=-1; FString Name;};
    struct FCriteria {uint32 Id=0,Achievement=0,Type=0,Asset=0,Quantity=0,Flags=0,TimedType=0,TimedAsset=0,Duration=0,Order=0; FString Name;};
    bool Load(FMpqManager& Mpq);
    const FAchievement* Achievement(uint32 Id) const {return AchievementRows.Find(Id);}
    const FCategory* Category(uint32 Id) const {return CategoryRows.Find(Id);}
    const FCriteria* Criterion(uint32 Id) const {return CriteriaRows.Find(Id);}
    const TMap<uint32,FAchievement>& Achievements() const {return AchievementRows;}
    const TMap<uint32,FCategory>& Categories() const {return CategoryRows;}
    const TMap<uint32,FCriteria>& Criteria() const {return CriteriaRows;}
private:
    TMap<uint32,FAchievement> AchievementRows;
    TMap<uint32,FCategory> CategoryRows;
    TMap<uint32,FCriteria> CriteriaRows;
};
