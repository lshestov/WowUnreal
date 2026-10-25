# Interface status

The client uses the original 3.3.5a FrameXML read from the user's MPQ files.
Opening a Blizzard panel does not by itself establish that its underlying APIs
are implemented. This document distinguishes the verified core from remaining
compatibility work.

## Implemented core

- Player and target health, resource, auras and live model portraits; action
  textures, item counts, cooldown swipes and spell/item tooltips.
- Known spellbook tabs and ranks, character attributes and combat statistics,
  skills, reputations, watched faction and character equipment preview.
- Inventory item metadata, stack counts and durability; exact bag-slot moves,
  equip/unequip and spell/item/action drag and drop. Action placement preserves
  a displaced action on the cursor and never deletes a physical item.
- Quest journal selection, objectives, formatted paragraphs, rewards and watch
  state. Reparented text measures and scrolls with the journal; the scrollbar
  uses the same units in Lua and native widgets.
- UTF-8 chat, channel and recipient fields, bounded message history and input
  focus. A native round-trip test uses an otherwise empty local realm.
- Outdoor minimap and zone/continent map textures, player position and zoom.
- Game menu, window close order, logout and character re-entry, ReloadUI and
  saved per-character UI preferences. Windowed mode remains the initial mode.
- Audio volume/toggles, window mode and resolution, gamma, texture quality,
  VSync and frame limit. Cancel restores the tested audio settings.
- Keyboard binding editor, save/cancel/reset and native routing, including
  releasing a remapped key after an edit box takes focus.
- Account and character macro creation, editing, icon selection, deletion,
  action-bar placement and execution through the original chat command path.
  Stable identities preserve action assignments when another macro is deleted.
  Macro changes save immediately and survive ReloadUI, including UTF-8 text.
  The multiline editor supports caret placement, insertion, wrapping and scroll
  notifications. Engine font fallback renders Cyrillic absent from enUS fonts.
- Live combat-log entries for melee damage/misses, direct spell damage and
  healing, environmental damage and observed unit deaths. Bounded history,
  event/GUID/object-mask filters and cursor traversal survive ReloadUI.
  Native self-healing verifies the original combat-log formatter and tab.

The UI bridge also supports anonymous frame identity, frame and region
reparenting, font objects, positional formatting, typed wheel/scroll callbacks,
numeric slider values, clipping, additive highlights and wrapped text geometry.
Button normal, pushed, disabled and highlight textures are real editable
regions. XML dimensions, anchors and UVs are retained, and custom mouse routing
updates their state. Micro-menu icons and panel clicks are verified natively.
CreateFrame recognizes Blizzard's uppercase type names. The uiScale CVar
reports the actual positive viewport scale, preventing invalid chat anchors.
Frame visibility follows the original FrameXML, including hidden intermediate
ancestors, explicit XML visibility overrides and action-bar grid options.
Bootstrap and layout refresh no longer force named buttons or flash textures
into a different state. See [stock interface references](StockInterfaceReference.md).
Runtime anchors accept an omitted relative point with frame/name arguments,
and resolve `$parent` references for frames and regions. Options category labels
stay inside their rows, and descriptions wrap inside explicit text widths even
when the original XML also supplies a height.
XML and runtime labels render inline texture icons, cropped/offset image runs,
color escapes and literal text safely. Their measured width includes the icons,
and wrapped/scaled labels retain their formatting. Lua FontString `GetText`
retains the original markup for category identity and script comparisons.
Native backdrops render tiled backgrounds, all eight border pieces, XML
insets/dimensions/colors and runtime SetBackdrop/clear/color changes. Geometry
is batched by texture and bounded for addon-supplied tiny tile sizes.
Virtual texture definitions and their inheritance supply the original panel
button skins and atlas crops. XML and Lua button font objects control normal,
highlighted and disabled captions.

## Verification

The Windows Editor and packaged Development client are built from the same
source tree. `WowUnreal.Hud`, `WowUnreal.UI`, `WowUnreal.Gameplay` and the related
player, collision, equipment, lighting, rendering, traversal and performance
suites cover both isolated controls and live protocol state. The interface
regression set contains 67 tests. Existing optional character texture-layout
DBC and test-fixture size warnings remain.

Native gameplay probes exercise live server state and rendered UI at 1280x720
and 1920x1080. Tests restore moved inventory items, temporary action slots and
key assignments. Screenshots use `FScreenshotRequest`, with file-content and
visual checks. Probe operations include frame click, drag, hover, wheel, Lua
assertions, native key down/up and key-state assertions. Logic-only Editor tests
may use NullRHI; visual checks require a rendered client.

## Remaining compatibility work

Full WoW UI compatibility is **not complete**. In particular:

- Additional frame/region and control API behavior. Visible panels and
  readable category lists do not establish complete visual parity.
- Advanced macro commands such as cast sequences, complete conditional syntax,
  additional combat event families and clickable chat links. See
  [macro support](Macros.md) and [combat-log support](CombatLog.md).
- Complete PvP/battleground, LFG and achievement panels and protocol support.
- World-map points of interest, indoor minimap tiles and tracking markers.
- Legacy options without an engine-side implementation; persisting a CVar
  does not imply that its associated rendering or gameplay feature exists.
- MP3 music decoding. WAV playback and finite UI-sound lifetime are supported.

Some unimplemented panels still log missing Lua APIs at bootstrap. They must
not be advertised as working features or replaced by fabricated game data.
