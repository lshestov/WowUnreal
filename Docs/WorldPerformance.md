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
