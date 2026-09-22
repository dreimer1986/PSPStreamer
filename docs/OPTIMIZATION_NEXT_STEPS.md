# Optimization status and remaining work

## Previously implemented: prepared execution

- Import-time preparation of PUSH/LOAD followed by ADD/SUB/MUL. The original
  instruction slots and source lines remain intact; preparation is idempotent
  and adds no program allocation. Both original fuel charges remain. Budget,
  stack and PSP yield boundaries use the original instruction path. Loop-bearing
  programs are deliberately excluded to preserve native clear-loop recognition.
- Exact 64-entry cache (1,280 bytes) for pure expensive functions: pow, atan2,
  atan, exp, log, log10, tan, asin and acos. The existing sin/cos cache remains.
  Keys contain opcode and both operand bit patterns, including signed zero.
  Only finite results are stored. Cache hits still consume original VM fuel.
  Randomness, writes, memory reads and control flow are never cached. This
  reuses pure function subresults, not arbitrary multi-statement expressions.

The PSP build was created before validation. A reference build disables both
optimizations with `PM_REFERENCE_EXECUTION`. The differential correctness test
compares results, error lines, fuel, rollback state and mocked PSP yield counts
over invocation/aggregate budget boundaries, warm-cache input changes, lazy
branches, nonfinite intermediates, signed zero and append/re-prepare. Seven
additional targeted integration tests cover pixel/wave state, exception paths,
conditionals, geometry budgets and native init clears. No timing benchmarks or
PSP profiling were run: performance measurement is deferred at the user's request.

## Completed batch: original items 1–6 (2026-09-23)

1. **One TV presentation per library redraw.** Compose the skin, artwork, menu
   and TLS/network notices, then present once. This removes one 1,474,560-byte
   copy and one VBlank wait per full library redraw. Other screens retain the
   compose-and-present wrapper.
2. **Idle TV library dirty region.** The 150-ms idle animation restores/draws
   only the receiver strip (`x=24..695`, `y=360..469`), copying 295,680 bytes
   rather than a full frame: approximately 80% less scanout-copy traffic per
   refresh, plus no repeated menu text/backdrop composition. Navigation/status
   changes still compose a complete frame. Artwork downloads suspend animation;
   decoder-owned output is never touched. Existing LCD/music paths are unchanged.
3. **Canonical artwork reuse.** Resolve series/album parents before caching.
   A v2 request sends the identity of the PSP's one retained image; an identical
   image receives a 68-byte confirmation instead of a 133,140-byte raw packet.
   Hide the retained image until confirmed, reject stale worker completions and
   free it before playback. No extra full-size retained cache. Old clients still
   receive v1 packets; the new client also accepts an old server's v1 response.
4. **Packet ownership review complete; profiling still deferred.** See the
   detailed review below. No speculative zero-copy or cache-flush change was made.
5. **Bounded artwork single-flight preparation.** Per provider: at most four
   packet jobs, four plane jobs, two simultaneous FFmpeg image conversions,
   eight cached packets and sixteen converted planes. Image identity includes
   provider/account namespace and paths/tags. Same-image concurrent requests
   share work; failures are not retained as complete packets, successful planes
   remain reusable, and source/account changes invalidate reuse.
6. **Local ffprobe reuse.** Metadata parses JSON once. Metadata and subtitle-codec
   probes share successful results by canonical path, device/inode, size,
   nanosecond mtime/ctime and probe arguments. At most 32 results (each below
   256 KiB of stdout+stderr), eight jobs and a 120-second result TTL. Remote/live
   sources and failed probes bypass storage. The separate metadata response cache
   is bounded to 128 entries and now checks file identity before reuse.

Docker and Home Assistant ship identical Python sources in server **0.1.48**.
The PSP build was produced before targeted validation. The affected artwork,
cache, TV layout/ownership, metadata, subtitle and browser-worker checks pass.
These are correctness checks, not PSP timing measurements. No preset sweep.

### Packet-copy review (item 4)

- `timed_stream.h:timed_put()` allocates an aligned, owned compressed packet.
  The reader reuses both `body` and `annexb` on the next tag, so storing pointers
  to either buffer would corrupt queued packets. `timed_get()` already transfers
  ownership without copying bytes and clears the queue slot before reuse.
- Audio copies into the codec's fixed input buffer, then frees the queue packet.
  Decoder/cache synchronization and PCM output buffer lifetimes remain unchanged.
- Video currently goes FLV/AVCC → `flv_annexb()` → owned queue packet →
  `h264_hw.c:make_avcc()` → aligned decoder workspace. The format round trip is
  a genuine future candidate, but removing it requires explicit configuration
  (SPS/PPS) ownership, access-unit validation and queue/decoder API changes.
- First measure copy/allocation/format-conversion cost against AVC/CSC and GPU
  waits. If worthwhile, design an owned AVCC access-unit path and validate both
  LCD/TV, seeking, EOF, reconnect and stop. Do not remove DMA/cache barriers or
  replace queue ownership with borrowed reader pointers.

## Remaining candidates (not measured speed guarantees)

### MilkDrop

7. **Longer prepared expression blocks and safe loop coverage.** Current prepared
   pairs are intentionally small. Larger blocks could reduce dispatch further,
   but need precise fuel, branch, source-line and scheduling behavior.
8. **Pure expression-tree reuse.** Beyond the implemented scalar-function cache,
   dependency tracking could reuse larger frame-invariant subexpressions. Must
   account for assignments, per-point inputs, q/t state, memory and random calls.
9. **Geometry/renderer batching and resumable expensive work.** Profile custom-wave
   calculation, clipping/submission and expensive nested searches before deciding
   between math batching and bounded continuation across frames. Continuation can
   affect visual timing and is a significant change, not merely a higher limit.

## Next measurement session

PSP profiling is explicitly deferred until the next session. Compare identical
presets, resolution, clock and audio source; include Wave-Budget, Cauldron and a
memory-heavy preset. Separate formula, geometry, GPU wait, UI and network costs.
Use these observations to choose among the candidates above. Live dual-preset
transitions remain deferred until sufficient CPU/RAM headroom is demonstrated.

Before measuring, verify on PSP: TV idle animation and navigation with artwork;
successive episodes of one Plex/Jellyfin series versus a different series; image
request cancellation; and normal LCD/TV music/video start/stop. Install both
EBOOT.PBP and PSPStreamer.prx and update the server for v2 artwork reuse.
