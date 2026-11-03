# Wrath AccountLogin scene

The Wrath login background uses the user's build-12340 MPQs at runtime. No
Blizzard model, texture, font, shader bytecode or sound is embedded in this repo.
The `M_WowGlue*` assets contain our material graphs and engine placeholder
textures; `Scripts/CreateLoginSceneMaterials.py` regenerates them.

## Source evidence

* `Interface/GlueXML/AccountLogin.lua`: non-trial model
  `Interface/Glues/Models/UI_MainMenu_Northrend/UI_MainMenu_Northrend.m2`,
  camera 0, sequence 0. The sequence lasts 66667 ms and contains 288 bones,
  64 geometry batches, 40 particle emitters and one camera with 93 position keys.
* `AccountLogin.xml`: ModelFFX glow 0.08 (quantized to 20/255 by the original
  renderer). The original source implementation and the user's FFX shaders
  specify quarter-size Box4, horizontal/vertical Gauss4 and `base + blur² * glow`.
* `GlueParent.lua`: `CurrentGlueMusic = "GS_LichKing"`. SoundEntries.dbc ID 12765
  resolves `Sound/Music/GlueScreenMusic/WotLK_main_title.mp3`: stereo 48000 Hz,
  544.080 seconds. The scene's four `$DSO` events resolve sound IDs 14143–14146
  at 7000, 10533, 15067 and 26233 ms.
* Glue UI BLPs, FRIZQT font and localized strings supply the Wrath login controls.
  Connection host and existing expansion choices remain available through Options.
  This change does not implement the original external account/credits/TOS flows
  or claim pixel-identical reproduction of the entire Glue UI.

## Rendering and lifetime

`AWowLoginScene` owns an isolated SceneCapture2D, the M2 renderer, one display
render target, two quarter targets and dynamic FFX materials. The source camera
splines, roll and diagonal FOV drive framing. The scene has its own capture list
and source M2 directional lighting; it does not add lights to the game world.

Glue rendering opts into source two-texture combiners, secondary/environment UVs,
texture addressing, display-space blending and original priority planes. Separate
procedural mesh primitives per priority prevent sky/cloud layers from covering
the dragon and foreground particles. Existing spell/character rendering keeps its
previous defaults. `ffxGlow` controls the original FFX pass without substituting
Unreal bloom.

Clearing the login screen destroys its scene, geometry, emitters, capture and
render targets. The controller shares one `AWowAudioManager` between lobby and
world. Menu transitions keep the title track; entering the world stops it and
removes its decoded cache entry. Returning to the lobby restarts the original
track. Finite model sounds have explicit deadlines and are stopped on scene exit.
EndPlay stops owned components. No background timers or external decoder process
are needed for music. UE's bundled static MP3 decoder runs asynchronously;
compressed bytes and immutable PCM are bounded and safe if its owner is destroyed.
Audio options use the existing UI API and persist in `Saved/Config/WowAudio.ini`.

## Verification

Run build/cook, automation and rendered clients sequentially. Preserve any manual
client. The local automation selection includes Hud, UI, Gameplay, Player,
Collision, Equipment, Lighting, Rendering, Traversal, Performance, Animation,
World, Login and CharacterCreation (195 tests).

`Invoke-LoginCapture.ps1` runs the installed executable with the opt-in
`WowLoginTestGameMode`, an isolated UserDir and blank credentials. Its login action
is a spy: the replay never authenticates or accesses the database. The acceptance
specification has 34 steps, 16 engine screenshots and ten menu transitions,
including delayed GC, validation/error display, realm/selection/creation,
simulated loading, return and repeat. Captures cover arrival, landing, roar,
departure, flight, late cycle and looping. Run it at 1280x900, 1280x720 and
1024x768 and inspect every final PNG individually.

The replay records the real XAudio2 master submix. `Test-LoginAudio.py` checks a
nonzero waveform, music/master mute, volume changes, uninterrupted cursor while
muted, stop for simulated world entry and restart on return. This proves actual
mixer output, not merely an `IsPlaying` flag. It does not prove physical-speaker
audibility or an actual network/world transition.

`Verify-LoginInstall.ps1 -Runs <three run names> -Regression <report name>
-Reviewed` checks published revision, clean runtime inputs (with the two explicitly
local documents reported separately), source/stage hashes, package/install hashes,
regression results, image hashes/dimensions, audio evidence and closed clients.
Its report explicitly separates offline login verification from native server QA.

For real authentication/world/logout/re-entry use only a disposable owned QA
account. Compare protected GUIDs 1, 2, 4 and 5 against the last closed baseline
before creating fixtures. A changed protected baseline requires the user's
explicit approval; never silently accept it, restore a dump or modify user rows.
`assert_lobby_lifecycle` in the native gameplay probe can check zero login scenes
in world, one preview in selection and a single shared audio owner.
