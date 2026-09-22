# Monkey geometry study and bounded PSP implementation

The stable tube prototype is retained. The new **Cave** mode implements an
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

The original contributor animation, camera trajectory, octave coordinate
matrices and complete normal calculation have **not** been reconstructed.

## PSP implementation and differences

- Same compact field kernel and noise interpolation/octave ratios; independently
  seeded noise, three animated path contributors and a new camera path.
- Independent face-connected Marching Cubes polygonizer. Its center-sign
  ambiguity rule is not identical to the original fixed triangulation table.
- 12×12 cells per depth slab, 16 cached slabs. At most one slab is generated per
  rendered tick; the camera cannot advance beyond prepared geometry.
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
`python3 -m unittest tests.test_tunnel` and the two affected music-view tests.
Checks cover all 256 cube sign cases in four variants, bounded slab generation,
camera clearance, LCD/TV buffers, presentation, throttling and teardown.
Real PSP performance and appearance still require hardware testing.
