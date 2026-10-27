# Original PvP interface

The build 12340 FrameXML supplies the PvP/honor tabs, battleground browser,
queue menus, battlemaster dialog, arena team details and scoreboard. The bridge
reads BattlemasterList, Map introductions and WorldStateUI from the user's MPQ.
The 12340 field layouts follow AzerothCore and the
[WorldStateUI schema](https://github.com/wowdev/WoWDBDefs/blob/master/definitions/WorldStateUI.dbd).
No Blizzard data files are committed.

## Authoritative state

Three bounded queue slots retain their actual type, arena size, bracket,
instance, rated flag and receipt clock. Queued elapsed time, invitation deadline
and active match duration are distinct protocol fields. Join, invite acceptance,
queue cancellation and active-match exit send their corresponding requests;
the UI waits for the server status instead of changing membership locally.
NPC replies replace the browser's previous selection. Closing a browser does
not cancel a queue. Instance lists and random/holiday rewards use server replies.

Honor today/yesterday/lifetime, honor currency and arena currency read actual
player fields. Season availability follows world states. The obsolete ranked
honor-progress bar has no progression in this client version.

Battleground and arena scoreboard layouts decode separately, including winner
zero, objective columns, damage/healing and arena rating changes. Score rows
resolve names, race and class through player identities; faction filtering does
not rewrite the original snapshot. World-state descriptions, icons, capture
bars and stat headings come from WorldStateUI. Flag/player positions use actual
server world coordinates and the selected map transform.

Arena teams use query, stats and roster packets, actual captain flags, weekly
and seasonal counters and stable roster selection. Invitations, removal,
captain changes, leave and disband requests use the server protocol. Member
events request replacement metadata and roster, so stock windows update without
being reopened. Visible arena opponents and their pets resolve through real
unit tokens and receive health/resource/aura events.

Wintergrasp queue and entry invitations retain the actual battle ID, zone and
deadline. Queue responses and exits publish typed arguments to the original
popups. The wait timer reads the server world-state clock.

## Verification scope

Native disposable-account routes verify seven inspected images over 40 steps:
honor/currency values, original tabs, rewards and descriptions, physical list
selection, server-confirmed Warsong queue, the minimap's actual Leave Queue item
and ReloadUI. A second 47-step route verifies five inspected images: creation of
an owned arena team, stock invitation of an owned Warrior, automatic roster
updates, seasonal statistics, actual NPC gossip, the skirmish queue, cancellation
and disband. Recovery can disband only the recorded QA team after verifying all
members belong to that fixture. Existing characters and inventories are hashed
and remain unchanged.

Packet tests reject every truncated queue/scoreboard prefix, trailing bytes,
duplicate/oversized entries and late session state. UI tests cover real honor
tuples, winner/faction semantics, anonymous parentKey descendants, independent
click/wheel routing and changing arena health. Complete rendered matches,
opponent visibility transitions and final scoreboards still need a native
multiplayer route; opening the windows alone does not verify those cases.
