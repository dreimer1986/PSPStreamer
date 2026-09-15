# Dynamic waveforms and motion vectors

Per-frame formulas now accept `wave_mode` and `mv_a`, `mv_r`, `mv_g`, `mv_b`,
`mv_x`, `mv_y`, `mv_dx`, `mv_dy`, `mv_l`. They start each frame from the preset's
static values, matching the inspected MilkDrop 2 `LoadPerFrameEvallibVars`
behavior (reference revision `b5e4136c2f050eafa10aa199bb72c8e5c12c9320`).
Use persistent user variables or `q` values for accumulated state, not these
reset outputs. As with other outputs, init assignments do not override the
static defaults for later frames. Per-grid formulas may read these evaluated
values but cannot assign them.

`wave_mode` must be an integer from 0 through 8. Motion limits remain alpha/RGB
0–1, grid X 0–16, grid Y 0–12, offsets -1–1 and length 0–10. Invalid or non-finite
results reject the frame without committing its state or outputs. The native
ring remains available for older presets without a built-in waveform selection.
Unlike desktop casting/wrapping, out-of-range modes are explicitly rejected.

PCM snapshots carry their capture format (right-only, stereo or FFT source).
When a formula changes the required format, the renderer clears retained samples
and waits for a matching coherent snapshot. It never interprets the old PCM
layout as the new one. A transition can briefly display a silent waveform;
audio playback and GU/feedback state continue normally. The producer adds only
a format marker, with no allocation, FFT or blocking operations.

## Test

Copy the updated EBOOT.PBP, PSPStreamer.prx and `presets/wave-switch-demo.milk`.
During music press Circle and select that preset. It cycles modes 0–8 every
three seconds (27-second cycle), while animating motion-vector density, color,
position and length. Check several cycles on LCD and TV, both embedded and
fullscreen. Check that music stays clean across the FFT/stereo transitions,
then try track changes, another preset and video playback.

No server update is needed. Audio timing, video synchronization, stream handling,
feedback resolution and display memory layout are unchanged. Host coverage
includes all modes, typed snapshot rejection, formula validation and atomic
failure; actual PSP performance still requires hardware testing.

## Remaining work

Independent custom-shape init/frame programs, custom-wave frame/point programs,
full NS-EEL semantics, persistent per-grid contexts, additional engine inputs,
image adjustment passes and automatic preset transitions/blending remain absent.
Shaders and external textures are still deferred. This is a bounded compatibility
extension, not support for arbitrary MilkDrop 2 presets.
