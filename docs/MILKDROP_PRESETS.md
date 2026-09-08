# Bounded .milk subset

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
rejected. This includes arbitrary EEL, `per_pixel_*`, shader code,
`nWaveMode`, custom waves/shapes and currently fixed transform properties
such as `fZoomExponent`. Removing fields does not guarantee that a preset
retains its original appearance.

The [expression source audit](MILKDROP_EXPRESSIONS.md) records the implementation
boundary, including original per-frame state and music-variable semantics.

## Time formulas

The optional `time-demo.milk` demonstrates changing rotation, warp and ring
colors. Copy it as `presets/active.milk` on the PSP and restart music; the
existing static example and three built-in effects are unchanged.

```ini
[preset00]
warp=1
per_frame_1=rot=0.025*sin(time*0.6); warp=warp+0.5*cos(time);
per_frame_2=wave_r=0.5+0.4*sin(time);
```

Use consecutive keys `per_frame_1` through at most `per_frame_16`, in order.
Each contains one or more assignments ending in semicolons. Supported syntax:
decimal/scientific constants, parentheses, unary `+/-`, arithmetic `+ - * /`,
and single-argument `sin`, `cos`, `abs`. Trigonometric arguments are radians.
Readable/writable outputs are `zoom`, `rot`, `warp`, `decay`, `wave_r`,
`wave_g`, `wave_b`. Note that formula `decay` corresponds to static `fDecay`.
Warp speed/scale remain static fields. `time` is read-only elapsed seconds
since visualization activation, not the audio position; it continues while
music is paused. Stop/restart resets it. No `fps`, `frame`, music variables,
persistent variables, conditions, loops or per-pixel programs are accepted.

Inputs reset from static values before each rendered frame; assignments in
that frame execute sequentially. The program is compiled once before music
workers start: at most 128 instructions total, 16 nesting levels and a
24-float execution stack. No allocation, disk access or source parsing occurs
in the rendering loop. Execution runs only in the lower-priority UI thread,
once per adaptive visual frame, never in decoder/audio workers.

Division by zero, non-finite results or outputs outside the table's ranges
disable the custom effect and display an invalid-preset message with the
source line. Music continues; Square returns to the normal spectrum and the
built-ins remain available. Stop/restart music to reload a corrected file.
This is a new arithmetic subset, **not an NS-EEL compatibility claim**.

The bundled example is newly authored for PSPStreamer; no third-party
presets are bundled. See [warp attribution](MILKDROP_PROTOTYPE.md#attribution).

## Test

The host suite checks the example, all range boundaries, defaults, BOM/CRLF,
missing files, atomic failures, malformed numbers, duplicate/unsupported
keys, size limits and 300 deterministic malformed byte strings. Formula tests
cover precedence, sequential assignments, static reset, rejected syntax,
instruction/depth/line bounds, atomic runtime errors and 30 minutes of demo
values. The GU harness checks that a failed formula opens no render list.

On PSP, check the cyan example in slot four. Set `wave_r/g/b` to `1/0/0`,
restart music, and check the red ring. Append `per_pixel_1=zoom=1;` and
confirm an unsupported-field message without interrupting music.
Restore the example afterward. The three original effects remain available.
Also test the time demo in receiver/fullscreen views on LCD and TV, then
switch to another track/video. Check that audio remains uninterrupted.
