# FrameXML animations

The UI animation runtime supports Wrath's nonvisual `Animation` timers alongside
the existing `Alpha` and `Translation` animations. Omitting the Lua animation
type creates an `Animation`. Groups and child animations retain separate object
types and named/parent-key references.

XML comments are ignored at every animation level. Child `OnLoad` handlers run
after their properties and scripts are populated; the group `OnLoad` runs after
its children exist. This lets the original `AnimTimerFrame.xml` start its
15-second guild-roster delay without modifying Blizzard's FrameXML.

Group duration is available before playback and reflects changed child settings.
Orders run sequentially, with children of the same order running concurrently;
start and end delays contribute to the order duration. Pausing preserves elapsed
time. A child's `OnFinished` precedes its end delay and the group's completion.
Callbacks that stop, pause or restart a group abort its old update immediately.

`WowUnreal.Hud.AnimationTimers` covers XML initialization, comment filtering,
object identity, delays, pause/resume, duration changes, ordering and reentrant
callbacks. `WowUnreal.Hud.TextureAnimations` covers the shared visual runtime.
Rendered verification also exercises the actual stock countdown before and
after `ReloadUI`.

Verified on Windows: Editor and packaged builds; 147 passing regression checks
(four existing warning cases); a 20-step disposable-account route with three
validated and visually inspected `FScreenshotRequest` captures. Both original
15-second countdowns complete. No animation creation, initialization or update
warnings remain in the rendered route.

Scale/rotation animations and complete compatibility with arbitrary addons
remain outside this verified subset.
