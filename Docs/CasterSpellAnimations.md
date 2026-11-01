# Character poses during spell casts

The reported Holy Light recording showed the character repeatedly swinging its
weapon while the cast bar progressed. `OnSpellStart` had hardcoded animation 53
(`SpellCastDirected`) as a loop for every local and remote caster. This is a
release gesture, rather than the spell's preparation pose.

`UWowAnimationController` resolves the original
`Spell -> SpellVisual -> SpellVisualKit` chain. PrecastKit plays an available
StartAnimID once and then loops AnimID. CastKit plays its release once after
SPELL_GO. ChannelKit loops its own AnimID while the matching server channel
remains active. Original channel selection does not reuse a cast intro.

The controller owns a spell/counter/phase identity. Duration refresh and
pushback preserve the playback clock and never replay an intro. A newer counter
starts a new intro. Old completion cannot replace a newer cast or downgrade an
already-started channel. Zero duration, removal or deadline expiry fades out the
layer; death and character-selection cleanup remove it immediately. Cancellation
also discards any queued clip.

| Spell | Ready animation | Release animation |
| --- | --- | --- |
| Holy Light 635/639 | 52 ReadySpellOmni (kit 99) | 54 SpellCastOmni (kit 270) |
| Flash of Light 19750 / Flash Heal 2061 | 52 ReadySpellOmni | 54 SpellCastOmni |
| Fireball 133 | 51 ReadySpellDirected (kit 30) | 53 SpellCastDirected (kit 38) |
| Blizzard 10 | 52 ReadySpellOmni | ChannelKit: 125 ChannelCastOmni |
| Arcane Missiles 5143 / Drain Life 689 | — | ChannelKit: 124 ChannelCastDirected |
| Swarm 52440 | Intro 54, then loop 125 (kit 10676) | Original cast kit when present |

The runtime samples independent locomotion and spell tracks. Original torso
keybone 4 (head keybone 6 as fallback) and its descendants select upper-body
bones after UE's parent-first skeleton remapping. Feet retain their locomotion
clip and speed while the upper body casts. At rest, the authored full-body spell
stance blends in; starting/stopping movement blends the lower-body contribution
over 120 ms. Spell time is independent of locomotion rate. This renderer follows
server state and does not authorize casting while moving.

The intro-to-loop queue carries elapsed time across a long frame. A missing
intro falls back to an available steady clip; an intro-only kit ends after that
clip. Missing animation kits do not invent a gesture. Models without either
torso/head keybone retain a bounded full-body fallback. Existing generated
UAnimSequence previews retain their steady-clip fallback; production M2 models
use the runtime layer.

Verification: Windows Editor and packaged Development builds pass, along with
170 regression checks (166 clean, four existing warning cases). The
`WowUnreal.Gameplay.SpellCharacterAnimations` test reads the original DBCs and
actual HumanMale model, checks healing and Fireball IDs, and exercises looping,
one-shot release, channel ownership/deadlines, refresh identity and intro
cancellation through the runtime controller. `WowUnreal.Animation.SpellLayers`
uses interleaved M2 bone order and UE remapping to check that feet keep moving,
torso changes, the two clocks remain independent, transitions blend, missing
clips fall back and death clears the layer.

The ModelViewer has opt-in `capturespell`, `capturespellshot`,
`capturespellrelease`, `capturespellchannel`, `capturespellmove`,
`capturespellcancel`, `capturespelldelay`, `capturespellafter`, `captureweapon`
and `captureweapondrawn` parameters.
Its game mode creates no network connection. Rendered Holy Light captures with
the original starter hammer (display 8690) cover a moving ready pose and Renew
release, directed and omni channel poses, an intro and subsequent hold, and
both channel/intro cancellation. Eight initial FScreenshotRequest captures were
checked for content and visually inspected. Build and regression pass; the
published-install repeat remains pending before final signoff. Servers were not
started/restarted; no database queries or character/account operations are used.

Replay cases: `Scripts/Verification/13-caster-pose-offline.json`.
Host replay: `logs/wowunreal-ui-completion/Invoke-CasterPoseCapture.ps1 -Name NAME`.
Regression: `logs/wowunreal-ui-completion/13-spell-layers-regression/index.json`.
