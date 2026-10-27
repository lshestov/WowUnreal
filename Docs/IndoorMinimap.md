# WMO rooms and indoor minimaps (build 12340)

Each rendered building owns a `UWowWmoAreaComponent` beside its existing
collision. The component retains MOHD root identity, the placement's name set,
MOGP group identity and bounds, and the MOPV/MOPT/MOPR room portal graph.
It holds references to existing collision components, without duplicating
meshes. Unloading a building also removes its room metadata.

Location resolves the nearby physical floor and its owning WMO group. A broad
building box alone cannot turn outdoor terrain into a room. WMOAreaTable maps
the actual root/name-set/group tuple to area IDs, indoor/outdoor overrides and
localized building/room names. World-map floor IDs come from DungeonMapChunk
using the current map, group identity and world height. Current-zone selection
follows that floor, while explicit map and floor selections remain independent.
GetRealZoneText retains the geographical zone used by built-in chat channels;
display zone/subzone/minimap text follows the original WMO name precedence.

The indoor renderer follows the build-12340 `CMapObj::QueryMapObjMinimap`
algorithm: traverse eligible room portals inside the query volume, honor the
portal depth CVar and exterior/alternate-interior flags, and draw the current
room last. Single exterior maps use the root bounds and the root group index.
Group files use logical indices, distinct from their WMOAreaTable IDs.

Tiles resolve through the original md5 translation index using the WMO path,
group index and tile X/Y. Model space has half-yard texels, power-of-two tile
dimensions from 32 to 256 pixels, a 128-yard maximum tile step and the original
two-texel outer border. The renderer transforms textured quads through the
placed building, then applies the player's position and minimap rotation.
At most one texture is uploaded per tick, using the existing bounded cache.

Indoor/outdoor zoom CVars are separate. Entering or leaving a building updates
the zoom controls and indoor/zone events. ReloadUI resolves the current room
again and restores its zoom. Indoor object tracking stays within the current
building's interior groups; loaded group members retain their separate marker
path.

References include the local build-12340 WorldClient/MapObj, MapEntity,
AreaList and UI/MinimapFrame implementations. The independent
[World of MapCraft investigation](https://www.iamcal.com/world-of-mapcraft/)
also describes the WMO group-to-image naming and bounds relationship.

`WowUnreal.Collision.WmoRoomsAndMinimap` reads the real Northshire Abbey
geometry, portal graph, names and tile dimensions, and checks collision-based
room ownership after physics cooking. `WowUnreal.Hud.InteriorAreas` checks name
precedence, geographic zone retention, leaving a building and automatic
Dalaran floor/player projection. Native verification uses already-explored
abbey rooms and checks that level, XP and explored-zone bits are unchanged.
