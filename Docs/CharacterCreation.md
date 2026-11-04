# Character creation (3.3.5a)

The creation screen follows the stock `Interface/GlueXML/CharacterCreate.xml`
layout: faction columns, race/sex/class icons, five customization arrow rows,
race/class descriptions, name entry, rotation buttons and Glue dialogs. The
layout is adapted to Slate; its artwork, atlas coordinates, fonts and localized
strings are read from the user's MPQ at runtime. No game assets are included.

The background is the original race creation M2, including its animation,
geometry, particles, camera and character attachment zero. Gnomes use Dwarf,
Trolls use Orc, and Death Knights use their class scene, as in the original
client. Camera projection follows `GxuXformCreateProjection_SG`: the stored
angle is divided by `sqrt(aspect*aspect+1)` before converting vertical to
horizontal FOV. Dragging rotates the character; the wheel changes bounded
preview zoom. The isolated capture runs at 30 Hz. This is separate from the
account-login background and music.

Opaque and alpha-key background sections use depth-writing M2 materials;
transparent effects retain their particle materials. Resolved skin triangle
indices are remapped into compact section buffers instead of treating skin
lookup ranges as direct M2 vertex ranges. This preserves the original ordering
and geometry, including nonidentity lookups in the Blood Elf scene.

The capture uses a linear floating-point target with gamma 1. Slate performs
the display conversion once. Filmic tonemapping and bloom are disabled for
the preview, preserving the source artwork's shadow detail and colour values.

`RaceLights` comes from the original `GlueParent.lua`. Directional lights merge
using `CM2Lighting::SetupSunlight`; the ambient term includes its residual energy
and uses the existing character-material ambient parameter. Preview lights use
a dedicated lighting channel. The preview does not change world
lighting or streaming. Authored scene point lights retain their animated
colours, bone transforms and original falloff through `FWowM2Lighting`.
The built-in point-light exponent stays slightly above zero: Unreal's
`LightData.ush` uses zero as the inverse-square sentinel. The authored light
function supplies the actual distance falloff. Light/view channel 1 isolates
these lights from the default world view.
The data-only Lua VM has no gameplay API or addon
SavedVariables writes. Atlas ADD highlights use the existing region material
with explicit UV corners, rather than sampling the entire icon atlas.

## Appearance and equipment

* `CharBaseInfo.dbc` has two-byte records and supplies the 62 valid Wrath
  race/class combinations. Class names/tokens come from `ChrClasses.dbc`.
* Appearance controls enumerate actual `CharSections.dbc` IDs with the original
  creation flags, including Death Knight variants. Face depends on skin;
  hairstyle/colour and facial features retain their original dependencies.
* Choices are remembered by race, sex and normal/heroic appearance context.
  Direct race selection preserves a class that is valid at the destination.
  Invalid combinations cannot be selected or submitted.
* Facial hair maps the five DBC geoset columns to groups 1, 3, 2, 16 and 17.
  Authored zero values are retained. Death Knight/flagged-face eyes use 1703.
* `CharStartOutfit.dbc` uses the original 296-byte layout: packed identity,
  item IDs, display IDs and inventory types. The creation outfit hides helmets;
  hunters display their ranged weapon instead of overlapping a melee weapon.
  Shields use the original left-wrist attachment.

Creation and character-list previews use the same full character builder. List
previews receive appearance and equipment from `SMSG_CHAR_ENUM`, not just race
and sex. The preview explicitly retains its animation controller through GC.
Every screen owns one rig, one scene and one character. New requests coalesce
before the next tick; actor/component references and capture/input ownership
are released on screen changes. There are no delayed capture callbacks.

## Creation protocol

The form submits the displayed name and all eight identity/appearance bytes.
An in-flight request freezes the form; the widget and connection both prevent
duplicate sends. Original 12340 response codes distinguish name errors, account
limits, expansion/race restrictions and the Death Knight level requirement.
Success waits for the refreshed list. A missing response disconnects after
30 seconds and asks the player to reconnect/check the list, without retrying an
ambiguous creation. Server restrictions are unchanged.

## Reproducing verification

`WowUnreal.CharacterCreation` automation covers original DBC records, sparse
IDs/dependencies, exact geoset mappings, saved choices, isolated Lua data,
original artwork/scene availability and submission state. Run it together
with the existing full UI/gameplay/rendering/lighting/performance regression
and `WowUnreal.Login`.

`Scripts/Verification/Invoke-CharacterCreationCapture.ps1 -Name <unique-name>`
runs 36 rendered cases across every race/sex, multiple classes and boundary
appearances, individual appearance/identity button presses, live GC checks, and twelve repeated close/open cycles with queued
selection changes. Captures use `FScreenshotRequest`. Review every PNG after
the wrapper exits; its report also validates nonempty unchanged images.
A four-patch linear grey card checks the final screenshot against independent
sRGB values (63, 128, 188, 255), with eight levels of tolerance per channel.
This catches crushed shadows, double gamma and an invisible captured scene.

`Invoke-CharacterCreationNative.py` creates a fresh owned account for a declared
race/class/sex route. It presses the stock Slate controls, checks short-name,
heroic-level and duplicate-name server rejections, creates the customized
character, compares enum/database appearance, and returns through the list.
The participant never enters the world. Protocol deletion requires the exact
registered account, name, GUID, race, class and sex. Cleanup waits for the
participant to go offline and removes its credentials. Other characters can
change between runs without a confirmation step.

The install verifier checks published main, all tracked runtime inputs,
stage/package/installed hashes, clean regression, reviewed captures, owned
fixture cleanup and an ordinary owned world/native regression. The build and
verification distinguish `workingTreeModified` from `runtimeInputsModified`:
only unstaged edits to `Docs/WorldPerformance.md` and `IMPLEMENTATION_PLAN.md`
may remain local. They are neither published nor described as a clean checkout.
