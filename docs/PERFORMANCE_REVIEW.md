# Performance review after offline music support

Reviewed September 19, 2026. These are source-backed optimization candidates,
not measured PSP speedups. No renderer changes are included in this release.
Preserve audio priority, transactional formula evaluation and current visual
limits before attempting to spend any recovered headroom.

## Recommended order

1. **Cache immutable mesh coordinates.** `md_eval_pixel_grid` in
   `psp-client/milkdrop_preset.c` calculates radius and angle for every lattice
   node every frame. The 16x16 and fallback 8x8 lattices have fixed coordinates;
   cache their normalized positions, radius and angle. This removes repeated
   square roots and atan2 calls without reducing mesh density.
2. **Copy only active custom-wave geometry.** `md_eval_custom_waves` clears
   all four maximum-sized geometry arrays and copies all of them on success,
   even for a preset with one short wave. Initialize counts, write active
   vertices, and commit only each active range. Preserve the current rule that
   a failed formula leaves output and state unchanged. Audit consumers before
   leaving unused vertices uninitialized.
3. **Avoid rebuilding frame-constant pixel inputs.** `md_eval_pixel_grid`
   constructs the full VM input array and repeats signal, engine and display
   setup for each node. Prepare a frame template once. Coordinate inputs and
   mutable outputs must still reset per node; q/user state must continue to
   propagate in exactly the current evaluation order. A full template memcpy
   alone may not save much: measure which initialization actually disappears.
4. **Specialize neutral warp operations.** `md_warp_mesh_varying` in
   `psp-client/milkdrop_warp.c` evaluates four trigonometric warp terms even
   when a node's warp is zero. Skip these terms for finite, zero-warp inputs.
   Reuse rotation sine/cosine for repeated rotations where profitable; retain
   the per-node path for genuinely varying pixel formulas.

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
