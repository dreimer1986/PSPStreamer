# Formula compatibility and geometry density

Date: 2026-09-18. Baseline: `b26ac79`.

## Reference-based compiler corrections

The MilkDrop 2 reference under `vis_milk2/state.cpp` registers custom-shape
`instance`/`num_inst` and wave-point `sample`/`value1`/`value2` as ordinary EEL
values. `milkdropfs.cpp` initializes these around the native drawing loops.
They are now writable on PSP too (`instances` remains our `num_inst` alias).
Changing `num_inst` within a formula does **not** resize the native shape loop:
the Desktop also iterates the stored instance count, not that EEL variable.
Point inputs reset before each point; instance inputs reset before each shape
frame invocation, including immediately after init code.

The `ns-eel2/nseel-compiler.c` preprocessor/compiler skips empty statements.
Leading/repeated/trailing semicolons are now accepted here, including within
expression sequences. Malformed operands and missing function arguments still
fail, and diagnostic physical-line mapping is retained.

Native `time`, audio levels and other documented read-only inputs are **not**
made universally writable by this batch. General EEL equivalence is not claimed.

## Larger compiled programs, not a larger execution allowance

| Resource | Before | Now |
| --- | ---: | ---: |
| Init/frame compiled instructions | 512 | 2048 |
| Pixel/point compiled instructions | 256 | 1024 |
| Numbered records per program | 128 | 512 |
| Parser nesting budget | 16 | 64 |
| Operand stack | 48 | 48 |
| Named locals per context | 64 | 64 |
| Local/shared megabuf float slots | 1024 each | 1024 each |
| Execution steps per call / visual frame | 4096 / 262144 | unchanged |

The fixed program arrays add 552 KiB per preset structure. The importer uses a
temporary heap-allocated preset; these larger arrays are never placed on the
PSP thread stack. File and physical-line size limits remain 64 KiB / 2047 bytes.
Programs are not truncated; execution safeguards remain enforced.

## Custom-wave extension

MilkDrop 2's actual `DrawCustomWaves` path clamps computed points to **512**,
despite earlier waveform-window calculations. The new maximum of **1024** is
therefore a documented PSP extension, not a missing Desktop requirement.

- Presets requesting at most 512 retain the existing sampling path.
- Larger explicit requests use linear interpolation within the same 576-sample
  PCM snapshot or 512 spectrum bins. This does not invent additional audio
  detail, but gives procedural point formulas a finer parameter grid.
- Separation stays bounded and PCM indices stay within the captured window.
- Line smoothing supports up to 2047 submitted vertices per custom wave.
- Shapes, waveform capture, audio threads and audio/video clocks are unchanged.

## Finer warp grid with a bounded fallback

The drawing mesh now has 16×16 cells (289 control points), previously 8×8
(81 points). Before pixel evaluation, compiled work hints reserve custom-wave
work from the existing frame allowance. Expensive loops/bulk-memory operations
receive conservative hints. When dense evaluation would crowd out that work,
the engine evaluates the previous 8×8 lattice and interpolates transform
parameters onto the 16×16 drawing mesh. No formulas are rerun and no additional
execution fuel is granted. This is a conservative scheduling heuristic, not a
guarantee that every arbitrary program can complete within the budget.

## Memory and verification

The GU list grows from 1 MiB to **1.5 MiB of main RAM**, not EDRAM. Feedback
textures remain 512×256 and scanout/display switching is unchanged. The host
max-layer test records **1,414,496 bytes** of vertex allocations, leaving
158,368 bytes for GU commands and headroom. The harness requires at least
32 KiB headroom. It exercises all four thick 1024-point waves, maximum shapes,
motion vectors, mesh, borders and image effects.

PSP GCC `-O2 -G0 -fstack-usage` reports 142,552 bytes for custom-wave evaluation,
19,648 for VM execution, 3,392 for `md_frame`, 1,032 for `play_audio_once`,
96 for `play_audio` and 2,624 for `main`: roughly 170 KiB along this path,
below the existing 256 KiB main-thread stack, with room for called helpers.
These static checks do not replace an actual PSP performance/audio test.

## Collection comparison

The same 1,715 files and 120-frame deterministic host formula test as the
previous batch were run again using `tools/check_milkdrop_collection.py`:

| Check | Baseline | This batch | Gain |
| --- | ---: | ---: | ---: |
| Import succeeds | 1,003 | 1,607 | 604 |
| All exercised formula stages succeed | 838 | 1,188 | 350 |

No previously passing import or execution case regressed. The initial
unconditional dense-grid version exceeded the unchanged frame allowance in
four previously working presets; the budget-aware fallback resolves all four.
The final run has 108 import failures (94 invalid, 14 unsupported) and 419
execution failures among imported presets (206 frame/shape, 176 custom-wave,
37 pixel). These error classes still combine syntax/resource/math cases; they
must not be labeled unavoidable hardware failures.

All **164 automated tests** pass, including the supplied Geiss/Hyperdrive
fixtures, and the three new demos run through the LCD/TV GU harness. The preset
collection test does not execute skipped shaders or establish actual PSP speed.

## Hardware test selection

Included original test presets:

1. `extended-formula-demo.milk`: 200 records and over 512 compiled instructions;
   cyan waveform with slowly changing rotation.
2. `dense-wave-demo.milk`: explicit 1024-point cyan custom wave, audio modulation
   and fine procedural ripples.
3. `fine-mesh-demo.milk`: moving square and nonlinear spatial warp.

At the user's request, the local release folders also receive these unchanged
files from their collection (not vendored into the Git repository):

- `Fed + Geiss - Cauldron painterly 5 strippy rmx 1 auraltshift.milk`
- `Geiss + Rovastar - Julia Fractal (Vectrip Mix).milk`
- `Flexi + Geiss - Tokamak mindblob 2-0.milk`

These collection presets newly passed the host formula test in this batch.
Their shader sections remain silently skipped as before. Texture names inside
skipped shaders are not active external-texture dependencies on PSP; the chosen
presets contain no `psp_texture_*` file bindings.

Test on LCD and TV, embedded and fullscreen, switch presets/tracks, and listen
for crackling or dropouts. A successful host test is not proof of identical
Desktop appearance or real-time PSP performance.
