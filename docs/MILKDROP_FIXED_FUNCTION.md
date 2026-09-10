# Extended fixed-function subset

This batch extends the hardware-validated Hyperdrive path without changing
audio decoding/output, timestamps, TV switching, receiver apertures or texture
ownership. It is a larger test batch, not a complete MilkDrop 2 implementation.

## Try it

Copy either `presets/receiver-fx-demo.milk` or `presets/echo-dots-demo.milk`
as `presets/active.milk`, restart music and press Square four times. Both are
new PSPStreamer examples. The first combines shapes, echo, borders, an animated
transform center and a moving additive wave. The second uses thick dots,
volume-modulated opacity, zoom exponent and rotating echo orientations.

Check receiver/fullscreen on LCD and TV, pause/resume, track changes, disabling
the visualization and switching to video. Recheck your original Hyperdrive
file afterward. Existing controls and default-off behavior are unchanged.

## Transforms

| Static field | Formula variable | Range |
| --- | --- | --- |
| `cx`, `cy` | same | 0–1 |
| `sx`, `sy` | same | 0.25–4, nonzero |
| `fZoomExponent` | `zoomexp` | 0.5–2 |
| `dx`, `dy` | same | −1–1 |

Order: radially varying zoom, stretch around the selected center, warp,
rotation around that center, translation. Defaults preserve the prior
center .5/.5, stretch 1 and exponent 1. The radial calculation uses our square
feedback coordinates; it is not the desktop renderer's aspect-adjusted mesh.

## Wave, echo and borders

| Static field | Formula variable | Meaning / range |
| --- | --- | --- |
| `wave_x`, `wave_y` | same | Position 0–1; waveform y follows original bottom-to-top convention |
| `fWaveParam` | `wave_mystery` | Circular radius offset, −1–1 |
| `fWaveAlpha` | `wave_a` | Opacity 0–1 |
| `bWaveDots` | `wave_dots` | 0/1, points instead of lines |
| `bWaveThick` | `wave_thick` | 0/1, four one-texel-offset copies |
| `bAdditiveWaves` | `wave_additive` | 0/1, additive rather than alpha-over |
| `bMaximizeWaveColor` | `wave_brighten` | 0/1, normalize brightest RGB component to 1 |
| `bModWaveAlphaByVolume` | `wave_mod_alpha` | 0/1, modulate with mean relative bass/mid/treble |
| `fModWaveAlphaStart/End` | `wave_mod_start/end` | 0–4; end must exceed start when enabled |
| `fVideoEchoZoom` | `echo_zoom` | 1–100 |
| `fVideoEchoAlpha` | `echo_alpha` | 0–1, mix weight |
| `nVideoEchoOrientation` | `echo_orient` | Integer 0–3: none, horizontal, vertical, both flips |
| `fGammaAdj` | `gamma` | 1–4, additive output brightness |
| `ob_size`, `ib_size` | same | Outer/inner border width, 0–0.5 |
| `ob_r/g/b`, `ib_r/g/b` | same | Border RGB 0–1 |
| `ob_alpha`, `ib_alpha` | `ob_a`, `ib_a` | Border opacity 0–1 |

Borders are drawn into feedback after waves; the inner border is inset by the
outer border width. Echo combines two views of the completed feedback texture
offscreen, one ordinary and one zoomed/flipped. It does not create a third
texture or compound the gamma in feedback. Wave thickness is measured in
feedback texels, so its apparent output width depends on viewport size.

The new fields are reset to static values before each per-frame program.
After validation, all evaluated values are committed together. Invalid values
disable the custom effect with the existing error notice, not audio playback.
Wave styling applies to the real mode-0 waveform; it does not change our
original spectrum ring when `nWaveMode` is absent.

## Four static custom shapes

Use `shapecode_0_FIELD` through `shapecode_3_FIELD`:

- `enabled`, `additive`, `textured`: integer 0/1.
- `sides`: integer 3–32 (default 4).
- `x`, `y`, `rad`: 0–1 (defaults .5, .5, .1). Shape y follows the original
  top-to-bottom convention, unlike the built-in wave's y.
- `ang`, `tex_ang`: −100–100 radians (default 0).
- `tex_zoom`: 0.1–10 (default 1).
- `r/g/b/a`: center color; `r2/g2/b2/a2`: edge color, all 0–1.
- `border_r/g/b/a`: outline color, all 0–1.
- `thickOutline=0` is accepted; nonzero remains unsupported.

Shapes draw before the waveform. Textured shapes sample the previous feedback
texture, never the texture currently being rendered. Color interpolates from
center to edge. Outlines close the polygon. Radius is aspect-corrected and the
active viewport scissor clips geometry at its edges.

These are static shape definitions, not independent shape formula contexts.
Unknown shape fields/programs, external texture names and unsupported enabled
features still fail explicitly rather than being silently ignored.

## Additional math

Unary: `floor`, `ceil`, `atan`, `exp`, `log` (natural), `log10`, `sqr`, `sign`.
Binary: `pow`, `atan2(y,x)`, `above`, `below`, `equal`.
These supplement `sin/cos/abs/sqrt/min/max` and arithmetic.
Comparisons produce 0 or 1; `equal` uses absolute difference below 0.00001.
`sign` returns −1/0/1. Invalid domains, division by zero and nonfinite results
remain errors. This single-precision implementation does not claim full EEL
compatibility. [Conditional formulas](MILKDROP_CONDITIONS.md) now add lazy
`if`, `band/bor/bnot`, `tan/asin/acos` and `sigmoid`. Arbitrary assignment
targets, looping and persistent state remain unsupported.

## Resource and test boundary

- Existing 64 KiB GU list and two 512×256 textures. LCD uses 32-bit color;
  TV uses RGB565. See [memory layout and presentation](MILKDROP_PRESENTATION.md).
- Worst tested combination: four 32-sided textured/outlined shapes, thick
  dotted wave, both borders, echo, gamma 4 and TV fullscreen. Vertex storage
  peaks at 46,480 bytes, leaving 19,056 bytes for GU commands/alignment.
  Host mocks verify data bounds, blending calls and texture ownership; they
  do not emulate PSP rasterization or prove hardware frame time.
- Existing adaptive frame throttling remains in place; no catch-up rendering.
- The decoder/DAC and PCM snapshot implementation are unchanged in this batch.
- Formula budget remains 128 instructions, 16 source lines and 24 stack slots.
- Two demos are exercised for 20 minutes each with generated music inputs.
  The exact user-supplied Hyperdrive file remains an optional regression test.

Reference: MilkDrop 1.04b `DrawCustomShapes`, `DrawWave`, `DrawSprites`,
`WarpedBlitFromVS0ToVS1` and final echo/gamma passes; cross-checked with MilkDrop
2's fixed-function path. New geometry/layer helpers are PSP implementations;
the existing adapted warp file retains its original attribution.

## Next architectural boundary

Independent init/per-frame/per-point programs need scoped persistent state,
reset rules and per-context instruction budgets. Per-vertex equations need
their own context and mesh-wide cost limit. More waveform modes need stereo
or spectrum snapshots and revised worst-case geometry budgets. Shader presets,
external textures and preset crossfades need a larger rendering/resource plan.
Those are not silently approximated by this batch. First validate the combined
fixed-function features on hardware, then choose the next compatibility step.
