# World map (build 12340)

The map window uses the original `WorldMapFrame.xml`, `WorldMapFrame.lua`, quest
list, dropdowns, sizing buttons and quest information panels from the user's
MPQs. The earlier custom `IdleWowWorldMapFrame` is removed. M and Escape operate
the original panel; opening the map follows the current zone.

## Data and coordinates

`FMapUiDbc` reads WorldMapArea, WorldMapContinent, WorldMapOverlay,
WorldMapTransforms, DungeonMap, DungeonMapChunk, WMOAreaTable, AreaPOI and Vehicle.
It retains zero-bounds instance/city rows, display-map overrides and dungeon
floor IDs. Continent `.zmp` files are physical-map masks, rather than coordinates
normalized to the cropped continent image. Hit testing first recovers physical
coordinates, then resolves AreaTable parents to a displayed zone.

WorldMapArea raw IDs and Lua display IDs differ: `SetMapByID(30)` selects Elwynn;
`GetCurrentMapAreaID()` then returns 31. Continents follow WorldMapArea row order;
zone dropdown entries follow localized name order. Floor database IDs are
separate from their one-based dropdown indexes. Dalaran retains both floors.

`WowMapUi` shares selection and projection between the player arrow, corpse and
release markers, landmarks, gossip POIs and quest POIs. Zone projections use
their physical coordinates; continent and Azeroth overview projections apply
the DBC transforms, continent placement and scale. Outland is a separate cosmic
selection. Off-map units return `(0, 0)` to Lua. Player positions use actor feet
when available. Only player/party/raid unit tokens are accepted by the player
map-position API.

Explored overlays come from the player's exploration fields and AreaTable
exploration bits. Landmark visibility uses the actual selection, exploration,
world-state conditions and AreaPOI display flags; icon state selects the DBC
variant. Gossip POIs retain the server's actual position and label.

## Quest and death packets

The client requests active quest POIs with `CMSG_QUEST_POI_QUERY` (0x1E3), at most
25 quests. Pending requests have a bounded retry interval; valid empty answers
are cached. `SMSG_QUEST_POI_QUERY_RESPONSE` (0x1E4) is parsed atomically with
bounded quests, blobs and signed points. A malformed/truncated response cannot
replace the previous cache. Removed quests leave the cache, and disconnect
clears map-specific data.

Original quest icons, selection and details consume these POIs. Missing creature
names are requested from the server and refresh the affected quest text when
received. Destination selection excludes inactive objectives and prefers the
actual map, floor, zone and nearest remaining objective. Completed quests
use return objectives; unfinished quests use their remaining objective counters
and carried items. Completion summaries honor their explicit log index and use
the objective text when no separate completion string is supplied or the
auto-complete flag requires it. The QuestPOIFrame implementation draws and hit-tests server
polygons, supports fill/border textures and alpha, smoothing/merging settings,
and preserves raw zero-based objective indexes in mouseover tooltips. The
original quest-icon atlas remains controlled by FrameXML.

`MSG_CORPSE_QUERY` (0x216) and `SMSG_DEATH_RELEASE_LOC` (0x378) populate separate
corpse/entrance and graveyard locations. Living characters have no death markers;
requests are not continuously repeated. Packet regression tests cover these
paths without killing the user's character.

## Rendering and input

Map textures come from MPQ and are applied at most one new region per tick.
Queued regions retain their current Lua texture, UVs and opacity; clearing or
changing a queued region prevents an obsolete image from being applied. The
FrameXML arrow uses the MPQ minimap arrow and normal map anchors. The player ping
uses its original M2 texture. Frame alpha follows the original 0..1 clamp.

Unchanged texture/region size/anchor assignments do not invalidate the whole UI.
Changing a frame scale invalidates geometry lazily while geometry queries still
resolve current values. Parent/child traversal is indexed and kept in sync on
reparenting, so showing a large panel does not repeatedly scan every UI frame.
Scroll-content measurements update their own subtree instead of forcing a full
UI layout for each query; ordinary end-of-tick layout still updates other dirty
branches.
The Show Quest Objectives option defaults to enabled,
and its normal click handler controls the original quest display.

## Verification

`WowUnreal.Hud.WorldMapApi` checks actual MPQ selection, projections, physical
hit masks, floors, quest tooltips, deferred art and cancellation. Network tests
`WowUnreal.UI.MapPoiPackets` check real requests, every truncated response prefix,
empty replies, gossip markers, corpse locations and reset behavior.

The native navigation probe covers Elwynn, Eastern Kingdoms, Azeroth, cosmic
selection, both Dalaran floors and ReloadUI. The quest-area probe temporarily
adds untouched quest 7 without completing/rewarding it. Its persistent guard
removes only that zero-progress fixture after logout, verifies original quests
and rewards, and requires the position/UI guard to restore first. AzerothCore
saves current Unix time for an untimed quest's zero remaining timer; cleanup
checks the template is untimed and verifies actual objective progress instead.
No Blizzard map art is committed to the repository.

Verified on Windows UE 5.7.4: 111 regression tests (109 clean, two existing
warning tests), focused final quest-summary checks, and two native runs totalling
41 steps and nine non-black inspected captures. The native navigation run also
asserts opening below one second, a map update rate above 20 FPS during
navigation, an actual floor-button click and successful ReloadUI.

## Server-selected world entry

Initial terrain now uses the authoritative Map.dbc ID from LOGIN_VERIFY_WORLD,
including the matching loading screen. Previously, a Kalimdor character could
receive the default Azeroth tiles at the same coordinates. With no usable floor,
the pawn remained in the ground-loading hold and its visual movement was never
sent to the server; nearby corpse interaction consequently returned a release.

Selecting another map resets terrain, distant meshes, WDT/WDL, object IDs and
initial-load accounting. Background MPQ reads retain their original generation;
obsolete results are discarded without waiting on the game thread or clearing
pending keys belonging to the new map. Finalization builds at most one terrain
tile per tick, including initial loading. The existing terrain virtual texture
is reused when switching maps.

The server-map test checks Kalimdor/Azeroth selection, unchanged-map reuse and
rejection of unknown IDs. Four final focused map/loot checks pass without
warnings. Native disposable-character verification loads Kalimdor, finds the
ground, sends movement and obtains an actual group-roll reward. The rendered
world, corpse window and roll tooltip captures are nonempty and inspected.

Large WMO cities are streamed instead of being skipped above 100 groups. The
root's MOGI bounds determine a stable nearest-first order; one group is built per
game-thread ticker frame. Smaller WMO starts and doodad spawns are also bounded,
and root group counts are cached. Root actors retain room/portal metadata while
groups arrive. Initial login waits for building collision as well as terrain;
loading cannot place the player on the ADT surface beneath a city. Map changes
cancel pending city jobs, and component-budget accounting uses full group counts.

The real Stormwind root contains 286 groups. The large-WMO test checks its bounds,
unique nearest-first scheduling, initial-collision loading gate and cancellation.
Native verification enters Rebecca Laughlin's shop at the server height, talks
to the vendor, edits and saves a tabard, and captures the loaded interior.

Far transfers use the same collision-gated loading path as initial entry. A
validated NEW_WORLD packet selects the server map before notifying consumers;
old visible creatures, world states, terrain and separately spawned building
actors are removed. Player items, containers and quests remain available. The
client acknowledges entry, suspends the pawn until the destination is ready,
then restores its normal coordinate conversion, facing, capsule and movement.
The UI is retained and receives the normal leaving/entering world events.

Transfer regression tests reject every truncated packet prefix, trailing bytes
and nonfinite coordinates without modifying state. Native verification makes
an Azeroth/Kalimdor round trip, checks real floor collision and map selection,
and compares inventory, equipment, quests, health and money at both destinations.
