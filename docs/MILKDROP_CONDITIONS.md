# Conditional preset formulas

Copy `presets/branch-beat-demo.milk` as `presets/active.milk`, restart music
and press Square four times. Cross + Triangle toggles fullscreen. This new
PSPStreamer example selects dotted/continuous waves, color and zoom from music
inputs, with smooth sigmoid-controlled echo intensity and timed rotation.
No preset is selected automatically and no server update is needed.

## Functions

| Function | Behavior |
| --- | --- |
| `if(condition, yes, no)` | Executes exactly one branch; false when `abs(condition) < 0.00001` |
| `bnot(x)` | 1 when `abs(x) < 0.00001`, otherwise 0 |
| `band(a,b)` | 1 when both magnitudes are **greater than** 0.00001 |
| `bor(a,b)` | 1 when either magnitude is **greater than** 0.00001 |
| `tan(x)` | Tangent, radians |
| `asin(x)`, `acos(x)` | Inverse trigonometric functions, result in radians; input −1…1 |
| `sigmoid(x,k)` | `1/(1+exp(-x*k))`, evaluated without exponential overflow |

The slightly different equality boundary for named `band/bor` versus `if/bnot`
matches the inspected MilkDrop 2 source. These are logical functions, not
integer bit masks. `band` and `bor` evaluate **both** arguments, as in the
reference; use `if` when evaluation must be guarded.

```ini
per_frame_1=warp=if(above(psp_low,0),min(4,1/psp_low),0);
per_frame_2=wave_r=if(above(bass,1.1),1,0.2);
```

Silence takes the first formula's zero branch without dividing by zero.
Nested conditions work inside ordinary arithmetic and other functions.
Assignments within branches, statement blocks, `&&/||`, comparisons written
as operators, loops and arbitrary variable names are still unsupported.
Both branches must contain valid supported syntax even when one is never run.

## Bounds and failure behavior

Both branches count toward the existing total 128-instruction compilation
budget; nesting remains limited to 16 and the runtime stack to 24 floats.
Branches can only jump forward within that program: they cannot introduce
loops or unbounded execution. Compilation remains before music workers start;
execution is in the UI worker, with no allocation or audio scheduling changes.
Invalid selected branches, domain errors or out-of-range outputs fail
transactionally: no partial formula results reach rendering. An unselected
branch's arithmetic is not executed. Finite sigmoid inputs saturate at 0/1
for extreme products rather than failing on an overflowing exponential.

## Reference and limitations

Inspected MilkDrop 2 mirror revision
`b5e4136c2f050eafa10aa199bb72c8e5c12c9320`:

- [`nseel-compiler.c`](https://github.com/eef2697d62fbe08e2fd927278/milkdrop2/blob/b5e4136c2f050eafa10aa199bb72c8e5c12c9320/ns-eel2/nseel-compiler.c):
  named Boolean functions, sigmoid and math registration.
- [`asm-nseel-x86-gcc.c`](https://github.com/eef2697d62fbe08e2fd927278/milkdrop2/blob/b5e4136c2f050eafa10aa199bb72c8e5c12c9320/ns-eel2/asm-nseel-x86-gcc.c):
  `nseel_asm_if` calls only the selected compiled branch; `bnot` threshold.

The implementation is portable new C bytecode, not copied x86 assembly.
It uses floats and explicit domain checks, so this is not a claim of full
NS-EEL numerical compatibility. CPU tests cover epsilon boundaries, nested
branches, skipped/selected domain errors, eager Boolean arguments, malformed
syntax, budgets, invalid jump targets, atomic rollback and a 30-minute demo
simulation. The actual GU mock also runs the demo through parser, evaluator
and renderer on both outputs and viewport sizes. Hardware sound/render timing
still needs a PSP test.

## Remaining compatibility milestones

- Arbitrary named variables with persistent lifetime. [Init and q variables](MILKDROP_INIT.md)
  are now implemented: `q` values are reseeded from init before each preset
  frame, not treated as generic cross-frame accumulators.
- Per-vertex (`per_pixel_*`) programs with coordinate inputs and a mesh-wide
  execution budget.
- Independent custom shape and waveform programs, including per-point code;
  more built-in waveform modes and the required stereo/spectrum inputs.
- External textures, preset browsing/transitions and blending.
- Shader-based warp/composite presets: a separate renderer/compatibility
  problem, not something the PSP fixed-function pipeline executes directly.

The stable 512×256 render/presentation path, GUI apertures, audio and video
timestamps are unchanged by this formula extension.
