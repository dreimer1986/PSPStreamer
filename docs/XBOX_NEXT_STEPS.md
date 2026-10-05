# Xbox: optimization and parity checkpoint — 0.6.9

## Implemented without changing PSP paths or preset semantics

- SSE1 block copies, cached texture swizzling, command-publication batching and
  cooperative audio refill (0.6.6–0.6.8); hardware stability/speed confirmed.
- Start-to-start visual scheduling, no additive post-render 50 ms delay.
- 0.6.9 retains static SDL music pixels, copies effect/instrument rectangles
  instead of the whole HD GUI, and clears only active spectrum backing rows.
- Spectrum attack/decay now uses elapsed 50 ms steps, matching PSP timing even
  when Xbox redraw frequency is lower. No stronger signal or formula changes.
- Video remains independently optimized: YUY2 PVIDEO output, real sample/PTS
  clock and partial GUI/progress updates. No guessed synchronization offsets.
- HTTP buffer-to-consumer payload copies of at least 64 bytes reuse the same
  bounded SSE1 copy instead of SDK byte loops; headers keep the small-copy path.
  This covers media/artwork as well as effects, without a global libc override.

## Remaining optimizations, ordered by plausible benefit

1. **Large: direct GPU effect presentation/scaling.** Avoid offscreen GPU → CPU
   readback → CPU scale → HD software surface → scanout. Especially promising at
   720p/1080i. Requires explicit ownership and restoration around PVIDEO, SDL,
   output changes, menus and shutdown. Not yet implemented or benchmarked.
2. **Medium/large: batch Monkey projection/vertex conversion.** Preserve original
   transform, clipping, materials and draw order; use SSE1 where mathematically
   equivalent. Useful only if geometry dominates the timing trace, not copies.
3. **Medium: finer instrument damage regions.** Current windowed music still
   restores/copies the receiver strip. Per-needle/knob regions could reduce
   traffic further, but add overlap/restoration complexity for a smaller gain.
4. **Large: MPEG decoder work.** Profile IDCT/motion compensation before replacing
   libmpeg2 or adding NV2A motion-compensation support. Fast 1080p scenes are not
   proof of enough CPU capacity for complex scenes. Keep 720p as reliable target;
   physical 1080i output and 1080p encoded video are different settings.
5. **Conditional: network/packet allocation.** No evidence yet that more socket
   buffers/threads would improve this Ethernet path. Measure before allocating
   more of the stock 64 MB. Do not import PSP USB transport tuning blindly.
6. **Large: general formula optimization.** Only semantics-preserving work: same
   random calls, state changes and ordering. No simplified or skipped presets.

Use existing non-stopping `visual timing` logs for comparable presets, output,
window/fullscreen and CPU clock. Do not stop the serial debugger while timing
audio: that produces the repeated syllables being investigated. 0.6.9 partial
composition/presentation counters retain separate timing boundaries.

## Portable features implemented in 0.6.9, hardware test pending

- DNS hostnames: asynchronous lwIP callback, cancellation-safe lifetime, bounded
  concurrency/deadline; DHCP supplies resolver addresses. IPv4 still works.
- Session stop timer (15-minute increments, maximum 180), no automatic rearming.
- Save/remove existing server-side series/folder language/title preferences.
- Xbox-specific web button/text commands; no PSP mailbox sharing, held inputs,
  additional polling worker or text value leakage in status responses.
- ICY titles through existing polling and corrected Xbox live-radio framing.
- Controller-diagram help with library/video/music/keyboard contexts.

## Still authorized, not implemented

- **Downloads/local playback:** Xbox-native offline format/jobs, durable catalog,
  transfer/cancel/progress and disk-space handling, local seek/resume and metadata.
  Reuse server job semantics, not PSP's incompatible FLV payload. FATX filename
  and file-size limits need explicit handling, not a renamed extension.
- **Client subtitles:** shared text/bitmap/paging logic with Xbox renderer,
  seek/reconnect lifecycle, bounded memory and correct PVIDEO overlay composition.
  Server burn-in remains working until both text and bitmap paths are ready.
- **HTTPS:** maintained TLS library with Xbox entropy/time/socket adaptation,
  certificate validation/trust UI and cancellation. Do not send Basic credentials
  over a pretend TLS connection or silently disable authentication verification.
- **Additional codecs:** not a goal by itself. Server FFmpeg already accepts the
  source formats; client MPEG-1/2 + MP2 covers current transport. AC-3/DTS passthrough
  stays deferred until S/PDIF can be tested. Add codecs only for a measured benefit.

No console confirmation is claimed for 0.6.9 yet. This checkpoint does not mark
the whole PSP/Xbox parity task complete.
