# Unit portraits

The original FrameXML `SetPortraitTexture` path uses a scene capture of the
resolved unit, including its visible equipment and customization meshes.
Circular masking remains part of the UI image material.

12340 M2 camera records are 100 bytes. Camera type 0 supplies the portrait's
position, target and field of view; type 1 belongs to the character panel.
Position and target spline values are offsets from separate base pivots.
The beginning of the camera track is used for the portrait view. Bounds,
camera-array offsets, optional spline offsets and finite values are checked
before use. See the [M2 camera record definitions](https://skarndev.github.io/wowlib/python/m2/records/).

The skeletal mesh retains the portrait camera as runtime asset user data.
Its positions pass through the same axis swap, centimetre conversion and
component transform as the model. This preserves display scale and facing
without borrowing the NPC's oversized click-selection capsule. Models without
an authored camera retain the existing model/head framing fallback.

The `WowPortraitHead` socket also explicitly rebuilds the skeletal socket map.
Adding to the mesh socket array alone leaves runtime component lookups empty.
The regression fixture uses an M2 bone order different from UE's parent-first
order and verifies the resolved bone, camera selection and coordinate conversion.

Native verification covers the local player, Deputy Willem and a diseased timber
wolf, with nonempty Unreal screenshots inspected visually. Target reaction
colours are tracked separately in the interface implementation plan.
