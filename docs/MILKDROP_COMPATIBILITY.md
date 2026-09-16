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
  outlines, thick outlines, additive drawing, existing feedback sampling and
  optional external PNG images through the PSP extension described below.
* Zoom/exponent, rotation, translation, center, stretch, warp and per-mesh
  formulas; motion vectors, borders, echo, gamma, wrap/clamp, brighten, darken,
  invert, solarize and center darkening.
* Init/frame/pixel/shape/wave formulas, q1–q32, local t1–t8 where applicable,
  persistent named variables, global reg00–reg99 and memory buffers.
* Preset browser, saved selection, ordered/random/rating-weighted automation,
  playlists, snapshot-to-live fades, embedded/fullscreen LCD and TV layouts.

The new expression batch adds the remaining public function/operator families
of the supplied MilkDrop 2 NS-EEL reference, without implementing shaders.
**This is not a promise that every desktop `.milk` file
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

Still excluded: warp/composite shaders, shader blur stages, arbitrary shader
texture sampling, random texture selection, animated textures and desktop
image formats other than PNG. No DirectX or desktop shader execution is involved.

### Shader preset fallback

The PSP deliberately skips numbered `warp_1`, `warp_2`, … and `comp_1`,
`comp_2`, … shader source records without a warning. Numeric
`MILKDROP_PRESET_VERSION`, `PSVERSION`, `PSVERSION_WARP` and `PSVERSION_COMP`
headers are accepted, including before `[preset00]`. All supported non-shader
fields still run through the existing fixed-function renderer. The result can
look very different, or effectively blank, if the desktop preset relies on
shaders. This is not shader emulation or a shader-performance benchmark.

Unknown non-shader fields, formula errors, invalid textures and malformed
numeric headers still report errors. Existing file/line/encoding limits remain
in force, even on skipped shader records. A preset containing only headers and
shader code still fails the empty-preset check. The legacy `fShader` color
effect is not HLSL source and retains its existing validation.

Rationale: MilkDrop 2's `state.cpp` exports the numbered source and shader
version headers, while `utility.cpp` loads `D3DXCompileShader`. PSP GU instead
provides fixed texture/blend operations, not an HLSL execution target. A general
CPU interpreter would introduce substantial per-pixel work into music playback;
it has not been implemented or benchmarked. References:
[MilkDrop authoring](https://github.com/clangen/milkdrop2-musikcube/blob/master/resources/Milkdrop2/docs/milkdrop_preset_authoring.html),
[PSPSDK GU](https://pspdev.github.io/pspsdk/group__GU.html).

## External PNG textures: baseline

This is an explicit PSP extension, not a claim to execute desktop shader
samplers. Add `psp_texture_0=checker.png` to a preset's `[preset00]` section and
put the image in the **textures subdirectory beside that preset**. Slots 0–3
map to custom shapes 0–3, including all their instances. Enable the matching
`shapecode_0_textured=1`; without a filename, that shape still samples feedback
exactly as before. Rotation/zoom use the existing `tex_ang` and `tex_zoom`
shape fields; vertex colors and alpha modulate the PNG's RGBA pixels.

Each dimension must independently be a power of two from 16 through 256
(for example 128×64). No automatic image resizing occurs. Files are limited
to 1 MiB each; decoded RGBA storage is at most 1 MiB total for four slots,
plus temporary decoder/input memory during activation. Images live in aligned
main RAM, **not additional TV framebuffer/feedback VRAM**. They are decoded
once per activation, swizzled into GE cache-friendly blocks, flushed to RAM
once and reused on subsequent
frames. A brief activation delay is possible when reading the Memory Stick.
Assets are released only after GU synchronization on preset changes or exit.
The existing frame pacing, audio priority and display-switching paths remain
unchanged. Shader sampling, automatic file discovery and image animation are
not part of this implementation.

Names must be basenames ending in `.png`; directory separators, `..`, drive
prefixes and control characters are rejected. Missing, invalid or oversized
images go through the existing preset error display (`psp_texture_N`) rather
than silently rendering a different texture or stopping music.

Test `external-texture-demo.milk`: a rotating orange/cyan circular checkerboard
with transparent corners and a music-reactive size. Copy its accompanying
`textures/checker.png` too. Test embedded/fullscreen LCD and TV, then change
presets and tracks and return to video. The deterministic image can be rebuilt
with `python3 tools/generate_texture_fixture.py`. Host tests cover PNG limits,
alpha, path validation, activation cleanup and the actual GU drawing path;
the final performance/audio check still needs real hardware.

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

After the HTTPS startup fix, an additional visual-only optimization removes
redundant full-state copies in the GU adapter. Custom-wave evaluation stages
only enabled wave contexts rather than the entire preset state. Empty formula
programs return immediately; the rollback bitmap is initialized only on the
first actual memory/register write. Common load/store instructions are checked
before loop/memory operations. These changes preserve formula results, failure
rollback, audio priorities and the existing conservative renderer sleep policy.
In particular, a slow frame still increases the next visual-only delay; these
optimizations do not promise a fixed frame rate or eliminate every visible hitch.

Reference reviewed: MilkDrop 2 `ns-eel2/nseel-compiler.c`, `nseel-eval.c`,
`nseel-cfunc.c`, `nseel-ram.c`, and `vis_milk2/state.cpp`/`milkdropfs.cpp` from the
source archive already used by this project. The implementation remains new
bounded C code rather than copied architecture-specific assembly.
