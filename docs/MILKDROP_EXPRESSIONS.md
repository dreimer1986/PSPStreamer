# Expression support: source audit and implementation boundary

The bounded arithmetic/time subset is now implemented in `preset_math.c`
and integrated into the custom slot. See [syntax and limits](MILKDROP_PRESETS.md).
The source findings below guided the boundary; they do not imply full EEL
compatibility. Original NS-EEL differential testing remains outstanding.

Reference studied locally: MilkDrop 2 mirror revision
`b5e4136c2f050eafa10aa199bb72c8e5c12c9320`,
[milkdropfs.cpp](https://github.com/eef2697d62fbe08e2fd927278/milkdrop2/blob/b5e4136c2f050eafa10aa199bb72c8e5c12c9320/vis_milk2/milkdropfs.cpp)
and `ns-eel2/nseel-compiler.c`.

## Findings affecting correctness

- `LoadPerFrameEvallibVars` reloads transform and color inputs from preset
  state before per-frame execution. Applying formulas cumulatively to last
  frame's transformed zoom/rotation would not preserve this behavior.
- `time` is elapsed plugin time, `fps` is measured frame rate, and `frame`
  comes from the renderer. None is the video FPS or an audio playback clock.
  Our adaptive visualization rate means a fixed 20 FPS constant is wrong.
- `bass/mid/treb` use `mysound.imm_rel`; the corresponding `*_att` values use
  `mysound.avg_rel`. They are not our existing 0–100 twelve-bin display data.
  A raw division by 100 must not be advertised as the original semantics.
- Per-frame and per-vertex execution are separate. Merely accepting
  `per_pixel_*` in the current 8×8 warp mesh would require a distinct variable
  context and execution budget, not just reusing the per-frame program.
- The bundled compiler has architecture-specific x86/PPC machine-code paths.
  Linking that compiler into a MIPS executable does not yield a PSP backend.

## Implementation and deferred work

1. Implemented a portable, explicitly documented expression subset, compiling
   into bounded bytecode before music starts. No source parsing or allocation
   during rendering; no native code generation.
2. Implemented arithmetic, parentheses, `sin/cos/abs`, time and supported static
   output fields. Limit instructions, nesting and statements. Unknown
   identifiers/functions remain load errors with line/field information.
3. Per-frame output inputs reset from static preset values. Add persistent
   variables only with explicit lifetime/reset tests.
4. Non-finite results, out-of-range outputs and division errors are rejected with a visible diagnostic;
   do not pass invalid transforms to GU or silently substitute an effect.
5. Deferred: compare supported operations against original NS-EEL on a supported host
   before claiming compatible semantics. Keep tests for execution bounds and
   adaptive-frame timing separate from the PSP audio worker.
6. Deferred: add music variables only after defining and validating relative-level
   normalization and smoothing. Reuse analysis snapshots; never retain PCM
   ownership or move expression evaluation into the decoder/DAC threads.

Shader code, arbitrary EEL, per-pixel expressions and custom waves/shapes
remain separate later milestones. The supported-subset table must expand
only alongside implementation and tests.
