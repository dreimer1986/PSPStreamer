# Optional animated VSH background — investigation, not shipped

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

## Current hardware test: background texture observer

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
This is a diagnostic build, **not an animation renderer**.

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
and `~/Bilder/PSPStreamer-XMB/Cyberpunk-v2/`. No new background INI option or
background renderer is shipped yet; the preview MP4 is not a PSP plugin asset.

References inspected:

- [PSP media toolkit](https://github.com/TotalKommando/psp-media-toolkit): native icon and sound packaging.
- [XMBuddy](https://github.com/StefanTsonev/XMBuddy): explicitly a post-composition overlay, not a background replacement.
