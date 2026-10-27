# Actual zones and subzones

WorldToChunk uses the same centred ADT conversion as terrain rendering. The
world manager resolves the loaded chunk under the player, rather than the
camera or a clamped chunk calculated without the map-centre offset. Zone audio
uses the same calculation. The UI receives this resolver through a callback,
keeping the UI module independent of the world module. ReloadUI retains it.

Before the relevant terrain is available, a complete INIT_WORLD_STATES packet
provides the server area for the current map. Invalid or incomplete packets
cannot replace that fallback or the existing world-state snapshot. Areas from
another map are ignored. GetZoneText/GetRealZoneText use the actual parent zone;
GetSubZoneText and GetMinimapZoneText expose the actual subzone. Unavailable
positions return empty names rather than a fixed Elwynn fallback. PvP territory
uses DBC area flags, faction masks and the selected realm type. Flight conditions
use real area flags, including Dalaran's restriction.

Actual transitions emit zone/minimap events and a native zone update request.
World-state counter packets emit UPDATE_WORLD_STATES instead of manufacturing
zone changes. Standard zone-channel selection uses the same resolved zone.

Tests cover offset chunk coordinates across multiple tiles, Northshire server
fallback, loaded Stormwind/Durotar zones, mismatched maps, unavailable areas,
Outland flight and Dalaran restrictions. Native captures verify Northshire text
and standard channels before and after a UI reload. WMO interior area overrides
and interior minimap selection are separate map-renderer work.
