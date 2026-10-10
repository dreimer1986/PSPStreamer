# Optional animated VSH background

## Use and hardware test

`xmb_animation=1` is the default, including when the key is absent. To disable,
set `xmb_animation=0` in `PSPConsolizer.ini` and restart VSH. Install the matching
`PSPConsolizerUSB.prx` and `xmb-background.xba` in `SEPLUGINS/PSPConsolizer/`.
No EBOOT, firmware, OC or loader update is required. Set `xmb_probe=0` and
`xmb_texture_probe=0`; these diagnostics take priority over animation.

Restart VSH, wait about ten seconds, open Game / Memory Stick and select
PSPStreamer without starting it. Loading begins when its background is drawn;
the static image stays visible until the complete bank is checked and ready.
If it was already cached during startup,
leave Memory Stick and reopen it. The background should show the moving city
behind ordinary XMB icons/text. Verify changing selection, starting the app,
returning to VSH, LCD and TV, and disabling the INI option. With `report=1`,
`last.log` records install/fallback and load/bind/destruction counters.
The user confirmed smoother 12 fps playback and working multi-app loading and
playback. Initial on-demand loading takes a moment. The supplied Daedalus test
asset is deliberately smaller (160x90) than PSPStreamer's 256x144 asset; this is
not a per-app rendering limit.

The separate eight-second asset has 96 immutable RGB565 frames, 256x144 at
12 fps, displayed using native XMB geometry. Its 7704576-byte bank lives in
user partition 2, not kernel RAM. The loader requires an additional 2 MiB
contiguous reserve and accepts at most 8 MiB per animation. At most two banks
(16 MiB combined upper bound) are resident, loaded on demand rather than at
startup. Missing/corrupt
assets, unsupported layouts or allocation failure leave PIC1 unchanged.
The EBOOT's native static backdrop, animated icon and sound need no plugin.

## Rendering and ownership

After an eight-second startup delay, guarded hooks are installed before any
bank is loaded. The worker reads at most 32 KiB of asset data per iteration,
verifies length/geometry/checksum, and only then enables that bank. A draw of a known
PIC1 requests its bank from the worker; render hooks themselves do no I/O.
Identification uses PIC1 size/FNV, not a transient cache index or a hard-coded
runtime address. It supports
the captured 6.61 module layout and original 480x272 RGBA PIC1 surfaces only.
Up to sixteen live native PIC1 surfaces are tracked. Further surfaces retain
their original appearance. Apps without PIC1 or with unsupported dimensions/
native formats also keep the original XMB behavior. Path/title-based selection
for apps sharing an identical background is not implemented.

The PIC1 load wrapper observes the original successful loader result. A guarded
PAF call-site wrapper uses a 104-byte stack copy of the live surface to bind
the animation buffer; native storage, refcounts and object ownership remain
untouched. The verified binder reads the descriptor synchronously and only
emits GE commands, without retaining that stack copy. Both virtual surface
destructors clear the matching identity before releasing the native object.
The four float arguments of the binder and original return values are retained.
All wrappers execute in user context with no imports or relocations.

All frame pixels are immutable and flushed before publication. The worker
only changes an index; it never dereferences an old captured texture pointer.
No I/O, allocation, decompression or frame copying occurs in a render hook.
This avoids overwriting textures referenced by in-flight GE command lists.
The small published code/context remains allocated until VSH loadexec/teardown.
Banks record every native surface that has bound their pixels. A bank is only
eligible for replacement after all those surfaces have completed PAF's native
destructor, which also releases their original texture storage. An in-flight
bind additionally blocks reclamation; elapsed time alone never permits reuse.
This follows native texture ownership rather than overwriting a bank on each
selection. Two retained backgrounds can pin both slots during XMB caching or
fades: a further app stays static until a slot becomes available. Leaving and
reopening Game / Memory Stick can release that native cache. There is no
unbounded per-app allocation or kernel-RAM fallback. Suspend
and USB hand-off disable substitution; it resumes when the service is allowed.
The game-menu load hook can reattach after that module reloads.

## Building a replacement asset

Requirements: **Python 3 and FFmpeg** in PATH (Linux, Windows or macOS). No PSP
SDK, Python packages, firmware dumps, image editor or modification of the app
is needed. Input can be MP4, MKV, WebM, AVI, GIF or another video FFmpeg decodes.
Only the picture is used; animation audio is not played by this feature.

The PSP reads **`.xba`**, an uncompressed, versioned RGB565 frame bank with a
checksum and target-image fingerprint. It does not directly decode MP4 in XMB.
The converter is included in `PSPConsolizer/tools/` in the release ZIP.

Example, from the repository root (or use `python` on Windows):

```sh
python3 tools/build_xmb_background.py \
  --video my-animation.mp4 \
  --app /path/to/OtherApp/EBOOT.PBP \
  --output-dir ./animations
```

Copy the resulting `animations` folder to `SEPLUGINS/PSPConsolizer/` on the
Memory Stick. The tool automatically names files `HASH-SIZE.xba`, for example
`28108CC0-198253.xba`. **Keep that filename.** Several animations coexist in
this folder; no INI entry per app is needed. Keep `xmb_animation=1`, restart VSH
after adding/replacing files, and select the app. The old root-level
`xmb-background.xba` remains a compatible fallback when its fingerprint matches;
an app-specific file takes precedence. A damaged app-specific file falls back
to the static image rather than hiding the error with a different animation.

The default is an eight-second, 256x144, 12 fps loop. Short source videos repeat
to fill it; long ones use the beginning. To keep more detail with a shorter loop:

```sh
python3 tools/build_xmb_background.py \
  --video my-animation.mp4 --app /path/to/OtherApp/EBOOT.PBP \
  --output-dir ./animations --width 320 --height 180 --fps 12 --duration 5
```

The converter rejects settings exceeding 8 MiB and reports the maximum frame
count for the chosen resolution. `--frames` overrides `--duration` for exact
lengths. Video is scaled to the selected dimensions and finally to native XMB
background geometry; use a widescreen source to avoid unwanted distortion.
Assets are validated and atomically replaced on the PC. EBOOT is read-only.

Alternatively, use the exact PIC1 PNG extracted from an EBOOT or game's ISO;
the tool does not extract ISO contents. The current PSPStreamer
fingerprint is `28108CC0` (198253 bytes). Original dimensions must be 480x272;
the animation may be smaller. Apps sharing exactly the same PIC1 also match.

```sh
python3 tools/build_xmb_background.py \
  --video /path/to/background-loop.mp4 \
  --pic1 psp-client/assets/pic1.png \
  --output xmb-background.xba --width 256 --height 144 --frames 96 --fps 12
```

The tool converts existing video with FFmpeg, repeats border pixels, swizzles
16-byte by 8-row blocks and writes a versioned, checksummed container. It uses
BGR565 little-endian bytes to match PSP's low-bit red channel. The supplied
asset comes from the existing Cyberpunk-v2 background concept, not Sony data.
Changing the native PIC1 requires regenerating the matching asset fingerprint.
Resolution, duration and frame rate trade off against the fixed RAM budget.

## Investigation history

The native EBOOT now has ICON0.PNG, ICON1.PMF, PIC1.PNG and SND0.AT3.
It starts normally without PSP Consolizer. ICON1 is the animated icon; PIC1 is
the static full-screen fallback. These are independent of a future extension.

## Required implementation boundary

The requested extension belongs in Consolizer, is optional and loads animation
data from a separate Memory Stick file. Missing/invalid data or a disabled plugin
must leave the ordinary static XMB presentation and app launch untouched.
No flash0 changes, replacement VSH or mandatory custom theme.

The existing Consolizer present hook in `psp-controller/overlay.h` sees the
already-composited framebuffer. It can add status text **over** the XMB, but it
cannot recover the layers behind icons/text. Painting a whole movie here would
obscure the menu. Comparing framebuffer colors with PIC1 is not a reliable mask:
XMB fades, antialiased text, dimming and TV scaling alter those pixels.

A proper implementation needs a verified VSH/PAF background-texture update or
pre-menu composition point, plus identification of the selected PSPStreamer
entry. The image-loader path has now been identified for the captured 6.61/ARK
setup; the surface layout and lifetime still need runtime confirmation.
An independent animated overlay is not proof of an animated PIC1 solution.

## Capture and static analysis

First capture, 2026-10-09: PAF was complete (1,623,320 + 287,400 bytes), both
FNV-1a values verified. Its `scePaf` export table contains 1,078 functions and
24 variables. Observed NID offsets include `3874A5F8=3C5EC`,
`82B37153=36D88`, `C22BACF3=DE044`; these are evidence, not installed hooks.
Capture stopped at the next module's layout validation before printing its
layout. The exact rejected segment therefore cannot be recovered from that log.
The revised probe prints every layout first, supports the 64 MiB RAM boundary
on known later PSP models, accepts empty segments without dereferencing them,
and continues other modules after a layout rejection.

The second capture is complete: all six segments passed size/FNV verification.
VSH at `0A042600` and the game plugin at `0A573D00` were indeed above the old
32 MiB bound. The game plugin has text/data/BSS sizes 190664/84/2492 bytes.
Its PIC1 and PIC0 load branches call the image-cache helper at text offset
`1BDA0`, from offsets `B51C` and `B484`, respectively. Both call delay slots
are `move t2,zero`. The helper receives the encoded image and its size, a
cache key (`%d_bg`) and a surface-reference output. It delegates image parsing
and surface construction/copying to PAF; the caller retains the returned object.

PAF's observed surface-copy export is NID `E9570AA3` (text offset `13308C`).
The constructor allocates 104 bytes. Dimension, layout, stride and backing
pointer candidates are visible in the copy code, but these are not a license
to write guessed offsets. The next capture records the actual objects.

## Hardware result, 2026-10-10

The observer installed successfully and recorded four successful image loads.
`0_bg` matches the **actual EBOOT on the user's Memory Stick**: PIC1 has
198253 bytes and FNV-1a `28108CC0`. The EBOOT has no PIC0. The other captured
backgrounds (`1_bg`, `2_bg`) have different fingerprints; `2_info` is a smaller
310x180 surface. Cache keys are session-local indexes, not permanent app IDs.
Match image content, not `0_bg` or a previously observed memory address.

PSPStreamer's surface in this session was `0A0013A0`, with pixel backing at
`0966B9C0`. Its verified layout fields are:

| Offset | Value | Interpretation |
| --- | --- | --- |
| `08` | 3 | PAF pixel format, mapped to GU format by PAF |
| `0C` | 5 | log2 bits per pixel: 32 bpp |
| `18`, `1A` | 480, 272 | visible dimensions |
| `1C` | 2 | tiled/swizzled storage (16-byte by 8-row blocks) |
| `2A`, `2C` | 512, 512 | power-of-two texture dimensions |
| `30`, `34` | 0.9375, 0.53125 | visible-to-texture coordinate ratios |
| `38` | 542080 | bytes per backing slot, including padding |
| `3C` | 1936 | row stride in bytes, not 480x4 |
| `40` | 1 | number of backing slots |
| `48`, `50` | nonzero, zero | primary and optional secondary backing |

The stride and extra rows follow the constructor's edge padding, alignment and
swizzle rules. Treating this as a flat 480x272 bitmap would corrupt the picture.
The dimension/stride interpretation is supported by the captured metadata and
PAF's construction, upload and bind code, not just guessed from the numbers.

Further static tracing identifies the draw-time texture binding call at PAF
text offset `132A5C`, targeting `134194`. That routine reads the live surface,
sets format/swizzle and UV ratios, then submits its backing pointer to the GE.
This is the substitution point confirmed by the animation hardware test.
It also uses four floating-point
arguments, which must be preserved explicitly.

The requirements established before implementation were live-object destruction/
replacement handling, preservation of original surface ownership, and keeping frame buffers alive
until outstanding GE lists have finished. A successful load-time snapshot alone
does not authorize updating its address later from the service thread. No
additional capture of the same four loader records is needed at this point.
Private input logs are saved under `diagnostics/xmb-textures-20261010` outside
the repository. The EBOOT remains unchanged by the animation extension.

## Background texture observer procedure (completed for the current PSP)

Update **only** `PSPConsolizerUSB.prx`, preserving your other settings. Configure:

```ini
report=1
xmb_probe=0
xmb_texture_probe=1
```

Restart VSH. Open Game / Memory Stick, remain on another entry for ten seconds,
then select PSPStreamer without launching it. Wait fifteen seconds. Select
another application and return to PSPStreamer once. If PSPStreamer was already
selected/cached before the observer installed, leave Memory Stick and reopen it.
Collect `SEPLUGINS/PSPConsolizer/xmb-probe/textures.txt` and the plugin's `last.log`.
Disable `xmb_texture_probe` after this capture. No new module dumps are needed.

This optional observer replaces **two verified JAL instructions**, not the
display compositor or a live function prologue. It checks firmware, module
layout, loader signature, both call targets and delay slots, and revalidates
the module after allocation. Unrecognized builds remain untouched. Original
arguments, return values and object ownership are preserved. The imported
loader runs in its original user context; the observer has no kernel imports.

At most 16 records contain the encoded-image FNV, cache key, loader result and
104-byte surface header. No texture pixels are copied or changed. The existing
worker writes the records outside the loader; no new thread or frame polling
is introduced. Observation ends after 90 seconds, a full log, suspend, service
exit or USB hand-off. It is default-off and VSH-only, requires `report=1`, and
adds about 3.4 KiB of user RAM while enabled. The published self-contained
code/context allocation is retained until VSH teardown so a prefetched call
cannot jump into freed memory. Only one such allocation is made per session.

Build and host tests verify standalone payload layout, original argument/result
preservation, bounded snapshots, invalid-address rejection and record limits.
They do not prove the firmware hook or surface ownership on real hardware.
This diagnostic option itself is **not an animation toggle**.

## Older module-capture procedure (not needed again for the current PSP)

Consolizer now includes an **opt-in, read-only module capture**, not an animation
renderer. Install the current `PSPConsolizerUSB.prx` and add these settings to
`ms0:/SEPLUGINS/PSPConsolizer/PSPConsolizer.ini`:

```ini
report=1
xmb_probe=1
```

Restart VSH, open Game / Memory Stick and select PSPStreamer **without starting
it**. Leave the entry selected for about 30 seconds, then mount the Memory Stick.
The directory `SEPLUGINS/PSPConsolizer/xmb-probe/` contains `capture.txt` and the
loaded segments of `scePaf_Module`, `game_plugin_module` and `vsh_module`.
If a module is not loaded yet, the capture waits up to two minutes. A complete
capture ends with `finish=complete completed_mask=7`. Otherwise preserve the
text report too: it distinguishes an absent module, I/O error, exit or unload.
Set `xmb_probe=0` again afterwards; there is no need to leave diagnostics enabled.

This code installs **no hooks** and changes no module, texture, firmware file or
framebuffer. It waits eight seconds after service setup, uses an 8 KiB user-RAM
buffer, and writes at most one chunk per 20 ms from the service worker. GAME and
POPS never start it. Suspend, service exit and explicit USB hand-off stop a
running capture. Normal controller/audio/hook settings remain untouched. During
capture, extra Memory Stick I/O is expected; this is not a performance test.

Module identity/ranges are rechecked per chunk. Completed segments have sizes
and FNV-1a integrity values in the report; incomplete files retain `.part`.
These are runtime snapshots, not an atomic snapshot of all mutable module data.
Only files listed as complete in the **current** report belong to that session.
Do not publish the dumps or include Sony module data in release archives.

Host verification (read only):

```sh
python3 tools/check_xmb_capture.py /path/to/PSPConsolizer/xmb-probe
```

The analysis uses those exact module addresses, exports/imports and code
to trace PIC1 loading, the selected item, texture ownership and destruction.
The dump alone does not establish a safe update/lifecycle hook.

Identify and observe that texture/lifecycle before writing to it. Only then add
the optional renderer: delayed VSH initialization, bounded non-kernel asset
storage, no I/O/decoding inside a display callback, clean stop on app launch,
USB-storage hand-off and suspend, and coexistence with FuSa/status overlays.
Verify on LCD and TV, including cold boot and return from a game. Do not revive
the previously rejected early VSH initialization path just for visual effects.

The city panorama and silent motion study are prepared in the XMB concept assets
and `~/Bilder/PSPStreamer-XMB/Cyberpunk-v2/`. The preview MP4 is not directly a
PSP plugin asset; the separately encoded XBA file is used instead.

References inspected:

- [PSP media toolkit](https://github.com/TotalKommando/psp-media-toolkit): native icon and sound packaging.
- [XMBuddy](https://github.com/StefanTsonev/XMBuddy): explicitly a post-composition overlay, not a background replacement.
