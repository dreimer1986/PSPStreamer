# MilkDrop compatibility: current non-shader engine

This is the current feature/limit reference. Earlier `MILKDROP_*` documents
describe individual implementation batches; their lists of missing features
are historical. The renderer is PSP-native fixed-function GU and the formula
engine is a bounded interpreter, not a port of the desktop x86 JIT.

## Implemented feature families

The [import/resource update](MILKDROP_RESOURCE_LIMITS.md) expands importer
capacity, uses bounded sparse local and shared memory, and stores large
uniform local fills as compact defaults. Expensive setup/main-frame work has
separate bounded fuel and cooperatively yields on PSP. Point geometry retains
its existing budget. Try `local-sparse-fill-demo.milk` and `sparse-global-demo.milk`.

Import compatibility with fork exports: known numeric fields not read by
MilkDrop 2's `CState/CWave/CShape::Import` are accepted without an effect:
`nEchoWrap_x/y`, `nWrapMode_x/y`, custom-wave `bDrawBack/x/y`, and custom-shape
`bDrawBack/tex_capture/tex_cx/tex_cy/x_wrap_mode/y_wrap_mode`. This does not
implement those fork-specific effects. Numeric values must still be finite and
well formed; unknown names in supported slots remain errors.

Like the reference's `MAX_CUSTOM_WAVES=4` and `MAX_CUSTOM_SHAPES=4`, only slots
0–3 are imported. Additional numbered slots are skipped without allocating
storage or compiling their formulas. Malformed slot identifiers still fail;
a file containing only ignored records is not a usable preset.

Current import audit: **1,713/1,715** files load (previously 1,705).
The two rejected Hexcollie files contain missing formula separators. See the
resource-update guide for execution results and deliberate PSP approximations.
The preceding fork-field hardware checks were confirmed by the user:
`BDRV et AL shifter - tumbling cubes 5`, `ORB - Fire and Fumes 2`, and
`BrainStain- boiling-mix2(redi jedi full carb mix)`.
Host evaluation does not establish visual parity or real-time PSP performance.

Fixed-function reference corrections: render-target setup explicitly restores
smooth vertex color/alpha interpolation, including GU list restarts. Thick
wave copies follow Desktop's positive-y displacement translated to PSP's
downward screen coordinates (one texel upward). Formula-controlled shape
`additive`, `textured` and `thick` flags follow integer truth: magnitudes below
one are false. Shape outlines require positive `border_a` before byte wrapping.
These match MilkDrop 2's `DrawCustomShapes`/`DrawCustomWaves` paths. Static shape
flags also use integer truth; the first repeated shape/formula INI key wins.

Geiss export compatibility: original `ob_a`/`ib_a` and
`nMotionVectorsX`/`nMotionVectorsY` map to the same fields as the earlier aliases.
Empty/comment-only numbered equation records are accepted; numbering and real
syntax errors are still checked. Global/pixel `t` names are ordinary named
variables, distinct from custom shape/wave t1–t8 registers. Shader-only blur
metadata (`b1n` … `b3x`, `b1ed`) is validated but unused with shaders skipped.
Global init/frame code also permits ordinary locals named `x`, `y`, `rad` and
`ang`, as used by Cauldron painterly 5. In pixel formulas these are initialized
per mesh point and may be changed for subsequent equations in that point.
Shape fields keep their existing meaning. Names not registered in a context
become ordinary locals there; `zoom` inside shape code does not change global
zoom. Register-like names outside actual q1–32/t1–8/reg00–99 are ordinary locals.
Zoom exponent supports the original editor range 0.01–100 in static, frame and
pixel contexts; this is an explicit supported range, not a claim that the
desktop file importer rejects values outside it. Motion-vector density follows
the reference's fractional grid and 64x48 draw cap. Shapes support up to 100
sides. The temporary GU list grows from 768 KiB to 1 MiB while MilkDrop is active;
the maximum combined layer case is checked by the host renderer harness.

The unmodified Geiss Artifact 6d (junky warp distortion) and Trampoline files
are tested through frame/pixel evaluation when `GEISS_PRESET_DIR` is supplied.
Hyperdrive remains covered by `HYPERDRIVE_PRESET`. Explosion nz+ requests
311 instances of shape 1 and 281 of shape 2. The batched renderer now supports
both requested counts within the unchanged formula budget. Other expensive
presets can still use a reduced effective instance count. This is not full
Desktop equivalence. Shader-heavy presets can look
very different even when all their non-shader records are accepted. This is a
targeted compatibility pass, not yet a complete audit of all parameter ranges.

`fWaveScale` and `fWaveAlpha` follow MilkDrop 2's float import rather than a
0–1 input restriction. Values above one (and finite negative values) are
accepted. Scale multiplies audio amplitudes; negative scale reverses them.
`fWaveAlpha`/formula output `wave_a` retain their gain until rendering: apply
the waveform-specific factor, then the unclamped volume modulation factor,
then saturate the final opacity to 0–1. Mode 3 retains the reference's special
treble-derived opacity. This follows `state.cpp`'s `GetFastFloat` imports and
`milkdropfs.cpp`'s `DrawWave` in the MilkDrop 2 reference; the mode-dependent
constants match our 512-wide feedback surface. Non-finite static fields and
malformed values remain errors. Formula intermediates follow the assignment
rules below. Extremely large resulting built-in
wave coordinates outside the guarded PSP GU range report `wave geometry`
instead of submitting unsafe vertices; this is a hardware safety limitation,
not a claim of unbounded desktop rendering compatibility. Shader skipping is
independent of these non-shader parameters.

* Built-in wave modes 0–8, stereo PCM/FFT inputs, spectrum waves, smoothing,
  dots, thick/additive lines, volume-dependent alpha and color normalization.
* Four custom waves with independent init/frame/point contexts, up to 512
  points (1023 vertices after smoothing), per-point position and RGBA.
* Four custom shapes, up to 512 instances each with budget-aware fallback, polygon fills/gradients,
  outlines, thick outlines, additive drawing, existing feedback sampling and
  optional external PNG images through the PSP extension described below.
* Zoom/exponent, rotation, translation, center, stretch, warp and per-mesh
  formulas; motion vectors, borders, echo, gamma, wrap/clamp, brighten, darken,
  invert, solarize and center darkening.
* Init/frame/pixel/shape/wave formulas, q1–q32, local t1–t8 where applicable,
  persistent named variables, global reg00–reg99 and memory buffers.
* Preset browser, saved selection, ordered/random/rating-weighted automation,
  playlists, live automatic soft transitions with an optional snapshot fallback,
  embedded/fullscreen LCD and TV layouts.

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

The counted-loop ceiling is the reference's 1,048,576 iterations; fuel usually
stops expensive loops much earlier. Init-only sequential constant local fills
and shared zero clears run as bounded native operations. Their index/result
is preserved; local ranges are clipped to the logical PSP address space.
Shared ranges wrap according to EEL's address conversion. See the current
[resource guide](MILKDROP_RESOURCE_LIMITS.md).

`megabuf(i)` or `base[index]` accesses local memory. Each preset-frame,
preset-pixel, shape-frame, wave-frame and wave-point context owns its own buffer.
Init shares its matching frame buffer. Buffers reset on preset activation.
`gmegabuf(i)` and `reg00`–`reg99` are shared across contexts and preset activations
until application exit. `memcpy(dst,src,count)` handles overlapping local ranges;
`memset(dst,value,count)` fills them. `freembuf()` is an allocation hint/no-op
because this implementation reserves a fixed buffer.

Runtime arithmetic/address/budget failures roll back the current invocation's
values, random state and memory/register writes. An earlier successful
invocation's global writes are not rolled back by a later failure.
`sqrt(x)` uses `sqrt(abs(x))`. Inf/NaN intermediates are allowed; ordinary
assignment stores zero for exceptional/denormal values, while the inspected
Desktop compound stores do not. Non-finite renderer inputs are still rejected.
See [numeric compatibility](MILKDROP_NUMERIC_COMPATIBILITY.md) for the exact
scope and float/double differences. Invalid local addresses are errors; shared
addresses wrap, and resident-page saturation uses the documented quiet fallback.

Pixel named variables/memory persist between points and frames independently
of preset-frame locals. Pixel q values are seeded once from the current frame,
then carry across points without writing back into the frame q values.
`monitor` persists between preset frames. `meshx/meshy`, `pixelsx/pixelsy` and
`aspectx/aspecty` describe the active PSP visualization layout. The aspect
inputs use inverse aspect factors (at least 1), as the reference registers do.
`wave_usedots` aliases `wave_dots`; shape `num_inst` aliases `instances`.
`wrap` is writable per frame. Desktop engine inputs (`time`, `fps`, `frame`,
`progress`, relative audio levels and layout inputs) are context-local mutable
EEL variables. They never change playback clocks or native iteration counts.
Pixel/point inputs retain assignments within that frame's point traversal and
are reseeded next frame. Separate shape and wave contexts receive fresh engine
inputs, not a preset-frame formula's modified values. PSP-specific `psp_*`
audio inputs remain read-only. See [the native-input audit](MILKDROP_NATIVE_INPUTS.md).
Visual `fps` starts at the renderer's nominal 20 Hz before a measured interval
exists, then follows measured visual frame times. Same-time layout redraws keep
the last rate. This prevents first-frame division by zero; it is unrelated to
the video/audio playback clocks or chosen video frame rate.

## PSP limits and differences

| Resource | Limit |
| --- | --- |
| Preset file / physical line | 256 KiB / 2047 bytes |
| Compiled init/frame program | 8192 instructions, 2048 numbered lines |
| Compiled pixel/point program | 4096 instructions |
| Allocated bytecode per preset | 32768 instructions total; empty contexts allocate nothing |
| Named locals | 256 per context, 31-character names |
| Operand stack / parser depth | 48 / 64 |
| Local memory per context / resident shared memory | 8192 / 16384 float slots |
| Local / shared logical addresses | 0–1048575; 256-float pages; no eviction; shared indices wrap |
| Uniform local initialization | Up to 8 range defaults, independent of resident pages |
| Work per point/ordinary invocation / init / main frame program | 4096 / 4194304 / 4194304 steps |
| Shared point/geometry work per visual frame | 262144 steps; setup/main frame have separate allowances |
| Mesh | 16 × 16 cells; budget-aware 8 × 8 formula-evaluation fallback |
| Shapes | Up to 4 × 512 instances, budget-aware fallback; 100 sides each; GPU batches of 32 shapes |
| Custom waves | Up to 4 × 1024 points; budget-aware density, retaining audio-window endpoints |

The latest [owned formula storage](MILKDROP_STORAGE.md) documents allocation,
ownership, the collection comparison and `formula-storage-demo.milk`.
The earlier [memory expansion](MILKDROP_MEMORY_EXPANSION.md) is historical.
See [shape batching](MILKDROP_SHAPE_BATCHES.md) for instance planning, memory
ownership, framebuffer restoration and the `shape-batches-demo.milk` test.

These are current implementation/resource budgets, **not measured hardware
maxima**. See [the fallback inventory](MILKDROP_REFERENCE_AUDIT.md#silent-psp-fallbacks)
for the limits requiring an architectural change (**Umbau nötig**).

The engine uses single-precision floats, not desktop EEL doubles. Integer
operations therefore inherit float precision limits. Source numbering remains
sequential (`per_frame_1`, `_2`, etc.). Output fields retain their existing
safe PSP ranges; arbitrary desktop ranges, unbounded loops/allocations and
desktop host/plugin APIs are not provided. Numbered records now compile as
complete context blocks, including expressions, loops and block comments that
span entries. Desktop `//` and double-backslash line comments and the optional
leading backtick are handled before concatenation. Record separators disappear
(even identifiers can span entries), but explicit whitespace is preserved.
Physical source-line mappings survive into runtime diagnostics. Source buffers
are import-only. Try
`multiline-formula-demo.milk`; see [the collection comparison](MILKDROP_MULTILINE_AUDIT.md).
Empty statements (`;;`) are accepted. Custom-wave `sample`, `value1`, `value2`
and shape `instance`, `num_inst`/`instances` may be assigned within their formula
context; native point/instance loops are not resized by these assignments.
For the expanded compiler and geometry limits, see the
[parser and density batch](MILKDROP_PARSER_DENSITY.md) and try
`extended-formula-demo.milk`, `dense-wave-demo.milk`, `fine-mesh-demo.milk`.
Audio analysis is normalized for this player. Automatic soft transitions evaluate
both presets and blend their meshes on shared feedback, with live shape/wave
crossfades. Primary waves crossfade rather than morphing matching vertices;
desktop spatial transition wipes and shader transitions are not implemented.
Snapshot fades remain available as an option and allocation fallback. See
[live transitions](MILKDROP_LIVE_TRANSITIONS.md) for costs and test instructions.
The legacy `fShader` hue effect uses the Desktop no-shader corner-color equations
and GU interpolated vertex colors (strength clamped to 0–1). It reuses the
existing echo/gamma composition passes, without another buffer or drawing pass.
Random phases, rasterization and aspect handling can differ from Desktop.
Red/blue stereo remains silently omitted. Other unknown fields report errors.
Compare `legacy-shading-demo.milk` with `legacy-shading-off-demo.milk` on hardware;
both also exercise echo zoom below 1 (supported down to 0.01).

Reversed wave-alpha modulation endpoints follow the Desktop's signed interval.
Equal endpoints use a safe step instead of division by zero (an approximation).

Still excluded: warp/composite shaders, shader blur stages, arbitrary shader
texture sampling, random texture selection, animated textures and desktop
image formats other than PNG/JPEG. No DirectX or desktop shader execution is involved.

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
effect is not HLSL source and is rendered through fixed-function vertex colors.

Rationale: MilkDrop 2's `state.cpp` exports the numbered source and shader
version headers, while `utility.cpp` loads `D3DXCompileShader`. PSP GU instead
provides fixed texture/blend operations, not an HLSL execution target. A general
CPU interpreter would introduce substantial per-pixel work into music playback;
it has not been implemented or benchmarked. References:
[MilkDrop authoring](https://github.com/clangen/milkdrop2-musikcube/blob/master/resources/Milkdrop2/docs/milkdrop_preset_authoring.html),
[PSPSDK GU](https://pspdev.github.io/pspsdk/group__GU.html).

## External PNG and JPEG textures

This is an explicit PSP extension, not a claim to execute desktop shader
samplers. Add `psp_texture_0=checker.png` to a preset's `[preset00]` section and
put the image in the **textures subdirectory beside that preset**. Slots 0–3
map to custom shapes 0–3, including all their instances. Enable the matching
`shapecode_0_textured=1`; without a filename, that shape still samples feedback
exactly as before. Rotation/zoom use the existing `tex_ang` and `tex_zoom`
shape fields; vertex colors and alpha modulate the PNG's RGBA pixels.

PNG dimensions must independently be powers of two from 16 through 256
(for example 128×64). JPEG/JPG supports baseline and progressive RGB/YCbCr or
grayscale images up to 1024×1024. JPEG dimensions are rounded up to powers of two,
clamped to 16–256, using decoder downscaling where possible and nearest-neighbor
resizing once at activation. JPEG alpha is opaque; CMYK is not supported.
The originals are never modified. These are conservative loader limits, not
claims about the PSP's absolute hardware maximum. Files are limited
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

Names must be basenames ending in `.png`, `.jpg` or `.jpeg` (case insensitive);
directory separators, `..`, drive
prefixes and control characters are rejected. Missing, invalid or oversized
images go through the existing preset error display (`psp_texture_N`) rather
than silently rendering a different texture or stopping music.

For example, use `psp_texture_0=example.jpg` and copy the image to
`textures/example.jpg` beside the preset. JPEG preview images named after presets
are not automatically assigned as textures. This does not implement HLSL samplers.

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

The ongoing [Desktop parameter audit](MILKDROP_REFERENCE_AUDIT.md) distinguishes
import rules from editor sliders and draw-time limits. Supported global boolean
file switches accept nonzero integers; the legacy `bMotionVectorsOn` supplies the
default only when `mv_a` is absent. Shape sides are preserved for equations and
truncated/clamped to 3–100 at rendering/allocation time, matching the Desktop
draw rule. This does **not** remove the eight-instance-per-shape resource limit.

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
