# Higher-resolution visualization and offscreen presentation

## Current: 512×512 feedback on LCD and TV

Selectable in Settings → MilkDrop resolution, persisted as
`milkdrop_high_resolution=1` (default). Set `0` for the original 512×256 path,
including RGBA8888 feedback on LCD. The main-menu setting takes effect on the
next visualization start, without restarting the app. The renderer also detects
quality changes at frame boundaries and rebuilds buffers after GPU completion;
the host harness covers switching quality on the same output in one session.

Both outputs now use 512×512 RGB565 feedback. Scanout, GUI, video playback,
receiver apertures and fullscreen dimensions remain unchanged. Logical geometry
remains 256-square and is expanded on both axes, including texture coordinates,
half-texel offsets, echo, external textures and the center-darkening effect.
The warp mesh remains 16×16: resolution increases pixels, not formula work.

| Output | Scanout bytes | Feedback surfaces in EDRAM | Effect scratch | End |
| --- | --- | --- | --- | --- |
| LCD | 557,056 | 2 × 524,288 | 65,536 | 1,671,168 |
| TV | 1,474,560 | 1 × 524,288 | 65,536 | 2,064,384 |

LCD keeps the three-stage ping-pong sequence described below, but uses RGB565
instead of RGBA8888. Look for banding or different fade tails in dark gradients.
The audio/video pipeline and 32-bit scanout are not changed.

TV cannot fit two high-resolution surfaces after scanout. It therefore allocates
one aligned 512 KiB main-RAM raw-feedback buffer. The frame sequence is:

1. Sample last frame's RAM feedback; render new raw feedback into EDRAM.
2. Synchronize and copy raw feedback to RAM using `sceGuCopyImage`.
3. Sample the new RAM feedback; compose gamma/echo into the same EDRAM surface.
4. Apply image effects using a separate 64×512 EDRAM scratch tile, then present.

Texture synchronization separates the draw, transfer and sample operations.
No render pass samples its own destination. The CPU does not copy frames.
The RAM buffer is initialized with cache writeback, then GPU-owned until it is
freed after completion. Preset snapshots capture the composed EDRAM image.
Allocation failure retains the proven TV 512×256 two-surface layout. Switching
outputs retries the allocation; leaving visualization releases it.

This doubles feedback pixels, adds 512 KiB/frame of GPU transfer on TV, and can
increase GPU time. The existing adaptive throttle still prioritizes audio;
neither a constant visualization framerate nor crackle-free hardware operation
can be established by host tests alone.

The focused GU harness checks high-resolution geometry, texture ownership,
LCD/TV changes, effects, fades, EDRAM boundaries and allocation-failure fallback.
On PSP, test LCD and TV, windowed/fullscreen, dark trails, image-effects-demo,
external textures and Hyperdrive. Then switch tracks, presets and back to video.
No server/add-on update is required.

## Historical: first 512×256 trial

The first 512×256 hardware trial doubles horizontal feedback resolution.
Vertical resolution and the 8×8 warp mesh remain unchanged. Geometry helpers
retain logical 256-square coordinates; the GU adapter expands positions and
texture coordinates horizontally, preserving the warp's half-texel offset.
Receiver apertures, fullscreen dimensions and controls are unchanged.

## EDRAM layout

| Output | Scanout | Texture format | Two textures | First / second offset | End |
| --- | --- | --- | --- | --- | --- |
| LCD | 512×272×4 = 544 KiB | RGBA8888 | 1,024 KiB | 557,056 / 1,081,344 | 1,605,632 |
| TV | 768×480×4 = 1,440 KiB | RGB565 | 512 KiB | 1,474,560 / 1,736,704 | 1,998,848 |

Actual display widths remain 480/720; 512/768 are scanout strides.
Both layouts fit the checked 2 MiB EDRAM limit. Textures are cleared on first
render and output-layout changes, not on every frame. The TV GUI and scanout
stay 32-bit: only visualization surfaces use reduced color precision.

RGB565 has no stored alpha. These passes use vertex alpha for wave/shape
blending, not feedback texture alpha. The tradeoff is fewer color shades and
potential banding or changed fade tails from repeated feedback quantization.
Watch for this during the TV hardware test. No temporal dithering is added.

## Three-stage render sequence

1. Read old raw feedback A; write new raw feedback B, including shapes/waves.
2. Once A is consumed, read B and compose gamma/echo into A offscreen.
3. Read completed A and stretch it to scanout in one opaque pass.

B, not the gamma/echo result, becomes next frame's raw feedback. No pass
samples its own render target, and the existing texture synchronization and
flush commands separate render-to-texture stages. No third texture, CPU image
copy, extra audio work, display-mode change or framebuffer swap is introduced.

Previously the display could see a dark base followed by successive additive
echo/gamma passes. These incomplete stages are now hidden. This is **not full
display double buffering**: a single final blit into live scanout can still
tear. Hardware testing must establish whether any visible flicker remains.
The dotted demo also deliberately changes echo orientation every five seconds.

## Verification and hardware test

The GU host harness checks both memory layouts, 512×256 render targets, the
raw/composite source/destination sequence, one final scanout pass even at
gamma 4 plus echo, EDRAM bounds, list allocation and lifecycle failures.
Maximum tested vertex storage is 46,480 of 65,536 bytes. Host tests do not
measure PSP fill rate, emulate RGB565 quantization or reproduce display tearing.
The adaptive no-catch-up render throttle and audio scheduling are unchanged.

Test both demo presets and Hyperdrive on LCD and TV, receiver and fullscreen.
Compare flicker, fine horizontal details, dark gradients and fade tails. Check
that music stays crackle-free, including pause, track replacement and switching
from music back to video. No server/add-on update is required.
