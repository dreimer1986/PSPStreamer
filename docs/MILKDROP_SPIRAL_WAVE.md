# Built-in waveform mode 1

`nWaveMode=1` implements the stereo spiral from MilkDrop 2 `DrawWave`, case 1.
Right-channel amplitude controls radius, left-channel amplitude delayed by
32 samples controls angle, and time adds rotation. `wave_mystery` changes
radius. The 240-point path is open, unlike the closed mode-0 circle.
Opacity receives the original 1.25 multiplier before optional volume modulation
and clamping. Existing dot/thick/additive/color/position options are retained.

This reuses mode 4's coherent stereo snapshot. No additional producer work,
texture, render pass or audio timing change is introduced. Geometry uses one
272-float temporary array and 240 vertices, below the existing 241-vertex
circle maximum. The full layer stress test remains at 55,952 vertex bytes.
Our existing PSP normalization, smoothing and aspect convention remain in
effect; this is not a claim of pixel-identical desktop rendering.

Back up `presets/active.milk`, copy `spiral-wave-demo.milk` to that name and
restart music. Select the custom preset and check LCD/TV, receiver/fullscreen,
pause and track/video transitions. Stereo music should produce an evolving
rotating curve; mono input will look different but is valid.

Host tests verify the original equations against independent expected values,
smoothing endpoints, stereo channel consumption, path length and guard vertex,
and the real parser/renderer path in all four layouts. Hardware performance
still needs validation. Supported built-in modes are now 0, 1 and 4.
