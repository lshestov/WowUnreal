# Wrath spell visual data

The build-12340 loaders require the actual DBC layouts: 32 columns for
SpellVisual, 38 for SpellVisualKit and 7 for SpellVisualEffectName.
SpellVisual keeps sound/flags/impact-kit identifiers before the projectile
attachment; its six position offsets occupy columns 26 through 31. Ground
following values retain their signed integer representation.

SpellVisualKit has no AnimKitID column between its animation IDs and head
effect. Head/chest/base/hand/breath/weapon effects, world/special effects, sound,
shake and flags now use their own columns. Animation IDs preserve -1 sentinels.
Effect names retain their model scale limits, and model extensions normalize
case-insensitively to the M2 files stored in MPQ.

The layouts were checked against the actual local MPQ headers and the
[Wrath client record definitions](https://github.com/SatyPardus/wowdecomp/tree/main/WoW/Source/DB/Rec).
No Blizzard assets are included in this repository.

Two regression checks cover distinct fixture values, signed fields, offsets,
scale bounds, unsupported schemas and real spell/kit/model/sound references
for Fireball, Frostbolt, Arcane Intellect, Smite, Flash Heal, Renew and Power
Word: Shield. The full regression passes 150 checks (four existing warning
cases).

A packaged-client Fireball route exposed a separate existing fatal error:
Initialize used ConstructorHelpers::FObjectFinder outside a constructor when
loading the projectile's fallback material. It now uses LoadObject; the
dynamic material retains its parent through Unreal object references.
The 16-step rendered route completes cast, projectile release and arrival,
with three validated and visually inspected FScreenshotRequest captures.

This is the data foundation and crash repair. Rendering original M2 cast kits,
projectiles, impacts, healing and state/auras; actual attachment transforms;
concurrent caster ownership; and event timing remain in the spell visual task.
The legacy school-colored sphere/glow renderer does not establish visual parity.
The original M2 emitter record/track data is decoded separately; see
[M2 particle data](M2Particles.md).

## Unit-projectile launch identity

SMSG_SPELL_GO now also publishes a typed launch containing the cast object,
caster, spell, counter, server timestamp, explicit unit target, hit targets and
missed/reflected target records. Partial target data publishes no launch.
The existing cast/UI event contract remains available separately.

The gameplay renderer chooses the explicit server target, then the first hit or
miss when no explicit unit exists. It never rereads the caster's currently
selected unit. Unavailable targets do not generate an invented forward shot.
The projectile follows its retained actor through a weak reference; loss of
that actor leaves the last endpoint instead of acquiring another selection.

For ordinary unit missiles, flight duration uses Spell.dbc Speed and the paired
core's calculation: subtract caster combat reach from the unit distance, apply
the five-yard lower bound, then truncate to milliseconds. Invalid/instant data
does not invent a one-second shot. Extreme renderer lifetimes are bounded to
60 seconds. This is not server-clock synchronization or trajectory support.
Elapsed time starts at the actual packet-driven launch rather than counting a
frame delta that may have begun before the actor existed.
Ground destinations, multiple outgoing missiles and reflected return legs
remain in the original-effect renderer work.

Packet regression checks retain explicit/implicit/missed targets and reflected
miss metadata, full cast identity, wrapping timestamps and transactional handling
of partial records. Timing checks compare the core's near/ranged/faster cases,
instant spells, invalid numbers and the renderer lifetime bound.

A rendered 18-step route starts Fireball at an existing stationary wolf, retains
it as focus and selects the player before release. The live projectile retains
the packet's wolf GUID while the selected GUID is the player. The real server
combat-log event confirms the original destination. Three engine captures are
validated and inspected; the projectile is visible in flight. The final route's
calculated duration is 0.648 seconds and its observed actor elapsed time at
arrival is 0.649 seconds. This measures the renderer's own flight clock, rather
than claiming zero latency against the server damage event. The owned client
and account are removed and existing characters/inventories remain unchanged.

Verified: Windows Editor and packaged Development builds; all 161 regression
checks pass (157 clean and four existing warning cases). The two focused launch
and timing checks pass without warnings.
