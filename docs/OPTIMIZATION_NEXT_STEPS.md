# Optimization status and remaining work

## Hardware feedback and closed test items

The user confirmed the former ToDo items 4–7 as tested: raster-variable
optimization, sparse state copies, the server 0.1.48 artwork/TV/UI batch and
the direct AVCC path. They remain functional, without a major additional
perceived speed increase. Those test reminders are closed and removed from
the active ToDo; no numerical PSP speedup is inferred from this feedback.
The latest Cave topology/lighting/camera build is also hardware-confirmed.
The candidates below are optional further work, not unfinished validation of
these implemented changes.

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

### Direct AVCC packet path (item 4, playback confirmed on hardware)

The user confirmed film playback on 2026-09-23 and subsequently closed the
associated ToDo test item. No per-scenario timing measurements were supplied.

- Video now goes from FLV length-prefixed NAL units straight to an aligned,
  owned `AvcPacket`, then to `sceMpegGetAvcNalAu`. `avcc_packet.h` validates the
  complete sample before allocation, normalizes 1/2/4-byte lengths to four bytes
  while copying once, and omits AUD. SPS/PPS (including in-band replacements)
  are stored in each packet's configuration snapshot, not shared reader state.
- `timed_put_video()` owns the packet until queue handoff. `timed_get()` transfers
  ownership, and the video owner keeps it through decode/presentation before
  freeing it. Reader buffers may be reused immediately. Queue cancellation,
  codec locks, PTS scheduling, firmware modes and cache barriers are unchanged.
- Removed both Annex-B conversion passes, two full payload copies, the 256 KiB
  reader conversion buffer and the 768 KiB decoder conversion buffer. A packet
  header occupies 576 bytes, including alignment and its configuration; the
  queue still has a 128-packet bound. This is repackaging, never re-encoding.
- Both streamed and local FLV, on LCD and TV, use the same path. No server,
  configuration or existing downloaded-file changes are required. Audio still
  uses the established compressed queue and codec buffer.
- Targeted checks cover NAL lengths, malformed/maximum samples, parameter
  ownership, actual queue wrap/cancel/teardown and mocked firmware pointer/cache
  contracts. Short real FFmpeg files check container PTS, Main/CABAC payloads and
  seeking on LCD/TV profiles. Actual Media Engine operation, reconnect, stop and
  episode transitions remain hardware checks, not claims from host mocks.
- General packet-allocation pooling remains a separate profiling candidate.

## Remaining candidates (not measured speed guarantees)

The live-transition follow-up removes the second expanded warp mesh from the GU
list and blends unique grid points (289 instead of 1,536 corners). Audio capture
uses the requirements of both presets, avoiding stereo FFTs on PCM-only fades.
Exact geometry comparisons and focused renderer tests pass; no PSP speedup is
claimed before hardware feedback. See [details](MILKDROP_LIVE_TRANSITIONS.md).

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
transitions now have an optional shaderless implementation: two independent
formula states, blended warp grids and weighted geometry on shared feedback.
The outgoing context is about 937 KiB plus bytecode/assets, without another
framebuffer. Allocation failure keeps the snapshot fallback. Hardware evaluation
of visual rate and uninterrupted audio during heavy overlaps remains necessary.

The previous artwork/navigation and normal playback validation checklist has
been closed by the user's test confirmation. Further profiling is optional;
there is no outstanding request to repeat that checklist.
