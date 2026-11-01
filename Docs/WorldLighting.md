# World lighting verification

The lighting stage uses `Scripts/Verification/Invoke-WorldLightingCapture.ps1`
and `14-world-lighting-offline.json`. The dedicated game mode reads the user's
MPQs and streams the real map through `AWowWorldManager`; it has no account,
network login, synthetic floor or gallery lights. Captures use
`FScreenshotRequest`. The wrapper waits for shutdown and validates every PNG.

The initial route contains outdoor, doorway and indoor views at 09:00 and
20:00, plus five-frame series with a moving near/far camera and a moving sun.
Thread/GPU counters are sampled after scene settling, outside screenshot work.
It records loaded groups, shadow casters and room identity alongside timings.

Initial renderer baseline (2026-11-01):
`logs/wowunreal-lighting/14-baseline/verification.json` and `captures.json`.
Nine cases produced 21 nonempty PNGs, all visually inspected. Near/far shots
retain 590 WMO shadow-casting groups; the indoor subject resolves to abbey
group 1 and the doorway subject to group 3. Median game-thread time was
0.47–0.52 ms; median GPU time was 1.58–2.01 ms, without gameplay UI. These are
offline scene costs, not a claim about live gameplay frame rate.

## Source audit

Local original-client definitions are in `src/wowdecomp`, in particular:

- `WoW/Source/WorldClient/DayNight.cpp::LoadLightingBands`: band 0 is diffuse,
  band 1 ambient. WMO ambient does not come from a separate indoor Light.dbc
  row; the outdoor bands blend with WMO lighting at transition vertices.
- `MapObjRead.cpp::FixColorVertexAlpha` and `AttenTransVerts`: root flags,
  transition batches and portals control the stored colours/alpha.
- `MapObjGroup.cpp::QueryLighting` and `MapEntity.cpp::QueryInteriorLighting`:
  units sample the floor triangle's interpolated colour rather than a room mean.
- `Engine/Source/Model2/M2Data.h::M2Light`, `M2Model.cpp::Animate*` and
  `M2Light.cpp`: light colour/intensity/visibility tracks and bone transforms.

## WMO lighting, first verified cycle

The loader now retains MOHD ambient, MOGI group flags, MOGP batch counts and
MOPT portal planes. `FWowWmoLighting` follows the original alpha preparation
and portal-distance falloff. The WMO material blends its baked emission with
the outdoor lit contribution per vertex instead of relighting the entire room.
Unlit material flags and rooms without MOCV have explicit fallbacks.

Units sample the collision floor triangle with barycentric interpolation.
The original 168/255 diffuse minimum and 96/255 ambient cap preserve the floor
hue. Ambient is applied to the character material; a bounded character-only
point supplies the fixed interior diffuse direction. That point is an Unreal
rendering approximation of the original directional unit light, with a 500 cm
radius. At most 64 nearby subjects are tracked, with 16 updates per sky tick;
the old four broad room fills per building are removed. Doorway MOCV alpha
fades the local contribution back to the outdoor DBC light.

The DBC audit also corrected diffuse/ambient band indices and converted the
36x east/height/south editor coordinates and radii to movement coordinates.
Dungeon maps use the original zero-origin rule. Global rows are identified
by their zero position. Fog-distance bands use their original 1/36 scale.

Verification: `logs/wowunreal-ui-completion/14-wmo-ambient-tests/index.json`
contains eight successful checks (one existing room/minimap warning, no
failures). `logs/wowunreal-lighting/14-wmo-ambient/verification.json` records
nine rendered cases and 21 inspected nonempty PNGs, with no material fallback
or network login. The indoor floor sample is `(0.1323,0.1473,0.2862)`, outdoor
weight 0; the doorway weight is 0.77035. All 590 WMO shadow casters remain.
Median game-thread cost is 0.48–0.57 ms and GPU cost 1.56–1.97 ms in these
offline scenes. The same route's baseline was 0.47–0.52 and 1.58–2.01 ms.
This first cycle marks WMO lighting pending a published clean-build recheck.
M2 emitters and the shadow-flicker diagnosis remain separate open items.

## Published WMO verification

Published implementation `9322573d9abbd8b0f31464fbc26d0f2eb6945f26` was rebuilt
without local changes and installed. The full regression has 172 successful
checks and the same four existing warnings (WMO rooms/minimap, optional
equipment DBC, HUD scroll content and ADT area IDs). The repeated route
`14-wmo-published` produced 21 more nonempty PNGs, all individually inspected.
The first and published acceptance cycles therefore cover 42 inspected frames.

`logs/wowunreal-lighting/14-wmo-published/install-verification.json` verifies
903 source files and 1,469 tracked runtime inputs against the stage, all 47
packaged files against the installation, and three identical executable hashes:
`84471C7712B6C591C8979016124589DA1040450A9697B705966537267809CFA5`.
There are no extra staged sources or running UE processes. Verification used
no network login, database access, server operation or user character.

The shadow audit also found that `SetupLoginScene` spawns an unmanaged fixed
directional light in addition to the sky manager. Its effect and the sky's
10 Hz rotation updates require the next shadow-specific reproduction; the
offline WMO route uses the production sky manager without that login fill.

## Authored world M2 lights

M2 build-12340 light tables now retain both colour/intensity pairs, visibility,
attenuation tracks, local position and parent bone. Stand-clip and global
sequence clocks are independent; bone parents, pivots, rotations and scale
are evaluated before the ADT or WMO instance transform. MODS/MODD/MODR select
the original default and placed doodad sets. Models without a light table do
not acquire invented lights. Point-light ambient is retained in data but is
not added, matching the original `CM2Light::GxLightSet` point path.

`UWowM2LightSubsystem` selects at most 32 nearby points within 150 m. Weak
component ownership removes lights when a streamed instance unloads. WMO
probes use spatial cells and at most one nearby model read/build per frame;
far building decorations are not scanned every frame. Local lights do not
cast additional shadows. Existing WMO baked interior emission stays intact.
This work does not modify the completed spell-effect lifecycle.

The cookable light function uses a valid authored start/end range when present,
otherwise the original `(constant,linear,quadratic)=(0,.7,.03)` curve with a 1%
finite cutoff and a fade over its last tenth. UE's native exponent is .0001,
not zero: `LightData.ush` uses zero as its inverse-square sentinel. The tiny
nonzero native curve avoids that path while leaving the authored function
effectively unchanged. This is a deferred-renderer approximation, not a claim
that UE's PBR response exactly matches the original fixed-function renderer.

The real-data audit checks 612 models from the abbey, inn, Stormwind and nearby
ADT regions, retaining all three light-bearing models and 7,242 WMO references.
Seven lighting checks pass, including separate animation clocks and three
unload/reload cycles. `15-m2-fixed` contains eight individually inspected
captures. Its night on/off ROI changes 3,330 sampled pixels and increases mean
red by 5.88/255. A 1.5x placement scales the campfire's 1.6667-yard radius to
2.5 yards; unloading/reloading changes active points 3→2→3 and sources 7→6→7.
Typical subsystem time is about .012 ms, compared with the rejected initial
2.05 ms full-queue scan. Run `Scripts/Verification/Test-M2LightCapture.ps1`
against a capture directory to repeat the integration checks.

Published M2 commit `78ce1f3eb0378b196493fa93ffc4224fd1a250cf` was rebuilt
without local changes and installed. `15-m2-published-tests` repeats all seven
checks and `15-m2-published` repeats all eight inspected captures; its rendered
on/off, radius and ownership checks pass again (mean red increase 5.887/255).

## WMO shadow timing and ownership

The reproduction has both a frozen camera/clock and independently moving
camera/clock cases. `15-shadow-before` recreates the login scene's extra fixed
directional light: three directionals illuminate the world, including at night.
`16-shadow-isolation-before` removes only that diagnostic light. With the old
10 Hz sky tick and speed 1, 97.5–97.6% of sampled frames hold the previous
direction, followed by steps up to .046–.054 degrees. The old -5-degree sun
visibility cutoff also abruptly removes remaining direct illumination.

The production login scene now relies on its sky manager alone. Solar rotation
and horizon energy update every frame; DBC colours, fog and room sampling stay
at 10 Hz. Sun/moon energy fades smoothly within eight degrees above each
light's horizon, and neither lights surfaces from below the horizon. Default
speed is now realtime (1/60 game minute per second), with a double clock so
high frame rates cannot discard small increments. Cascades, contact shadows,
biases and WMO shadow casting remain enabled.

`16-shadow-acceptance` covers seven cases and 27 inspected, nonempty captures.
At the same accelerated speed, the held fraction is zero. Realtime step p95
is .000115 degrees by day and .000121 by night; UE component tolerance still
batches these tiny rotations, so the verifier bounds their amplitude instead
of requiring every microscopic transform to change. Screenshot readback
pauses remain included in maxima and must not be interpreted as a normal
frame's angular step. Motion blur is disabled only in the shadow diagnostic
route to expose edges clearly. All cases retain two directionals and 590 WMO
casters; `Test-ShadowCapture.ps1` checks the measured clocks and baseline.

The full regression has 175 successful checks (171 clean and the same four
existing warnings), no failures. `16-world-acceptance` repeats the nine-case,
21-frame exterior/door/interior route with the M2 and shadow changes combined.
Floor colours and doorway outdoor weight remain unchanged. Median game-thread
cost is .49–.60 ms, GPU 1.55–1.97 ms in this offline scene. These measurements
do not include networking or gameplay UI. The published recheck is recorded below.

## Final published installation

Implementation `670b291d3565ac7ae1803af3aa8f10cfef11f7ab` was rebuilt without
local changes and installed. `16-published-regression` repeats all 175 checks:
171 clean, the same four existing warnings, zero failures. The installed
executable repeats three routes: `16-published-world` (21 PNGs),
`16-published-shadow` (27) and `16-published-m2` (8). All 56 nonempty engine
captures were individually inspected, including moving camera, day/night,
sunset, door/interior, authored local falloff and unload/reload. Both numerical
render verifiers pass again. Realtime shadow step p95 is .000115/.000118
degrees by day/night; accelerated-clock held fraction is zero.

Each route's `install-verification.json` checks 908 source files and 1,481
tracked runtime inputs against the stage, no extra sources, all 47 package
files against the installation, and three identical executable SHA256 values:
`D4478E4B17E52B4954F1006931E70A40B3B785DC26B19BF58655DD8A525DD6D9`.
The source is published and clean, no overlays are applied, all clients are
closed. Verification used no server login, database operation or user
character. The final plan/documentation update does not change runtime inputs.
