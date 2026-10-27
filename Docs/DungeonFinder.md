# Original dungeon finder

The build 12340 LFG/LFD FrameXML reads its catalog from LFGDungeons and
LFGDungeonGroup in the user's MPQ. No dungeon names, level brackets, reward
items or Blizzard assets are copied into the repository. The catalog bridge
keeps the original fourteen-field tuples, negative category IDs, ordering,
collapse state and in-place table arguments.

Player and party lock replies replace complete snapshots atomically. The
browser combines authoritative restrictions with the original level bounds;
party members without a received lock snapshot remain unavailable. Random
dungeon choices and rewards use actual server replies. Reward item names,
icons, hyperlinks and tooltips resolve through item queries and MPQ data.
Deserter and random-cooldown displays read actual auras and expiry times.

Roles are limited by the player's class. Find Group sends the selected roles,
typed dungeon entries and comment; Leave Queue sends a cancellation. Queue
membership changes only on server updates. Wait estimates, role needs and
elapsed time use the queue-status reply and its receipt clock. Opening the
window requests fresh player/party restrictions, and periodic refreshes keep
the original minimap queue display synchronized. Join failures use the
original localized error strings.

Packet handlers reject truncated, oversized, duplicate and trailing data before
publishing a revision. Connection reset clears all previous character data.
Automation checks cover packet atomicity, reward/lock replacement, catalog
tuples, class roles, server restrictions and the original table contracts.

Role checks, proposals, dungeon transfers, vote kicks and raid-browser search
results require additional verification; a working browser does not establish
that those multiplayer routes are complete.

## Dungeon map prerequisite

WDT files flagged as global WMO maps provide one MWMO name and MODF placement,
rather than ADT terrain tiles. The loader validates both chunks, retains the
doodad/name set and scale, and maps a zero MODF origin to the map center as in
AzerothCore's vmap MapObject::Extract. The existing WMO renderer supplies the
same materials, rooms and collision as outdoor buildings.

Model groups load one per frame, starting near the destination. The world-entry
gate waits for the actual model and all scheduled groups; no ADT tiles are
invented for these maps. Repeated requests reuse the model. Transfers and world
shutdown destroy it and discard pending jobs.

Verified in a rendered disposable-account route: Stockades (26 groups),
Ragefire Chasm (12 groups), valid walkable floors and return to Azeroth. The
parser/lifecycle check also covers truncated WDT files, incomplete trailing
chunks, the unchanged Deadmines terrain path and old-model cleanup.
