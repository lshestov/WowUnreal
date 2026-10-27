# Unit reactions and target colours

Unit-frame relationship APIs, combat-log allegiance and minimap unit colours
share the 12340 faction/reputation resolver. A faction-template number is an
identifier, not an ordering of hostility.

The resolver follows template enemy masks and explicit enemies, then forward
and reverse friend masks/lists, and finally the default-hostile flag. Self,
controller ownership, group membership, duel teams and free-for-all PvP are
handled before the ordinary faction rules. Unknown metadata remains neutral.

For the active player, attitude toward a reputation faction uses its At War
flag. The faction's attitude toward the player uses server standing plus the
racial/class base from Faction.dbc, preserving all eight reputation ranks.
Ignore-reputation flags, contested guards and server-forced faction reactions
retain their distinct priorities. A friendly self reaction is 5, not Exalted.

`SMSG_SET_FORCED_REACTIONS` (0x2A5) replaces the complete override list. Its count,
exact length, faction identities and zero-based wire ranks are validated before
the previous cache is changed. Truncation, duplicate factions, invalid ranks
and trailing bytes leave the previous state intact. Empty lists clear it;
logout clears character-specific overrides.

Attack eligibility additionally respects nonattackable/unselectable units,
PC/NPC immunity, sanctuary, PvP eligibility and duel teams. Neutral creatures
remain valid attack targets. This distinction is also used by macro conditions.
Reaction changes refresh existing player/target/pet/group frames with typed
`UNIT_FACTION`/`UNIT_FLAGS` events without requiring the user to select again.

The original `UnitSelectionColor` function uses those relationships rather than
assuming every NPC is red. It returns green friendly NPCs, yellow neutral NPCs,
orange unfriendly reactions and red hostile reactions. Controlled player/pet
colours also distinguish one-way/mutual PvP eligibility and unflagged blue units.
Original party/raid player-or-pet membership APIs return numeric 1 or nil, so
target dropdown initialization can follow the native friendly-unit path.

Protocol and reaction logic were checked against the local AzerothCore
ReputationMgr and the original 12340 Unit_C faction/reaction/attack routines.
Tests cover actual Stormwind and Northshire wolf templates, directed standing,
At War, forced overrides, immunity, opposing PvP, sanctuary, FFA, groups,
duels, pets, UI refresh and malformed override packets.
The final native 14-step route asserts the actual FrameXML name-background tint
and inspects three Unreal screenshots, including ReloadUI. Original character
position, UI, health, inventory, money, level, XP and exploration are preserved.

This implements unit relationship APIs; the full PvP queue and battleground
windows are separate tasks in the interface plan.
