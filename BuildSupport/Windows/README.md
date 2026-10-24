# Windows x64 build

Prerequisites: Unreal Engine 5.7 with Windows support, Visual Studio 2022 with
Desktop development with C++, a Windows SDK, and CMake 3.20 or newer.
This fork was built with UE 5.7.4, MSVC 14.44 and Windows SDK 10.0.22621.0.
Supply your own unmodified WoW 3.3.5a MPQ archives; none are included here.

## Native dependencies

Run these commands in PowerShell from the repository root. StormLib, bundled
zlib/bzip2, Lua 5.1.5 and pugixml come from the tracked `Source/ThirdParty` tree.
The libraries use the release DLL CRT (`/MD`) required by Unreal Development.

```powershell
$repo = (Resolve-Path .).Path
$native = Join-Path $repo 'Intermediate/WindowsDependencies'
cmake -S "$repo/BuildSupport/Windows" -B $native -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_INSTALL_PREFIX=$native/install"
cmake --build $native --config Release --parallel 4 -- /nodeReuse:false
cmake --install $native --config Release
$libraries = Join-Path $repo 'Source/ThirdParty/Windows/x64'
New-Item -ItemType Directory -Force $libraries | Out-Null
Copy-Item "$native/install/lib/StormLib.lib", "$native/install/lib/lua51.lib" $libraries
& "$native/Release/dependency-smoke.exe" 'D:/YourWoW335/Data/enUS/locale-enUS.MPQ'
```

The smoke test executes Lua, parses XML and reads `ChrRaces.dbc` through the
same UTF-8/UTF-16 Windows MPQ wrapper used by the client. It opens the archive
read-only. A successful run prints three PASS lines and exits with code zero.
Stop on any failed command before building the client.

## Editor and packaged client

```powershell
$ue = 'D:/Epic Games/UE_5.7' # Your installed engine
& "$ue/Engine/Build/BatchFiles/Build.bat" WowUnrealEditor Win64 Development "-Project=$repo/WowUnreal.uproject" -WaitMutex -NoHotReload
& "$ue/Engine/Build/BatchFiles/RunUAT.bat" BuildCookRun "-project=$repo/WowUnreal.uproject" -nop4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive "-archivedirectory=$repo/Saved/WindowsPackage" -unattended -utf8output
```

All Windows fixes, regression tests and cooking settings live in this
repository. No source patching or external override directory is required.
Generated binaries and libraries are ignored by Git.

Runtime mesh builders bind each polygon group's material slot by name before
building. Editor builds can infer missing bindings, while a packaged game
otherwise produces section material index -1 and displays the default gray
material. `WowRuntimeMeshMaterials` keeps both build paths consistent.

The tracked sky, cloud, celestial, distant-terrain and liquid material assets
are cooked from `Content/Materials`. They can be regenerated with
`Scripts/CreateWorldEnvironmentMaterials.py` using the editor Python commandlet
and `-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities`. No Blizzard
textures are embedded in these assets; ground and object BLPs remain in MPQ.

## Runtime configuration

Launch the packaged `Windows/WowUnreal.exe` with
`-wowdata="D:/YourWoW335/Data"`. Alternatively, place `idle-wow.json` beside
that executable, containing a `DataPath` string. A relative `DataPath` resolves
against the directory containing that JSON file; `-wowdata` takes priority.
The local workspace layout also supports `../client-enUS-3.3.5a/Data` as a
fallback. MPQ data remains external to the package.

The login screen defaults to `127.0.0.1:3724` (authserver). Its known local
worldserver address `127.0.0.1:8085` is rejected before sending an auth packet.
Other auth hosts and ports can be entered normally. Accounts and passwords
are entered in the client and stay in the local Saved directory.

The default display mode is a 1280x720 window. Existing user settings can
override defaults; `-windowed -resx=1280 -resy=720` explicitly requests a window.
Runtime-created water, WMO liquid and WDL material slots initialize their UV
metadata, and NPC death retains the entity until server removal.

### Automatic world entry and screenshots

`-autologin` submits the saved default account, selects the first realm and
enters with the first existing character. Optional `-account="Profile Alias"`,
`-realm="Realm Name"` and `-character=CharacterName` select specific saved
profiles and destinations, ignoring case. Missing names stop automatic entry
instead of falling back to another account or character. No character is
created by this flow. Passwords stay in the local credential store.

`-worldscreenshot=world.png -worldscreenshotdelay=5` captures through Unreal's
screenshot API after initial terrain loading, player placement and removal
of the loading screen. Pending ground collision is logged and does not block
the screenshot, so incomplete world rendering can be diagnosed. Relative paths resolve under
`Saved/Screenshots`. `-quitafterworldscreenshot` exits with code zero after
saving, or a nonzero code on failure. `-autologintimeout=120` bounds the wait
for world readiness. Without the quit flag, errors remain visible in the UI
and manual login remains available.

The local multi-client workspace provides `scripts/capture-wowunreal-world.ps1`
to use its existing private verification profile, wait for a screenshot and
close only the client it launched. It supplies no passwords on the command
line and does not need Computer Use. Regular manual startup is unchanged.

### Packaged player placement and model

Terrain uses a hidden procedural mesh for asynchronous runtime collision
cooking. World entry waits at the server position until this collision is
ready, then snaps the capsule to the ground. Server movement coordinates
represent the character's feet; the capsule half-height is added only for
local placement. Repeated logins do not accumulate extra altitude.

Packaged targets build M2 skeletal render buffers directly, without the
editor import pipeline. The local character is visible from the normal
third-person camera. `UWowM2AnimInstance` samples parsed M2 bone tracks directly
in both Game and Editor. Translation and scale use the original key times;
rotations use quaternion interpolation with the mesh's coordinate conversion.
State changes blend over 120 ms. Worker evaluation consumes a copy of local
transforms, with no access to the M2 cache or gameplay objects.

Movement selects Stand, Walk or Run from actual velocity and the walk toggle.
Jump takeoff, airborne/fall and landing use their M2 clips. Existing attack,
cast and emote handlers share this playback path; one-shots return to the
movement pose when complete. Missing clips are reported to the caller so its
fallback chain can select an available animation.

For a rendered clip inspection, add `-captureanimation=5` alongside
`-autologin -worldscreenshot=run.png`. It previews the Run clip without moving
the player. The workspace capture script exposes this as `-AnimationId 5`.
Without that option, screenshots show normal gameplay animation selection.
`AnimationCapture` logs the clip time and actual mesh bone-pose diagnostics.

### NPC appearance and movement

Equipment uses the 25-column WotLK `ItemDisplayInfo.dbc` layout: three geoset
columns and eight clothing texture columns. Clothing regions come from those
column indices, rather than filename suffixes. Character enumeration preserves
appearance and inventory slot numbers; local-player setup owns its selected
character record after the copied character list goes out of scope.

Attached equipment has a unique component per item and attachment point,
explicit runtime material-slot bindings and the cookable character material.
Attachments use model-space positions relative to the reference bone pivot.
Moving a temporary character model onto the player also copies its equipment;
repeated model changes clear old parts instead of accumulating duplicates.
Visual item meshes opt out of collision cooking; targeting remains on the
character's collision components.

Creature displays resolve their model and all three replaceable monster skins.
Each mesh section keeps its literal or replaceable texture. Humanoid NPCs use
`CreatureDisplayInfoExtra.dbc` customization, baked clothing textures, equipment
geosets and separate hair following the body's pose and transform. A fallback
composite outfit never modifies the shared cached skin texture.

NPC actor transforms are applied after creating the scene root. Server facing
is converted once; the skeletal component supplies the model's basis rotation.
Monster movement decodes packed offsets, full spline points, final facing and
optional animation/parabolic payloads. Travel time is distributed by segment
length; locomotion uses the route speed and adjusts playback for model scale.
Full-point routes currently follow a polyline; smooth Catmull-Rom curves and
parabolic height simulation are not implemented by this interpolation path.

The workspace capture script accepts `-NpcDisplayId 3275` to frame a real
loaded NPC without moving the player. `-NpcDisplayId 31049 -NpcMoving` waits
for actual server-driven locomotion before capturing. NPC captures omit UI;
the log records position error, geometry, textures and evaluated bone poses.

### World collision and NPC support

WMO groups retain MOPY triangle flags. Collision includes explicit collision
faces, ordinary render faces without the detail flag, and material 255 faces.
M2 scenery uses its dedicated bounding vertices/triangles; visible foliage and
cloth do not become blockers. Both formats create hidden static procedural
collision meshes with asynchronous runtime cooking, independent of the visual
static mesh's cooked data. Incremental doodad removal destroys its collider too.

NPC support probes process at most 16 entities per frame. A local downward
trace places reference-pose feet 2 cm above terrain or architecture. This is a
visual offset: server positions and spline routes retain their original values.
Flying, hovering, swimming and falling entities skip this adjustment. Initial
player placement searches nearby walkable layers with capsule clearance, so a
floor can replace terrain beneath it without selecting a distant roof.

NPC screenshots log `NpcGroundCapture` (feet, support surface and gap) and
`NpcGroundingCost`. The captured position error can include the deliberate
vertical support offset.

## Regression tests

Run each filter separately with the editor command-line executable:

```powershell
& "$ue/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$repo/WowUnreal.uproject" '-ExecCmds=Automation RunTests WowUnreal.Login.AuthEndpoint' '-TestExit=Automation Test Queue Empty' -unattended -nop4 -nosplash -NullRHI -NoSound
```

Also use `WowUnreal.World.DeathPreservesEntityUntilServerDestroy` for the
entity lifetime regression and `WowUnreal.Network` for protocol tests. These
tests use no account credentials and do not connect to a server. NullRHI is
appropriate for these logic tests; it does not validate world rendering.
`WowUnreal.Login.AutoLogin` covers command-line parsing and explicit account,
realm and character selection, including missing profiles and empty lists.
`WowUnreal.Rendering` checks explicit runtime mesh bindings and the cookable
world material contracts. `WowUnreal.Network.QueuedPacketSocketLifetime`
checks that a queued packet cannot dereference a destroyed world socket.
`WowUnreal.Player` checks runtime skeletal render buffers, login placement
and camera distance, and ground tracing against asynchronously cooked terrain.
`WowUnreal.Animation` checks nonuniform key times, looping and one-shot clocks,
bone remapping, transitions, movement selection, controller transfer and actual
skeletal-component evaluation through the native animation proxy.
`WowUnreal.Npc` checks appearance record fields, server route packets and
models from the user's existing MPQs. The model integration test requires
`-wowdata=<client Data directory>` and uses no server account.
Datasets without the optional `CharComponentTextureLayouts.dbc` and
`CharComponentTextureSections.dbc` use the existing built-in atlas layout;
the MPQ integration test reports the loader's two warnings in that case.
`WowUnreal.Equipment` checks the WotLK item record columns, actual starter
clothing pixels, both of Deputy Willem's shoulders, cooked material bindings
and equipment transfer across source-actor destruction and garbage collection.
Its integration test also requires `-wowdata=<client Data directory>`.
`WowUnreal.Collision` checks asynchronous runtime floors, capsule wall sweeps,
layer selection, WMO collision flags and collider cleanup. With `-wowdata`, it
also parses Northshire Abbey's actual groups, traces its floor, sweeps its wall
and checks a real crate's dedicated M2 bounding geometry.
