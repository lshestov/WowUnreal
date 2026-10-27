# Stock loot, rolls and master distribution

The original 3.3.5a LootFrame.xml/LootFrame.lua drive the interface. No extracted
Blizzard scripts, textures or icons are stored in the repository.

Loot responses are validated atomically, including exact payload size, unique
server slots and supported permissions. The Lua display list retains its money
row and original item positions until a new response opens the window. Removing
one item never renumbers another button. A separate open serial handles a fresh
response for the same corpse even when the UI has not ticked between requests.
Item names, quality and icons come from real item queries and MPQ display data.

Coin/item clicks send the original server slot. Locked rolls cannot be picked
up; the master slot opens the stock candidate menu. Bind-on-pickup confirmation
is tied to the active corpse/slot. Auto-loot uses the saved setting and Shift
toggle, waits for metadata, sends each available row once and waits for removal
or release acknowledgements. Closing a corpse leaves its group rolls active.

Rolls use small public Lua IDs while retaining full 64-bit protocol GUIDs.
Eligibility masks control Need/Greed/Disenchant, timers use server countdowns in
milliseconds, and typed start/cancel events control the four stock frames.
Votes, winners and all-passed results validate their complete item identity.
AzerothCore's empty-GUID votes are matched only to an unambiguous active drop;
its special zero/zero Need selection and reversed all-passed property/suffix
order are handled explicitly. Roll IDs and event sequences survive session
resets monotonically so old UI bookkeeping cannot suppress a later roll.

The master list is associated with the pending loot request because the server
sends it before the loot response. Candidate names use real group/name state.
Distribution requires the acknowledged master role, current corpse, master
slot and server-provided recipient, and sends the fixed-width original packet.

Actual item and currency notifications produce localized, linked messages with
the complete twelve-argument chat signature. Stack awards use the received
count, rather than total inventory count; money uses the confirmed personal
share. The General window enables Loot/Money by default. Empty GM history uses
an empty string, preventing a spurious previous-conversation window for [0].

Corpses stop at their current authoritative position, accept a final server
position correction and remain until server removal. The former local minute
timer could hide valid loot or remove a resurrected model; retaining models
after server removal could leave unlootable ghosts. Both paths are removed.
Target frames clear when a removed unit has no remaining remote group state.

Verification covers truncated/trailing payloads, duplicate slots/candidates,
signed random-property links, money rows, confirmed awards, roll eligibility,
monotonic IDs, typed stock events, repeated opens and corpse model lifetime.
Native scenarios use disposable owned accounts and compare all pre-existing
character/inventory records after cleanup. They exercise real server rewards,
stock roll buttons, the recipient menu, visible chat, coins and auto-loot;
captures use Unreal's screenshot API and are checked for content and inspected.

Windows UE 5.7.4 verification: 129 regression checks pass (125 clean and four
pre-existing warning cases, including the additionally selected ADT test).
Five final focused map/corpse/loot checks pass without warnings. The group
scenario completes 126 steps and eight captures, including Need versus Greed,
all-passed pickup and clicking the actual master-recipient entry. The separate
coin/auto-loot scenario completes 39 steps and three captures. All eleven PNGs
contain rendered content and are inspected. QA accounts/characters are deleted;
all pre-existing character and inventory rows remain unchanged. The group
route's final UI profile has a 0.83 ms median and 2.84 ms p95.
