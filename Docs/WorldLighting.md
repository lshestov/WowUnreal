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

The remaining production changes and their acceptance evidence are recorded
below as each lighting task is verified. The three plan items remain open.
