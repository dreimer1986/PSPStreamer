# StreamMaster: complete currently identified optimization inventory

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
