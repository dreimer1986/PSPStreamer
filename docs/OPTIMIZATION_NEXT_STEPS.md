# Prepared execution and remaining optimization work

## Implemented in this step

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

## Remaining candidates (not measured speed guarantees)

### Application outside MilkDrop

1. **Remove duplicate TV presentation in the library.** `show()` calls
   `tv_draw_view()`, which presents a complete frame, then unconditionally calls
   `tv_present()` again. Preserve the second presentation only where a subsequent
   TLS/network notice actually changes the frame, or compose once and present once.
   Each avoided presentation saves a 1,474,560-byte copy and a VBlank wait.
2. **Dirty-region updates for idle menus.** TV library currently redraws every
   150 ms outside artwork downloads. Reuse the existing music dirty-region
   approach for indicators/selection/status without redrawing the whole skin,
   font layout and backdrop. More involved than item 1; preserve mode ownership.
3. **Reuse identical series artwork across episode selections.** The PSP keys its
   image by media selector; the server's PSP packet cache also keys by original
   episode before resolving the series parent. Canonical image identity and a
   small bounded client reuse policy could avoid identical 130-KiB downloads.
4. **Review packet ownership/copies after profiling.** `timed_push()` copies packet
   bytes into its queue and `flv_annexb()` creates the decoder format. Some copying
   is necessary for lifetime, NAL framing and DMA/cache ownership. Do not introduce
   zero-copy or remove cache synchronization blindly on this stable path.

### Server (Docker and Home Assistant together)

5. **Coalesce artwork preparation.** `Artwork.psp()` releases the cache lock before
   downloading/converting, so concurrent cold requests can prepare the same image
   twice. Share one bounded in-flight job, cache converted planes by image identity
   and limit concurrent FFmpeg image conversions. Keep auth/namespace isolation
   and failure retries intact.
6. **Targeted metadata/probe deduplication.** Local metadata already has a cache;
   repeated JSON parsing in `metadata()` is a small cleanup, not a major FPS win.
   Audit other ffprobe/subtitle calls for reusable results keyed by file identity
   and modification time. Do not cache changing/live sources indefinitely.

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
