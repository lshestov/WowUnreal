#pragma once
#include "CoreMinimal.h"
class FMpqManager;
/** Build 12340 records; assets remain in the user's MPQ archives. */
class WOWDATA_API FPvpUiDbc
{
public:
    struct FBattlefield {uint32 Id=0,Type=0,Group=0,Holiday=0,Min=0,Max=0; bool Groups=false; FString Name; TArray<uint32> Maps;};
    struct FWorldState {uint32 Id=0,Map=0,Area=0,Phase=0,Variable=0,Type=0,Extended[3]={}; FString Icon,Text,Tooltip,DynamicIcon,DynamicTooltip,ExtendedUi;};
    bool Load(FMpqManager& Mpq);
    const TArray<FBattlefield>& Battlefields() const {return BattlefieldRows;}
    const FBattlefield* Battlefield(uint32 Id) const {return BattlefieldRows.FindByPredicate([Id](const auto& B){return B.Id==Id;});}
    const TArray<FWorldState>& WorldStates() const {return WorldRows;}
    FString Description(uint32 Map,bool Alliance) const {const auto* D=Descriptions.Find(Map); return D?(*D)[Alliance?1:0]:FString();}
private:
    TArray<FBattlefield> BattlefieldRows;
    TArray<FWorldState> WorldRows;
    TMap<uint32,TArray<FString>> Descriptions;
};
