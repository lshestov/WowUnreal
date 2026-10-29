# Rich chat editing

The original single-line ChatFrame edit boxes now render WoW hyperlink labels
and colour spans through a Slate text marshaller. Lua and outgoing chat retain
the hyperlink descriptor, item fields and label. A complete link occupies one
editable position, so arrow movement, selection replacement and Backspace/Delete
cannot leave a partial descriptor in an otherwise valid link.

Item links now use the exact eight quality RGB values and nine numeric item
fields required by build 12340. Rounded quality floats and an extra trailing
field previously caused the server's strict hyperlink validator to reject
genuine inventory links. Inventory, loot and trade formatters share the corrected
field count; the server's validation stays enabled.

GetText returns wire text, while the layout displays the label with its supplied
quality colour. GetCursorPosition, SetCursorPosition and HighlightText map between
the layout and UTF-8 byte positions in that wire text. Insert preserves the
current caret and selection. Selected links copy their complete wire text;
paste uses the same insertion path. Enter, Escape, Tab and Up/Down dispatch the
original frame handlers. Newlines supplied to Insert cannot submit chat.

Chat keeps its original fixed single-line height. SetTextInsets reserves space
for the original channel/recipient header, and glyphs are centred within the
field. Ordinary multiline editors, including macro bodies, retain their
existing layout and scrolling path.

The parser handles complete |H...|h...|h hyperlinks, eight-digit colour spans,
colour resets and escaped pipes. Incomplete links stay literal and editable.
Link atoms are bounded to 128 and descriptor searches to 2048 characters.
Colour wrappers are normalized per run; arbitrary redundant formatting is not
preserved byte for byte. Texture escapes and complete legacy/addon editor
parity are outside this change.

ChatFrame_OpenChat defers its initial SetText until the following FrameXML
OnUpdate. Native probes wait for that update before moving the cursor, matching
the original script rather than overriding its deferred initialization.

Regression tests exercise parsing, wire identity, quality colours, raw/layout
offsets, escaped pipes, malformed syntax, actual editor insertion/selection,
atomic deletion, insets, Cyrillic UTF-8 offsets and control-key dispatch.
Rendered verification additionally checks a Shift-click on the equipped robe,
ordinary keyboard input around the link, an actual server whisper to the same
disposable character, and Cyrillic editing after ReloadUI.

Verified: Windows Editor and packaged Development builds; all 158 regression
checks pass (154 clean and four existing warning cases). The rendered route
passes 49 steps with six validated, visually inspected engine captures. The
server echoes the robe link with its exact descriptor and quality colour.
The owned disposable account is removed and existing characters/inventories
remain unchanged.
