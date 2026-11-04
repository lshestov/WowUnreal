# Macro conditions: build 12340 regression candidate

`SecureCmdOptionParse` tests predicates against the current target unless a
condition group specifies `@unit` or `target=unit`. It returns a second value
only for an explicit target. In particular, `[]`, `[nomod]`, `[help]` and
`[combat]` must preserve a nil command target. The stock `ChatFrame.lua`
`ASSIST` handler uses that distinction to interpret a following unit name;
`CastSpellByName` and item actions use it to apply configured modified clicks.
Each alternative group selects its own target before evaluating predicates,
including when the unit selector follows `help` or `nodead`.

The candidate fixes the implicit target, excludes self from `[party]`, accepts
slash-separated party/raid alternatives and uppercase modifier arguments, and
supports `[mod:SELFCAST]` through the existing saved modified-click assignment.
False groups do not dispatch a command. A rejected native request does not cause
the parser to try another clause. An empty successful result remains a Lua
truthy string, so `/stopmacro []` and `/stopcasting []` execute correctly.

Native state fixes use `FLYING=0x02000000` and `FALLING=0x00001000`;
`0x00800000` means descending and `0x00000002` means moving backward.
`IsStealthed` reads `SPELL_AURA_MOD_STEALTH=16` from Spell.dbc instead of a
three-spell allowlist, and ignores inactive or expired auras. All four Stealth
ranks, three Prowl ranks and Shadowmeld have this aura in the local client.
Invisibility uses a different aura and remains false.

`Scripts/Verification/Test-MacroConditions.py` takes `--lua` (a Lua 5.1 executable)
and `--chat-frame` (the user's extracted original ChatFrame.lua). It extracts the
production runtime from `WowMacroRuntime.h` and runs `MacroConditions.lua` with
unmodified stock secure command handlers and recorded native request boundaries.
The 90 assertions cover target priority, absent/dead/friendly/hostile units,
comma AND, adjacent-group OR, slash alternatives and negation, modifiers,
forms, pets, group, bars, equipment, channeling, vehicles, cursor and movement
conditions, `/cast`, `/use`, `/assist`, `/focus`, `/stopcasting` and `/stopmacro`.
`--runtime-header` can select the base header to reproduce the failing `[] Heal`
case. Blizzard source and DBC files are local inputs, never repository assets.

The local reference ChatFrame.lua SHA256 is
`2506a6ec7db6c3af66a279b963940360395a1680137b8570988f4dc824c87029`.
Real macro patterns were cross-checked against the author-published
[MacroActions 3.3.5 addon](https://www.curseforge.com/wow/addons/macroactions)
(July 2010, including its `mod:SELFCAST` action text) and
[3.3.5a macro collection](https://github.com/Stradios/World-of-Warcraft-3.3.5a-Wrath-of-the-Lich-King-Macro-Collection)
(focus, mouseover fallback and trinket patterns). These supply usage examples;
the command behavior is exercised using the user's original FrameXML handlers.

Engine automation is isolated in `IdleWowMacroConditionTests.cpp`:

- `WowUnreal.Hud.MacroConditions` runs the same Lua matrix against stock handlers
  loaded directly from MPQ (`-wowdata=<Data directory>`).
- `WowUnreal.Hud.MacroConditionState` uses actual unit APIs and local DBCs for
  flight/falling/swimming flags, eight stealth spells, aura expiry/removal,
  invisibility, party-self exclusion, implicit targets and configured SELFCAST.
- Existing `WowUnreal.Hud.MacroFlow`, `WowUnreal.Hud.MacroSequences` and
  `WowUnreal.Hud.ModifiedClicks` should also remain green.

`Scripts/Verification/34-macro-conditions-native.json` is a 15-step stock action
bar scenario for the acceptance session's gameplay runner. It requires the
existing `Smrleadhcwxg` QA character, empty action 84, no selected target/focus,
known Heroic Strike (78), and one free character macro slot. It creates only `QAMacroCond`, opens the
original macro editor, places the macro through `PickupMacro`/`PlaceAction`,
clicks `BonusActionButton12`, and verifies local command success/refusal and
event counts. It removes the macro/action/listener at the end. The acceptance
runner must restore owned state on interruption as for existing macro scenarios.
No spells, inventory, money or server fixtures are changed. Existing native
`test-wowunreal-macro-roundtrip.ps1` remains the server success/rejection check.

Local verification: 90 Lua checks pass; the base runtime fails on `[] Heal`.
A separate MSVC harness compiled the unchanged production bodies of
`L_IsFlying`, `L_IsFalling`, `L_IsSwimming` and `L_IsStealthed` against test context
objects and the local DBC aura values: 24 state assertions pass. This does not
replace compiling WowUI or running the new UE automation tests. Full UE build,
runtime screenshots and server tests are reserved for the acceptance session.

Remaining state-provider gaps found during the audit: `mouseover` currently
resolves to zero; `UnitCreatureFamily` returns an empty string; `UnitHasVehicleUI`
returns false. The matrix covers their parser behavior through controlled API
values, without claiming native support for these providers. Native ghost-state
and flyable-area edge cases also need separate coverage. Full FrameXML parity
is still pending.
