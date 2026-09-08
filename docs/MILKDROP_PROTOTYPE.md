# MilkDrop warp prototype (opt-in hardware test)

This is **not a complete MilkDrop player** and cannot load arbitrary
`.milk` presets. It ports the shader-free feedback UV equations from
[MilkDrop 2](https://github.com/eef2697d62fbe08e2fd927278/milkdrop2/blob/b5e4136c2f050eafa10aa199bb72c8e5c12c9320/vis_milk2/milkdropfs.cpp),
`WarpedBlit_NoShaders`, to a small PSP graphics adapter.

## Controls

Start music normally. **Square** cycles:

1. Gentle zoom/rotation feedback.
2. Stronger warp with opposite rotation.
3. Faster outward zoom.
4. Custom static file `presets/active.milk` (see [supported subset](MILKDROP_PRESETS.md)).
5. Original spectrum/receiver, visualization off.

**Cross + Triangle** still switches between the normal and enlarged music
layout. Analogue meters and the volume dial remain visible. Pause, volume,
Stop and web controls remain available. Each new track starts with the
original receiver; the prototype choice is intentionally not saved.

No server/add-on change or configuration key is required. Copy the supplied
`presets/active.milk` example to enable the custom slot.
Use the same binary for LCD and native TV GUI (`tv_ui=auto`).
The historical LCD-menu/TV-video mode still visualizes music on the LCD.

## What is original and what is new

- Adapted original MilkDrop zoom/rotation/warp equations and multiplicative
  feedback decay, with an 8×8 grid and two 256×256 feedback textures.
- Three new fixed parameter configurations. These are not imported presets.
- New PSP-native, spectrum-driven colored ring, using snapshots of the
  existing twelve frequency bands and VU peaks. It is **not** MilkDrop's
  waveform code, a fresh FFT, or a copy of the PCM buffer.
- PSP GU renders the feedback and stretches the result into the receiver
  viewport. Native GUI artwork and controls are not rescaled.

The selected subset fixes zoom exponent to 1, centre to 0.5/0.5, stretch to 1,
and translation to zero. EEL expressions, per-frame/per-pixel user programs,
custom waves/shapes, shader presets and preset blending are not implemented.

The studied source contains Direct3D 9 dependencies even in the no-shader
path, and its bundled NS-EEL has x86/PPC assembly rather than a PSP backend.
Consequently arbitrary preset support needs a separate interpreter/backend
decision; this prototype does not silently ignore unsupported preset syntax.
The original 2.25c ZIP download returned HTTP 406 during this study. The
audited mirror revision is `b5e4136c2f050eafa10aa199bb72c8e5c12c9320`.

## Resource ownership

- No decoder, DAC, PCM queue, audio timestamp or video scheduler changes.
- GU is initialized only on first activation and terminated on disabling,
  Stop, seek, track replacement or EOF, before the next media starts.
- 64 KiB RAM command list, allocated only while enabled; small stack mesh.
- 512 KiB EDRAM textures at offsets 1,474,560 and 1,736,704. Their end,
  1,998,848, is below the real 2 MiB limit and above native TV scanout.
  The adapter checks available EDRAM and allocation/init/start failures.
- No DVE switch, display-buffer swap or display stride change. Rendering
  targets change inside the GU list only; final writes use the current
  LCD stride 512 or TV stride 768.
- No catch-up renders: minimum 50 ms after completion. If a frame takes more
  than about 16.7 ms, wait three times its cost before another frame. The
  renderer stays at the existing low music-GUI priority.

The target-buffer approach was checked against the official PSPSDK
[render-target example](https://github.com/pspdev/pspsdk/blob/master/src/samples/gu/rendertarget/rendertarget.c)
and GU initialization/buffer-selection implementations. This is not a claim
that physical PSP/OSSC rendering has already been validated.

## First hardware test

1. Start a song on LCD; verify ordinary playback, then press Square once.
2. Check the animated feedback/ring, responsiveness and especially crackling.
3. Try all three variants, the enlarged layout, pause/resume and volume.
4. Return to the original spectrum with the fifth Square press.
5. Enable again, then remotely start another song and a video.
6. Repeat with the native TV GUI, restarting with the component cable attached.

Report whether the first Square press produces graphics, whether audio
stays clean, and whether music-to-video transitions remain clean. A hardware
failure in GU submission cannot be ruled out by host tests; the previous
stable release is retained separately for rollback.

Host tests cover identity UV mapping, 1,920 adapter frames in LCD/TV and both
layouts, finite/bounded vertices, distinct source/destination textures, memory
limits, init/start failures and repeated shutdown. They mock GU and do not
emulate rasterization, GPU timing or actual firmware integration.

## Attribution

MilkDrop-derived equations: Copyright 2005–2013 Nullsoft, Inc., BSD-3-Clause.
The complete original notice is retained in `milkdrop_warp.c` and
[the license file](../licenses/MilkDrop2.txt). The PSP adapter and new native
ring are PSPStreamer additions. No third-party presets are distributed.
