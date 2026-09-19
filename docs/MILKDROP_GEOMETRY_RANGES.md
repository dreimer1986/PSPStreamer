# Combined wave clipping and transform-range package

This finishes the current bounded geometry pass after native-input compatibility
and offscreen shape clipping. It does not implement unlimited Desktop geometry,
larger formula storage, higher feedback resolution or HLSL shaders.

## Wave geometry

Custom wave x/y outputs now retain values in −4..4 instead of being pinned to
0..1. Smoothing occurs before clipping. Both custom and built-in waves use a
shared renderer path that clips original segments against the feedback viewport.
Dots outside it are omitted. Thick-wave offsets are clipped independently and
stereo splits never acquire an artificial connecting segment. Interior waves
retain the prior compact fast path.

The clipping path copies source geometry before ending/synchronizing its GU
list, then restores the feedback target in a new list. Output is packed into
immutable untextured vertices. The renderer has 49,128 bytes of static scratch
(2,047 vertices); this avoids deep-stack growth or per-frame heap allocation.
A clipped 1,024-point smoothed thick wave reserves at most 262,016 vertex bytes
after the restart. Subsequent layers still fit the existing 1.5 MiB list.
Each clipped wave adds a list synchronization; this can cost visual performance,
but does not change audio priorities or playback timing.

## Expanded numerical budgets

| Input | Accepted/rendered range |
| --- | --- |
| Static, frame and pixel zoom | .01–100 |
| Rotation; warp amount | ±100 radians; ±100 |
| Warp animation speed; scale | ±100; .01–100 |
| Decay | 0–1 |
| Translation and transform centers | −4..4 |
| Stretch x/y; zoom exponent | .01–100 |
| Built-in wave centers; custom-wave positions | −4..4 |
| Custom-wave scaling | −100..100 |
| Global wave scaling at geometry generation | −100..100 |

The imported global `fWaveScale` remains a finite float; geometry alone uses its
bounded value. Ordinary preset values retain existing arithmetic. These are PSP
implementation budgets, not Desktop import limits or proven hardware maxima.

Nested zoom powers may underflow to zero or overflow to infinity despite finite
inputs. Effective zoom is saturated to 1/65536..65536 before division. Final
logical UV coordinates are saturated to ±4096 before the adapter's x expansion,
keeping physical coordinates within roughly ±8192. This deliberately approximates
extreme transformations; it does not wrap individual vertices modulo texture
size, which would create discontinuous interpolation. Negative/zero zoom and
stretch still use the positive lower bound; reflections are not implemented.

The following limits remain intentional and are outside this bounded expansion:
normalized wave colors/alpha, border sizes, gamma/pass counts, shape texture zoom,
sample/instance density and formula execution/storage budgets. Invalid formulas,
non-finite formula results and exhausted VM budgets still report errors. No formula
is silently truncated and shader execution remains excluded.

## Verification and one combined PSP test

Tests cover 45 extreme zoom/exponent/stretch combinations, existing reference
geometry, immutable GU ownership, retained stereo splits, all thick passes and
clean shutdown after a clipped-wave list restart fails. The full layer/density
stress tests cover LCD/TV and both layouts with the original Explosion preset.

The completed suite passes 179 tests. The 1,715-file collection retains 1,651
imports and 1,217 successful 120-frame formula runs, with no regressions or new
passes. Peak vertex storage remains 1,206,368 bytes. PSP GCC reports 5,880 bytes
for `md_frame` and 192 for the clipped-wave helper; the audio/decoder paths are
unchanged. These host checks do not replace the combined hardware test.

Use `geometry-range-demo.milk` for the combined hardware check. It includes
offscreen smoothed lines, thick dots, split built-in waves, moving shape geometry,
expanded zoom/centers and stronger warp. Check LCD/TV and fullscreen/window,
then one familiar demanding preset, track switch and video start. Listen for
clean audio and check responsive controls. One test round covers the package.

The next large memory-layout and resolution projects are deferred until after
the planned media-server integration work; they should have explicit opt-in
quality/resource settings rather than silently replacing stable defaults.
