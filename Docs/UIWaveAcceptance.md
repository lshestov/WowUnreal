# UI wave integration and replay

The wave starts at `a502600dc2058926dd5eb95da005431fa1068c31` and combines
character-local placed frames, macro conditions, build-12340 combat event
delivery, and native particle density/environment detail. The associated
contracts and limits are in [UserPlacedFrames](UserPlacedFrames.md),
[MacroConditions](MacroConditions.md), [CombatLogDeliveryQA](CombatLogDeliveryQA.md)
and [RenderSettings](RenderSettings.md).

The broad baseline suite exposed six stale fixtures: audio tables were deferred,
ADT coordinates were map-centered, and raid packets required valid player GUIDs
and group membership. Integration corrected those fixtures. The new heal-log
fixture uses Vampiric Embrace 15290's actual TREAT_AS_PERIODIC DBC flag; Renew
139 delivers its ticks through the periodic-aura packet instead.

The rendered stack route found a separate input defect already present in the
baseline: StackSplitFrame handled Shift key-up before PlayerInput could release
it. The next inventory click retained the item cursor. Consumed UI key-up now
also releases PlayerInput; the route asserts the modifier state and real server
stack counts after splitting and merging.

## Owned native coordination

`Scripts/Verification/Invoke-UIWaveNative.py` imports the host's existing
`scripts/test-wowunreal-ui-primary.py` lifecycle. Run it only while the common
UE/client infrastructure is idle. It checks the previous closed guard and
protected identities/hashes before provisioning. A changed protected baseline
requires a matching explicit approval file under `logs/wowunreal-ui-wave`.

The coordinator keeps credentials in the existing private fixture/UserDir.
`--restart-spec` reuses that same owned character and directory in a fully
closed new process. `--addon-dir` temporarily installs an authored QA addon,
refuses an existing destination, and removes only its unchanged owned files.
The original lifecycle verifies registration ownership, waits for logout,
deletes the owned account/character, removes credentials and compares protected
fingerprints on success and failure. Output names must be unique.

| Route | Specs | Purpose |
|---|---|---|
| Common | `34-ui-wave-native.json` | Actual cast/damage, loot receipts, real Shift splitting 5 to 3+2, merge/transfer, rich chat/self whisper, ReloadUI, logout/selection/reentry |
| Windows | `UserPlacedLayout/01-drag-reload.json`, then `02-full-restart-reset.json`, then `03-default-after-restart.json` | Actual drag/resize callbacks, nearest anchor, resolution changes, two full restarts and reset |
| Graphics | `34-graphics-native.json`, then `34-graphics-restart.json`, then `34-graphics-restart-minimum.json` | Stock Apply/Cancel/Defaults/Okay, live renderer snapshots, ReloadUI and two full restarts |
| Macros | `34-macro-conditions-native.json` | Stock editor/action button, focus, alternative groups, conditional refusal, stopmacro and party-self exclusion |
| Combat | `34-combat-delivery-native.json` | Stock log plus authored listener, exact typed tuples, filters, server rejection, repeated interruption, completed heal and retained history |

Paths in the table are relative to `Scripts/Verification`. The windows route
uses `--addon-dir Scripts/Verification/UserPlacedLayout/IdleWowLayoutProbe`.
Account/realm/character path separation is additionally covered by the UE
automation; native provisioning keeps its one-character ownership restriction.

Example from the source checkout:

```powershell
python Scripts/Verification/Invoke-UIWaveNative.py --name ui-wave-example `
  --spec Scripts/Verification/34-graphics-native.json `
  --restart-spec Scripts/Verification/34-graphics-restart.json `
  --restart-spec Scripts/Verification/34-graphics-restart-minimum.json
```

## Rendering and installation evidence

Run `Invoke-PerformanceCapture.ps1` with `34-graphics-world.json` and pass the
actual `-Source` checkout. `Compare-GraphicsCaptures.ps1` verifies an identical
camera, independent density/detail endpoints, greater live particle/object
counts, and invariant resident geometry, collision, WMO, terrain and lights.
Object counts describe distance/frustum eligibility before GPU occlusion.
The existing `17-performance-world.json` route can be replayed at both endpoints
by adding `particleDensity` and `environmentDetail` to every step.

Run `Automation RunTests WowUnreal` with the user's MPQs between build/cook and
rendered clients. The integrated complete suite has 247 clean successes and
three old missing-fixture warning cases (BLP terrain sample, tree M2 and the
Goldshire inn WMO sample path). Every rendered acceptance capture uses Unreal's
screenshot request and must be opened and inspected as well as checked for
nonempty pixels.

The host builder normally reads `src/WowUnreal` and its pinned commit. Candidate
builds must explicitly record their source override; offline capture helpers
accept `-Source` for the same reason. Keep candidate provenance distinct from
the published installation proof. Before installing a published revision,
preserve local documentation, check current main, compare every runtime input
with staging, and compare the built, packaged and installed executable/package
hashes and the replay evidence. Local logs and ownership/approval fixtures stay
outside the repository. Blizzard assets remain in the user's MPQs.
