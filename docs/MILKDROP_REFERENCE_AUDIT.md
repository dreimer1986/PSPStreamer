# Desktop compatibility audit (in progress)

The authority is MilkDrop 2's `vis_milk2/state.cpp` (import and variable
registration), `milkdropfs.cpp` (evaluation/drawing), and `plugin.cpp`/`menu.cpp`
(editor only). Presets are regression fixtures, not range specifications.

`python3 tools/audit_milkdrop_import.py /path/to/vis_milk2/state.cpp` produces a
JSON inventory of literal GetFastInt/GetFastFloat imports and constructed custom
wave/shape fields, including line numbers and the source SHA-256. It deliberately
does not invent minimum/maximum values: the importer generally has none. This
lexical inventory does not cover formula registration, shader text or draw-time
rules; those require separate review. Review unmatched source constructs if the
reference version changes.

## Findings and work queue

| Family | Desktop behavior | PSP status / required work |
| --- | --- | --- |
| Wave alpha/scale | Floating-point import; alpha saturated after mode/volume calculation | Finite floats accepted; implemented previously |
| Global supported boolean switches | Integer unequal to zero is true | Now normalized on import; fractional/malformed input still rejected |
| Old `bMotionVectorsOn` | Default for `mv_a`; explicit `mv_a` always wins | Implemented independently of file order |
| Motion density | Fraction retained; draw count limited to 64 × 48 | Implemented previously |
| Shape instances | Import integer; draw each instance | Up to 512 per shape, budget-aware fallback; batched renderer supports Explosion's 311/281 counts |
| Shape sides | Import integer; formulas may produce fractions; draw truncates and clamps to 3–100 | Implemented; original value remains visible to formulas, allocation uses the clamped count |
| Shape colors | Draw converts to integer bytes with `& 0xff` | Implemented with overflow-safe conversion, including border alpha |
| Shape coordinates/radius/angles | Float import; geometry evaluated when drawing | PSP ranges remain narrower; widen only with geometry and allocation guards |
| Custom waves | Integer flags/counts and float gains/colors imported | Audit sample limiting, smoothing and per-point semantics before widening |
| Zoom/rotation/warp/stretch/centers | Float import, not editor bounds | PSP still imposes conservative bounds; singular transforms and GE overflow need explicit handling |
| Decay/gamma | Float import; renderer determines result | PSP range and integer-conversion safety need joint review |
| Echo | Float zoom/alpha; integer orientation | Zoom .01–100 implemented, including values below 1 |
| Borders | Float import; draw behavior separate | PSP normalized colors and sizes remain restrictive |
| `fShader` | Legacy hue effect, distinct from HLSL source | Desktop corner-color equations, GU interpolation; strength 0–1 |
| HLSL / blur metadata | Desktop shader pipeline | Shader source and associated blur metadata intentionally ignored |
| Formula variables | Registered per context, then evaluated | Native input mutability implemented; other namespace/budget gaps listed below |

Useful examples of **editor** limits that must not be mistaken for import limits:
gamma 1–8; centers −1–2; log controls commonly .01–100; wave alpha .001–100.
Changing only the importer can expose invalid divisions, unsafe float-to-int
casts, excessive work or GU-list overflow in our renderer.

## Explosion: original limitation and subsequent batching

**Update:** the [batched renderer](MILKDROP_SHAPE_BATCHES.md) now implements
the architectural step described below. This paragraph records the earlier
eight-instance limitation, not the current playback path.

`Geiss - Explosion nz+.milk`, line 162, sets `shapecode_1_num_inst=311`.
The current evaluator stores all instances in a fixed-size `MdDecor` array.
Raising the limit also expands copies/stack usage and worst-case GU allocations,
not just parser acceptance. With current maximum geometry the 1 MiB GU list is
already close to its reserved command headroom.

Next architectural step: evaluate/draw instances in bounded batches, preserving
instance order, shared per-shape variables and rollback/error semantics. Budget
checks must account for sides, outlines and wave vertices. Silently capping
311 to eight was previously disallowed. Following the user's explicit preference
for usable approximations, the current fallback now does exactly that: up to
eight instances are evaluated, with `instance=0..7` and effective `instances=8`.
This changes density and the evolution of stateful per-instance equations.
Full-density rendering still needs the architectural change (**Umbau nötig**).
Audio scheduling and A/V timing remain unchanged.

## Silent PSP fallbacks

Finite values outside supported numerical ranges no longer stop a preset.
The importer bounds stored fields and the evaluator bounds outputs after the
equations have run. Ordinary user variables continue to evolve; bounded outputs
are not written back into them. This intentionally approximates the Desktop:
import-time bounding also means equations see the bounded static defaults.
No per-frame warnings/popups are emitted.

| Area | Current fallback | Classification |
| --- | --- | --- |
| Shapes | Four slots, at most 512 instances each with execution-budget fallback, 3–100 sides | Batched; sides follow Desktop draw cap; not unlimited Desktop density |
| Custom waves | Four slots, 2–1024 points; separation 0–128; above 512 points PCM/FFT inputs are interpolated | Expanded; Desktop itself clamps computed points to 512 |
| Custom wave gain/smoothing | 0–4 / 0–1; normalized colors and positions 0–1 | Conservative existing renderer limits, not hardware maxima |
| Shape position/radius | −4..4; angles −100..100; texture zoom .1–10 | Offscreen triangle/border clipping implemented; still bounded, not unrestricted Desktop geometry |
| Shape colors | Finite floats; convert to byte using truncation and wrapping | Desktop semantics; safe extension for values overflowing Desktop integer conversion |
| Static zoom / frame zoom | .8–1.2 / legacy frame .1–64, otherwise .8–1.2 | Historical renderer limits; not Desktop limits or proven hardware limits |
| Rotation / warp | ±100 radians / ±4 | Rotation widened consistently across import/frame/pixel; warp retains historical limit |
| Warp speed/scale, decay | 0–4 / .1–8 / .8–1 | Historical limits; not hardware maxima |
| Translation, centers, stretch | ±1, 0–1, .25–4 | Conservative geometry limits |
| Zoom exponent | .01–100 | Current safe-input budget, derived from Desktop editor range |
| Echo zoom/alpha/orientation, gamma | .01–100 / 0–1 / 0–3, gamma 1–4 | Pass/geometry budget; gamma affects GU-list usage |
| Built-in wave mode | 0–8; out-of-range values select nearest mode | Supported mode set |
| Borders / motion vectors | Border size 0–.5; colors/alpha 0–1; motion grid at most 64×48 | Geometry budget / Desktop motion-grid rule |
| Feedback and mesh | 512×256 feedback, 16×16-cell mesh; budget-aware 8×8 formula fallback | Mesh expanded; larger feedback buffers: **Umbau nötig** |
| External textures | Four, at most 256×256 RGBA each; JPEG source up to 1024×1024 | Loader/memory budget; not GE's absolute texture limit |
| GU list | 1.5 MiB; 1206368-byte vertex peak in layer stress test; shapes submitted in batches of 32 | Shape batching implemented; other layers retain bounded allocations |

Dynamic booleans use the EEL truth threshold (absolute value at least .00001);
integer file switches use nonzero as true. Formula-selected modes/orientations
are truncated after bounding. PCM separation is reduced instead of reading
outside the sample window.

Still reported: malformed/unknown fields, broken assets, NaN/infinity,
undefined formula operations, syntax errors and exhausted interpreter budgets.
Programs are not silently truncated: this could remove loop exits or state
updates. Parser/VM budgets are not yet converted to
approximation fallbacks. This is a numerical/resource fallback pass, **not a
claim of complete Desktop compatibility or a completed hardware-limit study**.

## Formula namespace audit

Compared with the per-frame, per-vertex, custom-shape, wave-frame and wave-point
registrations in Desktop `state.cpp`, plus identifier/register resolution in
`ns-eel2/nseel-eval.c`:

- Fixed: `ang`, `rad`, `x`, `y` were incorrectly reserved in global formulas.
  Cauldron painterly **5**, physical line 233 (`per_frame_11`), exposed this.
- Fixed: fields from another context were rejected rather than becoming local
  variables. Examples: `zoom` in shape code, `wave_r` in pixel code, `sample`
  outside wave-point code. In wave-point code, `samples` is now an ordinary
  local, as it is not registered there by the Desktop.
- Fixed: q/t/reg prefixes were over-reserved. `q33`, `q01`, `t9`, `reg100`, etc.
  are valid ordinary names, not unsupported registers. Actual registered ranges
  retain their original bindings. Unknown identifiers therefore no longer
  diagnose presumed typos, matching the Desktop's local-variable behavior.
- Fixed: pixel coordinates may be assigned inside a point's formula program.
  They are reinitialized for each mesh point and do not modify the engine mesh.
- Already correct: identifiers/functions are case-insensitive; registered q/t
  lifetimes and point-local coordinate initialization are independently tested.

Remaining differences found, **not hardware-limit claims**:

- Completed: empty statements and mutable shape instance/wave-point inputs;
  larger compiled programs without increasing execution fuel or megabuf memory.
  See [the parser/density report](MILKDROP_PARSER_DENSITY.md).

- Completed: native inputs such as `time`, `fps` and relative audio values are
  mutable within their EEL contexts, with separate native reseeding and
  point-traversal lifetimes. See [the input audit](MILKDROP_NATIVE_INPUTS.md).
- Numbered formula records now compile as complete context blocks, matching
  Desktop record/comment preprocessing, with physical diagnostic line mappings.
  **Completed:** see [the collection comparison](MILKDROP_MULTILINE_AUDIT.md).
- Compiled instruction counts, locals, memory and execution budgets remain
  bounded. Example: a 200000-iteration memory-initialization loop cannot simply
  be accepted or truncated without deciding the intended reduced behavior.
- `fShader` now uses the Desktop fixed-function corner-color equations in the
  existing composition pass. Random phases and raster/aspect details can differ.
  Red/blue stereo is still omitted rather than rejecting the preset.
- Shader blur variables currently behave as locals rather than having the
  Desktop's imported defaults/output processing; skipped shader rendering does
  not make this fully equivalent if other formulas read them.
- Function names and non-finite spellings remain reserved in this parser.
  Existing PSP extension inputs are retained rather than removed for exact
  namespace equivalence.

Collection check: 1530 `.milk` files in the user's supplied folder. After the
namespace and pixel-coordinate fixes, 683 parsed and 268 first failed on
`fShader`. With the explicit legacy-effect fallbacks and alpha-interval fix,
**876 parse successfully**. This is **parse acceptance only**, not proof of
complete rendering or playback. Cauldron painterly 3/4/5 and the original Geiss
fixtures are also exercised through frame/pixel evaluation.

Wave alpha modulation also no longer rejects reversed/equal endpoints. Reversed
intervals use the Desktop signed division. Equal endpoints use a deterministic
step (off at/below the threshold, ordinary alpha above it), avoiding division
by zero. This zero-width-interval rule is an explicit PSP approximation.
