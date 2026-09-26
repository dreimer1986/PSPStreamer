# Optional desktop music analysis

## Controls

In the library, press SELECT to open Settings. **Music analysis** selects
**Light (12)** (default) or **Desktop FFT**. **FFT bars LCD** and **FFT bars TV** independently select 12, 24, 32,
48 or 64 bars (32 by default). Use Left/Right, then START to save as usual.

Each output keeps its own count; changing the output selects its saved value.
The analysis mode is shared. Switching outputs does not select a different
MilkDrop analysis algorithm or change the three frequency ranges.

During music, Circle in Spectrum opens these three options directly; Left/Right
changes them and Circle returns/saves. MilkDrop exposes them at the end of its
existing visualization-options menu (inside the preset browser). Changes apply
without restarting the app. Windowed LCD/TV and fullscreen support every count.
Light mode always displays twelve bars; the saved FFT count is retained but
inactive. Monkey keeps its existing settings and beat detection.

Equivalent `PSPStreamer.cfg` entries:

```ini
spectrum_analysis=0
spectrum_bands_lcd=32
spectrum_bands_tv=32
```

Set `spectrum_analysis=1` to enable FFT analysis. Unsupported band counts are
ignored. The band count changes only display grouping: never MilkDrop's music
variables, preset geometry budgets or playback clocks. No server change needed.

## Source behavior and scope

The optional path follows MilkDrop 2's **DoCustomSoundAnalysis**, not the
different three-band calculation in CPluginShell::AnalyzeNewSound:

- Signed 8-bit-equivalent left-channel waveform, 576 samples, at the player's
  44.1 kHz output rate; 576-sample envelope and zero padding to a 1024-point FFT.
- 512 magnitudes with the reference logarithmic equalizer. The reference calls
  `myfft.Init(576, MY_FFT_SAMPLES, -1)`; -1 still enables its boolean equalizer.
- Sum bins `[0,85)`, `[85,170)`, `[170,256)` for the three preset music values.
  These are the source's actual ranges, not substituted conventional EQ bands.
- Attack/release 0.2/0.5 and history 0.9 for the first 50 analysis frames, then
  0.992, adjusted from the reference 30 Hz rates using elapsed time.
- The six standard `bass/mid/treb` and `_att` variables use this result. Both
  live-transition presets and hard-cut detection share it. Native extension
  variables keep their previous definitions.

Reference: [MilkDrop 2 source](https://github.com/eef2697d62fbe08e2fd927278/milkdrop2),
revision `b5e4136c2f050eafa10aa199bb72c8e5c12c9320`, `vis_milk2/plugin.cpp`
and `fft.cpp`; original attribution in NOTICE and licenses/MilkDrop2.txt.

The existing custom-wave PCM/spectrum path remains unchanged. Its differently
normalized FFT cannot simply be substituted for this reference transform. This
option improves the standard music variables; it is not a claim of complete
desktop waveform/shader parity. Sampling cadence and presentation latency still
depend on PSP decoding/rendering rather than Winamp's host callback.

## Resource boundaries

The producer only copies an additional 576 left-channel samples when needed.
FFT/history/display grouping run in the music/UI consumer at most 20 times per
second; the transform is reused until a new PCM snapshot arrives. Fixed scratch
uses about 17 KiB, with no per-frame allocation or blocking producer waits.
The new analysis is off for Monkey/video and in Light mode. Presets explicitly
requesting custom spectrum waves still use their existing FFT in either mode.
Bar rendering retains changed-strip updates, without new framebuffers.

## Verification and hardware test

The PSP build was made before targeted tests. Host checks compare all six music
variables against the original MilkDrop FFT plus its temporal equations, also
check an independent DFT, disabled capture, resets and pause behavior. LCD/TV
incremental renders match the full renderer for every count in both layouts;
fullscreen guards and theme bounds pass. Settings and help checks pass.

On PSP: compare Light with FFT at 32 and 64 bars; test windowed/fullscreen LCD
and TV, pause/resume, track changes, MilkDrop live transitions and return to video.
Confirm music remains free of crackling and that returning to Light restores
the previous performance. Host results do not establish PSP CPU cost.
