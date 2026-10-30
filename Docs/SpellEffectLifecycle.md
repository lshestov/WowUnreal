# Authored spell effect lifecycle

SpellVisual now drives precast, cast release, every outgoing projectile and
unit/ground impact. The former school-coloured sphere and runtime material
construction have been removed. Models, skins and textures are read from the
user's 3.3.5a MPQs; the two cooked unlit material parents are project-owned.

`FWowSpellLaunch` preserves cast object/caster GUIDs, spell, counter, server
millisecond timestamp, all hit/miss/reflect records, explicit unit target,
source/destination coordinates and their transport GUIDs. A partial target
record cannot publish a launch. Flight clocks use monotonic time measured
from packet handling, including time spent resolving models before the next
tick. Server timestamps remain available as packet identity; this does not
estimate unknown one-way network latency.

Each distinct server hit/miss gets its own route. Current selection is never
consulted after release. Moving targets update the retained endpoint; a lost
actor freezes that endpoint, and no unit impact is attached to a replacement
actor. A miss does not play the successful impact kit. Reflections retain the
reflect result and return to the original caster in half the outgoing flight
interval, matching the paired core's additional `timeDelay >> 1`. Unit flights
retain the core's combat-reach/five-yard calculation. Ground flights use exact
caster-to-destination distance without subtracting reach or imposing that
unit minimum.

Missile attachments are effect attachment enums unless SpellVisual flag 0x200
requests raw M2 IDs. For example, destination 1 means M2 chest attachment 34,
not equipment palm 1. Kits bind to animated head/chest/base/spell hands,
breath, weapons and special points. An unavailable binding uses the actor's
bounded base/height fallback instead of changing the server destination.

SMSG_SPELL_DELAYED extends the matching cast deadline and fires
UNIT_SPELLCAST_DELAYED. Active precast components extend their emission
without being recreated. Completion/failure affects only its spell/counter;
a late completion cannot clear the animation of a newer cast. Channel starts
retain the preceding GO counter. Channel updates extend existing components;
zero duration, interruption, expiry or loss of their owner ends emission.

Aura state is keyed by unit, server slot, spell and caster. Duration/charge
updates refresh its existing components. Removal and replacement stop the old
state and play StateDoneKit when present. Persistent area effects follow the
server DynamicObject's existence. Its POSITION block takes precedence over
STATIONARY_POSITION, preserving packed transport, both positions and both
orientations before reading fields and subsequent update blocks.

Blizzard's original character procedure 9 uses SpellVisualKitAreaModel.dbc,
its authored rate and the DynamicObject radius. Bounded shards sample the
area, trace to world support and play the MPQ model's own animation. A normal
client ground-spell request now keeps a pending cursor, sends the selected
world destination on a terrain click and supports cancellation/Escape.
Transport-relative ground destinations are retained in packets but do not
invent an unsupported world-space destination.

The M2 effect component supports geometry, embedded bone animation,
colour/opacity tracks, texture-weight and UV transforms, spherical/Z-locked
billboard bones, plane/sphere head and velocity-tail particles, and ribbon
history from the original records. Local particles use the emitter bone's
current transform; world particles keep their birth transform. Effects with
Hold (158) and Decay (159) sequences play Stand once, then Hold while active
and Decay on removal. Global scalar/material clocks keep absolute model age.
Additive mode 3 ignores texture alpha while preserving animated vertex alpha.

Work is bounded per model/attachment: 512 live particles, at most 128 per
emitter, 64 ribbon nodes per ribbon, limited emission per frame, finite model
clips and cleanup after drained particles/ribbons. This is a practical effect
renderer, not an assertion that every legacy M2 shader, spline emitter,
external animation or spell-specific character procedure has been implemented.
The representative fire, frost, holy/heal, aura and ground models use the
implemented paths. Separate world lighting and login work are unchanged.

## Verification status

Windows Editor and packaged Development builds succeed. The full regression
passes 168 checks (164 clean, four existing warning cases). Rendered owned-QA
routes cover Fireball cancellation/recast/retarget and authored impact (32
steps), Flash Heal/Renew/shield state and cancellation (31 steps), Frostbolt
and Blizzard channel/area cancellation (28 steps), and a real duel with
pushback past the original deadline and reflected return impact (38 steps).
Their 24 engine captures were checked for content and visually inspected.
The reflected flight is 271 ms outward and 135 ms back.

These are initial verification results, not the final clean-install signoff.
The last route's gameplay and peer cleanup passed, but the host wrapper's
protected-character checksum comparison failed. QA accounts were deleted;
existing character records were not restored or overwritten. The existing
binlog audit has not explained that checksum discrepancy. New realm QA runs
remain stopped. The wider-camera area capture, final Hold/Decay/refresh and
three-target healing scenario, and published-install verification are pending
resolution of that guard. The source plan remains pending verification.

Replayable scenario inputs are in `Scripts/Verification`. They contain no
credentials or Blizzard binary data. The host owned-account launcher supports
`qaClass`, bounded `peerSpells` and `peerOffset`; every participant is temporary.
The multiplayer healing scenario verifies three server heal destinations,
aura refresh and loss of an aura owner, and includes server-side cleanup.
