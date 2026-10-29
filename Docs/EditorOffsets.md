# Editor cursor and selection offsets

The build-12340 EditBox API uses UTF-8 byte positions for GetCursorPosition,
SetCursorPosition and HighlightText. GetUTF8CursorPosition instead counts Unicode
code points before the caret. The global strlenutf8 counts those code points in
a whole string. Both missing functions are used by original chat autocompletion.
Compiled XML OnTextChanged handlers now also declare their userInput parameter;
previously the correctly typed argument was passed but the original script read
a nil global instead, preventing the name suggestion list from opening.

Ordinary single-line fields, chat and multiline macro bodies now share that
contract. Byte offsets map to Slate's UTF-16 positions and back, including line
breaks. Offsets inside a multibyte character snap before that character; a caret
cannot split a UTF-16 surrogate pair. Reversed selection retains the active
caret, and multiline HighlightText now replaces the requested range instead of
silently doing nothing. Chat retains its additional atomic-link mapping; cursor
queries describe the raw wire string, including hidden hyperlink markup.

Lua cursor-query errors finish Unreal string/shared-pointer work in a separate
non-inlined helper before raising the Lua error, preserving the Windows error
boundary. SetCursorPosition validates its numeric argument before creating
widget/string temporaries.

Regression tests exercise actual Slate controls with Cyrillic, supplementary
Unicode characters, line breaks, insertion and forward/reversed selections.
The Windows supplementary-character fixture uses an installed font containing
the tested glyph; it does not copy system fonts into the repository or alter
the client's font configuration. This is an API/selection change rather than a
claim of complete font, IME or legacy-editor parity.

The rendered route passes 43 steps with five validated, inspected engine
captures. It types a partial recipient name into the original whisper editor,
chooses the real suggestion, inserts/replaces Cyrillic text in the middle of
chat, edits/selects text across the macro's second line, saves via the original
New button, edits a Cyrillic macro name and reopens the saved body after
ReloadUI. The macro addon loads on demand after reload. No chat message or
macro command is sent/executed by this route. The disposable client closes and
its account is removed; existing characters and inventories remain unchanged.

Verified: Windows Editor and packaged Development builds; all 159 regression
checks pass (155 clean and four existing warning cases). The four focused
editor checks pass without warnings, including the supplementary-glyph fixture
and XML userInput true/false contract.
