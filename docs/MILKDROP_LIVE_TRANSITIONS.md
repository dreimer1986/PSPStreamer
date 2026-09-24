# Live shaderless transitions

Implemented 2026-09-24. Automatic soft changes retain both owned presets and
independent frame/pixel/shape/wave runtimes. The old clock and signal history
continue; the incoming state starts fresh. EEL global memory/registers remain
shared, as they already were between presets; local memories are independent.

Reference: locally available MilkDrop 1.04b `milkdropfs.cpp`,
`CPlugin::RunPerFrameEquations()` around lines 524–600: evaluate old and new state,
blend motion-producing UV grids afterward, interpolate non-motion parameters
with cosine easing. This avoids allocating two independent PSP feedback chains.

The PSP implementation blends warp UV/decay, retains both live wave/shape layers,
and interpolates borders, motion vectors, echo, gamma and legacy color shading.
Discrete effects/wrap/orientation switch at the midpoint. Primary waves are
crossfaded, not desktop same-mode vertex-morphed. Spatial transition masks and
shader-based transitions are not implemented. This is not a claim of complete
desktop transition parity.

Memory ownership is transferred only after successful incoming parsing. Failed
loads leave the current transition intact. Missing extra-state memory uses the
existing snapshot path; malformed formulas/textures still report errors.
An incoming third preset releases the oldest one; never retain three live states.
The blend clock starts on its first rendered frame, not while parsing files.
Stop, hard cut and layout changes release old bytecode, textures and state.

State: 959,360 bytes on the host fixture (PSP has smaller pointers), plus retained
bytecode/assets. No extra framebuffer. Additional GU-list restarts keep each live
geometry layer inside the original command-list budget. The same coherent
PCM/FFT snapshot feeds both presets; music decoding and clocks are unchanged.
The cost-based visual scheduler includes both states and still rests after work.

Targeted harness: both states advance independently, cosine weights, LCD/TV,
512x256/512x512, window/fullscreen, replacement mid-fade, failed incoming load,
forced extra-state allocation failure, hard cuts, disabled live mode and stop.
Run `python3 -m unittest tests.test_live_transitions tests.test_visual_options`.
Actual audio stability and visual quality are hardware tests, not host claims.

User controls and the two dedicated hardware-test presets are documented in README.
