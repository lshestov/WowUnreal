# WoW 3.3.5 Unreal Engine Client — Implementation Plan

## Overview
Build a performant WoW 3.3.5a client in UE 5.7 that reads original MPQ data files, renders zones, supports the full native WoW UI (Lua + XML + addons), and connects to AzerothCore servers.

## Architecture
8 modules: WowUnreal (game shell), WowData (format parsers), WowAssets (UE conversion), WowWorld (streaming/rendering), WowUI (Lua/XML frames), WowNetwork (auth/world protocol), WowClient (convenience features), WowTests (testing utilities).

## Test Server
- Remote: 192.168.1.5 (Auth: 3724, World: 8085)
- Account: WowTestUser / WowTestPass
- Characters: Testhumanm (Level 3, tile 29,51), Northshire (Level 1, tile 32,48), Stormheart (Level 1, tile 32,48)

## Reference Projects (~/projects/)
- noggit3: ADT/WMO/M2 struct definitions
- pywowlib: Python format parsers
- wowmodelviewer: M2 rendering pipeline
- azerothcore-wotlk: Network protocol, SRP6, opcodes

---

## Completed (Verified Working)

### Core Systems
- [x] MPQ reading (StormLib, 18 archives)
- [x] BLP texture parsing (DXT passthrough, paletted)
- [x] DBC parsing (21 typed wrappers)
- [x] ADT/WDT/WDL terrain parsing
- [x] M2 model parsing (vertices, bones, animations, skins)
- [x] WMO parsing (root + groups)
- [x] Coordinate conversion (WoW ↔ ADT ↔ UE)

### Terrain Rendering
- [x] Terrain mesh building (256 chunks/tile, splatmap textures)
- [x] Explicit cooked runtime material-slot binding for terrain/WMO/doodads/liquids, cookable environment materials and movable skylight capture (verified packaged automatic world entry, colored world screenshots, material contract tests and clean shutdown).
- [x] Async tile streaming (camera-based load/unload with LOD)
- [x] WDL distant terrain, LOD1 mid-distance meshes
- [x] Runtime Virtual Textures
- [x] Terrain collision (complex-as-simple)
- [x] Water rendering (MH2O, ocean plane, WMO liquid)
- [x] Nanite for WMO static meshes

### World Objects
- [x] M2 doodad spawning (instanced rendering)
- [x] WMO building rendering (per-group meshes, full rotation)
- [x] Runtime WMO/M2 world collision and NPC support: MOPY collision faces, dedicated M2 bounds, static async collision meshes, local floor/capsule placement and budgeted visual NPC grounding (10 physics/player/NPC/animation regressions passed; native packaged screenshots verified outdoor terrain and abbey WMO support with a 2 cm foot gap, 41–59 FPS).
- [x] Distance-based object streaming

### Sky & Atmosphere
- [x] Packaged lighting polish: movable sun/moon cascades and world-space contact shadows, instanced foliage shadows, corrected day/night sun path and DBC diffuse/ambient balance, preserved WMO baked colors and bounded character-only interior fill. Verified six lighting/rendering/equipment regressions and a 22-step native day/night route with rendered outdoor/interior screenshots and GPU/UI profiles.
- [x] Time-of-day sky with Light.dbc zone blending
- [x] Sun/moon disc billboards
- [x] Fog from LightFloatParams.dbc
- [x] Cloud layer

### Character Rendering
- [x] M2 → USkeletalMesh with bone weights
- [x] Composite character textures (skin + face + hair + underwear from CharSections.dbc)
- [x] Hair split into separate skeletal mesh
- [x] Geoset visibility (hair/facial hair/equipment)
- [x] Equipment attachment via ItemDisplayInfo.dbc
- [x] All 10 races × 2 genders
- [x] Animation parsing (M2 → UAnimSequence)
- [x] Creature spawning by DisplayId

### Networking (121 opcodes)
- [x] SRP6 auth + ARC4 encryption
- [x] Realm selection, character enum/create/delete
- [x] SMSG_CHAR_ENUM parsing (fixed firstLogin byte)
- [x] Player login + SMSG_LOGIN_VERIFY_WORLD
- [x] Entity system (UPDATE_OBJECT, typed hierarchy)
- [x] Movement sync (heartbeat, server correction)
- [x] Chat, spells, combat, quest, talent, social, inventory handlers
- [x] Keep-alive, time sync

### Login & World Entry Flow
- [x] WoW-themed login screen with expansion tabs (Classic/BC/WotLK)
- [x] Themed colors per expansion (gold/fel green/icy blue)
- [x] Credential prefill from WowCredentials.json
- [x] Realm select screen (WoW-styled)
- [x] Character select screen with class-colored names
- [x] Character creation screen with race/class/gender selectors
- [x] 3D character preview in character select (SceneCapture2D)
- [x] Deferred terrain loading (no world behind login screen)
- [x] Loading screen with tile progress
- [x] Long-lived loading screen Slate texture/brush lifetime stability (retain background resources while visible, release them before rebuilding the loading screen)
- [x] Ground snap after terrain loads (line trace to terrain surface)
- [x] Packaged player placement and visibility: asynchronous runtime terrain collision, feet-based server coordinates without repeated height drift, third-person camera and M2 skeletal render buffers (verified native world screenshot and three player regression tests).
- [x] `-autologin` flag (prefills and auto-submits through UI)
- [x] Reliable automatic world entry with saved account/realm/character selection, bounded verification runs and a screenshot after terrain loading and loading-screen removal (Windows; verified Editor/Game packaging, four login automation tests, rendered world PNG and exit 1 for a missing realm).
- [x] `-createchar` flag (auto-creates Human Mage, enters world)
- [x] Single-instance lockfile (prevents dual UE launches)

### Player Controller
- [x] WMO/M2 collision winding matches rendered geometry, preserving walkable capsule impact normals. Verified 20 Traversal/Gameplay/Hud/Player/Collision/Equipment tests and a rendered 23-step round trip: Northshire Abbey stairs and doorway in both directions, indoor jumping/landing and outdoor uphill/downhill movement. WotLK movement uses millisecond timestamps, matching time sync, complete fall/pitch fields and jump/landing transitions; keep-alive uses opcode 0x407.
- [x] 3rd-person chase camera (spring arm, zoom, orbit)
- [x] WASD movement, mouse look, jump, walk/run toggle, auto-run
- [x] Swim speed, fall damage tracking
- [x] Left-click targeting (CMSG_SET_SELECTION)
- [x] Entity model spawning from live server data

### Audio
- [x] Zone music from MPQ (MP3 with A/B crossfading)
- [x] Ambient sounds from MPQ (WAV)
- [x] Zone detection from MCNK area IDs

### WoW UI System (Lua + FrameXML)
- [x] Functional inventory interface: server item metadata/icons, owned item counts, item/equipment tooltips, exact bag-slot moves and action-bar drag/drop. Native server round trips restore the original inventory and action slots.
- [x] Core interface: live player/target portraits, health/resource, auras, action/spell icons and cooldowns, minimap and zone map, UTF-8 chat round trips, menus, ReloadUI, saved character preferences, keyboard bindings and window/pointer focus.
- [x] Core panels: character/stats, skills, reputation, known spellbook, quest text/rewards/tracking/scrolling, audio/video/interface settings and key bindings. Native autologin scenarios verify controls at 1280x720 and 1920x1080. Re-entry streaming and lobby-preview timer lifetime are covered during the same workflow.
- [x] Button texture regions and micro menu: render XML/Lua normal, pushed, disabled and highlight states; preserve region identity, dimensions/UVs and explicit visibility; resolve mouse-over geometry after child reanchoring. Verified by 45 targeted tests and 40 rendered native steps.
- [x] Basic macro panel and action execution: account/character storage, immediate byte-safe persistence, stable action identities, multiline editor/caret/scroll, icon selection and original slash-command dispatch. Native tests verify creation, typing, drag/drop, execution and ReloadUI persistence. See [macro support](Docs/Macros.md) for remaining advanced syntax.
- [x] Live combat-log foundation: typed melee/spell/heal/environment/death events, bounded session history, filters and terminating cursors; original combat-log tab, quick-filter buttons and ReloadUI history are verified with real server healing. See [combat-log support](Docs/CombatLog.md) for remaining event families.
- [ ] Remaining full FrameXML feature parity: advanced macros and combat events, PvP/battleground and LFG queues, achievements, world-map POIs/indoor minimaps, and unsupported legacy configuration controls. Their current stubs must not be described as complete WoW client functionality. See [interface status](Docs/InterfaceStatus.md) for verified scope and limitations.
- [x] Profile native UI/game/render/GPU frame times, reduce recurring OnUpdate GC slices from 1024 to 64 while preserving pressure checks and the 128 MiB sandbox, and move routine frame/model/animation traces to Verbose. Verified with 25 regression tests and the same 22-step native route: median UI time improves from about 3.1 ms to 0.7–0.8 ms, frame p95 from about 15 ms to 4.3 ms, with stable 56.6 MiB Lua memory. A transient-allocation test checks callback delivery and bounded heap use independently of timing.
- [x] Lua 5.1 VM (sandboxed, memory limited 128MB, instruction limited)
- [x] FrameXML parser (19 frame types, anchors, strata, templates)
- [x] Addon/TOC loader with dependency resolution (Kahn's algorithm)
- [x] SavedVariables persistence
- [x] Event system (OnEvent, OnLoad, OnClick, OnUpdate dispatch)
- [x] ~50 Lua API functions implemented
- [x] Frame methods (SetPoint, SetSize, Show/Hide, SetAlpha, etc.)
- [x] UMG widget integration
- [x] Packaged primary HUD layout and data: preserve anonymous containers, XML sizes, scaled anchors/input and region ordering; initialize live health/resource and stance actions; hide inactive menus and debug overlay (10 Hud/Player/Collision automation tests passed; native autologin screenshots verified at 1280x720 and 1920x1080).

---

## Recently Merged (from parallel agents, 2026-03-15)

- [x] **Lua API expansion**: ~50 → 150+ functions (string, table, math, unit, spell, action bar, chat, inventory, quest, social, CVar)
- [x] **Warden anti-cheat**: responds to all 6 packet types (module, hash, memory, page, MPQ, timing checks)
- [x] **Teleport handling**: MSG_MOVE_TELEPORT_ACK, SMSG_TRANSFER_PENDING, SMSG_NEW_WORLD with worldport ACK
- [x] **NPC waypoint movement**: SMSG_MONSTER_MOVE spline parsing + linear interpolation in tick
- [x] **Nameplates**: floating name + health bar above entities (UWidgetComponent, color-coded)
- [x] **Combat log**: scrollable color-coded widget (spells yellow, damage red, heals green)
- [x] **Loading screen images**: MapDbc → LoadingScreens.dbc → BLP background
- [x] **Death screen**: full overlay with "Release Spirit", health monitoring, CMSG_REPOP_REQUEST
- [x] **WoW cursors**: loads BLP from Interface/Cursor/ (Point, Attack, Speak, etc.)
- [x] **Tooltip system**: hover detection, entity name/level/health with WoW styling
- [x] **M2 particle effects**: basic point light + billboard for fire/smoke emitters

---

## In Progress / Needs Building

### P1: Fully Playable Character — Animation & Combat
Goal: Walk around, fight mobs, cast spells, see other players doing the same.

**Animation System (local player + networked players + NPCs):**
- [x] Animation state machine: idle, walk, run, jump, fall, swim, death, sit, sleep, kneel
- [x] Combat animations: attack (1H, 2H, bow, unarmed), parry, dodge, block
- [x] Spell cast animations: cast, channel, omni-cast (instant)
- [x] Emote animations: dance, wave, cheer, laugh, cry, etc. (AnimationData.dbc mapping)
- [x] Wire SMSG_EMOTE and SMSG_TEXT_EMOTE to animation playback
- [x] Map M2 animation IDs to UAnimSequence per-race/gender model
- [x] Packaged M2 animation playback without editor sequence generation: timestamp sampling, quaternion interpolation, short pose blends, idle/walk/run/jump selection and existing action/emote dispatch (verified Editor/Game builds, runtime playback regression and rendered idle/walk/run/jump captures).
- [x] Play correct animation based on entity movement flags (walking, running, falling, swimming)
- [x] Networked player animations: read movement flags from UPDATE_OBJECT, play matching anims

**Local Player Character Model:**
- [x] Preserve equipped starter clothing and attached item models in packaged play; fix WotLK item-display columns, copied-character-record lifetime, unique shoulder components and cooked equipment materials (verified native player/Willem captures and six equipment/NPC/animation regressions, including source destruction and GC).
- [x] Spawn local player character model using race/gender from SMSG_CHAR_ENUM
- [x] Attach to AWowPlayerCharacter pawn (skeletal mesh on the character)
- [x] Apply composite texture (skin + face + hair from customization)
- [x] Equipment rendering on local player (weapons, armor, helmet, cape from entity fields)
- [x] Update equipment when UNIT_FIELD changes (equip/unequip live)

**Networked Player Models:**
- [x] Spawn other player models by race/gender from UNIT_BYTES_0
- [x] Apply equipment from UPDATE_OBJECT equipment fields
- [x] Name query: send CMSG_NAME_QUERY for unknown player GUIDs, cache responses
- [x] Show player name on nameplate (from name query response)

**NPC/Creature Models:**
- [x] Packaged NPC placement, per-section skins, humanoid extra-display appearance and server-driven locomotion (verified DBC/MPQ and movement regression tests, native humanoid capture and actual server-driven wolf locomotion).
- [x] Spawn NPC by DisplayId (already works)
- [x] Name query: send CMSG_CREATURE_QUERY, cache creature name/title
- [x] Show NPC name + title on nameplate
- [x] NPC scale from CreatureDisplayInfo.dbc

**Combat — Attacking & Spells:**
- [x] Right-click to auto-attack targeted enemy (CMSG_ATTACKSWING)
- [x] Stop auto-attack (CMSG_ATTACKSTOP)
- [x] Cast spell by ID (CMSG_CAST_SPELL) — wire to action bar clicks
- [x] Show cast bar during spell casting (from SMSG_SPELL_START CastTime)
- [x] Show spell impact effects (basic: flash at target location)
- [x] Process SMSG_ATTACKERSTATEUPDATE for damage numbers
- [x] Floating combat text (damage/heal numbers above entities)

### P2: Action Bar & Spell Book UI
- [x] Action bar widget (12 slots × main bar + bonus bars)
- [x] Drag spells from spell book to action bar
- [x] Click action bar slot to cast spell
- [x] Cooldown sweep animation on action bar icons
- [x] Spell book UI (tabs per spell school)
- [x] Talent tree UI (read from SMSG_TALENTS_INFO)

### P3: Core Gameplay Systems
- [x] Packaged basic gameplay cycle: selectable NPC quest menus, WotLK quest/loot packets, authoritative quest log and loot acknowledgements, visible action clicks/keys and real spell ranges. Verified 19 Gameplay/Hud/Player/Collision/Equipment tests and a 60-step rendered run: world-ray NPC selection, quest acceptance, two fights using mouse/keyboard actions, two acknowledged loots, and boots removed/re-equipped with live model updates. Native inventory windows still use prototype slot visuals; broader quests and spells remain separate work.
- [x] Taxi/flight paths (SMSG_SHOWTAXINODES, taxi map UI, auto-fly along path)
- [x] Loot window (SMSG_LOOT_RESPONSE → show loot UI → CMSG_AUTOSTORE_LOOT_ITEM)
- [x] Vendor/merchant window (SMSG_LIST_INVENTORY)
- [x] Quest dialog (SMSG_QUESTGIVER_QUEST_DETAILS, accept/decline/complete)
- [x] Bank (SMSG_SHOW_BANK)
- [x] Mail system (SMSG_MAIL_LIST_RESULT)
- [x] Trade window (SMSG_TRADE_STATUS)
- [x] Group/party UI (invite, accept, leave, ready check)
- [x] Raid UI (raid frames, marks, ready check)
- [x] Duel system (SMSG_DUEL_REQUESTED)
- [x] Pet/companion system (pet bar, pet actions)

### P4: Bag & Inventory
- [x] Bag UI (16-slot backpack + 4 equipped bags)
- [x] Item icons from ItemDisplayInfo.dbc → BLP
- [x] Item tooltips (name, stats, flavor text from item cache)
- [x] Equip/unequip items (CMSG_AUTOEQUIP_ITEM)
- [x] Item quality colors (poor/common/uncommon/rare/epic/legendary)
- [ ] Stack splitting, item deletion

### P5: Chat System
- [x] Chat input box with channel switching (/s /p /g /w /y /1 /2)
- [x] Chat window with tabs (General, Combat Log, Trade, etc.)
- [x] Whisper support (CMSG_MESSAGECHAT type WHISPER)
- [x] Channel join/leave (CMSG_JOIN_CHANNEL, CMSG_LEAVE_CHANNEL)
- [ ] Chat link clicking (items, spells, achievements)

### P6: Minimap
- [x] Circular minimap rendering from terrain height data
- [x] Player arrow (direction indicator)
- [ ] Other player/NPC dots
- [x] Zone name display
- [x] Minimap zoom
- [ ] Tracking icons (herbs, mining, etc. from SMSG_UPDATE_OBJECT flags)

### P7: Effects & Polish
- [ ] M2 ribbon emitters (weapon trails)
- [ ] M2 light emitters (lanterns, spell glow)
- [ ] WMO interior lighting from Light.dbc
- [ ] Spell visuals (SpellVisual → SpellVisualKit → effect attachment chain)
- [ ] Custom terrain shader (reduce 256 draw calls/tile)
- [ ] Shadow flickering fix on WMOs

### P8: Full WoW UI Boot

Requested stock-interface sequence (complete in this order; each stage needs
build, automation, native autologin and rendered screenshot verification):

- [x] 1. Stock window rendering: backdrop edges/corners/tiling, fonts and inline icons.
  - [x] Shared backdrop renderer: XML dimensions/insets/colors, tiled backgrounds,
    complete border atlas, runtime replacement/clear/color APIs. Verified 65 tests
    and native original options/menu plus resized/scaled/colorized frames.
  - [x] Stock button texture templates and inherited font styles; normal,
    highlighted and disabled captions. Verified 66 tests and native menu,
    macro panel, hover/click and disabled-button screenshots.
  - [x] Shared rich labels: inline texture size/crop/offset, color runs, literal
    escaping, native width measurement, wrapping and scaling. Verified 67 tests
    and native options/category, Cyrillic chat, colored/icon labels and plain-text reset.
- [x] 2. Control behavior: movement/clamping, text alignment, region APIs and button states.
  - Native movement/resizing with bounds and screen clamps; shared dynamic/XML
    region anchors and size queries; horizontal/vertical text alignment, shadows,
    spacing and wrap controls; hit insets and disabled/pressed button input.
    Natural texture dimensions and unanchored-region visibility are retained;
    automatic text bounds reserve a physical pixel for fractional-scale glyphs.
    Verified 70 tests, 84 native probe steps and 22 rendered screenshots at
    1280x720 and 1920x1080, including original menu/video panels.
- [x] 3. Spellbook/class windows: hidden spells, ranks/tabs, talents, glyphs and specializations.
  - [x] Spellbook visibility, SkillRaceClassInfo grouping, alphabetical tabs,
    independent highest-rank indices and real spell-slot lookup. Automation:
    71 tests plus focused slot regression passed; native 21-step run verifies
    original general/class pages, tooltip, rank views, scaling and dragging into
    the active stance bar with the server action slot restored. Five screenshots.
  - [x] Talent wire state: player/pet payloads, two specs, free points, per-spec
    glyphs, atomic validation and typed change events. Verified 72 tests and native
    autologin assertions against authoritative player update fields, plus two screenshots.
  - [x] Original talent API/panels: DBC-backed tabs, player/pet ranks, per-spec
    previews, prerequisite/point validation, single and batch learning, spec
    activation requests and current/next rank tooltips. Fix independent anchor
    axes, editable check textures and scroll-child clipping/culling needed by
    these panels. Verified 75 tests and native learning/preview/reset/confirm;
    29-step learning run plus 22-step final run verify original panels, last tier,
    tooltips and restored zero talents/11 free points. Nine rendered screenshots.
  - [x] Slider thumb rendering/input: original vertical direction, XML range,
    default and step inheritance, exact numeric callbacks, native pointer drag,
    disabled input and editable/replaced/removed thumb regions. Verified 76 tests,
    targeted regressions and 46 native steps/10 screenshots at 720p and 1080p.
  - [x] Shared texture desaturation and arbitrary UV coordinates: preserve styles,
    alpha, cropped/flipped/rotated coordinates, additive modes and portrait masks
    through texture replacement. Own reproducible UI materials contain no game
    images. Verified 77 tests, focused highlight regression, 14 native steps and
    four screenshots including grayscale locked talents and circular portraits.
  - [x] Glyph metadata/socket APIs, targeting/application/removal and original panel.
    Real item targeting validates socket type/unlocks, item position and active spec;
    Escape cancels without consuming items. Apply/remove wait for server state.
    Texture templates retain real timed Alpha/Translation groups and parent keys;
    shared hit testing refreshes animation-invalidated geometry without false leave
    events. Spell descriptions evaluate numeric DBC expressions safely, and wrapped
    tooltips measure native text. Verified 81 tests plus focused regressions;
    final native 29-step use/cancel/apply/remove run and six nonempty screenshots
    show correct tooltips and restore all glyphs/items. No glyph/animation Lua errors.
  - [x] Live spell-rank replacement and complete class-window regression.
    Handle both learned and superseded spells: retain independently learned
    caster ranks, remove superseded inactive ranks, upgrade matching buttons and
    persist every changed slot. Numeric all-slot notifications refresh stance,
    auxiliary and paged bars. Verified 82 automation checks, native learning and
    saved upgraded actions after relog, then removal of the QA rank and restored
    rank-one button, talents and glyphs. Five rendered before/after screenshots.
- [x] 4. Inventory: split/delete, compare/try-on, merchant/bank/mail/trade round trips.
  - [x] Stock split-control text input and XML texture defaults: native viewport
    character events reach the top visible keyboard frame without stealing editor
    input. Digits, arrows, backspace, Enter and cancellation use original Lua.
    Unanchored XML textures stretch to the owner after inheritance, including
    explicitly sized backgrounds; Lua-created regions retain explicit positioning.
    Verified 84 tests and native 29-step/six-screenshot split/character/book/menu
    regression with the split panel and its number correctly aligned.
  - [x] Item cursor identity, actual split/merge operations and delete confirmation.
    Bind selections to source GUID/position, reject replaced or reduced stacks,
    preserve partial quantities during autostore and byte-sized destroy requests,
    and support full-stack selection/cancellation. Held items expose lock state;
    numeric bag events refresh visible counts. World drop opens the original
    quality-aware delete dialog, and Escape releases a selection. Verified 85
    tests plus native 36-step split/cancel/7+3/merge/delete-cancel/delete-confirm
    cycle and four nonempty screenshots. QA cloth restored to zero in server DB.
  - [x] Equipped-item comparison: original shopping-tooltip Lua, correct item
    name/link tuple, 3.3.5 equipment masks and dual-slot order, signed template
    stats/DPS/armor and automatic modifier/preference updates. Retain mixed
    tooltip row order, anchor adjustments and minimum widths; clear old contexts.
    Verified 86 automation tests and a native 20-step run/three screenshots for
    shield vs two-hander, armor gain, preference off and spellbook regression.
    Equipment is unchanged; comparison preference restored after verification.
  - [x] Independent character/dress-up model previews: build MPQ-backed UI actors
    with real customization/equipment, preserve per-slot try-on choices and handle
    two-handed/offhand replacement. Stock control-click, rotation buttons, Undress,
    Reset and reopen work without modifying world meshes or server inventory.
    Model scale is independent of frame layout; transparent HDR captures retain
    the original backdrop and fit physical mesh vertices, not animation bounds.
    Handle frames created before the network player and release hidden UI actors.
    Verified 87 automation tests plus focused regressions, native 26-step/six-image
    run with full-body paperdoll, shield/robe/boots, reset and unchanged gear/money.
  - [x] Shared FrameXML text alignment and texture replacement layout: retain
    original centered defaults, font-object alignment inheritance (including addon
    font definitions) and explicit per-region overrides. Preserve stretched texture
    anchors when setting a new image; canvas offsets are not natural file sizes.
    Native texture diagnostics expose real geometry, resources, UVs and opacity.
    Verified 88 tests and native 20-step/four-image character/dress-up/book/menu
    regression: centered titles, left spell labels and visible equipment/empty-slot
    icons with positive geometry. Natural image dimensions remain correct.
  - [x] Merchant protocol and authoritative cache: full GUIDs, sparse server slots,
    complete buy/buy-in-slot/buyback/repair requests, stock acknowledgements and
    separate buy/sell error results; direct interaction for plain vendor NPCs.
    Purchases use complete 21/29-byte requests; sales use uint32 counts (20 bytes),
    including stacks above 255. Stock and balances wait for server updates.
  - [x] Original merchant window, purchase/sale/buyback/repair server state.
    Original goods, buyback, stack selection, target bag slots, repair cursor and
    individual/all/guild repair APIs use cached server items and DBC costs.
    Refund quotes retain sparse token costs and online-played-time expiry;
    pending quotes cannot silently sell refundable items. Fix full guild-rank
    records, offline members and repair allowance queries. Render carried icons,
    stack counts and contextual cursors in engine captures; restore model brushes
    after static buyback art. Equipment highlights check real requirements.
    Verified Windows build, 91 automation tests plus focused regressions, native
    45-step/five-image buy/sell/buyback/buy-in-slot/cancel cycle and controlled
    individual repair of a temporary weapon. Server DB confirms original money,
    equipment, durability, bags and position restored; QA items removed.
  - [x] Original bank with authoritative deposit/withdraw, explicit slot drops,
    split/merge, bag-slot prices and purchase confirmation/result handling.
    Bind the server banker before opening FrameXML; support bank-bag inventory
    IDs and reject cached bank operations after close. Stock numeric bank events
    refresh icons/counts and slot availability. Nested virtual attributes no longer
    suppress concrete child frames, restoring the stock Purchase button while
    preserving top-level templates. Verified Windows build, 94 automation tests
    plus focused server-acknowledgement regressions, and a native 38-step/five-image
    bank-service cycle including the actual insufficient-funds response. Successful
    purchase/full-slot updates are tested in isolation; the live bankSlots value
    remains zero. Server DB confirms all original item GUIDs, counts, durability
    and money unchanged, with an empty bank after the round trip.
  - [x] Original mailbox and authoritative send/read/take/return/delete handling.
    Original inbox/composer, stationery and template subjects/bodies, sender names,
    currencies, attachment icons/tooltips, reply/invoice metadata and pending-mail
    notification. Serialize commands by message/item GUID and apply complete
    matching acknowledgements; preserve valid cache on truncated packets. Lock
    outgoing item identities, split real stacks into available carried bag space
    and require refund consent. Restore stock editor fonts/colors/focus, portrait
    name overloads and tooltip reset behavior. Verified Windows build, 97
    automation tests, focused parser/return/copy/automatic-collection cases, an
    86-step/14-image native receive/read/take/delete/composer/split/self-error
    cycle and a final 25-step/three-image money-tooltip cycle. Positive send,
    return and permanent-copy acknowledgements are isolated tests; live mail
    goes only to the test character itself. Original inbox message, item GUIDs,
    counts, durability and money are preserved; temporary QA mail is removed.
    See [mail support](Docs/Mail.md).
  - [x] Original trade window, local offers and authoritative transfer/accept/cancel.
    Match the original client: local offers/locks update after validated requests;
    AzerothCore sends extended snapshots to the counterpart. Inventory and money
    only change on server updates. Parse complete sparse records/state indices,
    preserve physical item identity, wait for real split stacks and cold metadata,
    revoke consent on changes and support money cursors/service-target packets.
    Original fields, counts, icons, tooltips, binding/replacement warnings and
    cancel/results are wired to FrameXML. Verified Windows build, 99 automation
    tests (two existing warnings), a native 74-step/seven-image completed exchange
    and return, then 27-step and 31-step final split/cancel/count/geometry checks.
    QA peers use native authentication/Warden and are deleted through the server;
    original inventory and zero money are verified after each run. See Docs/Trade.md.
- [ ] 5. Chat/combat/macros: hyperlinks, remaining combat events and macro execution families.
  - [x] Native chat hyperlink areas from real text blocks, original click/hover
    signatures and default hyperlink-only mouse input, including wrapped links.
    Original item/spell references and player whisper editor; real GetSpell tuple,
    header control spacing and optional texture colors for ChannelFrame bootstrap.
    Verified Windows build, 100-test regression, added tooltip/geometry/color cases
    and two native 26-step/four-image runs. See Docs/ChatLinks.md.
  - [?] Channel membership/roster APIs and original channel-window state.
    Server-confirmed joins/leaves, atomic member lists/watch updates, local
    channel numbers, chat-window filters and moderation requests. Original
    categories, collapse/selection and owner roles; viewport-local cursor and
    nil-relative-frame SetPoint fixes restore stock context menus. Verified
    Windows build, 103-test regression, focused geometry/API checks and native
    private-channel round trips with rendered roster/chat/menu captures.
    Zone channel selection depends on the actual area resolver in stage 6.
  - [ ] Remaining combat-log event families and original formatter integration.
  - [ ] Advanced macro execution families and acknowledgement-driven sequences.
- [ ] 6. Maps: points of interest, tracking, interior minimaps and original map bootstrap.
  - [ ] Resolve actual zone/subzone from streamed terrain areas and join/replace
    built-in zone channels using their real DBC definitions and server replies.
  - [ ] Server near teleports: handle the actual 12340 MSG_MOVE_TELEPORT_ACK
    request (packed GUID, order counter, MovementInfo) and reply with packed GUID;
    update actor/camera and shared map position. Current legacy handler is bound
    to other opcodes and misses same-map teleports, including GM verification.
- [ ] 7. Group/special windows: party/loot, PvP, dungeon finder and achievements.
  - [ ] Restore NPC portrait captures and friendly NPC unit-frame reactions.

- [x] Stock options category placement and text: numeric-offset SetPoint overloads, runtime $parent references, constrained description wrapping, and XML markup rendered without asset paths while Lua keeps original strings. Verified 64 automation tests and native category clicks, character/spellbook windows and rendered screenshots.
- [x] Stock FrameXML visibility: preserve hidden branches during parent Show/layout, respect explicit hidden=false, remove frame-name and Flash overrides. Verified 63 automation tests, native action-bar options/window clicks and rendered screenshots at 1280x720 and 1920x1080.
- [x] Load real Blizzard FrameXML from MPQ (action bars, minimap, chat, unit frames, buffs, bags)
- [x] Full addon loading from Interface/AddOns/
- [x] Font rendering with WoW .ttf fonts (in progress — P7 agent)
- [x] FrameXML child `OnLoad` bootstrap stability (preserve frame Lua objects across creation/compile, seed parent IDs before child `OnLoad`, keep RuneFrame bootstrap alive without reintroducing Lua heap blowups)
- [x] Secure attribute delegate dispatch for Blizzard dropdown bootstrap (`SetAttribute` now drives `OnAttributeChanged` so `UIDROPDOWNMENU_INIT_MENU`/unit popup dropdowns initialize)
- [x] Unit relationship API parity for Blizzard unit popup bootstrap (`UnitCanCooperate` and shared friend/hostile resolution for party/target dropdown setup)
- [x] UnitPopup utility API coverage for RAF/loot/difficulty gates (`IsReferAFriendLinked`, `GetOptOutOfLoot`, `HasLFGRestrictions`, `GetInstanceInfo`, `CanChangePlayerDifficulty`)
- [x] RAF summon helper API coverage for UnitPopup/FriendsFrame (`GetSummonFriendCooldown`, `CanSummonFriend`, `CanGrantLevel`, `SummonFriend`, `GrantLevel`)
- [x] Video options dropdown API shape parity (`GetMultisampleFormats`, `GetRefreshRates`, and no-op setters return the types Blizzard expects)
- [x] FrameXML animation-object global exposure for tutorial callouts (named `<Animations>` descendants resolve `$parent`, register in Lua, and support `Play`/`Stop`)
- [x] Frame hover hit-testing parity for `IsMouseOver` (Lua method routes through cached frame rects and cursor position with WoW-style top/bottom/left/right offsets)
- [x] UIParent arena layout stub parity (`GetNumArenaOpponents` returns a safe non-arena default for watch-frame placement)
- [x] Scoped XML `OnLoad function="..."` parity for LFR browse bootstrap (`LFRBrowseFrame_OnLoad` is allowed without reopening broad handler compilation)
- [x] Logged-in FrameXML script override parity (same-event merged template handlers now use WoW-style override behavior; verified chat edit boxes, GM chat bootstrap, target spell bars, and casting bars survive world login)
- [x] Blizzard utility global parity for logged-in addon boot (added `table.wipe`, `CombatLogResetFilter`, and `CombatLogAddFilter`; verified GM chat loads cleanly and combat log advances to `CombatLogGetNumEntries` after world login)
- [x] Blizzard combat log cursor API parity for logged-in addon boot (added `CombatLogGetNumEntries`, `CombatLogGetCurrentEntry`, `CombatLogSetCurrentEntry`, and `CombatLogAdvanceEntry`; verified Blizzard combat log loads through world login and the logged-in screenshot stays valid)
- [x] Blizzard combat log entry tuple parity for quick-button refiltering (combat log current-entry helpers now return the WotLK `timestamp, event, sourceGUID, sourceName, sourceFlags, destGUID, destName, destFlags` tuple Blizzard_CombatLog expects)
- [x] Blizzard combat log object-flag helper parity for quick-button refiltering (`CombatLog_Object_IsA` now follows WotLK composite combat-log mask rules, the logged-in Blizzard_CombatLog quick-button path loads cleanly, and the delayed screenshot fired after world login)
- [x] Lua `TargetUnit` parity for bindings and unit-frame clicks (implemented WotLK-style unit-token resolution for bindings and unit-frame click paths, including chained `...target` tokens and name fallback; verified with editor build, game build, automation, and a delayed post-login screenshot)
- [x] WoW `RegisterForClicks` parity for Blizzard button down/up semantics (button click-edge registrations are now stored in the frame manager, `OnClick` receives a real Lua boolean `down` value, and gameplay controller UI clicks route through press/release semantics; verified with editor build, UI automation, game build, and a delayed post-login screenshot)
- [ ] Character creation 3D preview with customization sliders

### P9: 3D Login Screen Backgrounds
- [ ] Classic: Dark Portal (WMO + effects)
- [ ] Burning Crusade: Outland portal
- [ ] Wrath of the Lich King: Sindragosa/Icecrown
- [ ] Slow-orbit camera + login music

---

## Test Infrastructure
- `./run_game.sh` — Login screen (credentials prefilled)
- `./run_game.sh --autologin` — Auto-login with first character
- `./run_game.sh --autologin --createchar` — Create new char + enter world
- `./run_game.sh --build` — Build first, then launch
- `./Scripts/run_map.sh <MapName>` — Launch specific test map
- `./Scripts/run_model_viewer.sh` — Model viewer with orbit camera
- Network test: `-testscene=network` (headless E2E)
