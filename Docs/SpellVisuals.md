# Wrath spell visual data

The build-12340 loaders require the actual DBC layouts: 32 columns for
SpellVisual, 38 for SpellVisualKit and 7 for SpellVisualEffectName.
SpellVisual keeps sound/flags/impact-kit identifiers before the projectile
attachment; its six position offsets occupy columns 26 through 31. Ground
following values retain their signed integer representation.

SpellVisualKit has no AnimKitID column between its animation IDs and head
effect. Head/chest/base/hand/breath/weapon effects, world/special effects, sound,
shake and flags now use their own columns. Animation IDs preserve -1 sentinels.
Effect names retain their model scale limits, and model extensions normalize
case-insensitively to the M2 files stored in MPQ.

The layouts were checked against the actual local MPQ headers and the
[Wrath client record definitions](https://github.com/SatyPardus/wowdecomp/tree/main/WoW/Source/DB/Rec).
No Blizzard assets are included in this repository.

Two regression checks cover distinct fixture values, signed fields, offsets,
scale bounds, unsupported schemas and real spell/kit/model/sound references
for Fireball, Frostbolt, Arcane Intellect, Smite, Flash Heal, Renew and Power
Word: Shield. The full regression passes 150 checks (four existing warning
cases).

A packaged-client Fireball route exposed a separate existing fatal error:
Initialize used ConstructorHelpers::FObjectFinder outside a constructor when
loading the projectile's fallback material. It now uses LoadObject; the
dynamic material retains its parent through Unreal object references.
The 16-step rendered route completes cast, projectile release and arrival,
with three validated and visually inspected FScreenshotRequest captures.

This is the data foundation and crash repair. Rendering original M2 cast kits,
projectiles, impacts, healing and state/auras; actual attachment transforms;
concurrent caster ownership; and event timing remain in the spell visual task.
The legacy school-colored sphere/glow renderer does not establish visual parity.
