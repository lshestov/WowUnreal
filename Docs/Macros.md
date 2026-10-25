# Macros

The original Blizzard macro panel is available from the game menu. It supports
36 account macros and 18 character macros, 16-character names, 255-character
bodies, icon selection, editing and deletion. Dragging a macro to an action
button uses the same cursor/drop path as spells. A question-mark icon resolves
the first supported spell or owned item in the body.

Macros are stored as SavedVariables under the current account and character.
Each edit saves immediately through temporary-file replacement. UTF-8 and
embedded Lua string bytes are escaped without changing their contents. Action
buttons refer to stable opaque identifiers rather than dense list positions;
deleting an earlier macro cannot retarget another button. Deleted identifiers
are not reused, including when an action survives on another character.

Execution sends slash lines to the original hidden MacroEditBox and its
EXECUTE_CHAT_LINE handler. This preserves the existing chat, spell and item
command paths. RunMacro, RunMacroText, RunScript, StopMacro and GetRunningMacro
are implemented. Recursion and Lua instruction limits bound execution. Explicit
spell/item unit targets are honored; item use no longer implicitly targets an
unrelated hostile selection.

Supported condition groups include unit targets, exists, help, harm, dead,
combat, modifiers, form/stance, pet existence, mounted, stealth, swimming,
flying, indoors/outdoors and party/raid membership where the corresponding
unit API supplies state. Groups are ORed, comma-separated conditions are ANDed,
and semicolon clauses provide fallbacks. Unknown conditions fail closed.

This is not complete secure-macro compatibility. Cast sequences, random cast
selection, additional condition families and arbitrary addon secure-button
macro attributes still require implementation and dedicated verification.

Verification covers isolated editor controls, stable action identities,
capacity, conditions, stop behavior, targeted item packet layout and byte-exact
persistence. Native autologin checks create a macro through the panel, type into
the real Slate editor, drag it onto an empty action, execute a Lua line and a
local-server chat round trip, reload the UI, execute again and remove the test
macro. Rendered screenshots verify the editor, icon picker and restored action.
