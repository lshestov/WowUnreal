#include "Formats/Dbc/CharStartOutfitDbc.h"

DEFINE_LOG_CATEGORY_STATIC(LogCharStartOutfitDbc, Log, All);

bool FCharStartOutfitDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid()) return false;

    Entries.Reset();
    IdIndex.Reset();
    RaceClassGenderIndex.Reset();
    Entries.Reserve(Parser.GetRecordCount());

    for (uint32 i = 0; i < Parser.GetRecordCount(); ++i)
    {
        FCharStartOutfitDbcEntry E;
        E.ID = Parser.GetUInt(i, 0);

        // 3.3.5a: four packed bytes, then 24 item IDs, display IDs and inventory types.
        if (Parser.GetRecordSize() != 296) return false;
        const uint32 Packed = Parser.GetUInt(i, 1);
        E.RaceID = Packed & 0xff;
        E.ClassID = (Packed >> 8) & 0xff;
        E.GenderID = (Packed >> 16) & 0xff;
        E.OutfitID = Packed >> 24;
        for (int32 j = 0; j < 24; ++j)
        {
            E.ItemIds[j] = static_cast<int32>(Parser.GetUInt(i, 2 + j));
            E.ItemDisplayIds[j] = static_cast<int32>(Parser.GetUInt(i, 26 + j));
            E.ItemInventorySlots[j] = static_cast<int32>(Parser.GetUInt(i, 50 + j));
        }

        IdIndex.Add(E.ID, Entries.Num());

        // Create lookup key for race/class/gender combination
        FString RCGKey = FString::Printf(TEXT("%d_%d_%d"), E.RaceID, E.ClassID, E.GenderID);
        if (E.OutfitID == 0) RaceClassGenderIndex.Add(RCGKey, Entries.Num());

        Entries.Add(MoveTemp(E));
    }

    UE_LOG(LogCharStartOutfitDbc, Log, TEXT("Loaded CharStartOutfit.dbc: %d records"), Entries.Num());

    // Debug: Log first few entries to verify parsing
    for (int32 i = 0; i < FMath::Min(3, Entries.Num()); ++i)
    {
        const FCharStartOutfitDbcEntry& E = Entries[i];
        int32 ItemCount = 0;
        for (int32 j = 0; j < 24; ++j)
        {
            if (E.ItemDisplayIds[j] > 0) ItemCount++;
        }
        UE_LOG(LogCharStartOutfitDbc, Log, TEXT("  CharStartOutfit[%d]: ID=%d Race=%d Class=%d Gender=%d Items=%d"),
            i, E.ID, E.RaceID, E.ClassID, E.GenderID, ItemCount);
    }

    return true;
}

const FCharStartOutfitDbcEntry* FCharStartOutfitDbc::GetById(uint32 ID) const
{
    const int32* Idx = IdIndex.Find(ID);
    return Idx ? &Entries[*Idx] : nullptr;
}

const FCharStartOutfitDbcEntry* FCharStartOutfitDbc::GetByRaceClassGender(uint32 Race, uint32 Class, uint32 Gender) const
{
    FString RCGKey = FString::Printf(TEXT("%d_%d_%d"), Race, Class, Gender);
    const int32* Idx = RaceClassGenderIndex.Find(RCGKey);
    return Idx ? &Entries[*Idx] : nullptr;
}