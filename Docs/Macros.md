# Macros and local spell requests

The original macro panel supports 36 account macros and 18 character macros,
16-character names, 255-character bodies, icon selection, editing and deletion.
The normal cursor/drop path places macros on action buttons. SavedVariables
retain account and character scope, byte-exact escaped text and stable opaque
action identifiers. Each edit saves through temporary-file replacement; deleted
identifiers are not reused. Execution uses the original hidden MacroEditBox and
EXECUTE_CHAT_LINE path, with bounded recursion and Lua instruction limits.

The original 3.3.5a ChatFrame.lua owns cast sequences and random actions. The
client supplies the missing item, casting, equipment and condition APIs and
typed UNIT_SPELLCAST events; it does not replace those FrameXML managers.
QueryCastSequence returns index, item and spell in that order. Numeric action
tokens follow the original inventory-slot parser, while spell names and ranks
use the loaded locale's DBC data.

Spell, item and auto-attack requests share a per-connection eight-bit cast
counter. Pending requests retain their spell identity. Only a matching local
server result carries a spell name to the sequence manager; foreign or
server-only effects cannot advance a sequence. Failed requests remain on the
current step. The original target, combat, timeout and modifier resets and
acknowledged random-choice lifetime remain in FrameXML. Local selection events
fire only when the target actually changes, including ClearTarget.

Spell-start/go/failure and channel packets maintain casting state. Casting and
channel APIs expose real spell metadata and times. Cancel-cast, cancel-aura,
cancel-form, equipment and explicit item-target commands send native requests;
the server remains authoritative. Action-bar icon refreshes follow results.

Automation loads the original sequence and random code directly from the local
MPQ and checks request identity, rejection, resets, tuple order, dynamic icons,
null actions, conditional commands and typed channel events. Packet tests cover
shared counters, wraparound, full GUIDs and incomplete server responses. Native
round trips use temporary self-healing spells and a character macro, click the
original action button, verify server acknowledgements, then remove all owned
test state. The root test wrapper restores the exact character macro file and
checks inventory, health and money even when a scenario fails. The account UID
allocator stays monotonic.

Unqualified casts use the configured self/focus modified-click assignments;
explicit unit arguments retain their target. Original modifier defaults,
preferences, mouse-button restrictions and runtime hotkey fallback are described
in [modified clicks](ModifiedClicks.md).
