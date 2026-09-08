# Static .milk subset

The hardware-validated warp prototype now has **one custom file slot**,
not a preset browser or a complete MilkDrop interpreter.

Copy the supplied `psp-client/presets/active.milk` into
`PSP/GAME/PSPStreamer/presets/active.milk`. The path is relative to the
application's working directory. Start music and press Square four times:
three built-in effects, then the file. The fifth press disables visualization.

The file is read once **before** music workers start. To apply edits, stop
and restart music. Missing/invalid files do not prevent music or built-in
effects from working. The fourth slot shows a localized error with line and
field name; it does not silently substitute another preset.

## Supported fields

Start with exactly one `[preset00]` section. Names are case-sensitive.
At least one supported field is required; omitted fields use these defaults:

| Field | Meaning | Range | Default |
| --- | --- | --- | --- |
| `zoom` | Feedback zoom | 0.8–1.2 | 1.025 |
| `rot` | Rotation in radians per rendered step | −0.2–0.2 | 0.015 |
| `warp` | Warp amount | −4–4 | 0.8 |
| `fWarpAnimSpeed` | Warp animation time multiplier | 0–4 | 1 |
| `fWarpScale` | Warp spatial scale | 0.1–8 | 1 |
| `fDecay` | Retained brightness per rendered step | 0.8–1 | 0.97 |
| `wave_r` | Native PSP ring red | 0–1 | 1 |
| `wave_g` | Native PSP ring green | 0–1 | 0.6 |
| `wave_b` | Native PSP ring blue | 0–1 | 0.2 |

Wave color names follow MilkDrop's static fields but color our native ring,
not its waveform renderer. Zoom/decay/rotation remain per-frame feedback
operations: their apparent speed depends on visual frame rate, never on an
adjustment to audio timing.

Decimal/scientific numbers, blank lines, CRLF, leading/trailing whitespace,
an optional UTF-8 BOM and full-line comments starting with `;`, `#` or
`//` are accepted. Inline comments after values are not supported.
Limits: 16 KiB per file and 255 bytes before each newline.

## Deliberate rejection

Unknown/repeated fields or sections, malformed values, NaN/infinity,
out-of-range values, NUL bytes and oversized input reject the whole file
without changing the previously loaded values.

Most ordinary MilkDrop presets contain additional fields and are therefore
rejected. This includes EEL `per_frame_*` / `per_pixel_*`, shader code,
`nWaveMode`, custom waves/shapes and currently fixed transform properties
such as `fZoomExponent`. Removing fields does not guarantee that a preset
retains its original appearance. Expression support is a later stage.

The [expression source audit](MILKDROP_EXPRESSIONS.md) records the next
implementation boundary, including original per-frame state and music-variable
semantics. Expressions are not enabled by the aperture calibration update.

The bundled example is newly authored for PSPStreamer; no third-party
presets are bundled. See [warp attribution](MILKDROP_PROTOTYPE.md#attribution).

## Test

The host suite checks the example, all range boundaries, defaults, BOM/CRLF,
missing files, atomic failures, malformed numbers, duplicate/unsupported
keys, size limits and 300 deterministic malformed byte strings.

On PSP, check the cyan example in slot four. Set `wave_r/g/b` to `1/0/0`,
restart music, and check the red ring. Append `per_frame_1=zoom=1;` and
confirm an unsupported-field message without interrupting music.
Restore the example afterward. The three original effects remain available.
