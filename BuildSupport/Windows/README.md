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
