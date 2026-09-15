# Built-in waveform mode 4

`nWaveMode=4` enables MilkDrop's horizontal "script" waveform. Mode 0 remains
the existing circular waveform. Mode 1 is now supported too (see
[stereo spiral](MILKDROP_SPIRAL_WAVE.md)); other numbered modes still fail explicitly.

The implementation follows MilkDrop 2 `DrawWave` case 4 in the inspected
`milkdropfs.cpp` reference: 170 points (512 feedback texels / 3), a centered
sample window, left-channel vertical displacement, right-channel horizontal
displacement delayed by 25 samples, and two-point momentum controlled by
`fWaveParam` / `wave_mystery`. The line stays open. Geometry is calculated
before the established logical-to-feedback coordinate expansion.
Sideways motion may cross the feedback edge; the existing viewport scissor
clips the line instead of flattening its shape by clamping each vertex.

The existing PSP normalization and smoothing are retained; this is not a
claim of identical desktop sound preprocessing or final rasterization. Wave
position, color, volume-modulated opacity, dots, thick lines, additive blending
and feedback apply through the existing renderer. Per-frame waveform-mode
switching is not yet implemented.

## Cost and ownership

Modes 1 and 4 request a stereo snapshot. Mode 0 continues to publish the right
channel only; disabled wave capture still returns immediately. A single
sequence counter guards both channels, so the renderer accepts or rejects
the pair together. There are no new audio-thread allocations, waits or locks.
Stereo adds 1152 bytes to the shared snapshot and 1152 bytes to the renderer's
retained samples. The consumer's temporary snapshot uses another 1152 stack
bytes; the new geometry routine uses two 576-float smoothing arrays.

No new textures, render passes, display modes or decoder scheduling changes.
The maximum combined layer test remains 55,952 vertex bytes: mode 4 has fewer
vertices than mode 0, including when thick lines are enabled.

## Test

Back up `presets/active.milk`, copy `script-wave-demo.milk` as `active.milk`,
restart music and select the custom effect. Use stereo music: the horizontal
line should move vertically and bend sideways with the audio. Check LCD/TV,
receiver/fullscreen, pause, next song and switching to video. In particular,
check that the extra stereo copy does not reintroduce audible crackling.

Host tests check channel order, coherent snapshots, short-buffer zero padding,
the mode-4 equations against independently calculated expected coordinates,
parser rejection of unsupported modes, and the real GU adapter on all layouts.
The unchanged mode-0 tests remain in the suite. Hardware timing needs a PSP.
