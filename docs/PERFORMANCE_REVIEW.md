# Performance review after offline music support

Reviewed and implemented September 19, 2026. The four CPU-side changes below
are now included. PSP speedups still require hardware measurement. Audio
priority, transactional formula evaluation and visual limits are unchanged.

## Implemented changes

1. **Cache immutable mesh coordinates.** `md_eval_pixel_grid` in
   `psp-client/milkdrop_preset.c` calculates radius and angle for every lattice
   node every frame. The 16x16 and fallback 8x8 lattices have fixed coordinates;
   Their normalized positions, radius and angle are cached. This removes repeated
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
   longer repeated per node. The VM's transactional value copy is unchanged.
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

## Larger changes, not first steps

- The formula VM copies its value array to preserve failure atomicity.
  Reducing this via dirty-slot tracking requires explicit rollback tests and
  profiling; simply removing the copy would change error behavior.
- Thick-wave drawing duplicates geometry. Reducing these copies or batching
  draws may help but must preserve blend order, line appearance and GU buffer
  lifetime. This is less isolated than the CPU-only changes above.

## Validation before claiming a gain

Compare identical preset/audio inputs and fixed timestamps on host tests,
including formula failure/rollback and both mesh densities. Then measure
PSP frame time, preferably median and worst-case, on LCD and TV with simple,
custom-wave and heavy per-pixel presets. Host timings do not establish PSP
speedups. Check track changes and long playback for audio underruns before
raising any preset limits. Keep shaders out of scope.
