# Monkey geometry study and bounded PSP implementation

The tube prototype was removed after successful cave hardware testing. **Cave** implements an
independent isosurface engine following the field/surface approach identified
in the local Winamp Monkey 1.0 DLL. It is **not a complete Monkey port**.
No original executable instructions, lookup tables or textures are distributed.

## Observations from the DLL

Addresses refer to the inspected 32-bit image at base `0x10000000`:

- `0x10042040`: canonical 256-by-16 Marching Cubes triangulation table;
  `0x100027b0`: cube classification/interpolation machinery.
- `0x10001e90`, especially `0x10001f62`: compact radial field contributions
  `q*q-q+0.25`, with `q = distance_xy_squared / radius_squared`, only for `q<0.5`.
- Noise uses a 16³ float lattice, cubic `t*t*(3-2*t)` interpolation and three
  octaves. Recovered octave multipliers are `1.937` for frequency and
  `0.51626223` for amplitude. The clamped noise modulates the field between
  `-0.22` and `+0.36` times its threshold.
- Slice caches, contribution culling and reused edge vertices avoid rebuilding
  the entire volume. Constructor `0x10001040` has quality-dependent geometry
  capacities; these are not evidence of a single global scene budget.

### Recovered octave initialization

`0x100015e8..0x10001721` initializes the noise lattice, three offsets and a
two-angle rotation per octave. For `r = rand() % 731`, lattice entries are
`2*r/730-1`, offsets are `16*r/730`, and angles are `6.28*r/730`.
At `0x10001fe4..0x10002047`, coordinates are transformed row-wise, multiplied
by the octave frequency, then translated. With `ca=cos(a)`, `sa=sin(a)`,
`cb=cos(b)`, `sb=sin(b)`, the recovered matrix rows are:

```
 ca*cb  -sa*cb  -sb
 sa      ca      0
 ca*sb  -sa*sb   cb
```

The PSP implementation now uses these rotations, ranges and quantization, with
an independent deterministic PRNG rather than Winamp's process-wide RNG state.

### Normals and remaining scene work

`0x10002300..0x1000262e` computes normals from contributor fields, including
cross-section interpolation and path differences. The PSP now computes the
analytic gradient of **its own complete field**, including rotated noise, and
interpolates gradients along cube edges before normalization and lighting.
This replaces per-triangle lighting; it is not a claim that Monkey's entire
normal/color/texture routine has been reproduced exactly.

### Recovered base path controller

The three invented sinusoidal contributors have been replaced with the **base
16-contributor oscillator branch** of `0x10004050..0x10004b12`, invoked by
`0x10004b20` before advancing the field slice. `cave_paths.c` independently
implements:

- Initialization at `0x10003dd7..0x10003e13`: scene seed `rand()%997 +
  (rand()%100)*.01`; `0x10003ec4..0x10003ef2`: base radii
  `.1 + (rand()&1023)*.0001640625`. Main radius becomes `.189 + .1*radius`
  at `0x10003ff7..0x1000401a`. Phase seeds are `(rand()%628)*.01`.
- Envelopes at `0x1000405c..0x100041a1`: two slow sine mixtures raised to
  `.05`, plus a narrow pulse raised to `18` controlling radius/extent.
- Four weighted oscillator terms per contributor at `0x1000448f..0x100046b1`.
  Weights are `(.52+.48*sin(angle))^3.8` for the main path, exponent `1.8`
  for other paths. Integer phase residues and path-dependent coefficients are
  retained; phase increments are normalized by the weight sum.
- `0x100046b7..0x10004739`: nonnegative phase steps limited to `.06` for the
  main path and `.12` for others. `0x10004769..0x100047d4`: distinct phase
  offsets `11.7*i-.351*i*i` and `14.7*i+.755*i*i` for X and Y.
- `0x10004964..0x10004973`: base radius plus twice the narrow pulse;
  field-generation radius limits `.05.. .5` from `0x10001b17..0x10001b41`.

PSP adaptations are explicit: fixed movement multiplier 1, deterministic PRNG,
one controller step per generated slab, normalized XY mapped to a 12-unit grid,
linear profile interpolation, and a 19-profile ring cache. Bass still modulates
the existing safe forward travel speed, not the decoder/audio clock. The camera
follows contributor 0; this is **not yet Monkey's complete camera model**.
Analytic Z gradients include the changing center and radius, without resampling
the field several times per vertex.

Still omitted: alternate trajectory blending at `0x100047db..0x1000495d`,
odd-contributor random perturbations at `0x1000497a..0x10004afa`, the original
camera orientation/controller, and original texture-coordinate/color behavior.
Thus this is a reconstruction of the base oscillator branch, **not of the full
scene routine**. The adapted field threshold/noise scale are unchanged.

## PSP implementation and differences

- Same compact field kernel and noise interpolation/octave ratios, recovered
  octave transforms and base 16-path controller; independently seeded noise
  and a PSP camera follower. Threshold, base frequency and amplitude remain adapted.
- Independent face-connected Marching Cubes polygonizer. Its center-sign
  ambiguity rule is not identical to the original fixed triangulation table.
- 12×12 cells per depth slab, 16 cached slabs. At most one slab is generated per
  rendered tick; the camera cannot advance beyond prepared geometry.
- Two reusable field/gradient planes: after initialization, only one new plane
  is evaluated per slab. Contributor trigonometry is prepared once per plane.
- Maximum 4,320 vertices per slab; fixed aligned scene allocation of about
  1.6 MiB, made only on mode activation and freed after GU completion.
- 512×256 RGB565 render target plus 16-bit depth buffer on both LCD and TV.
  With TV scanout these occupy 1,998,848 bytes of the 2 MiB EDRAM. This mode
  deliberately does not follow MilkDrop's higher-resolution setting.
- Fixed-function texturing, triangle lighting and distance fog, with the
  prototype's original procedural texture. No original assets or shaders.
- Audio clocks/queues, MPEG ownership and display-mode switching are untouched.
  The existing cost-aware visual frame pacing and FPU guard remain in effect.

## Validation

Build the PSP binary first, then run only the geometry/GU/control tests:
`python3 -m unittest tests.test_cave` and the two affected music-view tests.
Checks cover all 256 cube sign cases in four variants, bounded slab generation,
camera clearance, LCD/TV buffers, presentation, throttling and teardown. Rotation
orthogonality/orientation and 2,100 analytic-gradient comparisons against central
differences check the new math. Plane-count assertions verify actual cache reuse.
Both the initial cave and octave/normal builds passed hardware testing without
audio crackle or visible stutter. The 16-path build still needs its hardware
appearance/performance check. Targeted tests also cover 4,000 path steps, phase
limits, interpolation, camera clearance and the source's four-term oscillator
reduced independently for the `seed=t=i=0` fixture.
