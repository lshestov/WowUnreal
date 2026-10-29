# Combat interface and character statistics

The original FrameXML CastingBarFrame owns cast feedback when available. The
legacy Slate fallback is collapsed in that case, including after UI reloads;
it remains available when the original frame has not loaded. Cast start,
progress, interruption and completion still follow the server-driven stock UI.

GetSpellBonusDamage reads the signed positive and negative player modifiers for
the requested school (1 through 7). GetSpellBonusHealing reads the server's
healing modifier. Neither derives an invented spell-power value from intellect
or spirit. GetManaRegen reads the two server-provided per-second mana rates,
retaining Wrath's rounding epsilon and zero results for other active resources.

The character UI polls bounded stat-field groups at the existing 200ms state
update interval. Changed attributes, resistance/armor, attack speed and damage,
attack power, spell modifiers, ratings and level emit their original events.
Snapshots commit before callbacks, unchanged state stays silent, and character
identity changes clear the previous snapshot. This updates an open character
window after buffs or equipment changes without rewriting its FrameXML.

The API behavior and signed field semantics were checked against the paired
AzerothCore build-12340 update fields and the Wrath client script definitions.
The regression uses distinct spell-school bonuses/penalties, unrelated large
intellect/spirit values, changing fields, both mana rates, invalid schools,
non-mana resources, missing players and actual event argument contracts.

Verified: Editor and packaged builds, 151 passing regression checks (four
existing warning cases), and a 37-step rendered disposable-account route with
six validated and inspected captures. Frost Armor changes armor from 53 to 83
while the character window remains open; zero spell bonuses and actual mana
regeneration survive ReloadUI. The legacy white cast overlay is absent during
both cancelled and completed Fireball casts.

Remaining feedback issue: cancellation currently produces duplicate
Interrupted notifications, and the stock bar's failure presentation needs a
separate packet/event-contract check. The full spell-effects renderer is also
still pending.
