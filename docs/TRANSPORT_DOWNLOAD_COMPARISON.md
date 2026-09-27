# Transport measurement status

Firmware 0.2.3's larger buffers and shorter waits achieved a completed download
of 170,480,059 bytes in 552.635 s: **301.26 KiB/s**. The 107 recorded five-second
intervals had median 328 KiB/s, maximum 350 KiB/s and minimum 30 KiB/s. Nine were
below 200 KiB/s. Evidence: recovery log, completion tick 1055825. USB error count
remained at one from an earlier operation, with no further increase during download.

The subsequent PSP-only asynchronous read-ahead update is ready for comparison
against this 301.26 KiB/s baseline. No additional speedup is claimed until measured.

Do not compare playback demand or USB echo throughput with file-download speed.

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
