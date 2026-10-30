# Authored precast head sprites

`UWowM2EmitterComponent` reads the parsed M2 emitter definitions and the user's
BLP textures. Plane and sphere head emitters use their scalar animation tracks,
emission area, speed/variation, gravity, drag, lifetime colour/alpha/size curves,
spin and atlas cells. World-space particles retain their birth transform;
model-space particles follow the animated attachment. Each attachment has at
most 512 particles, each emitter at most 128, and spawning is bounded per tick.
Textures are cached by normalized MPQ path and created with unique UObject
names. Reusing a single transient name replaced earlier texture resources;
the rendered Fireball probe requires three distinct textures per hand.

The two cooked unlit material parents implement alpha and additive blending,
including the no-alpha additive mode. `Scripts/CreateParticleMaterials.py`
recreates these project-owned materials. Blizzard textures and models stay in
the user's MPQs and are never included in Git.

The controller resolves `SpellVisual.PrecastKit` and its original attachment
IDs. Human spell hands are attachment 21/22, rather than the equipment palms.
The component uses the animated skeletal bone plus the authored attachment
offset. The actual Fireball precast model has four sphere head emitters and one
tail-only emitter per hand. Both paths are now supported.

Each caster owns its components. Spell ID and cast counter distinguish a
completion or interruption from an older cast. Cancellation/completion stops
emission and existing particles drain before components are destroyed. The
older point-light fallback is removed only for that caster when authored
sprites are available. Effects have a bounded duration and stale registry
entries are pruned before new casts.

The initial head-sprite subset has been extended to tails, ribbons, animated
model geometry and the remaining spell phases. Cast pushback extends the
existing deadline. See [spell lifecycle](SpellEffectLifecycle.md) for release,
flight/impact, channels, aura refresh/removal, area procedures and limits.

The rendered probe inspects both bone bindings, live particle counts and CPU
submission cost, verifies cancellation and a second cast, retains the server's
projectile target after selection changes, and checks cleanup. Engine captures
must also be inspected; a nonzero particle count alone does not prove visibility.

Profiling this route exposed a UI cost independent of particle submission:
the moving cast-bar spark invalidates layout, and pointer picking refreshed
every hidden addon paragraph. Full layout now skips hidden branches; their
geometry remains queryable lazily and showing a branch invalidates its layout.
Moving a leaf region with no incoming frame/region anchors updates its own
slot directly. Referenced regions retain full dependent-layout invalidation.
The scaled-anchor regression also covers hidden changes, external anchors and
restored picking after Show. Active-cast and idle profiles are measured
separately by the native scenario.

Rendered verification: the 29-step owned-account route passes with six
validated, visually inspected engine captures. Both hands have four supported
emitters and three distinct texture objects; real server interruption and a
second cast are checked. Active-cast frame time averages 3.24 ms with p95
4.71 ms (310 samples); the hover/layout pass averages 0.53 ms. This is a warm,
short route, not a general world-performance claim. The owned fixture is
removed after the run and protected characters/inventories remain unchanged.
All 164 regression checks pass (160 clean and four existing warning cases).
