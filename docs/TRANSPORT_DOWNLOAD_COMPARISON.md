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

- `download timing`: body receive-call time/count, idle returns (`-2`), longest
  receive call, card-write time/count and longest write. Receive time includes
  polling, transport work and waiting; it is not pure USB bus time.
- `download timing final`: setup, body, receive, write and file-close times.
  Body wall time also includes logging/scheduling and other work. Do not add it
  to its component times. Subtract successive cumulative reports for intervals.
- `download USB socket`: only this socket's speculative groups, payload bytes,
  short groups and blocked finish time, plus existing ESP occupancy samples.
  Empty/full samples are snapshots, not percentages of elapsed time. Short groups
  can include EOF/busy replies. No additional USB requests are made by reporting.
  Finish time can be charged while another RPC drains this socket's queued read;
  it overlaps receive/storage work and must not be added to their totals.
- `download hash timing`: file bytes, total verification wall time, card-read and
  SHA-256 update time, cancellation/read-error state. This is timing, not a report
  that the expected digest matched; normal download validation remains authoritative.

No ESP update is required. Disabled debugging skips hot-path timer reads/counters.
This first attribution pass separates storage/hash costs from transport waiting;
it does not yet measure ESP task CPU, DMA gaps, radio retries or server delays.
Only instrument those layers if the result points there. The small logging/timing
cost is not assumed to be zero; compare the resulting overall throughput as well.

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
