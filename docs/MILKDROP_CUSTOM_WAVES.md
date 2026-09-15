# Custom PCM waves

Four optional custom waves have independent init, frame and point programs.
They use the original keys `wave_0_init1`, `wave_0_per_frame1` and
`wave_0_per_point1` (slots 0–3, consecutive lines starting at 1).

Static `wavecode_0_*` fields: `enabled`, `samples` (2–64), `sep` (0–128),
`bSpectrum` (currently only 0), `bUseDots`, `bDrawThick`, `bAdditive`,
`scaling` (0–4), `smoothing` (0–1), and `r/g/b/a` (0–1). Integer fields must be
integers. Disabled waves do not execute. Unsupported spectrum requests fail
explicitly rather than being interpreted as PCM.

Frame formulas set `samples`, `r/g/b/a` and local variables. Point formulas set
`x/y` and `r/g/b/a`, with read-only `sample` (0–1), `value1` (left PCM), `value2`
(right PCM) and `samples`. Positions and colors must remain within 0–1.
Time/frame/fps and the existing relative and PSP-specific music inputs remain
read-only. Arithmetic or range errors reject geometry and runtime state before
a GU list is opened; no partially evaluated wave is submitted.

Init runs once per activation, starting with preset-init q seeds. Frame code
receives the current preset frame's q values and its own init-seeded t1–t8.
Its q/t results seed point code. Point q/t changes persist along the wave, but
do not feed back into the frame context or another wave. Named variables use
separate frame and point namespaces and persist in their own contexts.
Point positions/colors reset for each sample. Preset changes clear all states.

This lifetime and two-pass audio smoothing follow MilkDrop 2's
`LoadCustomWavePerFrameEvallibVars`/`DrawCustomWaves`, inspected at revision
`b5e4136c2f050eafa10aa199bb72c8e5c12c9320`. The PSP adapter uses normalized
signed 16-bit PCM and strict output bounds, not desktop amplitude calibration
or wrapping. It selects a centered PCM window with channel separation, smooths
forward and backward, and draws a line strip or points. Desktop spline
subdivision, custom spectrum waves and 512-point waves are not implemented.

## Resource limits

Four waves, at most 64 points each; init/frame programs each have at most 128
instructions, point programs at most 64. Each program allows 16 lines. Each
frame or point namespace has at most 16 named variables. The whole preset is
still limited to 16 KiB. Thus point evaluation is bounded to 16,384 bytecode
instructions per rendered frame across all waves. No evaluation happens in
the audio worker and no heap allocation occurs during wave evaluation.

Four compiled wave objects add 29,008 bytes to preset storage and its temporary
parser copy; persistent runtime state adds 656 bytes and cached geometry uses
6,160 bytes. The parser runs on the main/UI thread, not the small audio stacks.
Combined FFT/PCM capture uses the existing shared arrays and one sequence-checked
snapshot. It adds copying only when custom PCM waves coexist with built-in mode
8; it does not run an FFT or formulas in the producer. Other capture paths remain
unchanged. Silence clears cached audio samples.

The existing 128 KiB GU list and feedback/display allocations remain unchanged.
The maximum layer test, including all four thick waves, uses 112,656 bytes of
vertex storage, leaving 18,416 bytes for command words and alignment. Host tests
do not guarantee timing on hardware; the renderer retains its adaptive throttle.
There are no changes to DAC timing, video sync, network timeouts or the server.

## Hardware test

Copy the new EBOOT.PBP, PSPStreamer.prx and both presets:

- `custom-wave-demo.milk`: orange left-channel line and blue right-channel dots,
  with per-point color and a slowly moving frame-controlled offset.
- `custom-wave-fft-demo.milk`: green stereo-driven loop over the built-in FFT
  waveform, testing simultaneous typed PCM and FFT capture.

During music open the Circle browser and select them. Check LCD/TV, embedded
and fullscreen, sustained playback, pauses, track and preset changes, and return
to video. Listen especially for crackling. No server update is necessary.

Remaining work includes custom spectrum waves, larger/spline-smoothed waves,
shape instances, full NS-EEL semantics and additional engine inputs, image
adjustment passes, automatic preset changes and blending. Shaders and external
textures remain deferred. This is not arbitrary MilkDrop 2 preset compatibility.
