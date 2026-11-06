# Movement and near teleports

The 12340 near-teleport request uses MSG_MOVE_TELEPORT_ACK (0x0C7): packed
mover GUID, server order counter, then MovementInfo. Its reply contains the
packed GUID, unchanged order counter and timestamp. Only the local mover can
produce an acknowledgement. MSG_MOVE_TELEPORT (0x0C5) is an observer movement
notification and does not produce that reply. The obsolete 0x2F0 pet opcode
has no teleport handler.

MovementInfo parsing validates the entire payload before changing entity
state, including transport/interpolation, pitch, fall/jump and spline elevation.
It rejects truncated or non-finite data and retains separately received movement
speeds. A near teleport updates the shared network position and local actor;
the capsule is placed above the supplied feet coordinates, residual velocity is
cleared and camera pitch is retained. Login and movement use inverse facing
conversions so the first heartbeat does not reverse the server's orientation.

Tests cover optional fields, every truncated prefix, invalid coordinates,
observer notifications, packed acknowledgements and facing round trips. Native
verification sends two real server teleports, checks actor feet and shared
network coordinates, captures both results and exits. The root round-trip
wrapper snapshots and restores the exact original position/facing/zone after
logout, while verifying that health, inventory and money are unchanged.

Reverse input preserves character facing, uses the server's RunBackSpeed,
and selects the original Walkbackwards clip (13). Releasing input restores
the current forward speed. A stationary landing keeps JumpEnd (39), but
moving immediately resumes the appropriate ground animation instead of
sliding in the landing pose. Ordinary weapon one-shots are not interrupted
by this landing-specific transition. `WowUnreal.Animation.RuntimePlayback`
and `Scripts/Verification/39-backpedal-and-landing.json` cover these paths.

World mouse selection and right-click interaction complete on release. A
pointer drag, camera rotation with a captured pointer, or two-button movement
does not also select, clear, or interact. UI clicks remain consumed by the UI.
An empty right click preserves the target and does not interact with it.
An ordinary empty left click clears the target only when deselectOnClick is
enabled (the original default is 1); disabling it provides sticky targeting.
The pressed unit is revalidated on release in case it despawned. The
MouseClickRetainsTarget and MouseSelectionRespectsOcclusion world tests cover
these rules and existing world occlusion.
