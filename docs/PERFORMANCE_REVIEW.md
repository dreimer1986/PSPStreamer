# Performance review after offline music support

Reviewed and implemented September 19, 2026. The changes below
are now included. PSP speedups still require hardware measurement. Audio
priority, transactional formula evaluation and visual limits are unchanged.

## Implemented changes

1. **Cache immutable mesh coordinates.** `md_eval_pixel_grid` in
   `psp-client/milkdrop_preset.c` calculates radius and angle for every lattice
   node every frame. The 16x16 and fallback 8x8 lattices have fixed coordinates;
   their normalized positions, radius and angle are cached. This removes repeated
   square roots and atan2 calls without reducing mesh density.
2. **Copy only active custom-wave geometry.** `md_eval_custom_waves` clears
   all four maximum-sized geometry arrays and copies all of them on success,
   even for a preset with one short wave. Now only counts are initialized and
   active vertices written/copied. A failed formula still leaves output and
   state unchanged. The GU consumer uses count for copying and smoothing;
   unused vertices are not read. Every active vertex initializes all fields.
3. **Avoid rebuilding frame-constant pixel inputs.** `md_eval_pixel_grid`
   constructs the full VM input array and repeats signal, engine and display
   setup for each node. A frame template now prepares these once. A template
   copy still resets every node; q/user state propagates in the same order.
   Display aspect divisions, signal/effect setup and progress clamping are no
   longer repeated per node. The later VM optimization below preserves rollback.
4. **Specialize neutral warp operations.** `md_warp_mesh_varying` in
   `psp-client/milkdrop_warp.c` evaluates four trigonometric warp terms even
   when a node's warp is zero. These terms are now skipped for zero warp.
   Rotation sine/cosine are reused until rotation changes. Zoom-exponent
   radii are cached, while genuinely varying pixel formulas retain their
   per-node transformations.

The caches use 5,780 bytes of static storage (289 nodes, five floats each).
The pixel frame template adds one VM value array to the evaluator's stack.
There are no new heap allocations or audio-thread changes. Caches are owned
by the existing single renderer thread. Neither feedback resolution nor
formula budgets were reduced.

Regression coverage includes both mesh densities, repeated coordinate edits,
changing per-node rotations/warp, disabled-wave counts, untouched unused wave
vertices, initialized active GU attributes and late formula failure rollback.

## Follow-up: sparse VM rollback and compact thick waves

- The VM now reads/writes its input array directly and records each assigned
  slot's original value on first write. Success needs no whole-array copy;
  failure restores only touched slots. A small bitmap replaces two complete
  value-array copies per nonempty execution. Memory/register journaling,
  random-state rollback and instruction fuel remain unchanged. No bytecode or
  preset format changes are involved. Repeated stores and dense programs still
  incur tracking overhead; speedup depends on the preset.
- Thick custom and standard waves now share one helper. The three extra passes
  are prepared in one traversal using 16-byte color/position vertices instead
  of 24-byte textured vertices. Positions retain float precision, offsets,
  colors, split-line order and blending are unchanged. No temporary mutation
  of previously queued geometry is allowed: each pass retains its own storage
  until GU completion. Draw-call count is intentionally unchanged. This saves
  one third of extra-pass vertex storage and reads each source vertex once.
- In the same host GU stress harness, peak vertex storage decreased from
  1,414,496 to 1,206,368 bytes (208,128 bytes saved). This is a storage
  measurement, not an FPS prediction. The list allocation remains 1.5 MiB.

Additional tests cover sparse/dense repeated assignments, rollback after
division failure and fuel exhaustion, and compact vertex contents after all
three draws have been queued (including split lines and full-length waves).

## Follow-up: exact trigonometric memoization

The PSP phase report for `Fed + Geiss - Cauldron painterly 5 strippy rmx 1
auraltshift.milk` attributed 33.203 ms of 48.781 ms measured average frame work
to custom-wave evaluation (688 LCD-fullscreen frames). Pixel formulas were
negligible. Its wave program repeatedly evaluates rotation sine/cosine at
identical angles across points.

The VM now memoizes `sin` and `cos` in a bounded, two-way, 32-set cache.
Entries use the exact 32-bit float argument, including the sign of zero;
cache misses still call the same `sinf`/`cosf` functions. No angle rounding,
interpolation, instruction reordering or formula-budget changes are involved.
Sin and cosine share an argument entry but are computed only when requested.
Pure results may be reused across programs/frames; global VM reset clears the
cache. Exceptional results are not retained. Storage is 1,056 bytes, with no
heap allocations. Like the VM, this cache belongs to the renderer thread.

Tests compare cached and uncached state/wave output byte-for-byte, exercise
20,000 generated float bit patterns, signed zero and cache eviction, and count
actual VM library calls. Presets with entirely changing arguments may gain
nothing and still pay lookup overhead; compare their PSP timings as well.
In the deterministic 24-frame host comparison, Cauldron's VM calls fell from
160,393 to 47,752 with identical outputs; dense-wave-demo remained at 24,576.
These are call counts with synthetic inputs, not PSP timing measurements.
The existing render scheduler and all profiler measurements remain unchanged
so this optimization can be evaluated independently of duty-cycle changes.

## Follow-up: render idle factor 2

The next PSP capture showed no clear Cauldron speedup from trig caching:
custom-wave evaluation averaged 34.164 ms and total measured work 49.706 ms
(692 LCD-fullscreen frames). Different music inputs prevent a strict A/B
comparison. The cache is retained for this isolated scheduler test.

The scheduler now waits `max(50000 us, 2 * frame_cost)` after completing a
frame, instead of `max(50000 us, 3 * frame_cost)`. The branch boundary moves
to 25 ms to preserve the 50 ms minimum idle interval without a discontinuity.
At 50 ms work this gives a nominal 150 ms frame period instead of 200 ms;
actual PSP scheduling may add delay. There is still no catch-up loop. The
same policy applies on LCD and TV, windowed and fullscreen. Audio priorities,
clocks, formula limits, geometry and diagnostics are unchanged. Hardware
testing must verify sound, controls and track transitions under heavier load.

## Validation before claiming a gain

Compare identical preset/audio inputs and fixed timestamps on host tests,
including formula failure/rollback and both mesh densities. Then measure
PSP frame time, preferably median and worst-case, on LCD and TV with simple,
custom-wave and heavy per-pixel presets. Host timings do not establish PSP
speedups. Check track changes and long playback for audio underruns before
raising any preset limits. Keep shaders out of scope.
