# Unit portraits

`SetPortraitTexture` renders an isolated copy of the unit in its neutral pose and keeps the resulting texture. Movement, attacks and death in the world do not change the unit-frame image. Selecting a different unit or changing a player's appearance or equipment invalidates the cached portrait. Creature transformations use the current display ID. Full-body character and dress-up panels have their own models and cameras.

The renderer uses the original M2 camera position, target and field of view. When the model has a `WowPortraitHead` socket, both camera points are transformed relative to the head in the first Stand frame. For `Wolf.m2` the standing head sits about 0.2 model units below its bind pose. Using Stand as the reference keeps the ears and muzzle in frame. Models without a Stand clip use their reference pose.
