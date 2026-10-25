# Combat log

The original Blizzard combat-log addon receives actual parsed server events.
The bridge emits the 3.3.5a eight-field prefix (timestamp, event, source GUID,
source name, source flags, destination GUID, destination name, destination
flags), followed by typed spell and event-specific fields. GUIDs remain strings
so Lua numbers cannot lose their high bits. Flags use the shared unit
relationship, ownership and group state; unavailable names remain nil.

Supported inputs are attacker-state updates (SWING_DAMAGE and SWING_MISSED),
spell non-melee damage (SPELL_DAMAGE), spell healing (SPELL_HEAL), environmental
damage and health transitions from alive to dead (UNIT_DIED). Melee parsing
retains overkill, multiple damage schools, optional absorb/resist arrays,
victim state, blocked damage and the correct critical-hit flag. Truncated
payloads do not create entries. Timestamps record client receipt time.

A fixed 1,024-entry ring belongs to the network session. History therefore
survives Lua/UI reloads and clears on logout or disconnect. Filters combine
comma-separated event names with optional source/destination GUIDs or composite
object masks. Individual filters are ORed; their conditions are ANDed. An empty
event list matches nothing. The filtered cursor starts at the newest entry
when set to zero and returns nil after the end, allowing the original
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

Remaining work includes periodic spell/aura logs, casts and failures,
energize/drain/leech, dispels, interrupts, additional ranged/miss variants,
complete combat-log configuration controls and clickable message hyperlinks.
Event-time affiliation snapshots across later group/ownership changes are also
pending; object masks currently use the known unit state when inspected.
The currently supported subset must not be advertised as every WoW combat event.
