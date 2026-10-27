# Guild, inspect and guild-tabard UI

The original build-12340 FrameXML is loaded from MPQ. The client supplies real
guild metadata, member status, rank permissions, notes and protocol actions to
the guild roster, information, event-log and rank-control panels.

Guild query, roster, invite, result, permissions and event-log packets are
validated before replacing live state. Rank names remain available while a
refresh is pending. Selection follows a member GUID across sorting and offline
filtering. Officer notes and editing controls respect the local server rank.
Rank edits stay in a draft until an authoritative roster/query update arrives.
MOTD changes and system notifications are delivered once with complete chat
arguments. Guild-bank rank limits and remaining member allowances stay separate.
Bank tab labels are taken from validated bank metadata when it is available.

`Blizzard_InspectUI` uses a separate GUID-scoped cache for both talent groups,
glyph IDs, item/enchantment/random-property data, honor statistics and arena
teams. Late packets for a previous target cannot overwrite the current view.
Inspection APIs preserve local-player data. Equipment tooltips and UI character
models use the inspected player's actual items. Shared, unqualified FrameXML
includes are resolved after addon-local resources.

The tabard editor uses original MPQ background/emblem/border layers on an isolated
character preview. Cycling any of the five settings changes that preview without
changing inventory or guild state. Save requires the real vendor, guild leader,
changed design and sufficient money. The request disables repeated submission;
only the server result commits the submitted design. Further preview changes
during a pending request remain unsaved. The server's five saved indices are
recovered on reopening. An equipped guild tabard uses those layers in the world
and the character window. The cost is the 12340 server's 100,000 copper.

`SMSG_GOSSIP_COMPLETE` closes the prior conversation before another NPC panel
opens. Structural FrameXML containers can paint externally anchored regions and
their controls; references between a menu's own regions do not position a hidden
or unanchored menu.

Verification uses disposable accounts. A Mage inspects a separate Warrior while
the guild route checks invitations, notes/MOTD/info, promotion/demotion, rank
creation/deletion, permissions, event log and disbanding. The tabard route clicks
the five original arrow buttons, verifies the 10-gold debit, reopens the saved
design and equips the real tabard. Screenshots come from Unreal's native API.
The QA lifecycle can disband only a token-owned guild containing its own temporary
characters before removing their accounts; existing characters stay unchanged.
