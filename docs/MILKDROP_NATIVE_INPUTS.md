# Native input compatibility and next work

## Reference-backed changes

The local MilkDrop 2 reference in `vis_milk2/state.cpp` registers engine inputs
as EEL variables. `milkdropfs.cpp` loads them in `LoadPerFrameEvallibVars`,
`LoadCustomShapePerFrameEvallibVars`, and `LoadCustomWavePerFrameEvallibVars`.
Despite "read-only" comments, EEL permits assignments. The native loader,
not a VM write restriction, controls their lifetime.

- `time`, `fps`, `frame`, `progress`, `bass/mid/treb`, their `_att` values and
  registered layout inputs can be assigned within formulas.
- Init input assignments do not overwrite the next frame's engine seeds.
- Shapes reload native inputs for each instance, independently of preset-frame
  assignments. Current preset q outputs still reach shapes as before.
- Pixel inputs are seeded before preset-frame code on Desktop. Our separate
  grid evaluator seeds the equivalent original engine values, then retains
  assignments across points within the same grid, not across frames.
- Wave-point inputs are seeded before wave-frame execution on Desktop. The PSP
  restores those engine seeds after evaluating wave-frame code; frame q/t and
  color outputs retain their existing behavior. Point input assignments persist
  across points, then reset on the next wave frame.
- The PSP-only `psp_*` audio extensions retain their read-only contract.
- Playback timestamps, measured visual FPS, native shape/sample counts, audio
  output and scheduler priorities are not changed by formula assignments.

The old ±0.2-radian rotation cap is widened consistently to ±100 radians for
static, per-frame and per-pixel rotation. Rotation uses bounded sine/cosine,
does not grow vertex counts and does not require a new framebuffer. This is
still a PSP policy bound, not a Desktop or measured hardware limit.

## Verification and hardware test

Unit tests cover mutable names in all formula contexts, init/frame reseeding,
cross-context isolation, point-to-point persistence and unchanged native
frame/FPS state. `native-inputs-demo.milk` combines rotating diamonds, a wave
and feedback with assignments to native inputs and rotations above the old
cap. It is included in the LCD/TV, window/fullscreen host GU test.

All 177 automated tests pass, including the original Explosion preset. The
native-input collection comparison retains 1,651 imports and 1,217 successful
120-frame formula runs from 1,715 presets, without regressions. This change
improves semantics rather than adding passing presets in this collection.

The other 64 presets fail import. Among the 434 imported presets failing the
runtime audit, first failures occur in wave-point code (178), preset-frame code
(140), pixel code (44), preset init (36), and shape-frame code (36). These are
failure locations, not proven root causes: budgets, memory access and invalid
arithmetic must be distinguished before deciding on a fix.

Two import failures are confirmed malformed static values (`rot=-` in
`Idiot24-7 - Ascending to heaven 2` and `TobiasWolfBoi - The Pit`), not a
rotation-limit rejection. Other failures include extension fields such as
`nEchoWrap_x`, `wavecode_0_bDrawBack`, and `shapecode_0_tex_capture`, absent from
the supplied MilkDrop 2 `state.cpp`/`milkdropfs.cpp` reference. They must not be
silently classified as HLSL shader source merely to inflate acceptance counts.

On PSP, check that this demo animates without errors, retains clean audio and
can switch back to existing presets and video. Higher rotation may noticeably
change the appearance of existing presets; that is intentional.

## Ordered remaining work (current, not historical milestones)

1. Continue numerical-range compatibility: offscreen shapes/waves need correct
   clipping, not simply a larger accepted coordinate range. Zoom/exponent
   combinations also need finite geometry guarantees before widening ranges.
2. Classify collection failures by actual import/runtime cause. Host formula
   acceptance is not proof of visual equivalence or PSP performance.
3. Larger program/local/memory budgets require a memory-layout/stack review;
   do not silently truncate formulas or increase execution work indiscriminately.
4. Higher feedback resolution needs a separate VRAM/buffer-layout change.
5. Optional dual-live-preset transitions and remaining fixed-function fidelity.

Shape batching and the first formula-memory expansion are complete. Textures
are implemented. Desktop HLSL shader execution remains deliberately excluded.
Older step-specific documents describe their historical state, not this list.
