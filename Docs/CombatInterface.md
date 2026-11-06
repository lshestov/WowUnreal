# Combat interface and character statistics

The original FrameXML CastingBarFrame owns cast feedback when available. The
legacy Slate fallback is collapsed in that case, including after UI reloads;
it remains available when the original frame has not loaded. Cast start,
progress, interruption and completion still follow the server-driven stock UI.

GetSpellBonusDamage reads the signed positive and negative player modifiers for
the requested school (1 through 7). GetSpellBonusHealing reads the server's
healing modifier. Neither derives an invented spell-power value from intellect
or spirit. GetManaRegen reads the two server-provided per-second mana rates,
retaining Wrath's rounding epsilon and zero results for other active resources.

The character UI polls bounded stat-field groups at the existing 200ms state
update interval. Changed attributes, resistance/armor, attack speed and damage,
attack power, spell modifiers, ratings and level emit their original events.
Snapshots commit before callbacks, unchanged state stays silent, and character
identity changes clear the previous snapshot. This updates an open character
window after buffs or equipment changes without rewriting its FrameXML.

The API behavior and signed field semantics were checked against the paired
AzerothCore build-12340 update fields and the Wrath client script definitions.
The regression uses distinct spell-school bonuses/penalties, unrelated large
intellect/spirit values, changing fields, both mana rates, invalid schools,
non-mana resources, missing players and actual event argument contracts.

Verified: Editor and packaged builds, 151 passing regression checks (four
existing warning cases), and a 37-step rendered disposable-account route with
six validated and inspected captures. Frost Armor changes armor from 53 to 83
while the character window remains open; zero spell bonuses and actual mana
regeneration survive ReloadUI. The legacy white cast overlay is absent during
both cancelled and completed Fireball casts.

## Cast feedback

Wrath sends SMSG_CAST_FAILED privately and SMSG_SPELL_FAILURE to nearby players
for the same local cancellation. The client pairs those complementary packets
by caster, spell and request counter. It records one combat failure, publishes
one UI error and completes the request once. A new start or outgoing local
request clears the pair; consecutive attempts and counter wrap remain valid.
No timer suppresses later errors. Failure reason 40 publishes
UNIT_SPELLCAST_INTERRUPTED; other rejections publish UNIT_SPELLCAST_FAILED.

The actual byte counter now reaches start, result and stop events, matching
UnitCastingInfo and the stock bar's castID. A late failure cannot clear a newer
cast of the same spell. Successful timed casts publish UNIT_SPELLCAST_SUCCEEDED
for the original macro managers, followed by UNIT_SPELLCAST_STOP for the stock
green flash/fade. Channel updates retain their existing counter. The missing
GetStatusBarColor method returns the rendered UProgressBar's RGBA values.

The regression loads the original CastingBarFrame.lua from MPQ and exercises
nonzero IDs, complementary packets in either arrival order, private rejection
followed by generic broadcast failure, stale results, server completion and
255-to-0 wraparound. A 28-step rendered disposable-account route verifies one
red Interrupted notification/bar, hold/fade, ReloadUI, a subsequent timed cast,
server-confirmed green completion and final disappearance. Its four captures
were validated and inspected; existing characters and inventory are unchanged.

The full original spell-effects renderer is still pending.

This cast-feedback revision passes all 152 regression checks (148 clean and
four pre-existing warning cases), including the original macro sequence suite.

## Melee animation variations

Each new attack selects an authored M2 variation using the original 32-bit
frequency and linked source-sequence indices with a 15-bit random roll. The
selected source variation is mapped to its loaded runtime track, whose index
can differ after filtering. Selection stays within the equipped weapon's
animation family, restarts the swing, and uses that variation's duration.
Repeated polling of an already looping state preserves its variation and
playback time. Missing or malformed chains retain a playable fallback.

The animation regressions cover exact probability boundaries, reordered
runtime tracks, missing clips, malformed/cyclic links, and actual bone poses
from repeated one- and two-handed attacks on the original DwarfMale model.
`Scripts/Verification/43-world-input-offline.json` exercises the shared melee
event handler, mouse release selection, and original corpse cursor textures
in a rendered, disconnected dwarf fixture without accessing character data.

## Next-swing abilities and resource errors

An accepted next-melee-swing spell remains current until its matching server
completion, rejection or cancellation. The original action button's checked
texture shows this queue; accepting Heroic Strike does not start a timed cast
bar or precast hand effects. Spell IDs and cast counters protect a newer queue
from stale replies. Local attack stop, death and session teardown clear it.

NO_POWER failures use the original resource-specific error string from the
spell definition in both the UI and combat log. Rank-one Heroic Strike costs
150 protocol rage units (15 displayed rage); this change does not alter costs
or award resources. Regressions cover the queue lifecycle, controller effects,
paired failure deduplication and resource display/usability at the threshold.
The disconnected rendered route replays server acknowledgements and failures;
it does not establish successful combat against a live server.
