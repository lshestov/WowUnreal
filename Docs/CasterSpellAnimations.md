# Character poses during spell casts

The reported Holy Light recording showed the character repeatedly swinging its
weapon while the cast bar progressed. `OnSpellStart` had hardcoded animation 53
(`SpellCastDirected`) as a loop for every local and remote caster. This is a
release gesture, rather than the spell's preparation pose.

`UWowAnimationController::PlaySpellAnimation` now resolves the original
`Spell -> SpellVisual -> SpellVisualKit.AnimID` chain. PrecastKit selects the
looping ready pose; CastKit selects the one-shot release after SPELL_GO.
Normal movement resumes after the release clip. Missing/no-animation kits do
not invent a gesture. The existing spell/counter guard still prevents an old
SPELL_GO from replacing a newer cast. Remote movement updates preserve the
ready pose while its server cast deadline remains active, including pushback.

| Spell | Ready animation | Release animation |
| --- | --- | --- |
| Holy Light 635/639 | 52 ReadySpellOmni (kit 99) | 54 SpellCastOmni (kit 270) |
| Flash of Light 19750 / Flash Heal 2061 | 52 ReadySpellOmni | 54 SpellCastOmni |
| Fireball 133 | 51 ReadySpellDirected (kit 30) | 53 SpellCastDirected (kit 38) |

This correction selects the kit's steady character animation. The existing
full-body runtime still has no original torso/keybone layering or generic
StartAnimID intro queue.

Verification: Windows Editor and packaged Development builds pass, along with
169 regression checks (165 clean, four existing warning cases). The new
`WowUnreal.Gameplay.SpellCharacterAnimations` test reads the original DBCs and
actual HumanMale model, checks healing and Fireball IDs, and exercises looping,
one-shot release and immediate idle restoration through the runtime controller.

The ModelViewer has opt-in `capturespell`, `capturespellshot`,
`capturespellrelease`, `captureweapon` and `captureweapondrawn` parameters.
Its game mode creates no network connection. Rendered Holy Light captures with
the original starter hammer (display 8690) show the ready pose and separate
raised-hand release. Captures use FScreenshotRequest, were checked for image
content and visually inspected. Servers were not started/restarted; no database
queries or character/account operations were used for this correction.

Host replay: `logs/wowunreal-ui-completion/Invoke-HealPoseCapture.ps1 -Name NAME`.
Regression: `logs/wowunreal-ui-completion/12-heal-pose-regression/index.json`.
