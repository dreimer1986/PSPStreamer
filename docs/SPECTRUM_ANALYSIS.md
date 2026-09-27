# Optional desktop music analysis

## Controls

In the library, press SELECT to open Settings. **Music analysis** selects
**Light (12)** (default) or **Desktop FFT**. **FFT bars LCD** and **FFT bars TV** independently select 12, 24, 32,
48 or 64 bars (32 by default). Use Left/Right, then START to save as usual.

Each output keeps its own count; changing the output selects its saved value.
The analysis mode is shared. Switching outputs does not select a different
MilkDrop analysis algorithm or change the three frequency ranges.

During music, Circle in Spectrum opens these eight options directly; Left/Right
changes them and Circle returns/saves. MilkDrop exposes them at the end of its
existing visualization-options menu (inside the preset browser). Changes apply
without restarting the app. Windowed LCD/TV and fullscreen support every count.
Light mode always displays twelve bars; the saved FFT count is retained but
inactive. **FFT gain (dB)** changes only analyzer height (-24 to +24, default 0).
The gain is shared by LCD/TV and does not affect volume or either visualization.
Monkey uses its own [reference analysis and beat detector](MONKEY_AUDIO.md)
in Desktop FFT mode; Light mode keeps the previous inexpensive detector.

Equivalent `PSPStreamer.cfg` entries:

```ini
spectrum_analysis=0
spectrum_bands_lcd=32
spectrum_bands_tv=32
spectrum_gain_db=0
spectrum_style=1
spectrum_segments=0
spectrum_led_count=20
spectrum_peak_hold=1
```

Set `spectrum_analysis=1` to enable FFT analysis. Unsupported band counts are
ignored. The band count changes only display grouping: never MilkDrop's music
variables, preset geometry budgets or playback clocks. No server change needed.

Bars use unweighted FFT magnitudes normalized for the Hann window and full-scale
signed-8-bit input, mapped logarithmically from -60 to 0 dBFS. A full-scale sine
can reach the top; ordinary music need not fill every bar. Try +6 or +12 dB for
a more prominent display. There is no automatic gain riding. Windowed bars
still reserve space for the title/volume text above them.

### Colors, LED segments and peaks

**Spectrum colors** selects Original (0), Rainbow (1, default), Classic VU (2),
Ice (3), or Fire (4). Rainbow runs blue/cyan/green/yellow/orange/red/pink from
bottom to top. Colors are fixed to the available canvas height, not stretched
to each bar: quiet bars do not acquire pink tips. Original preserves the three
horizontal color groups. The palette is shared by LCD and TV.

**LED segments** (default off) uses whole, solid-color LEDs, never a partial top
segment. **LEDs per bar** selects 8..32 vertical segments (default 20), shared by
LCD/TV and independent of the horizontal frequency-band count. Integer pixel
boundaries divide the available height; remainder pixels are distributed across
complete segments, including the top one. The peak marker occupies one whole
LED on the same grid. **Peak hold** (default on)
adds a bright tip held for 450 ms, then falling at half the available height per
second; a new higher peak immediately takes over. Paused/stopped audio drains
the bars and markers instead of leaving them frozen.

All four display options work in Light and Desktop FFT modes, windowed and fullscreen.
Change them with Left/Right in main Settings or the Spectrum Circle menu; they
are saved without an app restart. They never modify audio analysis or playback.
A shared cached color table and bounded per-band peak state require about 7 KiB;
there are no additional textures/framebuffers or FFTs. Only changed bar/marker
strips are painted; TV copies at most one enclosing dirty span per changed band.

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
uses about 19 KiB, with no per-frame allocation or blocking producer waits.
Monkey reuses the same FFT scratch with its input filter instead of running a
second FFT. Analysis remains off for video and in Light mode. Presets explicitly
requesting custom spectrum waves still use their existing FFT in either mode.
Bar rendering retains changed-strip updates, without new framebuffers.

Palette verification: all five styles, LED/peak combinations and six canvas
heights match a separate full-frame pixel oracle under sanitizers. Actual LCD
and TV incremental renderers match their full renderers, also with Rainbow,
LEDs and peaks enabled. Native fullscreen guards, decay-to-silence and settings
bounds pass. A host check does not establish PSP audio stability or CPU cost.

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
