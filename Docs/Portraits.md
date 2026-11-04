# Animated unit portraits

Unit portraits retain the original M2 camera position, target and field of
view. A model-space camera fixed on the reference pose could lose the face
when the live world actor bowed, cast, attacked or fell. The portrait renderer
maps both authored camera points from the head in the first Stand frame to the
current animated WowPortraitHead socket before applying the body transform.
For Wolf.m2 the standing head is about 0.2 model units lower than its bind
pose. The original camera already accounts for that displacement. Tracking
starts at Stand so the ears and muzzle remain in frame. Models without a Stand
clip use their reference pose.

Models without a head socket or matching reference bone retain their authored
camera. Full-body character and dress-up views keep their own framing logic.
The helper's regression covers a multi-bone parent hierarchy, unchanged rest
pose, scaled/rotated/bowed heads, falling poses and absent-head fallback.

A rendered 29-step disposable-account route captures idle, cancelled and
completed Fireball states before and after ReloadUI. All five engine captures
were validated and inspected: the face remains in the player portrait during
the cast instead of falling below its circle, and the target head remains
visible after combat. The original red interruption and green completion bars
still hold and fade correctly. Temporary QA characters are removed and the
existing characters/inventory are checked unchanged.

This preserves authored framing while following head motion. It does not
replace model cameras or claim complete portrait/animation parity for every M2.

Verified: Windows Editor and packaged builds; all 153 regression checks pass
(149 clean and four existing warning cases), including the geometry check.
