# Waveform pack and in-app preset browser

## Built-in waveform modes

All built-in `nWaveMode` values 0 through 8 now have implementations. This is
not full MilkDrop preset-language compatibility. Geometry follows the inspected
MilkDrop 2 `DrawWave` cases; PSP sound normalization, smoothing and aspect
handling remain distinct from desktop MilkDrop.

| Mode | Waveform | Example |
| --- | --- | --- |
| 0 | Closed circular waveform | Existing presets |
| 1 | Stereo spiral | `spiral-wave-demo.milk` |
| 2 | Faint stereo spiro trace | `spiro-demo.milk` |
| 3 | Spiro trace with treble-driven opacity | `pulse-spiro-demo.milk` |
| 4 | Horizontal script | `script-wave-demo.milk` |
| 5 | Rotating complex stereo trace | `complex-demo.milk` |
| 6 | Angle-adjustable single-channel line | `line-demo.milk` |
| 7 | Two separated stereo lines | `dual-line-demo.milk` |
| 8 | Frequency-spectrum line | `fft-spectrum-demo.milk` |

Mode 7 is two separate draw paths: no artificial connecting segment. Mode 8
uses a 1024-sample left-channel Hann-windowed radix-2 FFT, yielding 512 bins
combined in pairs for its 256-point log-magnitude line. This is a new PSP FFT
front-end, not desktop MilkDrop's complete analysis pipeline. Silence uses a
finite logarithm floor. Modes 2/3/5 preserve the 480-point geometry; 6/7 use
170 points per line. Existing dots, thickness, color, alpha and additive
blending apply. Mode 3's treble input remains our relative PSP band estimate.

## Preset browser

During music, press **Circle** to open the browser. Playback continues.

- Up/Down: select; holding repeats.
- L/R: eight entries backward/forward; holding repeats.
- Cross: load and activate the selected custom preset.
- Circle: cancel and return to the previous effect.
- Start: stop music. Remote Stop/Play/Seek leave the browser and follow the
  normal music teardown; remote Pause/Resume remain handled.

The browser lists regular `.milk` files in `presets/`, case-insensitively
sorted. It does not recurse. The catalog holds 128 filenames; exceeding that
limit produces an explicit warning. File parsing retains the existing size,
line and instruction limits. Invalid files display the existing error notice;
they do not replace the previously parsed object or terminate the audio worker.

`music_preset=filename.milk` is saved in `PSP/SYSTEM/PSPStreamer.cfg` and restored
at app startup (default `active.milk`). Paths and embedded newlines are rejected.
The remembered file is distinct from the existing session-only effect/fullscreen
selection. The browser doesn't rewrite any preset file. GU is stopped before
the browser draws and recreated on exit; textures/scanout never change format.
The directory scan and selected file parsing occur on the low-priority UI
thread, not in a DAC callback or render tick. Slow/failing storage can still
delay opening/loading; there is no claim of asynchronous filesystem I/O.

## Motion vectors and formula clock

`mv_a`, `mv_r`, `mv_g`, `mv_b`, `mv_x`, `mv_y`, `mv_dx`, `mv_dy`, `mv_l` are
supported as static fields and in per-frame formulas. Limits: alpha/RGB 0–1, grid X 0–16, grid Y 0–12,
offsets -1–1, length 0–10. Zero opacity disables them. Vectors sample the actual
warp triangles, including per-grid formulas, with a minimum visible segment.
Segments are clipped to feedback bounds before submission. This is a bounded
motion-vector subset, not the desktop 64x48 grid. See the
[dynamic waveform update](MILKDROP_DYNAMIC_WAVES.md) for formula controls.

Read-only `frame` and `fps` are available in init/frame/grid formulas. `frame`
counts successfully evaluated visualization frames starting at zero; `fps`
uses the interval between those evaluations, not video fps or the audio clock.
First-frame fps is zero. Throttled calls do not increment the counter.
`motion-clock-demo.milk` combines these fields with a moving warp grid.

## Resource boundary

The music-only GU list grows from 64 to 128 KiB in ordinary RAM. Textures stay
512x256 with the established LCD/TV formats and EDRAM layout. The maximum
tested layer combination (all nine wave modes, thick/dotted waves, four thick
textured shapes, echo/gamma/borders and motion vectors) uses **88,080 bytes**
for vertices, leaving **42,992 bytes** for command words/alignment. The harness
enforces a 112,000-byte vertex ceiling. No new external textures are loaded.

Mode 8 alone requests 1024 left-channel PCM samples; other modes retain the
existing right-only/stereo snapshot paths. FFT scratch is UI-thread-owned,
with no allocation or FFT computation in the audio producer. FFT/window arrays
use 12 KiB, and shared/consumer spectrum snapshots add 4 KiB. Rendering remains
adaptively throttled. Host tests do not establish PSP frame time or crackle-free
playback; sustained mode-8 testing is especially important.

## Test batch

Copy the new EBOOT/PRX and the seven new examples from the release `presets/`
directory. No more renaming to `active.milk` is required. Start music, press
Circle and try each table entry plus `motion-clock-demo.milk`. Check LCD and
TV, receiver and fullscreen, hold navigation keys, cancel selection, malformed
presets, pause, track changes, and music-to-video transitions. Recheck Hyperdrive.
No server update is required. Stream timeouts and A/V synchronization are unchanged.

## Still separate architectural work

Custom-shape init/frame contexts are now supported in a bounded subset; see
[shape formulas](MILKDROP_SHAPE_FORMULAS.md). Custom-wave frame/point contexts,
complete NS-EEL compatibility (including memory/loops), larger/persistent
per-grid contexts and preset blending are not minor switches. They remain
unimplemented rather than silently approximated. Shader execution and external
textures remain explicitly deferred. Therefore this pack completes the built-in
waveform modes, not all MilkDrop 2 features or all shader-free presets.
Other remaining gaps include `progress`/additional engine inputs,
inversion/brighten/darken/solarize passes,
automatic timed preset switching, ratings and playlists. Our FFT, limited grid
and single-precision evaluator are also not a bit-exact desktop implementation.
