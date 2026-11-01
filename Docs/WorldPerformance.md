# World streaming performance

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
