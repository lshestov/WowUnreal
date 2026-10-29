# Build-12340 M2 particle data

The MD20 version-264 particle record is 476 bytes. Its blend mode is a byte at
offset 40, emitter type is at 41, scalar tracks begin at 52, lifetime colour
starts at 260, and visibility is a 20-byte track at 456. The old smaller record
and unbounded UTF-8 conversion of numeric bytes did not describe this layout.

The parser now retains blend/texture/atlas/bone fields, scalar speed, variation,
latitude, longitude, gravity, life, rate, width, length and z-source animation
tracks, plus visibility. Step/linear sampling retains per-sequence data and
global sequence durations. Lifetime curves retain their actual RGB/255,
fixed16 alpha, two-dimensional scale and texture cells. Scale variance, wind,
drag and spin fields are also preserved. The old approximation's compatibility
fields are derived from those authored values instead of invented orange
colours and fixed rate/lifetime values.

Particle records, sequences and keys have explicit limits and share an 8 MiB
decoded-track budget, including sequence-header allocation. Out-of-bounds,
unordered or nonfinite curves are omitted transactionally. Strings use their
bounded spans and overflow-safe offsets. Unsupported scalar interpolation
types are omitted; Hermite/Bezier, spline emitters and full particle simulation
are not implemented by this data change.

Effects with no vertices do not require a skin file. Ordinary mesh models
still require their valid skin. The parsed-model cache accepts emitter-only
models without inventing geometry.

Synthetic fixtures verify distinct adjacent records, offsets, signed fields,
global time wrapping, independent scalar tracks, lifetime keys, atlas fields,
truncation/offset limits, invalid numeric data and bounded model strings. The
real MPQ test loads Fire_Cast_Hand, verifies its three emitters/textures, original
colour/alpha/scale keys and scalar tracks, and resolves every texture path.

This supplies data to the forthcoming authored sprite/effect renderer. It does
not replace the existing sphere/glow approximation or establish visual parity.
No Blizzard models or textures are copied into the repository.

Verified: Windows Editor and packaged Development builds; all 163 regression
checks pass (159 clean and four existing warning cases). Both focused particle
checks pass without warnings. An 18-step rendered autologin/Fireball route with
three validated, inspected engine captures confirms world, character and
projectile loading with the new parser. The owned client/account are removed
and existing characters/inventories remain unchanged.
