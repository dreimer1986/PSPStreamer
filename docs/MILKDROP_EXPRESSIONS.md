# Expression support: source audit and implementation boundary

The bounded arithmetic/time subset is now implemented in `preset_math.c`
and integrated into the custom slot. See [syntax and limits](MILKDROP_PRESETS.md).
Native `psp_*` music inputs are also available using existing display snapshots.
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
2. Implemented arithmetic, parentheses, `sin/cos/abs/sqrt`, `min/max`, time and supported static
   output fields. Limit instructions, nesting and statements. Unknown
   functions remain load errors with line/field information. Ordinary unknown
   variable names can now use the bounded persistent namespace described below.
3. Per-frame output inputs reset from static preset values. Init/q reseeding
   and named persistent variables now have explicit lifetime/reset tests;
   see [named variables](MILKDROP_VARIABLES.md).
4. Non-finite results, out-of-range outputs and division errors are rejected with a visible diagnostic;
   do not pass invalid transforms to GU or silently substitute an effect.
5. Deferred: compare supported operations against original NS-EEL on a supported host
   before claiming compatible semantics. Keep tests for execution bounds and
   adaptive-frame timing separate from the PSP audio worker.
6. Native display variables now have explicit names (`psp_low/mid/high/level`
   and smoothed band variants), bounded 0–1 inputs and elapsed-time smoothing.
   They are not aliases for original music variables. Original names now use
   separate relative-level normalization with the reference's attack/release
   and history equations. Input bins, initialization, silence scale and
   elapsed-time warm-up remain documented PSP approximations. Host tests
   compare the equations, not the original FFT or full NS-EEL runtime.
   Reuse analysis snapshots; never retain PCM
   ownership or move expression evaluation into the decoder/DAC threads.

Forward-only conditional expressions and additional Boolean/math functions
are documented in [conditional formulas](MILKDROP_CONDITIONS.md).
Shader code, arbitrary EEL, per-pixel expressions and custom wave/shape programs
remain separate later milestones. The supported-subset table must expand
only alongside implementation and tests.
