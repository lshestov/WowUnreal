# Focus through ReloadUI

The selected focus GUID belongs to the active world session. Recreating the Lua
VM previously discarded it, so the original FocusFrame disappeared and focus
cast modifiers lost their target. ReloadUI now carries that GUID into the new
context before loading FrameXML. Original OnLoad/unit events rebuild the frame,
portrait and health/resource bars from the same server-owned unit data.

An empty focus stays empty. Existing unit-resolution and group-update handling
still reject/clear an unavailable focus. The GUID is session state and is not
written to a character's saved preferences. General user-placed frame layouts
and arbitrary addon runtime state are outside this change.

Rendered verification uses a disposable non-GM mage and a stationary existing
wolf. It verifies a focused NPC before/after ReloadUI, compares its name and
health/max-health with the selected unit, then focuses the player, reloads again
and sends a real Shift+2 Frost Armor while the wolf remains selected. The server
confirms the player aura. ClearFocus followed by another ReloadUI retains an
absent focus and hidden frame. All 31 steps pass; four engine captures are
validated and visually inspected. The test client closes and the owned account
is removed without changing existing characters or inventories.

Both Windows Editor and packaged Development builds pass. All 158 regression
checks pass (154 clean and four existing warning cases).
