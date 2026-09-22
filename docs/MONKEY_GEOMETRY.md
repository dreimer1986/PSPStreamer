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

### Recovered side-path perturbations

`0x1000497a..0x10004afa` perturbs only odd-numbered contributors. The main
camera path and even-numbered paths are unchanged. Two `%173` random samples
produce a triangular X/Y displacement; another sample multiplicatively changes
radius. Influence is 1 below distance `.1`, falls linearly to zero at `.3`, and
is zero thereafter. The inspected binary unusually computes that distance as
`sqrt(2*dx*dx)`, not `sqrt(dx*dx+dy*dy)`; this is preserved, not silently corrected.
Position/radius limits are applied before field generation.

This branch is now implemented with an independent deterministic random stream.
The PSP profile chooses roughness `.015`; this is **not claimed as Monkey's
default** (the original value is a configurable field at object offset `0x129e4`).
Perturbations are generated once per cached path profile, never each display
frame, so already-generated walls do not flicker or shift.

### Alternate trajectories, look-ahead and base texture coordinates

The alternate branch at `0x100047db..0x1000495d` now runs alongside the base
oscillators. Its 48 independent generators use four random knots, nonuniform
time intervals and cubic Hermite interpolation (`0x1000a380..0x1000a717`).
Intervals are `period * 2^((2*r/3996-1)*spread)`, with `r=rand()%3997`;
spread is `.3` for the primary path and `.5` for others. The fixed movement-1
profile uses period 26, increased for the primary path and capped at 100.
The third generator is advanced although its result is discarded by the DLL.
Generators initialize when first used, shift knots when the current interval
ends and restart after a gap beyond their available future interval.

`0x10012340` is **not a power function**: it repeatedly applies
`(1-cos(pi*x))/2`, then blends the next iteration for fractional amounts.
The scene uses positive shaping `.45+.6*pow(sine_mixture,.4)` for random
values, and two easing iterations for its slowly varying oscillator/spline
blend. Negative shaping is unused by this branch and is not implemented.
Cubic overshoot is retained; normalized field positions are bounded to 0..1.
All this work happens per generated profile, not per vertex or display frame.

The camera now uses the source's **six-profile look-ahead** observed at
`0x1000704e..0x10007152`, interpolating both current and future primary-path
positions. A level-up GE view matrix replaces translation-only viewing. This
does not reproduce the subsequent source roll, sway or audio-dependent camera
disturbances. Missing future profiles during startup fall back to looking forward.

Base UV assignment at `0x10005afc..0x10005b13` uses normalized X plus the
longitudinal 1/96 offset, and normalized Y. It replaces the per-triangle dominant
axis projection. Our depth convention reverses the longitudinal sign; coordinates
stay unwrapped across triangles/slabs and GE repeat handles wrapping after
interpolation. This avoids independent projection seams. Original texture-stage
animation, alternate passes and source lighting/color changes are still omitted;
the procedural rock texture remains an original PSP asset.

This is **not the full scene routine**. The adapted field threshold/noise scale,
travel speed and bounded geometry remain unchanged.

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

### Right-edge depth-buffer regression

The reported photo `PXL_20260922_232911376.MP.jpg` shows a stationary-looking strip
occupying approximately the last 1/16 of the screen. Inspection found a concrete
clear-size mismatch: the offscreen target is 512×256, but
[PSPSDK sceGuClear](https://github.com/pspdev/pspsdk/blob/master/src/gu/sceGuClear.c)
uses the SDK's global display width/height, initially 480×272.
[sceGuDrawBufferList](https://github.com/pspdev/pspsdk/blob/master/src/gu/sceGuDrawBufferList.c)
does not update those values. Scissoring limits height correctly, but the last
32 depth-buffer columns retain previous-frame depth and can reject new walls.

The cave renderer now emits a size-explicit color/depth clear sprite under GE
clear mode, immediately restoring normal rendering. It neither changes display
buffers nor bypasses the music GU owner. The host harness checks full 512×256
coverage and restoration on all LCD/TV, window/fullscreen and resolution cases,
including dirty right-edge color/depth sentinels on every frame. The user has
confirmed the visible fix and side-path perturbations on hardware.

Build the PSP binary first, then run only the geometry/GU/control tests:
`python3 -m unittest tests.test_cave` and the two affected music-view tests.
Checks cover all 256 cube sign cases in four variants, bounded slab generation,
camera clearance, LCD/TV buffers, presentation, throttling and teardown. Rotation
orthogonality/orientation and 2,100 analytic-gradient comparisons against central
differences check the new math. Plane-count assertions verify actual cache reuse.
The initial cave, octave/normal and 16-path builds passed hardware performance
testing. The 16-path photo exposed the separate right-edge clear bug above.
The combined spline/look-ahead/base-UV update needs a hardware test.
Targeted tests also cover 4,000 path steps, phase
limits, interpolation, camera clearance and the source's four-term oscillator
reduced independently for the `seed=t=i=0` fixture.
Perturbation tests cover the source's X-only influence gate, unchanged main/even
paths, disabled perturbations, displacement bounds and interpolation.
Additional checks cover easing fixtures, nonuniform cubic linear reproduction,
10,000 spline samples, path bounds including overshoot, view orthonormality and
camera-origin transformation, plus source-style base UV consistency.
The combined build passed these targeted checks: 696 generated slabs, peak 624
vertices per slab in the test trajectory, 1,687,552 bytes scene allocation and
1,136 bytes peak tested GU command-list usage. These are host-check results,
not a PSP frame-rate measurement. No unrelated full preset sweep was run.
