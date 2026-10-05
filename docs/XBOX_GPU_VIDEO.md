# Xbox 0.4.3: NV2A video presentation

## Console finding and repair

The 0.4.2 cold-start log reported `gpu_frames=0`, `gpu_busy=8`, followed by
software playback and roughly 75% scheduler frame drops at 1280x720. The
earlier performance impression therefore did not validate the GPU path.
0.4.3 resets only PVIDEO through **PMC ENABLE bit 28**, preserving every other
engine bit. It configures the pinned nxdk PVIDEO burst/watermarks and releases
STOP before testing first-submission ownership. Logs record the inherited and
post-reset buffer states. Physical scanout and the improvement remain unverified
until the next Xbox test; host register mocks cannot prove hardware behavior.

A separate warm-start audio DMA stall disappeared after a cold boot. This
change does not claim to fix that SDK/audio initialization issue.

## Scope

Keep libmpeg2 MMX/MMXEXT decoding and real container PTS paced by the audio DMA
clock. Replace SDL software YUV conversion, scaling and per-video-frame RGB
screen copies with the NV2A PVIDEO overlay. The server, PSP player and audio
clock are unchanged. `-O3`, LTO and Pentium III compilation remain enabled.
No claims of hardware MPEG decode or universally smooth 1080p decoding.

PVIDEO was chosen before a register-combiner/pbkit renderer because it does not
need three RGB backbuffers plus a depth surface on a 64 MB console. Two packed
YUY2 buffers cost approximately 1.32 MiB at 720x480, 3.52 MiB at 1280x720 and
7.91 MiB at 1920x1080 (plus existing decoder/GUI/framebuffer allocations).
All scanout allocations are checked. This is not the future 3D visualization
renderer; it is a separate, substantially smaller video-only path.

## Implementation

- `yuy2_pack.h`: actual MMX unpack/interleave and SSE1 non-temporal stores,
  respecting all plane strides. Chroma is expanded vertically from 4:2:0 to
  4:2:2; each chroma load is reused for two adjacent luma rows. SFENCE publishes the WC writes before GPU submission; EMMS restores
  floating-point state. No whole-cache WBINVD per frame. New Clang MMX intrinsics
  unexpectedly require SSE2; explicit assembly avoids unsupported instructions.
- `video_overlay.h`: 64-byte-aligned, physically contiguous WC memory below
  64 MiB; alternating PVIDEO register sets with buffer ownership checks. Busy
  slots are skipped, never overwritten or waited on in the playback loop.
  A busy interval over 250 ms selects software fallback for this stream.
- Immediate stop and disabling only PVIDEO precede release. If the engine
  cannot be disabled, retain its allocation. Restore its original enable bit
  after freeing the stopped buffers; other engines are never reset.
- Color key `0x010203` marks the video viewport only. GUI/controls/progress use
  other colors and remain above the overlay. SD BT.601 / HD BT.709 follows the
  existing SDL automatic conversion selection. Existing anamorphic/4:3 fitting
  is retained in both fullscreen and embedded views.
- The software GUI updates at 4 Hz during unchanged video playback. Fullscreen
  progress copies only its three-pixel strip, embedded video only the footer
  and receiver area; an open control panel copies its area. A full repaint is
  needed on first frame, fullscreen change or opening/closing controls.
  Static content stays in SDL's existing surface (no extra full-screen cache).
  The app's Xbox SDL adapter honors damage rectangles; upstream copied the
  entire screen regardless. Video retains the unchanged timestamp clock.
- Text rasterization/texture uploads use a 96-entry, 2 MiB LRU cache, released
  before changing renderer/font and on exit. It does not cache entire menus.
- `video_hardware=0` selects the previous SDL path, for direct comparisons or
  recovery if this hardware test exposes a scanout issue. No settings migration
  or server update beyond 0.1.73 is needed.

## References

Register behavior and buffer selection were checked against these sources;
the application adapter/packer is an independent implementation:

- [NVIDIA Xorg overlay implementation](https://github.com/NetBSD/xsrc/blob/trunk/external/mit/xf86-video-nv/dist/src/nv_video.c)
- [Xbox PVIDEO example](https://github.com/JayFoxRox/xbox-fps-overlay/blob/master/main.c)
- [xemu register definitions](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/nv2a_regs.h)
- [xemu stop semantics](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pvideo.c)

## Verification and console test

Native XBE built **before** targeted host tests. The actual packing and buffer
ownership code is exercised in `tests/xbox_video_overlay.c`, with hardware
register/allocation boundaries stubbed. Eight sizes cover tiny/tail cases and
all six profiles, padded source planes, destination guards, exact Y/U/V byte
order, register layout, inherited busy-state reset, preservation of other engine
bits, double buffering, busy timeout, failed engine stop, allocation failure
and software selection. ASan/UBSan enabled. This does not prove physical scanout.

```sh
cc -O2 -Wall -Wextra -fsanitize=address,undefined \
  tests/xbox_video_overlay.c -o /tmp/xbox-video-overlay
/tmp/xbox-video-overlay
```

`tests/xbox_gui_cache.c` checks the actual rectangle copy/format conversion,
clipped bounds/guards, UTF-8 text cache hits, eviction, byte limits and cleanup
under ASan/UBSan. `tests/xbox_video_lifecycle.c` checks the actual SDL adapter's
window recreation across five sizes. These are targeted host checks only.

Console test: same episode and resolution with NV2A and Software. Verify image
colors/geometry, controls/progress, fullscreen/windowed, pause/seek, stop/restart,
autoplay and output-mode changes while stopped. Start with 720x480, then try
720p video and desired output modes. 1080i scanout and interlaced modes require
their own visual confirmation. Do not conflate display size with decode size.

Diagnostics are cumulative per playback: `gpu_frames`, `gpu_busy`, `decode_ms`,
`pack_ms`, `gui_ms`, `gui_kib`, `gui_copies`; `renderer=nv2a|software` identifies the active path.
`shown` is scheduler delivery, whereas `gpu_frames` counts accepted overlay
submissions. `gpu_busy` is skipped submissions, not an audio underrun. Compare
counter deltas over equal durations; GUI/pack milliseconds can round to zero for
individual short operations. No measured speedup is claimed before console logs.

## PSP follow-up (no PSP changes)

The analogous library/music paths already retain static content and copy dirty
areas (`tv_library_receiver_refresh`, incremental music scene in `tv_gui.h`).
No equivalent unconditional full-frame-per-animation bug was found there.
The idle library still restores/copies the complete 672x110 receiver strip at
150 ms intervals; measuring smaller per-instrument damage is a later, low-priority
option, not a demonstrated playback bottleneck. Decoder/framebuffer changes or
text-renderer changes on PSP are outside this Xbox task.
