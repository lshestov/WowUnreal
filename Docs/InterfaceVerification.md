# Isolated native UI scenarios

Scenarios that award items, spend currency, create guilds or update achievement
progress must use a disposable primary character, rather than restoring a
user's real progress after the fact. The project coordinator provisions a fresh
local QA account, records its salt/verifier registration hash, and checks that
it owns the exact character before changing or removing anything.

`WowQaCharacter` is a headless commandlet for this lifecycle:

```
UnrealEditor-Cmd WowUnreal.uproject -run=WowQaCharacter -fixture=<private-path>/wowunreal-ui-primary-fixture.json -mode=prepare -nullrhi -unattended
```

`prepare` creates a Human Mage (or an explicitly selected Human Warrior QA peer) through the 12340 character-create protocol and
disconnects at the character list, before world entry. `delete` requires the
recorded nonzero GUID and removes that character through the ordinary protocol.
`disband` enters a recorded QA leader only after matching its guild ID/name and
verifying every roster GUID belongs to that fixture. It sends the ordinary guild
disband request, waits for the membership acknowledgement and disconnects.
`disband_arena` applies the same ID/name/complete-membership ownership checks to
the fixture's recorded QA arena team, then waits for its actual player-field
membership removal. It cannot delete or disband a user's arena team.
All modes require an isolated `QA_UI_` account, a matching `Uiq` character, a
hex ownership token, at most one character on the account, and the project's
single loopback realm at 127.0.0.1:8085. Unexpected identities fail closed.

Credentials are read from a private fixture and never passed on the command
line. The native scenario uses its own UserDir and credential store. The outer
coordinator waits for logout, validates registration ownership again, deletes
the temporary account, removes its credential file, and compares hashes of all
pre-existing characters and their inventory records. Failure leaves a recovery
fixture instead of deleting unrelated data or concealing a state difference.

The first lifecycle check successfully creates and prepares the character,
autologs into the rendered world, captures its original HUD with Unreal's
screenshot API, and deletes the character/account. All pre-existing character
and inventory records remain unchanged. The Windows Editor and packaged game
build successfully; the native image is nonempty and visually inspected.

Server credentials, fixture JSON and extracted MPQ data are private project
inputs and are not stored in this repository.

Native profiling skips the first observation because Unreal's frame and thread
counters describe the preceding frame, outside the requested interval. Profile
intervals run for their requested duration, up to 60 seconds. Initial FrameXML
bootstrap remains visible in the startup log and is separate from world-streaming
and steady gameplay measurements.
