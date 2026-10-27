# Original party and raid interface

The original 3.3.5a party frames, raid roster and ready-check UI consume real
server state. The legacy Slate party panel is not added over the stock frames.

Group-list packets retain the group identity, leader, own subgroup, member
flags/roles, loot method/master/threshold, difficulty and LFG metadata.
Initial members request full party stats and name-query metadata. Remote
members and their pets have typed UI snapshots separate from streamed world
entities: leaving visibility never creates a phantom actor or destroys a
member's name, health or resource information. Signed remote coordinates and
the transmitted zone supply group map/minimap positions. The protocol does
not transmit remote pet coordinates or aura durations; these are not invented.

Full and delta party-stat packets validate their masks, bounded aura/pet data,
packed GUID and exact length before replacing the previous snapshot. Roster
validation bounds counts, names and subgroups and rejects duplicate identities.
Leaving the group or logging out clears its snapshots, marks and ready state.
Ordinary party roster refreshes preserve active ready checks and target marks.

Party unit tokens include only the player's raid subgroup; raid tokens include
the complete roster. Pet aliases resolve the actual remote pet. Connection,
visibility and range remain separate properties. Unit identity compares GUIDs,
including aliases and player names. The focus frame follows real streamed or
remote member state, with explicit focus-change events.

Invites, acceptance/decline, leaving/removing members, leadership/assistant
changes, raid conversion, subgroup moves/swaps, main-tank/main-assist assignments
and loot settings use the 12340 protocol and wait for server acknowledgements.
Command failures reach the stock localized error display. Roster selection
follows a GUID across index changes. Role and roster APIs keep their original
3.3.5a return shapes rather than modern-client signatures.

Ready checks use the server initiator, 30-second lifetime and member replies.
Typed stock events update every relevant alias, distinguish completion from
replacement, and close the listener correctly for the initiating player.
XML instances inherit a template's parent unless explicitly overridden, so
raid groups and member buttons move and hide with their owning panel.

Portraits use a live capture when an actor exists and the original temporary
race/sex portrait while it is outside visibility. Hidden portrait owners are not captured.
Addon OnLoad batches use bounded incremental GC between groups of callbacks;
they no longer repeatedly traverse the entire UI heap for every small handler.
Pressure collection allows bounded growth above a retained live heap before
retrying, while preserving the allocator limit and emergency collection reserve.
Top-level inline XML scripts run in document order alongside referenced Lua
files, including the stock raid event/update initializer. Normal/pushed/disabled button art stays below artwork labels, so opaque raid
button backgrounds do not cover the first member-name text region.

Verification covers malformed/truncated roster and stat packets, remote health,
power, aura masks and pets, raid/subgroup API shapes, focus, typed ready events,
identity aliases, template parenting and memory limits. Native verification uses
a disposable second account on an otherwise empty local realm, exercises the
original invitation button, raid controls, ready checks and a 120-yard departure
and return, and captures the rendered interface with Unreal's screenshot API.
The coordinator removes only its own test account/character and verifies/restores
the main character's recorded position, UI, GM mode, health, inventory, money,
level, XP and exploration state.

Loot roll/master-looter windows and the guild, PvP, LFG and achievement panels
remain separate interface-plan tasks.

The final native route passes 55 steps with six inspected, nonempty screenshots,
including ready-check activation through the original roster button. The open
roster's UI median is 0.85 ms and p95 is 2.70 ms, versus 62.64/358.44 ms before
the retained-heap collection fix. First-load work remains visible in the maximum;
it is not represented by the steady median. A second 55-step route checks the
actual XML-installed raid callbacks. An 11-step/three-image solo/ReloadUI route
also verifies multiline colour runs without rendering internal Slate markup.
The broad regression contains 124
passing tests; optional DBC and fixture warnings are recorded in its report.
