# Chat channels

Channel membership comes from complete server notifications. Join requests reserve
a local channel number until acknowledgement or timeout; leave requests retain
membership until the server confirms departure. Channel names, passwords and
moderation arguments are bounded before serialization. Passwords are not logged.

The original channel window uses ChatChannels.dbc definitions, category headers,
collapse preferences, selection, member counts, player names and owner/moderator
flags. Roster snapshots are committed atomically; incomplete or duplicate-member
packets preserve the previous roster. Channel watch updates refresh individual
members and counts. Voice chat is unavailable on this server.

GetChannelList returns number/name pairs. GetChannelName accepts either a local
number or a name. Chat-window channel filters persist independently, and incoming
chat includes the original numeric channel ID/number and full channel name.
Numeric channel sends resolve to confirmed membership. Moderation commands await
server notifications instead of changing roles locally.

Built-in zone channels use the actual streamed area and its DBC parent zone.
General and LocalDefense use reserved ordinals 1 and 3; Trade uses 2 only in
eligible city zones. Membership still requires a real server reply. A server
normalizing the requested name or replacing a zone channel retains its DBC
identity and local ordinal, while clearing the old member list. Explicit leave
preferences prevent automatic rejoining; an explicit join enables the channel
again. The current enUS data uses the standard City suffix for city channels.
Public channel messages are not needed for verification.

Isolated tests cover exact join/leave/list requests, truncated notifications,
roster replacement, member updates, numeric message routing, categories,
selection, collapse/expand, owner checks and independent window filters.
Cursor-anchored menus use viewport-local, bottom-left coordinates with the same
scale as GetEffectiveScale, rather than desktop coordinates.

Verification: the Windows build and 103-test regression pass, followed by focused
cursor/anchor regressions. Native private-channel probes confirm membership,
the single owned participant and owner flags, server chat echo with numeric
channel metadata, and departure. The final 23-step run opens the original owner
menu by right click, validates its viewport bounds and leaves through its actual
Leave button. Four nonempty engine captures show the roster, chat echo, visible
menu and cleared membership. Money and inventory are unchanged.

The area integration passes 108 regression tests and a native 16-step run with
three rendered captures: Northshire subzone text, confirmed General/LocalDefense,
the real General roster, explicit leave without automatic rejoin, explicit
rejoin and UI reload. The test restores the exact original UI preference file,
position, facing and zone; health, money and inventory are verified unchanged.
