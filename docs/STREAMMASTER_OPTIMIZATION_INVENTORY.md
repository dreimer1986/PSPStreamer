# StreamMaster: complete currently identified optimization inventory

## Current decision: 0.2.9 rollback

The new hardware run transferred 170,143,443 bytes in 403.719 s:
411.563 KiB/s, versus 479.480 KiB/s for the previous same-sized run
(346.533 s). Current recovery log, completion tick 1129256; comparison history
`00E31C993E3527AF`, tick 416333. This is about 14.2% slower, not an improvement.
Receive-call time is 399.490 s; blocked media writes only 3.686 s. Final
verification succeeds in 82.459 s (hash 56.544 s).

Bulk read-ahead accounts for 121,748,652 bytes versus previously 158,967,731.
The remaining bytes are not covered by those bulk-only counters. Checksum work
is 2.042 s for that bulk subset, so it does not explain the approximately
57-second body-time increase. The logs do not prove a cause or whether the
last code change versus other timing/network effects triggered it. No speculative
scheduling or retry changes are justified by this evidence.

At the user's request, both 0.2.8 hot-loop changes are reverted exactly to the
0.2.7 source on PSP/ESP; the new package is version 0.2.9 to identify the rollback.
All earlier gains are retained. Restored throughput still needs a hardware
comparison; it is not guaranteed by reverting these two changes.

## Implemented follow-up: 0.2.8 test build

- Same-wire FNV-1a now handles four bytes per loop iteration on ESP and PSP.
  Scalar-reference tests cover alignment, tails and bulk-length boundaries.
- Contiguous socket ring copies no longer make an empty second `memcpy` call;
  wrapped transfers remain identical (33,024 focused boundary cases).
- No buffer/profile defaults, memory ownership, PSRAM placement, task priorities,
  timeouts or recovery rules changed. Firmware/app/USB bridge remain compatible
  with their previous protocol-v1 counterparts. Hardware throughput is unmeasured.
- The remaining candidates below require bottleneck evidence or affect timing,
  memory/lifetimes, security or recovery. They are not regression-free and were
  deliberately not included in this low-risk follow-up. In particular, there is
  no blind switch to deeper/larger queues and no new zero-copy/DMA protocol.

## September 29 follow-up: what is still worth doing?

This review changes documentation only, not firmware or transport behaviour.
The older measurements/status notes below are historical snapshots. The user has
confirmed the subsequent download/SHA work; no new controlled throughput run was
performed for this review. There is no risk-free implementation change: “low risk”
means a narrow, reversible change with equivalent outputs and focused tests.

The source already has pointer reply queues, two internal 8 KiB reply buffers,
asynchronous PSP storage/logging, read-ahead and batched SHA. Importantly,
`network_worker()` already calls `usb_host_client_unblock()` after publishing a
reply. The USB loop's 20 ms event timeout is **not** an unavoidable 20 ms delay
per reply. Reimplementing these completed changes is not an optimization.

One accessible completed 479.472 KiB/s run (346.539 s body, log history
`00E31C94DE1D036D`, completion tick 838592) attributed approximately 1.196 s to the
ESP DMA staging copy, 2.882 s to checksums and 9.253 s to ring copying. These
overlap other work and cover the measured bulk window, not exclusive wall time.
Even completely removing the measured DMA copy would account for only about
0.35% of that body's wall time; checksum time is about 0.83%. This does **not**
prove a corresponding speedup, nor that today's critical path is unchanged.

| Remaining option | Recommendation | Risk / complexity |
| --- | --- | --- |
| Same-file attribution using existing counters | Best prerequisite; compare HTTP, same card/clock, body versus final verification separately | Low risk; no production change needed |
| Existing block/depth profiles | Compare 8×2 with 8×4 first, keeping hot replies internal; larger 16/32 KiB profiles only as explicit comparisons | Low implementation effort, but runtime RAM/control-latency risk; not a blind new default |
| FNV hot loop, same bytes and exact result | Small, bounded candidate if profiling justifies it; keep format and corruption checks identical | Low–medium risk; likely small benefit; protocol and malformed-frame tests required |
| Diagnostic formatting/UI frequency | Only remove demonstrated redundant work; background logging is already implemented | Low–medium risk; preserve cancellation, useful diagnostics and audio fairness |
| HTTP/server delivery and reuse | Useful when source/SMB stalls or many small requests dominate; not when the ESP receive ring is already full | Low–medium risk; reuse requires lifecycle/auth/reconnect checks |
| AP/channel/link conditions | Worth checking when the ring empties; no PSP protocol change | Low code risk; environmental changes still need comparison |
| Receive bursts, CPU affinity and task priorities | Only if worker scheduling is measured on the critical path; current idle yield and USB wakeups must survive | **Complex**, medium–high risk: watchdog, control and socket fairness |
| Hot internal RAM/IRAM placement | Target individual measured hot objects/functions; retain internal hot replies | **Complex**, medium risk: scarce contiguous DMA/internal RAM; no general PSRAM expansion |
| TCP/lwIP/Wi-Fi buffer balance | Only if radio/TCP starvation remains; current window is 32 KiB and receive mailbox 26 | **Complex**, medium risk across six channels and total RAM budget |
| Fuse ring copy and checksum | Could remove a memory pass without changing the protocol, but only worthwhile if cache/memory cost dominates | **Complex**, medium risk: wrap boundaries, locks, exact checksum order |
| Direct-to-DMA reply ownership | Removes the remaining staging copy, but the measured cost is small | **Complex**, high risk: callbacks, detach, cancellation, buffer lifetime; low priority |
| Continuous credit-based receive | Most substantial remaining transport redesign; can remove request/group gaps rather than micro-optimize copying | **Complex**, very high effort: bounded flow control, multiple channels, fairness and recovery |
| PSP shared/pinned zero-copy buffers | Only after proving kernel/user copies dominate | **Complex**, very high risk: cache coherency, DMA and buffer ownership; not recommended now |
| Negotiated replacement checksum | Only if exact-compatible FNV work is insufficient and checksums genuinely dominate | **Complex**, medium–high risk: version negotiation and equivalent detection; not recommended for current timings |
| TLS record buffering/session reuse/crypto | Relevant to HTTPS only; separate handshake/startup from sustained body cost | **Complex**, medium–high risk; must retain certificate verification; no HTTP gain |
| PSP storage chunk/alignment tuning | Investigate residual write stalls, preserving completed-prefix resume; overlap already exists | Medium–high risk; a third buffer cannot make the physical card faster |
| Further SHA/read-back tuning | Improves finishing/verification time, not USB body throughput; latest rolling schedule remains | Low–medium risk for exact-output hot-loop work, **complex** for pipeline changes; do not skip read-back |
| Targeted O3/LTO/PGO | Only on measured hotspots; previous firmware O3 comparison did not establish a win | **Complex**, medium risk/build effort; code size can negate gains |
| Existing PSP clock policy | Controlled comparison at already proven settings, not new PLL experiments | Runtime stability/power risk; not a free optimization |
| Pipelined PSP uploads | Useful only for a future reverse-direction feature | **Complex**, medium–high effort; no current download benefit |
| Parallel ranges/multiple downloads | Shared USB/card bottleneck makes benefit doubtful | **Complex**, high effort and resume/order risk; defer |

Recommended next step, if speed work resumes: one same-file counter comparison,
then existing 8 KiB depth tuning or exact-output FNV work **only if indicated**.
There is no compelling measured reason to destabilize buffer ownership for a
sub-percent staging-copy target. Credit-based streaming remains a separate,
optional larger project, not a promised increase or part of the current build.

---

This is the complete set of avenues identified during the September 27 review,
not a claim that future measurements cannot reveal another bottleneck. Potential
is conditional: no percentage gain is promised for unmeasured changes. Items below
are not additional unfinished requirements of the 0.2.4 implementation.

## Completed and measured baseline

1. Two actual outstanding read requests, rather than only overlapping one request
   with application work. ESP receives a second request during the first response.
2. Negotiated 8 KiB replies, two per group (16,320 payload bytes), instead of
   4 KiB replies (4,064 payload bytes). Small/control transactions stay compatible.
3. Kernel-to-mailbox delivery, ownership swapping instead of mailbox-to-cache
   copying, exact-length compact copies, header/payload-only bulk initialization.
   Larger replies amortize header/checksum setup; payload integrity remains checked.

Confirmed 0.2.4: 419.37 KiB/s over 171,481,659 bytes, `bulk=1`, zero USB errors.
Download-only diagnostic batching, double-buffered async card writes and prefetched
read-back verification raised the measured body rate to 483.324 KiB/s, with
1.544 s blocked write wait. Hashing still used 82.860 s of the 105.530 s verification.
The files were similar-sized, not byte-identical; see the comparison document.

## Current follow-up: PSP-side diagnostic I/O

0.2.6 recovered USB startup but dropped to 340.876 KiB/s; hash CPU time improved
to 65.241 s. 0.2.7 replaces full-size PSRAM reply queues with pointer queues and
two internal 8 KiB workspaces. Larger reply/DMA buffers are only created when
requested. Two 8×2 hardware runs recovered 464.091 and 479.472 KiB/s, respectively,
against the earlier 483.324 KiB/s best (similar-sized, not identical files).
The latest run still has brief stalls: 5.689 s blocked media writes and 15.720 s
outside receive/write-wait counters. PSP logging now uses its own bounded writer;
new submission/reporting/log-I/O timings separate these costs. The asynchronous-log
build achieved 485.279 KiB/s with successful verification and no USB errors.
Transport/logging optimization is paused at the user's request. Only the 16-word
SHA schedule change now awaits comparison: baseline hash CPU 66.672 s,
verification total 95.727 s. Firmware remains 0.2.7.

- Offline-only SHA-256 batch compression, identical digests checked against Mbed
  TLS; full card read-back retained. Compare hash time separately from USB speed.
- Negotiated 8/16/32 KiB × 1/2/4 profiles (maximum ~64 KiB/group); 8×2 remains default.
- ESP queue/worker/copy/checksum/transfer/gap timings, logged as download-window
  deltas. These attribute overlapping costs; they do not measure radio retries.

Earlier baseline: 0.2.3 plus single-request read-ahead transferred 170,598,447 bytes in
516.684 s = 322.44 KiB/s. Interval median 338, maximum 357 KiB/s; one of 100
recorded intervals below 200 KiB/s. This is the comparison target, not a limit.

## Remaining avenues on current hardware

| Area | Concrete option | Potential / cost / constraint |
| --- | --- | --- |
| Attribution | Compare PSP submission/reporting/background-log timings; add server/radio attribution if indicated | 0.2.7 throughput recovered; asynchronous diagnostic writer hardware comparison pending |
| Block/depth tuning | Compare implemented 8/16/32 KiB and 1/2/4 profiles | Conditional; more RAM and longer control/cancellation latency. All supported profiles are now selectable; hardware comparison pending |
| Continuous receive | Credit-based push/ring transfer instead of request/response polling | Potentially meaningful; very high effort; new flow control, fairness and recovery protocol |
| DMA/queue ownership | Measure implemented pointer queues/internal reply pool; possible future direct-to-DMA pool or scatter/ring delivery | Pointer queues implemented in 0.2.7; removing the remaining DMA copy is separate high-effort work requiring callback lifetime/detach safeguards |
| PSP zero-copy | Explicitly owned/pinned shared buffers instead of kernel-to-user copy | Conditional; very high risk/effort; DMA/cache coherency and arbitrary caller lifetimes prohibit casually passing user pointers |
| Checksums | Profile FNV cost; faster implementation or negotiated CRC with equivalent corruption detection | Low–medium potential; medium effort; keep integrity checks, validate both platforms, do not simply omit checks |
| Scheduling | Core affinity, task priorities, bounded larger receive bursts, event-driven wakeups instead of polling | Conditional; medium–high effort; keep watchdog idle time and audio/control fairness |
| Memory placement | Profile hot buffers/code in internal RAM versus PSRAM, alignment/cache maintenance, eligible IRAM placement | Conditional; medium effort; scarce internal/DMA memory, not all buffers can move |
| TCP buffering | Further receive-window/mailbox/socket-buffer tuning and lwIP/Wi-Fi buffer balance | Conditional; medium effort; currently 32 KiB/26; larger is not automatically faster, especially across six channels |
| Wi-Fi/radio | AP/channel/congestion, signal/retries, driver aggregation/buffer options | Can dominate on a bad link; environment-dependent; power saving is already disabled |
| HTTP/server path | Inspect server disk/SMB/provider delivery and socket write batching; reuse connections for many small requests | Conditional; low–medium effort; keep-alive mostly helps startup/small files, not a single large body |
| TLS | Profile TLS record buffering, session reuse and supported cryptographic acceleration | HTTPS only; medium–high effort; preserve verification; no gain for HTTP measurements |
| PSP storage pipeline | Tune implemented double-buffered async writes, chunk size/alignment; investigate remaining card stalls | Overlap measured: only 1.544 s blocked write wait; further gains conditional; preserve committed offsets and resume correctness |
| Final file verification | Retain batched SHA-256/read-ahead; further hot-loop work only if worthwhile | Measured 88.234 s total, 65.241 s hashing versus 105.530/82.860 s before; full read-back integrity retained |
| UI/logging/background work | Profile rendering, diagnostic writes and remote polling during transfers | Usually modest; low–medium effort; do not sacrifice controls or blindly disable diagnostics |
| Compiler/code generation | Targeted O3/LTO/PGO or hot-loop optimization after profiling | Uncertain, generally incremental; medium effort; O3 echo comparison did not establish a win |
| PSP CPU/bus policy | Compare already proven clock settings during transfer/hash versus idle | Conditional, with power/thermal/stability costs; no new overclock settings introduced here |
| Reverse direction | Add symmetric large/pipelined writes for future PSP uploads | Separate feature; does not improve today's downloads; medium–high effort |
| Connection batching | Parallel HTTP ranges or multiple file transfers | Uncertain, often worse on a shared bottleneck; high effort; ordering, server load, storage and resume complexity |

## Physical limits and non-solutions

- The ESP32-S3 USB-OTG peripheral is **Full Speed, 12 Mbit/s raw**, not High Speed.
  Raw 1.5 MB/s is not usable application throughput; USB framing, scheduling,
  acknowledgments, software, Wi-Fi and storage reduce it. Larger frames still use
  64-byte physical bulk packets. The 429 KiB/s echo result is neither a guaranteed
  download speed nor a theoretical ceiling. [Espressif USB overview](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_otg.html).
- Reliable VBUS, cable and hub behavior matter. The user's direct cable caused
  failures; retain the working powered/hub setup. Removing a hub does not guarantee
  a speedup. A cable cannot turn this controller into a High-Speed host.
- A genuinely higher hardware ceiling needs a different USB host/controller and
  another port of the bridge (for example suitable High-Speed hardware). That is
  a hardware project, not another firmware flag for the Onju S3.
- Compressing already compressed H.264/MP3 is not a promising transport gain.
  Lowering media bitrate saves bytes but does not increase USB throughput.
- Removing checksums, abandoning cancellation, starving audio, increasing all
  buffers blindly or unsafe clock/voltage changes are not acceptable shortcuts.

## Recommended order after 0.2.4 testing

Measure the same ready file, same HTTP route, hub, card and clock. Confirm `bulk=1`.
First establish sustained rate and control/audio stability. If speed is still worth
pursuing: attribute stalls, then tune block/depth or the specific measured hot path.
Treat verification-time optimization separately from USB bandwidth. Do not promise
all remaining items will provide gains or quietly turn them into mandatory work.
