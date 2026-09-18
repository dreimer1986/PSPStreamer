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
| Shape instances | Import integer; draw each instance | **Only eight per shape on PSP; not compatible with Explosion's 311** |
| Shape sides | Import integer; formulas may produce fractions; draw truncates and clamps to 3–100 | Implemented; original value remains visible to formulas, allocation uses the clamped count |
| Shape colors | Draw converts to integer bytes with `& 0xff` | PSP normalized limits differ; review safe conversion of large/negative floats |
| Shape coordinates/radius/angles | Float import; geometry evaluated when drawing | PSP ranges remain narrower; widen only with geometry and allocation guards |
| Custom waves | Integer flags/counts and float gains/colors imported | Audit sample limiting, smoothing and per-point semantics before widening |
| Zoom/rotation/warp/stretch/centers | Float import, not editor bounds | PSP still imposes conservative bounds; singular transforms and GE overflow need explicit handling |
| Decay/gamma | Float import; renderer determines result | PSP range and integer-conversion safety need joint review |
| Echo | Float zoom/alpha; integer orientation | PSP rejects zoom below 1; Desktop editor even permits .01–100 |
| Borders | Float import; draw behavior separate | PSP normalized colors and sizes remain restrictive |
| `fShader` | Legacy hue effect, distinct from HLSL source | Nonzero still unsupported; must not silently treat it as HLSL |
| HLSL / blur metadata | Desktop shader pipeline | Shader source and associated blur metadata intentionally ignored |
| Formula variables | Registered per context, then evaluated | Separate audit needed; file field names are not a complete formula-variable inventory |

Useful examples of **editor** limits that must not be mistaken for import limits:
gamma 1–8; centers −1–2; log controls commonly .01–100; wave alpha .001–100.
Changing only the importer can expose invalid divisions, unsafe float-to-int
casts, excessive work or GU-list overflow in our renderer.

## Explosion is not a one-line fix

`Geiss - Explosion nz+.milk`, line 162, sets `shapecode_1_num_inst=311`.
The current evaluator stores all instances in a fixed-size `MdDecor` array.
Raising the limit also expands copies/stack usage and worst-case GU allocations,
not just parser acceptance. With current maximum geometry the 1 MiB GU list is
already close to its reserved command headroom.

Next architectural step: evaluate/draw instances in bounded batches, preserving
instance order, shared per-shape variables and rollback/error semantics. Budget
checks must account for sides, outlines and wave vertices. Do not silently cap
311 to eight or disable that shape. Audio scheduling and A/V timing must remain
unchanged. This work is still pending; the preset is not fixed by this batch.
