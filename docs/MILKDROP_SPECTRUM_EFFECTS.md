# Stereo spectrum, smooth waves and image effects

This batch adds three features together. It is not full MilkDrop 2 support
minus shaders/textures; the remaining limitations are listed below.

## Stereo spectrum custom waves

`wavecode_N_bSpectrum=1` gives point formulas magnitude data from a 1024-sample
Hann-windowed FFT on each channel. `value1` is left and `value2` is right.
Both transforms run only in the UI/render thread, once per fresh coherent
snapshot, shared by all custom spectrum waves. The producer only copies PCM.
The same snapshot can feed PCM custom waves and the existing built-in waveform.
One extra 1024-sample shared right buffer, its consumer copy and two 512-float
bin caches add 8 KiB. No decoder, audio-clock or networking changes are involved.

Each wave samples the first `512-sep` bins at equal intervals for its requested
point count, matching the reference's frequency-range selection. `scaling`,
global static wave scale and forward/backward smoothing still apply. Magnitudes
use our normalized FFT, not the desktop sound-analysis calibration. There are
still at most 64 formula points per wave and four waves.

## Smooth custom lines

Line strips insert one interpolated point between each pair of original points,
using MilkDrop's `SmoothWave` coefficients (-0.15, 1.15, 1.15, -0.15)/2.
Endpoints and original sample positions/colors are retained. Inserted colors
follow the preceding sample, as in the reference. Interpolation overshoot is
clamped to the valid feedback rectangle. Thus 64 formula points draw as 127
vertices without evaluating extra formulas. Dots retain their original points.
This applies automatically to existing custom-line presets too.

## Fixed-function image effects

Static switches and corresponding per-frame outputs:

| Static field | Formula output | Operation |
| --- | --- | --- |
| `bDarkenCenter` | `darken_center` | Small soft black center diamond in feedback |
| `bBrighten` | `brighten` | `1-(1-C)^2` |
| `bDarken` | `darken` | `C^2` |
| `bSolarize` | `solarize` | `2*C*(1-C)` |
| `bInvert` | `invert` | `1-C` |

Flags accept exactly 0 or 1; other formula results fail transactionally before
opening a GU list. The four color effects run in table order after echo/gamma
composition, independently per RGB channel. They do not enter the raw feedback
loop. Center darkening instead belongs before feedback borders, as in the
reference, and is intentionally subtle (center alpha 3/32).

Unlike desktop Direct3D, PSP cannot directly multiply a destination color by
itself. We copy one 64x256 tile of the composed image to separate EDRAM and blend
from that copy. Each enabled effect recopies the current tile, preserving the
correct result when flags are combined. No texture samples the active render
target; the raw feedback texture is untouched. Transfers, texture sync and cache
flush commands are queued on the GE. There is no CPU framebuffer filtering.
RGB565 quantization on TV can make dark gradients differ from LCD/desktop.

## Memory and verification

- Feedback remains 512x256: LCD RGBA8888, TV RGB565; scanout is unchanged.
- Effect scratch: 64 KiB on LCD, 32 KiB on TV, after both existing surfaces.
  Its end offsets are 1,671,168 and 2,031,616 respectively, within 2 MiB EDRAM.
- The ordinary-RAM GU list grows from 128 to 192 KiB for subdivided thick lines.
  Worst tested combined geometry is 138,656 bytes, leaving 57,952 for commands
  and alignment. The host harness enforces a 170,000-byte geometry ceiling.
- Four waves still execute no more than 16,384 point bytecode instructions per
  visualization frame. Adaptive render throttling remains enabled.

Tests cover separate-channel FFT peaks, snapshot type changes, waveform
endpoints/bounds, effect flags, combinations of blend equations, scratch bounds,
raw-feedback ownership, and every new preset on LCD/TV and both layouts.
Actual GPU transfers and sustained audio cleanliness require PSP testing.

Reference: MilkDrop 2 revision `b5e4136c2f050eafa10aa199bb72c8e5c12c9320`,
`DrawCustomWaves`, `SmoothWave`, `DrawSprites` and fixed-function `ShowToUser`.
This is an adapted bounded implementation, not bit-identical desktop rendering.

## Test presets

Copy EBOOT.PBP, PSPStreamer.prx and these files into the existing PSP release:

- `custom-spectrum-demo.milk`: orange left-channel and blue right-channel
  spectra, stacked vertically; both lines respond to frequency content.
- `smooth-wave-demo.milk`: a green moving smooth curve with orange dots marking
  the original 16 formula points. The curve should pass through the dots.
- `image-effects-demo.milk`: changes every four seconds through normal,
  brighter, darker, solarized, inverted and subtle center-darkened output.
  The full cycle lasts 24 seconds; color changes at boundaries are intentional.

Use Circle during music to select them. Check LCD/TV and fullscreen, run multiple
cycles, pause/resume, change tracks/presets and return to video. Also recheck the
previous custom-wave presets because their lines now use subdivision. No server
update is needed.

## Still missing besides shaders and external textures

This was the status of the spectrum/effects batch. The later
[automation batch](MILKDROP_AUTOMATION.md) implements 512-point waves, eight
shape instances, progress, timed/rated/playlist selection and snapshot crossfades.
It also documents the remaining bounded-renderer differences from desktop.

- Larger custom-wave sample counts and complete desktop audio analysis/scaling.
- Multiple instances of one custom shape and additional legacy rendering flags.
- Full NS-EEL: unrestricted programs, loops, memory/register facilities and
  missing functions/operators; larger or persistent per-grid contexts.
- Additional engine inputs such as meaningful preset `progress`.
- Automatic timed/rated preset selection, playlists and preset crossfades.

Shader execution and external/user textures remain explicitly deferred. Existing
feedback-textured shapes are already supported and are not external textures.
