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
