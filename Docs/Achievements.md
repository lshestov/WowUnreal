# Achievements

The original 3.3.5a achievement window reads its catalog, categories, criteria,
names, descriptions, points, rewards and SpellIcon references from the user's
Achievement DBC files. No Blizzard data files are committed or redistributed.
The bridge implements the original eleven-value achievement tuple and the
criteria tuple used by Blizzard_AchievementUI and WatchFrame.

Own achievements and inspected-player achievements use separate snapshots.
Full/inspect snapshots, earned achievements, criteria updates and deletions are
decoded from the AzerothCore 3.3.5 wire format. Packed GUIDs and 64-bit counters,
sentinels, dates, owners and message boundaries are validated before a snapshot
is replaced. Incomplete data and late replies for another comparison target
cannot overwrite current state. Session changes clear both snapshots.

The UI supplies faction/category filtering, progressive ranks, completed and
incomplete rows, aggregate achievement/statistic pages, dates, total points,
criteria counters and achievement links/tooltips. Statistic values come from
server counters, including sum/max and average counters. Character level does
not substitute for missing server progress.

Up to ten valid, incomplete achievements can be watched. WatchFrame receives
tracking/progress changes; a completed achievement leaves the tracker. Tracking
is registered as per-character SavedVariables and restored for that character.
Changing player identity also clears the old in-memory SavedVariables table,
so a character without a saved tracking file cannot inherit another's list.
Completion notifications carry the parsed numeric achievement ID. Nearby
players' notifications cannot award achievements to the local player.
Achievement chat retains its appended achievement ID, selects the original
achievement chat event and replaces `$a` with the actual achievement link;
the stock chat formatter supplies the player name.

Comparison sends the packed player GUID required by the server and waits for
the inspected snapshot before notifying the original comparison window. Its
points, dates and statistics never mutate the local player's achievements.

Sources: the user's unmodified Blizzard_AchievementUI/WatchFrame/ChatFrame Lua
and XML, and AzerothCore AchievementMgr, MiscHandler and achievement commands.

## Verification

The packaged Windows client passes the 146-check interface/world regression:
142 checks without warnings and the same four previously recorded optional-DBC
and fixture warning cases. Packet tests cover every truncated snapshot/update
prefix, trailing data, packed high counter bytes, duplicate/nearby completions,
late comparison replies, deletion and reset. Lua checks cover original tuples,
catalog categories, points/dates/links, authoritative criteria, comparison
isolation, tracking and clearing a previous character's in-memory watch list.

Rendered disposable-account routes pass 84 steps at 1280x720 and 89 steps at
1920x1080. Their 23 captures are validated and visually inspected: real earned
notifications/chat, original summary, selection and criteria, tracking,
ReloadUI restoration, automatic completion removal, completed/incomplete
filters, statistics, inspected-player comparison and an original ItemRef
tooltip opened from the actual achievement chat event.

AzerothCore refuses achievement gains in GM mode. The owned test character
disables that mode before requesting completions and criteria checks; normal
server packets drive the UI. Both disposable accounts are removed, and the
verification wrapper checks that existing characters/inventories are unchanged.
