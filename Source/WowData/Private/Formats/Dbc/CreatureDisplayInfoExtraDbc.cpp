#include "Formats/Dbc/CreatureDisplayInfoExtraDbc.h"

bool FCreatureDisplayInfoExtraDbc::Load(const FDbcParser& Parser)
{
    if (!Parser.IsValid() || Parser.GetFieldCount() < 21) return false;
    Entries.Reset();
    for (uint32 Row = 0; Row < Parser.GetRecordCount(); ++Row)
    {
        FCreatureDisplayInfoExtraDbcEntry Entry;
        Entry.ID = Parser.GetUInt(Row, 0);
        Entry.Race = Parser.GetUInt(Row, 1); Entry.Gender = Parser.GetUInt(Row, 2);
        Entry.Skin = Parser.GetUInt(Row, 3); Entry.Face = Parser.GetUInt(Row, 4);
        Entry.HairStyle = Parser.GetUInt(Row, 5); Entry.HairColor = Parser.GetUInt(Row, 6);
        Entry.FacialHair = Parser.GetUInt(Row, 7);
        for (int32 Slot = 0; Slot < 11; ++Slot) Entry.ItemDisplay[Slot] = Parser.GetUInt(Row, 8 + Slot);
        Entry.Flags = Parser.GetUInt(Row, 19);
        Entry.BakedTexture = Parser.GetString(Row, 20);
        Entries.Add(Entry.ID, MoveTemp(Entry));
    }
    return true;
}
