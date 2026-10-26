# Combat log

The original Blizzard combat-log addon receives actual parsed server events.
The bridge emits the 3.3.5a eight-field prefix (timestamp, event, source GUID,
source name, source flags, destination GUID, destination name, destination
flags), followed by typed spell and event-specific fields. GUIDs remain strings
so Lua numbers cannot lose their high bits. Flags use the shared unit
relationship, ownership and group state; unavailable names remain nil.

Supported inputs include attacker-state updates, non-melee/ranged/split/shield
damage, healing, environmental damage, casts/rejections, periodic damage/healing,
energize/drain/leech, dispels/steals/breaks and failed dispels, interrupts, extra
attacks, summons/creation/resurrection, durability damage, enchantments, party
kills and instant kills. Destructible-building damage/healing use signed damage.
Aura changes distinguish apply/remove/refresh and stack dose changes; initial
all-aura snapshots do not invent application events. Health transitions classify
normal death, gas-cloud dissipation and destroyed totems using real server type
and SummonProperties metadata. Melee parsing
retains overkill, multiple damage schools, optional absorb/resist arrays,
victim state, blocked damage and the correct critical-hit flag. Truncated
payloads do not create entries. Multi-effect logs and all-aura updates commit
only after the entire record set validates. Spell-go hit/miss arrays contain
full 64-bit GUIDs; their explicit target block uses packed GUIDs. Failure tokens
resolve through the original localized FrameXML strings. Rage, runic power and
pet happiness retain their original display-unit conversion. Timestamps record
client receipt time; object affiliation/control flags are captured at receipt.

A fixed 1,024-entry ring belongs to the network session. History therefore
survives Lua/UI reloads and clears on logout or disconnect. Filters combine
comma-separated event names with optional source/destination GUIDs or composite
object masks, spell ID or localized spell name. Incomplete numeric masks report
the original API error. Individual filters are ORed; their conditions are ANDed. An empty
event list matches nothing. The filtered cursor starts at the newest entry
when set to zero, supports reverse indices and unfiltered cursor traversal, and
returns nil after the end, allowing the original
distributed refilter loop to finish.

COMBAT_LOG_EVENT_UNFILTERED delivers every supported entry; COMBAT_LOG_EVENT
honors the active filters. CombatLogGetCurrentEntry, CombatLogGetNumEntries,
CombatLogSetCurrentEntry, CombatLogAdvanceEntry, CombatLogAddFilter,
CombatLogResetFilter and CombatLogClearEntries operate on real history.
CombatLogGetCurrentEventInfo also exposes the same Wrath tuple.

Native verification uses a harmless server GM self-heal with no inventory or
learned-spell changes. It checks receipt, filtering, tab/button interaction,
visible formatted text, ReloadUI history restoration and cleanup. Editor tests
cover multi-school packets, spell damage/healing suffix types, truncation,
filter traversal, empty history and ring eviction.

The original formatter, quick filters and spell hyperlinks receive the same
typed tuples as addon listeners. Isolated tests cover multi-effect truncation,
periodic fields, resource conversion, interrupts, dispels/steals/breaks,
shield/kill variants, exact cast targets, localized failures, historical flags,
spell filters, cursor boundaries and transactional aura updates. Unknown record
types with unspecified lengths are rejected rather than interpreted as events.

The 104-test regression passes with the two existing warning tests. Additional
focused cases verify building suffixes, totem/gas-cloud death and enchantment
metadata. A native 22-step run verifies actual Renew application, periodic heal,
refresh and removal, an item-reference spell link, a rejected self-targeted
Heroic Strike and retained history after ReloadUI. Temporary Renew is removed;
health, money and inventory are preserved. Named DBC description variables expand
before arithmetic, so the original Renew reference displays its total healing.
