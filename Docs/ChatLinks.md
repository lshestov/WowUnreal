# Chat hyperlinks

The shared label renderer retains WoW hyperlink data and original colored text.
It creates hit areas from the actual Slate text blocks, including links that
wrap onto multiple lines. Link data is kept outside rich-text markup attributes.
Literal angle brackets, escaped pipes, inline icons and color changes retain
their existing rendering behavior.

Scrolling message frames enable hyperlinks by default, as in the 12340 client.
They support SetHyperlinksEnabled/GetHyperlinksEnabled and the original
OnHyperlinkEnter, OnHyperlinkLeave and OnHyperlinkClick arguments. A link can
receive input while the chat background itself ignores the mouse. Pressing one
link and releasing elsewhere does not activate it. Disabled, hidden, scrolled
out or obscured text is excluded from pointer selection.

The original ChatFrame and SetItemRef Lua decide what a click does. Item links
open an item reference; spell/enchant/talent links resolve real DBC spells;
player links use the stock whisper/dropdown path. Unknown tooltip links clear
the previous item/spell context. Tooltip GetSpell returns the Wrath name, rank,
ID tuple. Reference titles reserve space for visible corner controls.

Message rows construct and measure their real text widget before allocating
height. Texture vertex colors accept the original optional components, clamp
their values and preserve omitted alpha, preventing ChannelFrame bootstrap
errors after desaturation.

Verification includes markup, true text-block hit areas, right clicks,
background mouse passthrough, wrapping, disabling, tooltip metadata and corner
control spacing. Two native 26-step runs use the original chat to open an item,
spell and whisper editor and select a wrapped link. Eight engine captures were
validated; final images show an unobstructed item title and spell description.
No chat messages are sent during these probes.
