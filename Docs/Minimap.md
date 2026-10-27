# Minimap and tracking (build 12340)

The original MPQ Minimap.xml/lua owns the dropdown, tracking button, zoom
buttons, compass and animated ping. GetTrackingInfo returns the original four
values: localized name, icon, active `1`/nil, and `spell`/`other` category.
Learned tracking auras (44, 45 and 151) sort before the fifteen service/object
types; class masks filter ammunition, poisons and stables. One common selection
is saved in the character's `minimapTrackedInfo` CVar. Tracking spells are cast
normally and become active through the server aura update. Common selections
cancel an active tracking aura with CMSG_CANCEL_AURA.

Outdoor zoom uses the original six world radii (233.33333 through 66.66667
yards). The map streams one MPQ tile per frame and keeps a bounded texture
cache. Rotation transforms the tiles and markers together; the player arrow
and rotating compass use the original textures. Player/blip texture setters
and player texture dimensions retain addon changes.

Markers use the original 8-by-2 ObjectIcons atlas and actual nearby entities.
Service markers depend on NPC flags and faction-template reactions. Resource
markers match PLAYER_TRACK_RESOURCES against the gameobject's Lock.dbc skill
slots; mailboxes use their object type. Names and lock data come from the
normal CMSG_GAMEOBJECT_QUERY response. Creature tracking respects type masks,
dead/ghost player state and untrackable unit flags. Loaded party/raid members
and controlled pets have distinct dots. Hovering a dot shows its cached name.

Quest dots come from SMSG_QUESTGIVER_STATUS and the multiple-status response,
including available, complete and repeatable statuses. Low-level available
quests require the corresponding tracking selection. Status queries are
read-only, rate limited and refreshed when visible givers or quest fields
change. Incomplete/invalid lists cannot partially replace the cache.

Minimap pings carry absolute world X/Y coordinates in MSG_MINIMAP_PING. A
local ping always animates; a packet is sent only when grouped. Received pings
must belong to the player or a current group member. GetPingPosition and the
original MINIMAP_PING event expose normalized UI coordinates; the position
updates as the player moves, rotates or changes zoom.

`WowUnreal.Hud.MinimapTracking` checks the real MPQ definitions, class filtering,
selection, aura acknowledgement, radii, markers, rotation and local ping.
`WowUnreal.Hud.MinimapPackets` checks every truncated packet prefix, atomic
replacement, empty/negative query responses and foreign ping rejection.

Interior WMO tile/area/floor resolution and remote group member positions are
separate tasks; outdoor tracking tests do not establish those features.
