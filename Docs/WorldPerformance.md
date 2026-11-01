# World streaming performance

## Follow-up: startup and p99, first verified cycle (2 November)

`31-wmo-future-existing` improves ordinary whole-route p99 from the regressed
9.910 ms to 3.932 ms (original: 4.385 ms), p95 2.977→2.467 ms and frames above
50/100 ms 130/28→1/1. The remaining maximum is 224.79 ms during initial loading.
Initial settled time is 8.22 s; crossing maximum 18.09 ms and Abbey returns
13.94/15.01/12.58 ms. Its ten nonempty PNGs are individually inspected.
The repeated 19-step native route is `run-20261102-015722-503`; world readiness
is 9.40 s, versus 21.19 s in the regressed run and 13.01 s originally.
Its three PNGs are inspected and owned fixtures/fingerprints are restored.

The scheduler now uses a cooperative 1 ms budget, at most eight jobs per frame
and at most one terrain/LOD/WDL upload. An indivisible operation may overrun,
then yields. Cheap operations no longer each consume an entire UI frame.
Terrain, LOD, WDL-transition reads and WMO group preparation share four
active/ready slots. Cancelled WMO futures keep counting until completion;
shutdown waits before closing MPQs. Root snapshots use thread-safe shared
ownership. WMO read/parse, authored portal/MOCV lighting, topology/buffer
preparation and temporary topology destruction run on workers without UObjects.
Each successive group releases its consumed Unreal future explicitly; a real
two-group regression prevents reusing a valid-but-consumed future state.

Registering empty procedural collision components before populating sections
avoids the synchronous body creation observed when registering populated
pending-async geometry. Readiness now additionally waits for actual physics
bodies. Terrain collision drops from roughly 40 ms to a few milliseconds;
terrain water, static M2 and WMO rendering avoid the editor mesh builder.
An independent test compares prepared object vertex colours byte-for-byte
with that previous engine builder, including its sRGB conversion. Water keeps
source depth alpha/UVs/tangents; WMO/M2 sections explicitly retain shadow flags.

The initial 256-entry parsed-model limit caused 2,113 evictions while retaining
only about 4 MiB. Metadata is now bounded at 2,048 entries while payload budgets
remain 128/64 MiB. This route retains 723 models / 12.75 MiB with zero evictions.
This is bounded reuse rather than retaining unloaded actors or forcing GC.

`32-combined-tests` passes all 30 related checks without warnings. The latest
`32-coverage-smoke` is also inspected and reports zero missing full/mid/far
source-tile coverage, 750 terrain sections, 13,471 instanced placements and all
590 WMO shadow casters. Distant radii remain unchanged. Pending WDL requests
covered by loaded LOD1 are discarded, so retained WDL object counts may differ
from the earlier overlapping load order; coverage is checked independently.
Full clean publication, regression, both cache modes and memory repeat remain
pending before final acceptance.

## Follow-up: warning fixes

`24-warning-tests-final` executes all four formerly warning-bearing tests with
four clean successes, zero warnings/failures. The shared parser-test MPQ helper
now honors `-wowdata=`; AreaIds fails when its data is unavailable instead of
reporting an unexecuted check as success. The real ADT assertions execute.
Optional character-atlas extension tables use the existing fixed WotLK layout
when absent; present corrupt tables still report failure. Equipment verification
also checks the actual 512x512 composite in the no-extension case. Region
creation can legally precede Lua sizing/reparenting, so its transient zero-size
diagnostic is Verbose; the scroll test continues to assert final measured text,
anchors and scroll range after layout. `24-warning-smoke-final` supplies two
individually inspected, nonempty exterior/interior engine captures. Full clean
regression and the performance follow-up remain pending.

## Clean published verification, 1 November 2026

Implementation `8d996a241f61a830bd9f6c03a188d07ed32bcb7a` is published and
installed without local patches. The clean repeat passes 179 regression checks
(175 clean, four previously recorded warning cases), the 19-step native
UI/network route and 57 individually inspected, nonempty engine captures:
10 existing-directory, 10 isolated-directory, 13 memory and 21 lighting PNGs,
plus three native PNGs. Source/stage/package/runtime verification matches 921
source files, 1,504 runtime inputs and all 47 packaged files. The three EXEs
share SHA256 `0C5E7F26576A224357B6A0ADA661603CF8F00862930FD6C76C0BE467BBE70F95`.
Owned fixtures are closed/restored, protected fingerprints match, credentials
are removed, and the realm and client process counts are zero.

Machine: Windows 11, UE 5.7.4 Development, Ryzen 7 9800X3D, RTX 5080,
approximately 32 GiB RAM, 1280x900 window, no VSync. These are individual runs
on this machine, with ordinary host activity and system/driver caches
uncontrolled. They are not a mid-range hardware or universal FPS claim.

### Loading and streaming

Times below include each route's unchanged settling/minimum-duration rules.
Process launch to first settled capture additionally includes startup, MPQ/DBC
and map initialization; it is distinct from the initial phase's own timer.

| Measurement | Original existing | Published existing | Original isolated | Published isolated |
|---|---:|---:|---:|---:|
| Launch to first settled capture, s | 21.08 | 19.18 | 24.19 | 20.09 |
| Initial phase settled, s | 13.50 | 12.93 | 16.76 | 13.45 |
| Initial frame p95 / p99 / max, ms | 11.04 / 270.26 / 569.73 | 9.03 / 45.87 / 257.97 | 32.25 / 363.92 / 692.04 | 9.43 / 51.40 / 261.79 |
| Elwynn crossing maximum, ms | 275.49 | 179.13 | 377.40 | 67.92 |
| Abbey return maxima, ms | 548.29 / 542.76 / 543.92 | 107.28 / 68.76 / 72.62 | 707.25 / 818.80 / 1050.54 | 85.73 / 75.94 / 71.28 |
| First Westfall settled, s | 20.18 | 11.00 | 20.20 | 11.12 |
| Later Westfall settled, s | 16.42 / 16.42 | 8.00 / 8.00 | 16.46 / 16.65 | 8.00 / 8.00 |

Eight seconds is the route's minimum duration, not a measurement that the
revisited region requires exactly eight seconds of loading. The differing
crossing maxima are both retained; their cause has not been isolated.

Across the ordinary existing-directory route, the frame mean improves
3.057→2.522 ms and maximum 569.73→257.97 ms, but p95 **worsens**
2.767→2.976 ms and p99 **worsens** 4.385→9.910 ms. There are fewer very large
stalls, with more work distributed among smaller frames. Samples are
42,663→41,098 and phase durations differ, so these aggregate percentiles are
descriptive workload mixtures, not controlled steady-state FPS comparisons.
Frames above 50/100 ms change from 142/128 to 130/28. Raw diagnostic frames
remain separately available and are not discarded from the evidence.

| Ordinary whole-route counter | Original median / p95 / p99 / max | Published median / p95 / p99 / max |
|---|---:|---:|
| Game thread, ms | .506 / .895 / 3.536 / 570.13 | .617 / 1.723 / 9.079 / 195.93 |
| Render thread, ms | 2.184 / 2.710 / 3.167 / 163.56 | 2.087 / 2.576 / 3.203 / 180.42 |
| GPU, ms | 1.555 / 1.781 / 1.918 / 14.35 | 1.544 / 1.638 / 1.781 / 9.99 |
| RHI draw calls | 4876 / 8488 / 8758 / 8886 | 1542 / 2770 / 3324 / 3688 |

Warm entry frame p95 is 2.34→2.27 ms, with draw-call median 1628→1190;
this small frame-time difference must not be promoted as an overall FPS gain.
The initial terrain has 6,395→750 sections/materials (88.27% fewer). All 923
source textures, 13,471 instanced placements in 1,175 HISMC batches and 590 WMO
shadow casters remain; original/new initial placement totals also match the
independent instancing logs. The 25 alpha atlases occupy 100 MiB; source
texture mip bytes total 20.97 MiB in the corrected compressed-format accounting.
Visual RGB-grid mean differences versus the original are .98–1.48/255 in the
existing run and .95–2.05/255 in the isolated run. Animation/temporal history
are not frozen, so these are diagnostics rather than pixel-identity claims.

Final existing-run terrain-finalization p95/max is 127.60/133.13 ms, LOD1
.54/2.41 ms, WDL 14.61/18.53 ms and WMO-job creation 11.17/104.29 ms.
M2 model-group p95/max is 1.34/9.95 ms; it now measures one model group rather
than the old whole tile. The worker and queue reports retain at most four
active/ready tiles and one selected heavy job per frame. Terrain collision,
materials, uploads and WMO work still produce visible hitches; a fully
16.7-ms streaming budget has not been achieved.

### Retention and native gameplay

The clean six-cycle diagnostic route records the following Abbey returns.
Every return has exactly 54,588 UObjects, 2,772 live static meshes, 21,624
instanced placements and the same terrain/shadow populations. Parsed-world
cache payload remains 3.96 MiB / 256 entries, below its 128 MiB limit.

| Return | Before diagnostic collection, MiB | After collection and settling, MiB |
|---|---:|---:|
| 1 | 5019.1 | 4366.1 |
| 2 | 4932.3 | 4408.6 |
| 3 | 4612.6 | 4342.1 |
| 4 | 4938.0 | 4429.1 |
| 5 | 4948.2 | 4430.6 |
| 6 | 4205.7 | 4196.2 |

The late range is 234.4 MiB, with no sustained growth and identical retained
scene populations. Seven explicit diagnostic collections and two additional
natural collections are recorded. The unchanged three-cycle route instead
ends at 6,122.2 MiB before collection (isolated: 6,380.1 MiB). The original
last return already followed natural collection; comparing those endpoints
as an equal-state memory reduction would be misleading. No forced collection
was added to production gameplay.

The repeated native route is
`logs/wowunreal-gameplay/run-20261101-220636-487`. All 19 steps pass, including
real login, floor/capsule movement, doorway/interior and return, and all three
PNG views preserve the original UI and world. Its first 12-second frame
median/p95/p99/max is 7.60/10.35/12.25/14.16 ms, versus
7.12/9.54/10.55/11.69 ms originally. Draw-call median falls 2757→1832;
GPU p95 is 2.66→2.62 ms. Profile p95 values across all ten intervals are
9.71–10.69 ms, versus 9.18–25.40 ms before, with differing live NPC/network
and render-thread activity. These results do not establish a general native
frame-rate gain.

The native `wait_world` interval **regresses from 13.01 to 21.19 seconds**.
This full UI/network readiness result is distinct from the quicker offline
settled route; its cause has not been isolated by these measurements. Native
startup throughput and remaining upload hitches are explicit limitations of
this implementation, not accepted as solved or hidden by weakening collision
readiness or selecting a faster offline figure.

### Reproduction and evidence

Run `Invoke-PerformanceCapture.ps1 -Name <unique-name>` for the existing mode;
add `-CacheMode fresh-process-isolated-project-cache` for a new project user
directory. Use `-Spec .../21-performance-memory-stress.json` for the six-cycle
diagnostic route. Run `Summarize-Performance.py <run-directories>` after the
clients exit. `Invoke-PerformanceNative.ps1` uses only the owned QA launcher;
`Verify-PerformanceInstall.ps1 -Reviewed` checks clean publication, every staged
runtime input, package identity, queue/cache bounds, stable retained populations,
capture hashes and fixture cleanup. `-Reviewed` requires personal inspection
of every final PNG before asserting acceptance.

Evidence is under `D:/Games/idle_wow/logs/wowunreal-performance/`: the two
`17-baseline-*` directories, three `22-published-*` route directories,
`before-after.json` (full distributions/native intervals/raw CSV hashes) and
`install-verification.json`. Each route retains its spec/executable provenance,
raw CSV, runtime JSON, scope log, nonblack/hash checks and PNGs. The 21 lighting
captures and their installation proof are in
`logs/wowunreal-lighting/22-published-lighting`. The historical cycles below
explain the measured implementation steps; the clean results above are final.

The performance route uses real MPQ terrain, M2 placements, WMO collision and
the production sky. `Scripts/Verification/Invoke-PerformanceCapture.ps1` launches
a new installed process and records raw wall-clock frame intervals, game/render
thread counters, GPU counters and RHI draw calls, with nearest-rank p95/p99.
Thread/GPU counters are pipelined observations of preceding frames, not precise
per-operation GPU attribution. `-wowstreamprofile` enables wall scopes in the
production loader and is identical in before/after comparisons.

The route crosses an Elwynn tile boundary, enters/exits Northshire Abbey, then
makes three Westfall/Abbey round trips far enough to unload terrain. Each stop
records terrain sections/material slots, WMO shadow casters, weak asset-cache
counts, process private/working-set memory and pending queues. All screenshots
use `FScreenshotRequest`. Four subsequent frames are classified as diagnostic
work, separately from ordinary frames. Step setup and snapshot overhead also
uses that diagnostic category. Raw CSV preserves both populations.

Cache terminology is explicit: every wrapper launch is a **new process**.
The default keeps its project user directory; the isolated mode creates a new
task-owned project user directory and does not delete an existing cache. Neither
mode clears the operating system's file cache or the GPU driver's cache. They
must not be reported as a cold disk or a cold machine. Cooked shaders and the
engine's shared derived-data cache remain available. Repeated visits within a
single route are a separate warm-process measurement.

The native companion route uses the existing owned-QA launcher, original UI and
real server login/movement. Its profile steps exclude screenshot operations;
walk profiles measure real capsule movement and collision. Temporary accounts
are removed by the launcher and protected-character fingerprints must match.
Offline figures are never presented as an overall gameplay FPS improvement.

## Original installed smoke check

Before instrumentation or optimization, installed `0c97bfd` completed the two
real exterior/interior cases in `17-original-smoke`. Initial 25-tile queue to
lighting-ready took about 12.35 seconds in that new process with existing
project caches and uncontrolled system file cache. Both PNGs were nonempty and
visually inspected. Both stops retained all 590 WMO shadow casters. This is a
preliminary offline observation, not the route-wide performance baseline.

## Unoptimized route baseline

`logs/wowunreal-performance/17-baseline-existing` contains the full raw CSV,
scope logs, distribution summary and ten nonempty, individually inspected PNGs.
The 25 initial terrain tiles have **6,395 sections/materials**, nearly 256 per
tile. Initial queue to settled screenshot is 13.50 s (including the explicit
three-second settling window). Ordinary loading-frame p95/p99/max is
11.04/270.26/569.73 ms. The warm Abbey entry/exit p95 is 2.34/2.35 ms; those
small figures must not hide the streaming spikes.

Across 116 terrain builds, finalization p95/max is 172.06/247.11 ms. M2 tile
instancing p95/max is 45.47/84.51 ms; LOD1 is 34.12/63.86 ms. WMO creation
p95/max is 23.35/325.23 ms. Combined world-tick p95/max is 255.33/567.09 ms.
The first Westfall stop settles in 20.18 s, repeated Westfall stops in 16.42 s.
The repeated Abbey returns still have ordinary-frame maxima around 543–548 ms.

Process private memory at initial Abbey is 4,132.7 MiB; successive round trips
reach 5,313.8, 6,619.7 and 5,504.9 MiB on Abbey returns, with a Westfall peak
of 7,118.0 MiB before automatic garbage collection. This does not establish
an unbounded UObject leak: collection recovers memory, while parsed-M2 cache
ownership and the repeated allocation peak still need investigation.

The second unoptimized run, `17-baseline-isolated`, uses a new project user
directory and repeats all ten inspected captures. Initial settled time is
16.76 s; ordinary initial p95/p99/max is 32.25/363.92/692.04 ms. The last Abbey
return peaks at 1,050.54 ms. GPU time on the entry route remains about 1.60 ms
while render-thread/frame distributions vary substantially between these runs;
the difference must not be attributed solely to cache state. Both retain the
same sections, textures and geometry. Their reports preserve the different
sample counts and complete distributions.

The corrected 19-step native baseline is
`logs/wowunreal-gameplay/run-20261101-201527-342`: real server login, UI,
stairs/doorway/interior and return, three inspected nonempty PNGs. The ten
profile intervals have frame medians 6.86–13.88 ms, p95 9.18–25.40 ms, maxima
11.69–44.69 ms and typical draw counts 1,927–3,548. UI/network/entity costs
remain included. All owned fixtures are removed; protected fingerprints match.
An earlier failed QA path is retained but excluded: automatic wall avoidance
diverted it at the doorway. The corrected path uses the already verified
stair/doorway coordinates and the probe's explicit direct-walk option.

`baseline-runtime-manifest.json` retains the staged runtime input hashes and
executable hash before any terrain optimization. The only later baseline
instrumentation adjustment adds the direct-walk option to profile steps;
production streaming/rendering is unchanged.

## Batched terrain, first verified cycle

The cookable `M_WowTerrainBatched` shader groups chunks by their ordered
original ground-texture set. A single 1024x1024 RGBA texture stores the three
unchanged 64x64 alpha maps in RGB. UV0 remains local alpha coordinates, UV1
remains the original tiled coordinates, and constant UV2 selects the chunk.
Per-fragment half-texel clamping exactly reproduces each old clamp sampler
without filtering across atlas cells. Source ground textures are not resized,
atlased, baked or replaced. Original triangles, holes, normals and seams remain.
Default-lit shading, roughness/specular and WMO/M2 shadows are retained.

`18-terrain-tests` passes 22 Rendering/World/Collision/Lighting checks (20 clean,
the same two relevant pre-existing warnings). Tests independently compare
bilinear sampling at cell edges/corners in all three channels, verify real ADT
alpha bytes and ordered texture sets, and retain original hole/UV topology.

`18-terrain-existing` repeats the entire ten-stop route. All PNGs are nonempty
and individually inspected. Initial terrain sections/materials fall from
6,395 to **750** (88.27% fewer); the Abbey tile itself falls from 256 to 28.
All 923 cached source textures, 2,157 unique static meshes and 590 WMO shadow
casters remain at the initial stop. The four-pixel visual comparison grid has
mean RGB differences 1.01–1.88/255 across the ten images; time-based scene
animation and temporal rendering are not frozen by this route.

This change alone does **not** improve streaming latency: initial settled time
is 15.03 s and ordinary-frame maximum 627.36 ms. Combined world-tick p95/max
is 272.25/623.43 ms. Root/group WMO builds, LOD1 and full terrain mesh preparation
remain synchronous. Private memory reaches 6,437.3 MiB before collection and
5,285.2 MiB on the final return. These results justify a separate scheduling
and geometry-preparation change rather than claiming an overall FPS win.

## Bounded streaming, first verified cycle

Terrain and LOD1 ADT parsing, topology, normals/UVs, alpha packing and collision
arrays are prepared on workers without actor/UObject construction. The same
four-slot limit covers active reads and completed results waiting for upload.
Requests are nearest-first; obsolete map generations are discarded without
waiting on the game thread. One heavy unit per engine frame rotates between
terrain upload, one M2 model group, one WMO group, LOD1 upload, WDL and one WMO
root. The previous 100-group cutoff no longer sends smaller buildings through
an unbounded whole-building build. Root probing is budgeted as well as spawning.

Initial collision readiness includes pending M2 model groups. The performance
route additionally waits for LOD1/WDL queues to empty; it therefore does not
obtain a quicker result by capturing an incomplete distant world. Debug-map
queued loads still finish when camera streaming is disabled. Normal object
scans return to their 10 Hz cadence when the queue is empty.

`19-streaming-tests` passes 23 checks (21 clean, the two existing relevant
warnings). The worker regression independently compares every generated
position/UV and the original triangle/hole/collision counts. The accepted
`19-root-jobs-existing` route has ten inspected, nonempty PNGs. Queue telemetry
records at most four prepared/active results, and scope frame IDs verify one
heavy terrain/M2/LOD1/WDL unit per engine frame. Initial terrain still has
750 sections, 923 source textures and all 590 WMO shadow casters.

Ordinary initial frame p99 falls from 270.26 to 61.65 ms; maximum from 569.73
to 336.37 ms in these existing-project-directory runs. On the Elwynn crossing,
maximum falls from 275.49 to 88.39 ms. Repeated Abbey-return maxima fall from
543–548 to 89–97 ms. First Westfall settling improves 20.18→13.96 s; repeated
Westfall settling 16.42→8.28/11.50 s. WMO creation p95/max is 14.18/94.21 ms,
compared with 23.35/325.23 ms in the first original run. Initial fully settled
time is still 15.05 s: total mesh construction work remains substantial.

The shorter route finishes before the previous run's automatic collection
window; private memory reaches 7,856.4 MiB. This is retained as an unresolved
memory result. Repeated asset reconstruction and parsed-model cache ownership
are the next changes, followed by a longer post-collection cycle test.

## Prepared runtime buffers

`FWowRuntimeStaticMesh` in WowAssets converts the prepared geometry directly
to static-mesh vertex/index buffers with explicit section material indices.
Terrain/LOD1 preparation and temporary topology destruction both run on the
worker. WDL keeps its original heights, holes, transition strips, UVs and
normals but prepares its small buffers without an intermediate MeshDescription.
UObject creation, material binding and RHI initialization remain on the game
thread. No imported collision is substituted for the existing runtime collision.

`20-buffers-tests` passes the same 23 checks. The worker test now reads the
actual prepared buffers and compares their positions/UVs, triangle counts and
section material indices with the original terrain builder. The accepted
`20-worker-release-existing` route repeats ten nonempty, individually inspected
captures with preserved terrain layers and 590 initial WMO shadow casters.

LOD1 upload p95/max is .89/3.54 ms, versus 34.12/63.86 ms in the original
synchronous read/build path. Terrain finalization p95/max is 95.52/99.64 ms,
versus 172.06/247.11 ms originally. Its remaining cost includes runtime collision
and material/resource creation. First Westfall settles in 10.27 s; all later
Westfall/Abbey stops reach the route's eight-second minimum. Initial full-scene
settling is 13.00 s, compared with the original 13.50 s; this is a small loading
time improvement, not a claim that all world entry is now fast. Initial ordinary
frame p99 is 42.73 ms; crossing maximum 74.65 ms and repeated Abbey-return maxima
64–67 ms. Private memory still rises to 6,768.9 MiB before collection, so the
memory stage remains open.

The added Present counter is near zero in this accepted run. It does not explain
the frame/render-thread variation in the other runs; that variation remains
explicitly unassigned rather than being mislabeled as a cache effect.

## Bounded ownership and memory verification

Parsed world M2 data now uses an LRU with a 128 MiB retained-payload budget;
character M2 data uses 64 MiB. Both also cap metadata at 256 entries. Accounting
includes nested animation, light, material, particle and ribbon track arrays,
not just vertex buffers. Oversized models remain usable by their live owner
without entering the cache. Eviction releases cache ownership only: active
animation, light and effect shared references preserve their original data.
Container allocation overhead and active owners outside the cache are separate
from this payload budget. The ownership regression exercises eviction, oversized
data, metadata bounds and an active animated light after eviction.

Terrain and WMO meshes reuse the existing weak asset cache. A live mesh keeps
its materials; an unloaded mesh gains no persistent strong cache owner. Each
new terrain actor still builds its original separate collision component; WMO
instances still prepare their room, collision, light and shadow metadata. This
does not pool actors or retain an entire previous region. Terrain upload can
reuse still-live meshes before natural garbage collection, while worker reads
and collision preparation remain bounded by the same four slots.

Snapshots now separate source-texture pixel-format/mip bytes from the terrain
alpha atlases, parsed-model payloads, live meshes/instances/UObjects and process
private/working-set memory. The source-texture figure replaces the earlier
RGBA-plus-mip approximation, so these two estimates must not be directly
compared. Mesh estimates remain approximate; process memory is measured.

`21-performance-memory-stress.json` makes six unload/revisit cycles. Its seven
explicit diagnostic collections happen only in this verification game mode,
after streaming has settled; ordinary gameplay does not force collection.
Before/after private memory and collection counts are preserved, and collection
frames join the diagnostic population rather than ordinary latency figures.
This route tests post-collection retention separately from the unchanged
three-cycle route and its natural collection timing. A plateau across this
finite route is evidence for these two regions, not an unlimited-world proof.

The first `21-memory-existing` route has ten individually inspected, nonempty
captures. It retains 750 initial terrain sections and 590 WMO shadow casters;
13 still-live terrain meshes are reused on later returns. Private memory is
4,035.2 MiB initially and 6,392.2 MiB at the final pre-collection return.
Initial settled time is 12.93 s, initial ordinary p99/max 41.00/251.87 ms;
Abbey-return maxima are 85.61/71.50/71.90 ms. The crossing maximum is 189.45 ms,
higher than the preceding prepared-buffer run, and is not hidden by selecting
the faster sample. The four-pixel visual grid differs by .38–1.50/255 on average.

The separate provisional `21-memory-stress` numeric report records 4,371.8,
4,411.9, 4,346.3, 4,431.4, 4,433.3 and 4,199.7 MiB after collection on successive
Abbey returns. All six retain exactly 54,588 UObjects, 2,772 live static meshes
and 21,624 instanced placements. The parsed-world cache stays at 256 entries
and about 4 MiB of retained payload. Final acceptance still requires the clean
published repeat and individual review of that repeat's complete capture set.

`21-memory-tests-fixed` passes 26 Performance/Rendering/World/Collision/Lighting
checks: 24 clean, the same two relevant pre-existing warnings. The ownership
test explicitly selects linear interpolation for its independently expected
midpoint sample; its first fixture accidentally left the default step sampler.
