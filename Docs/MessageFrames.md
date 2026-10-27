# Timed messages and initial character identity

The stock PaperDollFrame initializes stat dropdowns on VARIABLES_LOADED, before
the first complete player-object update. UnitClass now falls back to the
selected character's actual server character-list record during that interval.
It does not invent an identity when neither source is available. Existing stat
dropdown choices remain intact; fresh characters receive the original
class-dependent defaults.

MessageFrame XML displayDuration, fadeDuration, fading, insertMode and its
direct FontString style survive parsing and template inheritance. Message rows
use the specified font and horizontal alignment. TOP insertion puts the newest
row at the top. Set/GetTimeVisible, Set/GetFadeDuration, Set/GetFading and
Set/GetInsertMode operate on the rendered messages.

Each message has a monotonic receipt time. Transient messages hold their
visibility, fade and expire. Scrolling messages keep their history; scrolling
back reveals faded rows. Invisible rows do not expose hyperlink hit targets.
Clear resets both history and the scroll offset. Notification expiry no longer
leaves old dungeon role-check errors permanently over the world.

The timer regression checks inheritance, placement, intermediate opacity,
expiry, disabled fading, preserved chat history and scroll reset without
sleeping. Six focused checks and a 21-step native route verify character stats
and visible/fading/expired notification captures. All four images were
validated and inspected. The native log has no startup PaperDoll error.
