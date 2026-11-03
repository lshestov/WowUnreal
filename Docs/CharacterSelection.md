# Character selection (3.3.5a)

The layout follows the user's original `Interface/GlueXML/CharacterSelect.xml`:
Wrath logo at the upper left, realm and ten character rows at the right,
original additive selection highlight, name/level/class/location labels,
character name and Enter World at the bottom, rotation arrows, Create, Delete,
Change Realm and Back. Artwork, fonts and localized strings are loaded from
the user's MPQs. No game assets are shipped in this repository.

The character list includes the original tiled Glue tooltip background and
border, with an 85% opaque blue tint. Realm text occupies a separate clipped
line above Change Realm. List rows and Create stay inside that panel. The
character name, Enter World and rotation buttons occupy separate vertical
regions, including the transparent margins of the original button textures.
Selection text uses the original 15/12 Glue style sizes with a black
outline and explicit line heights; Enter World retains the larger label. Long realm names are
shortened against measured font width, with the full name in the tooltip.
The original 256x74 additive selection texture is aligned to contain all three
rendered text lines, including the location. Selected and hovered rows use the
same artwork; hovering another row leaves the selected row highlighted.
`29-character-selection-highlight.json` uses identical synthetic appearances
to compare first/middle/last selection and actual Slate hover without changing
the background scene or contacting a server.

The selected character uses the same original race/class M2 scene and corrected
depth/linear-colour capture as character creation. Gnomes share the Dwarf scene,
Trolls share Orc and Death Knights use their class scene. The full enumeration
appearance and equipment are passed to the character builder. Empty lists show
the Human scene without a character. Switching rows resets the camera and
coalesces pending model requests; screen changes release the rig and model.

A single row click highlights. A double click, Enter or the Enter World button
enters using the selected GUID. Up/Down changes the selection. Selection is
preserved across list refreshes while the GUID remains present. Empty lists
disable entry/deletion and a full ten-character list hides Create.

Deletion uses the original confirmation text and localized DELETE keyword.
The window follows `CharacterDeleteDialog` and `CharacterDeleteDialog_OnShow`:
512 source units wide, content-driven height, tiled `UI-DialogBox-Background`,
32-unit `UI-DialogBox-Border`, the 48-unit warning icon, native colour markup
in the 400-unit prompt, a separate small instruction, a 130x32 edit box with
the two original chat-input border textures, and 220x40 Okay/Cancel buttons.
The Okay button uses the original disabled texture until the keyword matches.
The modal captures input across the screen without adding a dark overlay.
The scene image remains enabled so Slate does not dim it. Enter on the focused
Cancel button cancels even when the DELETE keyword is already valid.
`28-character-deletion-dialog.json` captures empty/partial/valid keywords,
different scenes, the selected character line and the server-rejection dialog.
The dialog retains the exact selected GUID, prevents selection changes and
requires a nonempty matching keyword. Confirming locks the screen before
dispatch; duplicate actions are rejected. The controller validates membership
in the current enumeration and the session state before sending the existing
delete packet. Success waits for a fresh enumeration; failures unlock the
dialog. An ambiguous 30-second timeout disconnects and asks the user to check
the list after reconnecting, without sending the deletion again.

`Scripts/Verification/Invoke-CharacterSelectionCapture.ps1 -Name <unique>` runs
an ordinary offline rendered client. Its synthetic roster covers all ten races,
both genders, Death Knight, empty/single/full lists and deletion dialogs. Real
Slate controls exercise click/double-click/Enter/arrow navigation, repeated
entry, full/empty-list rules, retained selection, wrong/empty confirmation,
cancel and exact-GUID deletion dispatch. All account actions terminate in spy
delegates: no login, server entry, deletion or database access occurs. Captures
use `FScreenshotRequest`, with source/executable provenance, nonempty-image
hashes, enum appearance/equipment checks, GC checks and repeated screen cleanup.
Review each PNG before accepting an installed build.

For layout review, pass `-Spec Scripts/Verification/27-character-selection-layout.json`
and `-ResX`/`-ResY` to the capture script. Review 1280x900, 1280x720 and 1024x768:
full, four- and nine-character lists, bright/dark scenes, a long realm name, a single
character, an empty account and the confirmation dialog. The wrapper rejects
captures whose actual viewport dimensions differ from the requested size.
