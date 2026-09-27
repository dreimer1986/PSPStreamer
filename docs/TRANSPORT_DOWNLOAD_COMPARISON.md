# Transport measurement status

Firmware 0.2.3's larger buffers and shorter waits achieved a completed download
of 170,480,059 bytes in 552.635 s: **301.26 KiB/s**. The 107 recorded five-second
intervals had median 328 KiB/s, maximum 350 KiB/s and minimum 30 KiB/s. Nine were
below 200 KiB/s. Evidence: recovery log, completion tick 1055825. USB error count
remained at one from an earlier operation, with no further increase during download.

The subsequent PSP-only asynchronous read-ahead update completed 170,598,447 bytes
in 516.684 s: **322.44 KiB/s**. Its 100 recorded five-second intervals had median
338, maximum 357 and minimum 171 KiB/s; only one was below 200 KiB/s. No USB errors
were recorded. This is approximately 7% above the preceding large-download result,
though the files were not byte-identical.

Firmware **0.2.4** and the matching PSP app/bridge now implement two outstanding
8 KiB replies and reduced copying. Confirmed with `bulk=1`: 171,481,659 bytes
in 399.320 s = **419.37 KiB/s**, interval median 446 and maximum 462 KiB/s,
minimum 240, zero USB errors. This is 26.3% above the preceding compact-only
332.15 KiB/s run of a nearly equal-sized file. Post-body completion took 123.319 s.
Compare the same ready file over HTTP, with the same hub, card and clock; confirm
`bulk=1` in diagnostics. Remaining avenues are listed in
[the optimization inventory](STREAMMASTER_OPTIMIZATION_INVENTORY.md).

Do not compare playback demand or USB echo throughput with file-download speed.

## Download bottleneck instrumentation

With application debugging enabled, file downloads emit cumulative counters every
five seconds and a final summary (also on cancellation/failure):

- `download timing`: body receive-call time/count, idle returns (`-2`), blocked
  write-wait time/count, buffer size and async mode. Receive time includes
  polling, transport work and waiting; it is not pure USB bus time.
- `download timing final`: setup, body, receive, write and file-close times.
  Body wall time also includes logging/scheduling and other work. Do not add it
  to its component times. Subtract successive cumulative reports for intervals.
  After the async-storage update, `write_ms` and `write_max_us` are observed
  submission-to-reaping lifetimes, overlapping reception (and potentially including
  completed work not yet reaped). They are not exclusive card-service times.
- `download storage final`: `wait_ms` is the time actually blocked draining a
  write, `lifetime_ms` includes overlap, `committed` is successful completed writes,
  `received` also includes any uncommitted tail, `chunk` is the selected buffer
  size and `async=0` indicates synchronous fallback.
- `download USB socket`: only this socket's speculative groups, payload bytes,
  short groups and blocked finish time, plus existing ESP occupancy samples.
  Empty/full samples are snapshots, not percentages of elapsed time. Short groups
  can include EOF/busy replies. No additional USB requests are made by reporting.
  Finish time can be charged while another RPC drains this socket's queued read;
  it overlaps receive/storage work and must not be added to their totals.
- `download hash timing`: file bytes, total verification wall time, `read_wait_ms`
  (first synchronous read plus waits for prefetched reads) and SHA-256 update time,
  cancellation/read-error state, async mode and chunk size. This is timing, not a report
  that the expected digest matched; normal download validation remains authoritative.

No ESP update is required. Disabled debugging skips per-network-read diagnostics.
This first attribution pass separates storage/hash costs from transport waiting;
it does not yet measure ESP task CPU, DMA gaps, radio retries or server delays.
Only instrument those layers if the result points there. The small logging/timing
cost is not assumed to be zero; compare the resulting overall throughput as well.

## Storage pipeline update (hardware comparison pending)

The instrumented synchronous baseline transferred 170,555,495 bytes in 420.256 s.
Writes consumed 63.736 s, with a longest write of 1.511 s. The longest receive call
was 0.147 s; the ESP ring was full in 75/87 download-specific snapshots. Verification
took 120.974 s: 41.385 s reading and 79.275 s hashing. These snapshots do not prove
an exact stall frequency or distinguish card, adapter and filesystem behavior.

- During file transfer/verification only, recovery logs collect in a bounded 4 KiB
  RAM batch, normally persisted by the worker every five seconds and at completion,
  failure or cancellation. Other workers append without card I/O. Overflow/contention
  drops are explicitly counted; the last unflushed batch may be lost on power loss.
  Outside that phase, immediate logging is retained. A stalled filesystem can still
  delay persistence beyond five seconds. This is not a hard real-time guarantee.
- Two 512 KiB download buffers overlap network reception with a single outstanding
  asynchronous file write. Allocation falls back down to two 32 KiB buffers; unsupported
  async submission falls back to synchronous writes. Buffer size is logged. No buffer
  or descriptor is released until its outstanding operation has completed. Cancellation
  drains that write, discards the unsubmitted tail and resumes from actual file length.
- Verification reads ahead into two 128 KiB buffers while Mbed TLS hashes the current
  one. SHA-256 itself, the expected server digest, full read-back and resume validation
  are unchanged. No hashing of only network data and no custom crypto primitive.
  Allocation failure retains the 16 KiB synchronous stack-buffer path. Hash API errors,
  read failures and cancellation still fail validation.

This targets overlap and fewer filesystem operations, not a guaranteed CPU hash speedup.
Test the same ready file through verification, then cancel a second transfer and resume.
Compare overall duration, storage wait, remaining read wait and responsiveness.

| Route | Available measurement | Actual media-download throughput |
| --- | --- | --- |
| StreamMaster 0.2.2 O2 | Previously recorded USB echo: 429 KiB/s; HTTP playback intervals about 105–125 KiB/s | Not measured yet |
| StreamMaster 0.2.2 O3 | Previously recorded USB echo: about 426 KiB/s | Not measured yet |
| PSP internal WLAN | New September 27 file transfers, details below | 245.43 / 237.47 KiB/s |
| StreamMaster, firmware optimization variant not identified in log | New September 27 file transfers, details below | 82.20 / 92.65 KiB/s |

## September 27 actual downloads

| Route | Bytes | Request duration | KiB/s |
| --- | ---: | ---: | ---: |
| Internal WLAN | 1,320,445 | 5.254 s | 245.43 |
| Internal WLAN | 1,192,736 | 4.905 s | 237.47 |
| StreamMaster | 1,048,312 | 12.454 s | 82.20 |
| StreamMaster | 1,438,194 | 15.159 s | 92.65 |

Combined byte/time averages: internal 241.59 KiB/s, external 87.94 KiB/s.
These are different small files, not a controlled same-file maximum-throughput test.
All four returned HTTP 200 and completed successfully. Internal evidence:
`PSPStreamer-recovery.txt.history-00E31C758A0DE99F`, ticks 128905/171315.
External evidence: current recovery log, ticks 184556/231488.

External USB counters remain `err=0` during these transfers. ESP receive-buffer
samples are predominantly empty, but aggregate several sockets and cannot isolate
the download socket or prove the cause. The user's initial ~250 KiB/s observation
is not separately captured by the request-level timing. Earlier USB attach failures
belong to the user's direct-cable/no-hub trial, not these completed downloads.

A partial file download is sufficient: divide the `download HTTP` media-body
`bytes` count by its `ms` duration, then multiply by 1000/1024 for KiB/s.
Do not use conversion percentage or status-JSON transfers for this calculation.
The duration includes connection/header setup and Memory Stick writes; this is
end-to-end request throughput, not raw radio bandwidth.
