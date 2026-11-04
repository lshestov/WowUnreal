# Combat delivery candidate for build 12340

This is a source candidate, awaiting the acceptance session's UE build and native
run. It does not supersede earlier verified-build reports in CombatLog.md.

## Reference audit

Compared with the local `src/wowdecomp` build-12340 client and
`src/azerothcore-wotlk` packet writers (outside this private clone):

| Documented family | Wire input / original implementation | Candidate finding |
| --- | --- | --- |
| Melee | SMSG_ATTACKERSTATEUPDATE; UnitCombatLog_C.cpp PushEvent | Zero mitigation and inactive critical/glancing/crushing use nil; active flags use number 1. |
| Spell/ranged/periodic/split/shield damage | 0x250, 0x24E, 0x24F; UnitCombatLogSpellDamage, UnitCombatLogSplitDamage | Same nullable damage suffix. A fully absorbed split stays DAMAGE_SPLIT and retains its caster; a zero ordinary damage record without mitigation is silent. Spell-school prefix remains DBC based; damage-school suffix remains packet based. |
| Shield/periodic/proc misses, reflection | 0x24B, 0x260, 0x263, 0x132; HandleSpellLogMiss, Packet_SMSG_SPELLORDAMAGE_IMMUNE, OnSpellGo | Immune packets retain their original shield/periodic family; diagnostic miss rolls remain silent. A reflected spell's nonzero secondary miss result follows REFLECT, against the original caster. |
| Heal / periodic heal | 0x150, 0x24E; HandleSpellHealLog, UnitCombatLogHeal | 0x150 must honor AttributesEx3 0x02000000. Critical uses 1/nil; amount, overheal and heal-absorb retain numeric zero. Empty heals are silent. |
| Environmental | SMSG_ENVIRONMENTALDAMAGELOG; CombatLogPackets.cpp EnvironmentalDamageLog::Write | Paired core writes amount, resisted, absorbed. Read that order, then publish the damage suffix with nil for absent mitigation/flags. Reject trailing or missing bytes. The decompiled client's local variable names disagree with this writer; the distinct-value fixture follows the paired core. |
| Cast / rejection | 0x131, 0x132, 0x130, 0x133; Spell.cpp SendSpellStart/Go/Interrupted; Spell_C.cpp OnSpellStartPacket/OnSpellGo | Previously ignored optional tails could publish truncated packets. Validate power, rune masks/cooldowns, missile, ammo, chain, immunity and destination data first. Preserve existing private/broadcast failure pairing. Timed spells normally produce CAST_START; the original CastGo emits CAST_SUCCESS only at zero effective cast time. |
| Energize / drain / leech | 0x151, 0x24E, 0x24C; AddPowerDrainEntry, UnitCombatLogPowerDrain | Drain gain is nil when zero. Classify leech using raw integral gain before power-display conversion. Zero raw amounts do not publish. Happiness remains outside combat-log drain delivery. |
| Dispel / steal / break / failed dispel / interrupt | 0x14F, 0x27B, 0x333, 0x262, 0x24C; ExtraSpell / PushEvent | Preserve event and extra-spell ordering; unavailable extra-spell name is nil, with school zero. |
| Summon / create / resurrection / extra attacks / durability | 0x24C; HandleSpellLogExecute, AddSpellCastEntry | Create versus summon follows the destination GUID high type, not the effect number. Zero extra-attack count is silent. All execute records validate before publication. |
| Enchantment / party kill / instant kill / building / unit death | 0x1D7, 0x1F5, 0x32F, 0x32, health updates | Existing metadata and event selection retained; nullable suffix contract also applies to building rows. |

`CombatLogEntry::PushEvent` and `GUIDToHexString` in UnitCombatLog_C.cpp also
require the string `0x0000000000000000` for an absent source/destination GUID.
Names remain nil when unavailable. The eight-field prefix is unchanged; there
are no retail raid-flag fields. A listener must use `select('#', ...)` to retain
trailing nil arguments. Historical tuples, filtered delivery and unfiltered
delivery share this contract.

## Focused editor checks (acceptance session)

Run with the user's `-wowdata=...`, then run the complete WowUnreal suite:

- `WowUnreal.Hud.CombatWrathTuple`: direct heal/damage/environment, complete tuple
  types/counts, distinct resist/absorb values, filter suppression, historical
  equality, all prefix truncations, fully absorbed split.
- `WowUnreal.Hud.CombatResourcesAndCreation`: transactional execute truncation,
  raw/display gain boundary, nil drain gain, GUID-based summon/create, periodic
  heal from DBC, empty heal/energize/extra-attacks suppression.
- `WowUnreal.Hud.CombatCastTrailers`: every truncation of combined optional start
  and go tails; no cast/history/launch/result mutation before full validation;
  private/broadcast failures in both orders, localized failures and filters.
- `WowUnreal.Hud.CombatMissFamilies`: immune/proc-resist family selection, debug
  miss suppression and ordered secondary miss results after reflection.
- Existing `WowUnreal.Hud.CombatFamilies`, `WowUnreal.Hud.CombatHistory`,
  `WowUnreal.Hud.CastingFeedback`, `WowUnreal.Gameplay.SpellGroundTargets`,
  `WowUnreal.Gameplay.SpellLaunchTargets`, `WowUnreal.Hud.MacroFlow` and
  `WowUnreal.Hud.MacroSequences`.

The GroundGo fixture now includes the server's destination sequence byte. Only
existing combat tuple expectations in IdleWowHudTests.cpp are adjusted.
No shared registration/build/module files change.

## Native listener, stock log and filters (acceptance session only)

Use an owned disposable priest with known Renew 139, Lesser Heal 2050 and
Smite 585, and sufficient mana. Open the original Combat Log tab and enable its
spell/heal/aura/failure quick filters. Load the complete
`Scripts/Verification/CombatLogProbe.lua` through the existing QA Lua bridge or
as a temporary addon. Keep Blizzard_CombatLog and its event handler running.
The steps below use original 3.3.5a APIs; await server responses between actions.

1. `IdleCombatProbe:Begin('SPELL_CAST_SUCCESS,SPELL_AURA_APPLIED',139)`; cast Renew
   on player. Expect one CAST_SUCCESS and one APPLIED in both arrays (remove an
   existing Renew first). `CheckHistory()` must pass. The stock log formats both
   entries with working spell links, no Lua errors or duplicate rows.
2. Let Renew tick with filter `SPELL_PERIODIC_HEAL`, spell 139. The unfiltered
   listener also sees unrelated supported rows; filtered rows contain only this
   spell/event/source. A tick has 15 arguments, numeric amount/overheal/absorb
   and nil critical when noncritical. Check the original quick-filter UI and
   spell hyperlink. Avoid exact tick-count assertions across timing windows.
3. `IdleCombatProbe:Begin('SPELL_CAST_FAILED',585)`; request Smite on player
   through the QA cast API and await the server rejection. Expect one
   CAST_FAILED in each array and one localized UI_ERROR_MESSAGE. No success for
   that attempt. Verify failure text in the stock log, with a zero destination
   GUID string and nil destination name. If the stock UI blocks this invalid
   target before sending, issue the known-spell request through the QA network
   bridge; a local-only error is not evidence of packet delivery.
4. `IdleCombatProbe:Begin('SPELL_CAST_START,SPELL_CAST_FAILED',2050)`; start Lesser
   Heal on player, then `SpellStopCasting()` before completion. Expect one
   CAST_START and one CAST_FAILED in each array, one interrupted UI error and
   no heal from this attempt. Private and broadcast packets must not duplicate
   the failure. `CheckHistory()` must pass.
5. Repeat interruption with the same spell, then complete a subsequent cast.
   Each new attempt remains visible. Success of this timed cast is shown by its
   heal and stock cast bar; do not require a combat CAST_SUCCESS for a timed cast.
6. Set an empty event filter with `IdleCombatProbe:Begin('',139)`, cast Renew
   again and verify unfiltered delivery continues while `filtered` is empty.
   Reset the filter and verify the stock log can refilter retained history.
7. Before ReloadUI record unfiltered history count/newest tuple through the QA
   bridge. Reload, reinstall the listener and verify retained count/newest tuple
   types and original combat-tab history. New events resume once each.
8. `IdleCombatProbe:Stop()`; cancel Renew, remove the temporary addon and delete
   only owned QA fixtures. Use the UE screenshot API to capture formatted stock
   log + successful cast, rejection and interruption, and validate each capture.

This private session does not launch UE, change shared staging/pins, create
server fixtures, cook, or publish. Native results remain acceptance work.
