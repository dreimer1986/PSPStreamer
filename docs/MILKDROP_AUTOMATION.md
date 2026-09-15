# Shape instances, larger waves and preset automation

This batch does not add expression-language operators, functions, loops or
memory facilities. It supplies three engine inputs to the existing evaluator:
`instance`, `instances` and `progress`.

## Shape instances

`shapecode_N_num_inst=1` through `8` creates that many copies of shape N (0–3).
Each copy executes the shape's existing frame program with read-only `instance`
(zero-based index) and `instances` (total). Init executes once per shape.
Each instance restores q from the current preset frame and t from the shape's
init seeds. Named shape variables continue through instances and frames, as
with the reference's shared per-shape VM. Invalid output rejects the whole
frame; no partially evaluated copies are drawn.

This follows MilkDrop 2's `num_inst` export and `DrawCustomShapes` instance loop.
The PSP limit is eight instances per shape, 32 rendered shapes total, with the
existing 32-side limit. Static shape data remains separate from the extra
rendered copies. `shape-instances-demo.milk` shows eight colored orbiting shapes.

## Large custom waves and progress

Custom PCM/spectrum waves now accept 2–512 formula points. Lines subdivide to
as many as 1023 drawn vertices; dots retain their original points. Four waves
are still supported. PCM windows must satisfy `samples + sep <= 576`; invalid
combinations fail before any sample access. Spectrum range selection is unchanged.
The point instruction budget remains 64, bounding the worst case to 131,072
point bytecode instructions per visualization frame. This can reduce the
visual frame rate; it does not change the audio clock or worker priorities.

`progress` is read-only in preset, shape, wave and grid programs. It is elapsed
visual-preset time divided by configured `preset_seconds`, clamped to 0–1.
It also works with automation disabled and then stays at 1 after the interval.
The visual clock still advances during paused music; automatic selection waits
for resume and starts a fresh wait interval. `large-wave-demo.milk` renders a
512-point line whose color changes with progress.

## Automatic selection and playlist

Open the music preset browser with Circle. Square cycles Off / Order / Random /
Rated; Triangle cycles 30 / 60 / 120 seconds. Cross selects a starting preset;
Circle cancels file selection but retains automation settings. Start still
stops music, L/R pages, Up/Down selects. New labels are localized in EN/DE.
Automation runs only in the custom-preset visualization slot during playback.
It is off by default. Other built-in effects and the normal spectrum do not
automatically change. Track changes retain the chosen filename and settings,
but restart the timer. Manual preset selection also restarts the timer.

Configuration in `PSP/SYSTEM/PSPStreamer.cfg`:

```ini
preset_auto=0
preset_seconds=60
preset_fade_ms=1500
```

- `preset_auto`: 0 off, 1 ordered, 2 uniform random, 3 rating-weighted random.
- `preset_seconds`: 30–600, default 60. GUI choices are 30/60/120.
- `preset_fade_ms`: 0–5000, default 1500; zero disables the transition image.

When automation is enabled, the catalog is loaded once per music session (at
startup or when first enabled in the browser), with the existing 128-file limit.
If `presets/playlist.txt` exists, it replaces the automatic-selection list.
Use one exact filename per line, optionally `#` comment lines. Names must refer
to regular .milk files in the catalog; path traversal and missing names are
ignored. An empty playlist intentionally yields no automatic switches. Without
a playlist, the catalog's case-insensitive filename order is used. A supplied
`playlist.example.txt` is inert until copied/renamed to `playlist.txt`.

Ratings come from `fRating=0..5`, default 3 when absent. Rated mode excludes
zero-rated presets. Ordered and uniform-random modes still include them.
Immediate repeats of the same filename are excluded. Loading/parsing failures
leave the current parsed preset intact; one further candidate is attempted
after 250 ms. Runtime-invalid presets fall back safely and are also excluded
for that music session. No candidate means no switch, not an endless retry loop.
There is no rating editor; edit `fRating` in the .milk file. Reload the music
session after editing playlist/ratings. Automatic switches do not write config
on every transition, avoiding needless Memory Stick writes.

File reads run on the lower-priority music UI thread, outside GU command lists
and all audio workers. Slow storage can delay the visual switch; reads are not
asynchronous. The existing music workers continue independently.

## Snapshot-to-live crossfade

At an automatic switch, the renderer snapshots the previous fully composed
512x256 image, resets custom runtime state, and starts the new preset. Over the
configured interval the old image becomes transparent over the running new
preset. This is deliberately not desktop MilkDrop's two concurrently evolving
VMs/warps: the old side is a still frame. The snapshot is sampled from aligned,
cache-flushed main RAM; no extra EDRAM surface is required. It never feeds back
into the new preset's raw feedback. Failed allocation gives a hard cut instead.
Display-mode changes and teardown cancel/free the snapshot. The browser's
manual selection stops GU for its menu and therefore remains a hard cut.

## Resource and regression checks

The GU list grows to 768 KiB in normal RAM to hold the bounded maximum of large
thick waves and 32 shapes. The combined stress test including a fade uses
594,656 vertex bytes, leaving 191,776 bytes for commands and alignment.
The cached wave geometry grows to 49,168 bytes.
Crossfade temporarily needs 512 KiB on LCD or 256 KiB on TV; the playlist catalog
uses about 33 KiB. Allocation failures disable the affected optional feature.
Display/feedback EDRAM layout, DAC, MPEG/AVC, network timeouts and A/V sync remain
unchanged. Tests cover maximum geometry, both display layouts, fade lifetime,
instance isolation/bounds, playlist order, ratings, no-repeat selection and
browser controls. Actual performance and audio cleanliness require hardware tests.

## Hardware test

Copy EBOOT.PBP, PSPStreamer.prx and both new demo presets. Check the orbiting
instances and 512-point wave on LCD/TV, embedded/fullscreen. In the Circle
browser choose Order or Random and 30 seconds, then select a starting preset.
Wait through several transitions and verify music stays clean, fullscreen is
retained, Stop works, and a subsequent video still plays. Optionally copy
`playlist.example.txt` to `playlist.txt` to cycle a known four-preset subset.
No server update is required. Existing config and active playlists are not
overwritten by the prepared release.

## What remains

Full expression-language/NS-EEL work is deferred by request, along with shaders
and external textures. This remains a bounded PSP implementation, not exact
desktop output: normalized audio analysis, fixed mesh/side/instance limits,
strict field bounds and snapshot crossfades differ from desktop MilkDrop.
Obsolete switches without an active MilkDrop 2 render path (including the old
red/blue-stereo setting) are not claimed as implemented features. Nonzero
unsupported legacy flags continue to be rejected explicitly.
