# MilkDrop compatibility: current non-shader engine

This is the current feature/limit reference. Earlier `MILKDROP_*` documents
describe individual implementation batches; their lists of missing features
are historical. The renderer is PSP-native fixed-function GU and the formula
engine is a bounded interpreter, not a port of the desktop x86 JIT.

## Implemented feature families

* Built-in wave modes 0–8, stereo PCM/FFT inputs, spectrum waves, smoothing,
  dots, thick/additive lines, volume-dependent alpha and color normalization.
* Four custom waves with independent init/frame/point contexts, up to 512
  points (1023 vertices after smoothing), per-point position and RGBA.
* Four custom shapes, up to eight instances each, polygon fills/gradients,
  outlines, thick outlines, additive drawing and existing feedback sampling.
* Zoom/exponent, rotation, translation, center, stretch, warp and per-mesh
  formulas; motion vectors, borders, echo, gamma, wrap/clamp, brighten, darken,
  invert, solarize and center darkening.
* Init/frame/pixel/shape/wave formulas, q1–q32, local t1–t8 where applicable,
  persistent named variables, global reg00–reg99 and memory buffers.
* Preset browser, saved selection, ordered/random/rating-weighted automation,
  playlists, snapshot-to-live fades, embedded/fullscreen LCD and TV layouts.

The new expression batch adds the remaining public function/operator families
of the supplied MilkDrop 2 NS-EEL reference, without implementing shaders or
external textures. **This is not a promise that every desktop `.milk` file
loads or renders identically.** The bounds and adaptations below are deliberate.

## Formula language

Names are case-insensitive. Arithmetic includes `+ - * / % ^` (`^` means power),
integer bitwise `& |`, comparisons `== != < <= > >=`, logical `! && ||`, ternary
`condition ? true_value : false_value`, and assignments `= += -= *= /= %= ^= &= |=`.
Assignments are expressions; semicolon-separated expression blocks work inside
function arguments and parentheses. `if()`, `&&`, `||` and ternary only execute
the selected branch. Named `band()`/`bor()` remain eager, as in NS-EEL.

Functions: `sin cos tan asin acos atan atan2 abs min max sqrt invsqrt floor ceil
int exp log log10 sqr sign pow above below equal bnot band bor sigmoid if rand
assign exec2 exec3 loop while megabuf gmegabuf memcpy memset freembuf`.
`int()` is the reference alias for `floor()`, not truncation towards zero.
`rand(n)` produces a floating value scaled by `max(1,floor(n))`; it is not a
cryptographic RNG. `$pi`, `$e`, `$phi`, `$xFF` hexadecimal and `$'A'` character
constants are accepted. Inline `//` and same-line `/* ... */` comments work.

Examples:

```ini
per_frame_init_1=counter=0;memset(0,0,16);
per_frame_1=counter+=1;i=0;loop(8,megabuf(i)=sin(time+i);i+=1);
per_frame_2=reg00=psp_low_smooth;wrap=(counter%100<80);
per_pixel_1=dx=(x>.2 && x<.8)?q1*sin(ang):0;
```

`loop(count,body)` truncates the positive count; zero/negative counts skip its
body. `while(body)` repeats while the last expression is true. Both consume
the execution budget, including nested loops. Exhaustion reports a preset
error and the application falls back to its normal visualization; it does
not stop the audio worker.

`megabuf(i)` or `base[index]` accesses local memory. Each preset-frame,
preset-pixel, shape-frame, wave-frame and wave-point context owns its own buffer.
Init shares its matching frame buffer. Buffers reset on preset activation.
`gmegabuf(i)` and `reg00`–`reg99` are shared across contexts and preset activations
until application exit. `memcpy(dst,src,count)` handles overlapping local ranges;
`memset(dst,value,count)` fills them. `freembuf()` is an allocation hint/no-op
because this implementation reserves a fixed buffer.

Runtime arithmetic/address/budget failures roll back the current invocation's
values, random state and memory/register writes. An earlier successful
invocation's global writes are not rolled back by a later failure. Invalid
math (for example division by zero or `sqrt(-1)`) fails explicitly instead of
propagating non-finite geometry. Out-of-range addresses are errors, not wrapped.

Pixel named variables/memory persist between points and frames independently
of preset-frame locals. Pixel q values are seeded once from the current frame,
then carry across points without writing back into the frame q values.
`monitor` persists between preset frames. `meshx/meshy`, `pixelsx/pixelsy` and
`aspectx/aspecty` describe the active PSP visualization layout. The aspect
inputs use inverse aspect factors (at least 1), as the reference registers do.
`wave_usedots` aliases `wave_dots`; shape `num_inst` aliases `instances`.
`wrap` is now writable per frame. Engine inputs remain read-only.
Visual `fps` starts at the renderer's nominal 20 Hz before a measured interval
exists, then follows measured visual frame times. Same-time layout redraws keep
the last rate. This prevents first-frame division by zero; it is unrelated to
the video/audio playback clocks or chosen video frame rate.

## PSP limits and differences

| Resource | Limit |
| --- | --- |
| Preset file / physical line | 64 KiB / 2047 bytes |
| Compiled init/frame program | 512 instructions, 128 numbered lines |
| Compiled pixel/point program | 256 instructions |
| Named locals | 64 per context, 31-character names |
| Operand stack / parser depth | 48 / 16 |
| Local memory per context / shared memory | 1024 float slots each |
| Work per invocation / visual frame | 4096 / 262144 steps, including bulk memory work |
| Mesh | 8 × 8 cells, interpolated by GU |
| Shapes | 4 × 8 instances, 32 sides each |
| Custom waves | 4 × 512 points |

The engine uses single-precision floats, not desktop EEL doubles. Integer
operations therefore inherit float precision limits. Source numbering remains
sequential (`per_frame_1`, `_2`, etc.). Output fields retain their existing
safe PSP ranges; arbitrary desktop ranges, unbounded loops/allocations, multiline
comments spanning separate preset entries and desktop host/plugin APIs are not
provided. Audio analysis is normalized for this player. The renderer retains
snapshot transitions rather than running two full preset engines simultaneously.
Obsolete flags without an active path in the reference MilkDrop 2 renderer are
not silently accepted as implemented effects.

Still excluded: warp/composite shaders, shader blur stages and external/user
textures. Sampling the renderer's own existing feedback is not a new external
texture loader. No DirectX or desktop shader execution is involved.

## Tests and hardware check

The host suite covers precedence, lazy branches, assignment sequences, loops,
loop limits, memory scope/overlap/bounds/rollback, random values, engine inputs,
pixel persistence, malformed bytecode and the earlier rendering regressions.
The real GU adapter is exercised with all three new presets on LCD and TV,
embedded and fullscreen. PSP compilation and stack-usage checks are separate
from hardware audio/performance validation.

Copy the updated **EBOOT.PBP and PSPStreamer.prx**, plus these preset files:

1. `eel-memory-orbit-demo.milk`: eight colored hexagons orbit; shared memory
   and audio levels drive their colors/size.
2. `eel-grid-logic-demo.milk`: alternating mesh motion with conditional transforms
   and periodic dots/wrap changes.
3. `eel-wave-loop-demo.milk`: a thick cyan multi-harmonic line, with harmonics
   evaluated through loops for each point.

Select them using Circle during music. Check embedded/fullscreen with
Cross+Triangle on LCD and TV, several track changes, Stop, then a video with
subtitles. Audio must remain clear. Existing active.milk/config/playlists are
not overwritten. No server update is needed for this MilkDrop batch.

## Code audit changes

Larger compiled presets are parsed in checked heap storage rather than on the
PSP stack; rendering uses static single-render-thread scratch for large candidate
states. The compiler reports roughly 116 KiB for the largest evaluator and
19 KiB for its VM callee, below the main thread's 256 KiB stack (not a complete
runtime high-water measurement). Memory rollback uses a dirty bitmap to avoid
quadratic searches. A socket leaked on DNS failure while downloading binary
data was fixed, and the visual `fps=0` startup trap was removed. Stable decoder
initialization and A/V timing were not changed.

Reference reviewed: MilkDrop 2 `ns-eel2/nseel-compiler.c`, `nseel-eval.c`,
`nseel-cfunc.c`, `nseel-ram.c`, and `vis_milk2/state.cpp`/`milkdropfs.cpp` from the
source archive already used by this project. The implementation remains new
bounded C code rather than copied architecture-specific assembly.
