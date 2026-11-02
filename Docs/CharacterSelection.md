# Character selection (3.3.5a)

The layout follows the user's original `Interface/GlueXML/CharacterSelect.xml`:
Wrath logo at the upper left, realm and ten character rows at the right,
original additive selection highlight, name/level/class/location labels,
character name and Enter World at the bottom, rotation arrows, Create, Delete,
Change Realm and Back. Artwork, fonts and localized strings are loaded from
the user's MPQs. No game assets are shipped in this repository.

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
