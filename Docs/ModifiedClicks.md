# Wrath modified clicks

The sixteen build-12340 modified-click actions are enumerated in their original
Bindings.xml order. GetModifiedClick reads actual character preferences, and
SetModifiedClick changes them. Defaults retain mouse-button restrictions:
CHATLINK is SHIFT-BUTTON1, DRESSUP is CTRL-BUTTON1 and SOCKETITEM is
SHIFT-BUTTON2. FOCUSCAST defaults to NONE, rather than the previous hardcoded
CTRL. Unknown actions return nil; invalid assignments raise an ordinary Lua
error instead of silently changing state.

Bindings support NONE, the general and side-specific ALT/CTRL/SHIFT keys, and
BUTTON1 through BUTTON5. Strings normalize into the original modifier order.
Named modified clicks use the original matching modifier/button contract;
direct modifier queries require every specified modifier family. Queries read
routed gameplay key state and the platform modifier state. The resolver survives
ReloadUI. Unit tests supply the same resolver with controlled key states.

SaveBindings saves modified clicks together with ordinary bindings in
IdleWowUIState. LoadBindings restores the saved set or defaults. The unmodified
Controls and Combat panels therefore apply, cancel and persist dropdown changes
through their existing SetValue/SaveBindings handlers. Invalid old preferences
fall back to the original definition.

Action buttons and item actions resolve SELFCAST before FOCUSCAST. Book casts,
unqualified CastSpellByName/CastSpellByID and item commands also use these
assignments. Explicit macro unit arguments retain their specified target. The
selected world target is unchanged when a different cast target is chosen.

The regression compares every default and enumeration position with the local
MPQ, then checks parsing, invalid values, mouse buttons, left/right modifiers,
self/focus priority, saved/default restoration and corrupt older preferences.
The invalid-value case also guards the Windows Lua error boundary: Unreal
string work finishes in a separate helper before luaL_error is called.

Unassigned modified hotkeys fall back to their base gameplay action. Explicit
chords, including explicitly unbound chords, retain priority. GetBindingAction
continues to report exact assignments rather than this runtime fallback.

Single-line EditBox Insert now follows Slate's caret and selection through its
ordinary edit path. HighlightText works on those fields, and inserted newlines
cannot submit chat. Regression checks cover middle insertion, consecutive
inserts, selection replacement, whole-field selection, hyperlinks, Cyrillic
text and invalid non-editor calls. Multiline editors retain their existing path.

Remaining presentation issue: the plain chat edit widget displays WoW hyperlink
and colour markup literally even though the stored link is correct for sending.
Rich chat-editor rendering and cursor-to-raw-text mapping are a separate task.

Rendered verification covers 77 steps and seven validated, inspected engine
captures using a disposable non-GM mage. It changes the original Controls and
Combat dropdowns, cancels a preview, saves assignments, reloads the UI, routes a
real Ctrl+2 Frost Armor to self while a wolf remains selected, then routes the
same spell to focus with Shift and a mouse click. The actual Alt auto-loot query
uses the configured key state. A Shift-click inserts the equipped robe's link
without moving the item. The owned test account is removed; existing characters
and inventories remain unchanged.

Verified: Windows Editor and packaged builds; all 155 regression checks pass
(151 clean and four existing warning cases), including native cast/macro tests.
