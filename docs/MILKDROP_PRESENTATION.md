# Higher-resolution visualization and offscreen presentation

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
