# Layout during a stock cast

The original cast-bar OnUpdate moves its spark texture. Region changes formerly
cleared all rectangle caches and requested full layout; the next pointer pass
measured every loaded addon panel, including hidden paragraphs. A native
active-cast profile attributed about 287 ms per frame to that hover/layout pass.

Full layout now skips explicitly hidden branches. Their rectangles can still
be resolved lazily when queried or referenced, and Show invalidates layout so
their native slots and picking are current. Initial placement uses the WoW
hierarchy's visibility rather than Slate's previous collapsed state: otherwise
a newly anchored ScrollChild could remain collapsed indefinitely.

A leaf region with an anchored owner and no incoming frame/region references
updates its own native slot directly. Regions referenced by frames or other
regions still invalidate the full dependent layout. Pending global layout is
never cleared by the leaf path.

Regression coverage verifies scaled hidden changes and Show/picking, external
anchors, scroll-child placement, immediate leaf texture movement/size, and a
button following a moved referenced texture. Active-cast and idle costs are
measured separately in the rendered gameplay probe.

On the final warm 29-step route, active-cast hover/layout averages 0.53 ms;
total frame time averages 3.24 ms, p95 4.71 ms, maximum 6.99 ms over 310 samples.
The idle portion averages 2.96 ms over 1,016 samples. These are measurements
of that route, with authored hand particles enabled, rather than an FPS promise
for cold streaming or other locations.
