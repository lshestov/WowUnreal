# Stock video controls

`extShadowQuality` controls Unreal's shadow scalability (0 through 4), and
`ffxGlow` independently controls the renderer's bloom switch. GetCVar reads the
live native values. Saved UI settings reapply the selected values on world entry
and ReloadUI; native quality changes preserve the separate glow preference.

The XML parser retains slider/status-bar orientation, including horizontal
orientation inherited from a virtual template. The options sliders therefore
respond to horizontal mouse movement and paint their thumbs on the same axis.
Runtime orientation overrides and the default vertical slider still work.

GetCVarMin/GetCVarMax expose known native limits. Other settings return nil so
the original FrameXML can use its own range. A universal 0..1 range is invalid
for view distance, player textures, shadows and most effects sliders.

Effects CVars have bounded initial values and defaults. Invalid saved values
from the old placeholder implementation are clamped before display/application,
avoiding spurious setting changes and restart prompts when merely opening the
panel. Character texture level defaults to 9, matching the renderer's 512x512
Wrath atlas. The current shadow default is Unreal quality 3. The other numeric
defaults follow the Wrath CVar registrations and FrameXML ranges.

The original current-category Defaults action resets all effects preferences;
Apply, Okay and Cancel keep their original FrameXML semantics. This does not
establish renderer support for every effects CVar: ground-effect
density, farclip, filtering, weather and several texture options
still need their own engine implementations. Their stored values and valid UI
ranges must not be confused with completed visual features.

Regression coverage includes actual renderer CVars, native quality values,
independent glow, defaults, invalid old preferences and inherited horizontal
pointer input. Rendered verification uses the unmodified stock options window.

Verified on Windows: Editor and packaged builds; 148 passing regression checks
(four existing warning cases); a 46-step disposable-account route through the
original options window. Five FScreenshotRequest captures were validated and
visually inspected. The route covers both range endpoints, glow on/off, Apply,
Okay, Cancel, current-category Defaults and persistence across ReloadUI. The
test client exits and its owned account is removed after verification.

## Particle density and object detail candidate (4 November 2026)

`particleDensity` and `environmentDetail` now read/write a live, per-world
`UWowRenderSettings` subsystem. Saved preferences reapply through the same
bounded path on world entry and ReloadUI, including old invalid preferences.
The original FrameXML remains responsible for pending edits, Apply, Okay,
Cancel and current-category Defaults; the native defaults remain 1 and 1.

Reference inspection used the user's build-12340 MPQ
`Interface/FrameXML/VideoOptionsPanels.lua` and `.xml`, plus the local
`wowdecomp` reference:

* `WorldClient/WorldParam_CVar.cpp`: `ParticleDensityCallback` (0x0078D860)
  accepts 0.1..1 and sets the particle scaler; `EnvironmentDetailCallback`
  (0x0078DC60) clamps 0.5..1.5 and calls `ScaleFadeDist`.
* `Services/ParticleSystem2.cpp::EmitNewParticles`: density multiplies emission
  rate. The M2 sprite renderer now does this before fractional accumulation,
  preserving live particles, lifetimes, geometry/ribbons, per-frame limits and
  the existing bounded capacities. The separate Glue/login rendering stays at
  its existing density.
* `WorldClient/MapDoodadDef.cpp::UpdateBounds` uses the longest transformed AABB
  axis to select size bands <=1, <=4, <=15, <=100 and >100 WoW units.
  `WorldScene.cpp::ScaleFadeDist` gives these distances in WoW units:

| Size band | Minimum detail 0.5 | Default 1 | Maximum detail 1.5 |
|---|---:|---:|---:|
| <=1 | 30 | 30 | 30 |
| <=4 | 50 | 100 | 150 |
| <=15 | 100 | 200 | 300 |
| <=100 | 375 | 750 | 1125 |
| >100 | 1250 | 1250 | 1250 |

ADT visual ISMs are split by these size bands when placements of one model
cross a boundary. Native instance cull distances update existing batches and
are inherited by new streaming batches. Legacy single M2 visuals also register.
Registrations are weak and pruned. Terrain rings, streaming queues/budgets,
resident placements, WMO buildings, separate collision geometry and authored
M2 lights are retained. This implements the distance cutoff; original smooth
alpha fading over the final 5/10/15/20/50 units is not implemented here.
`farclip` is a separate original terrain/fog/projection control and remains a
stored preference; it is not substituted for `environmentDetail`.

Local verification: `Scripts/Verification/Test-GraphicsPolicy.ps1` compiles
the production policy with MSVC C++17 `/W4 /WX` and passes 28 checks against
the original distance table, size boundaries, clamping, fractional emissions
and emission limits. No UE build, native run or screenshot is claimed for this
candidate. Those checks belong to the shared acceptance session.

Acceptance inputs:

* Automation: `WowUnreal.Graphics` (native Lua bridge, existing/new ISMs,
  invalid saved preferences, per-world restoration and real particle simulation),
  plus existing `WowUnreal.Hud.RenderSettings`, particle/spell lifecycle,
  streaming, collision and M2-light regressions.
* Offline comparison: run `Invoke-PerformanceCapture.ps1 -Name <unique-name>
  -Spec <absolute-path>/34-graphics-world.json`, then
  `Compare-GraphicsCaptures.ps1 -RunPath <capture-directory>`. Four identical
  cameras independently vary density and object detail in the real Northshire
  scene, with the original `Spells/Fire_Precast_Hand.m2` placed five units in
  front of the camera. This is an explicit source-effect fixture, not a server
  spell cast. Each capture has native settings, live particles, resident and
  distance/frustum eligible M2 counts, collision triangles/bodies, WMO groups,
  light counts and terrain coverage. The comparison rejects changed scene
  invariants and writes JSON plus a side-by-side HTML report. Inspect all PNGs.
* Original options UI: `34-graphics-native.json` covers both sliders, pending
  edits before Apply, minimum/maximum Apply, Cancel, current Defaults, Okay and
  ReloadUI. `inspect_render_settings` records the native counters in `probe.json`.
* Full restart: close the first client, retain its **same owned QA account and
  user directory**, then launch with `34-graphics-restart.json`. It must observe
  density 1/detail 1.5, then saves the minimum through the original sliders.
  Close it and launch `34-graphics-restart-minimum.json`: density 0.1/detail 0.5
  must survive, then all original effects preferences from phase one are
  restored. Do not let a one-shot QA coordinator delete/recreate the fixture
  between these three runs. The coordinator must back up/restore config and
  credentials and remove the fixture after all phases, including failure paths.

The visibility counter explicitly means instance bounds inside the native
distance and camera frustum **before GPU occlusion**, not objects that produced
visible pixels. Particle snapshots are actual simulation counts and can vary
with frame timing/randomness; they are not expected to be pixel-identical or
exactly 10:1 when authored rates saturate the unchanged renderer capacity.
