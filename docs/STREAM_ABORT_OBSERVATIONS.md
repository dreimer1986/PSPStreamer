# Stream-abort observations

Latest card logs inspected during the VBR/frame-rate implementation:

- Result `-1320` (`FFFFFAD8`), FLV tag body, read inactivity timeout.
- Requested body: 5965 bytes; received before failure: 5749 bytes.
- Last receive result: 1440 bytes; no reported socket errno or poll error.
- Last data tick: 467908 ms; failure tick: 472918 ms; gap: 5010 ms.
- Last received video/audio PTS: 99125 / 99161 ms.
- AP control state: 4 (GOT_IP). This alone does not prove an operational
  end-to-end TCP path.
- TV sync trace: 1920 displayed frames, zero dropped frames, zero PTS errors.
  Last periodic sample: video 98125 ms, audio block 98116 ms, eight queued
  audio blocks. These are internal timestamps, not a measurement of TV latency.
- Watchdog: startup, monitor running, monitor stopped; no stall report.

The immediate cause is a five-second receive gap inside a packet, not evidence
of a timestamp parser failure. The logs cannot distinguish hotspot/network
stalling from a blocked encoder or server. The user reports the PSP lies next
to the access point. No timeout, recovery, audio-clock or synchronization
changes were made as part of the stream-options update.

## Following completed episode

The next TV trace reports `result=30302` (positive frame count, not a failure),
29,350 displayed frames, six dropped frames and zero PTS errors. The final
periodic sample reaches video PTS 1,263,162 ms. The user reports reaching the
episode's end with short pauses. The error snapshot is unchanged from the
previous failed run; successful playback does not erase it.

Watchdog entries with heartbeat ages 4294967283 / 4294967295 are unsigned
underflows: a concurrent heartbeat update can become newer than the worker's
previously sampled clock. Progress age is zero and queues are populated in
those entries. They are not evidence of multi-day stalls. This diagnostic
race should be fixed separately; playback timeouts and scheduling remain
unchanged. The periodic trace cannot identify the cause of every brief pause.

## 2026-09-16: partial FLV body delivery gap

The new error snapshot reports `result=-1320`, `FLV tag body`, 4520 of
9722 bytes received, and `gap_ms=5012`. AP state is 4; this confirms an
associated/IP-configured connection, not end-to-end delivery. Remote polling
also had no recent success. The trace reaches video PTS 181164 and audio PTS
181185 ms; it does not implicate timestamp parsing or prove a radio problem.

The playing-state inactivity budget is now 30 seconds rather than five.
This is a bounded tolerance, not a diagnosis or cure for the underlying gap.
Already received packet bytes remain intact. Each successful read renews the
budget; genuine EOF/socket errors still fail immediately. The startup/subtitle
budget remains 180 seconds. Stop can still cancel through the 100 ms poll loop;
no decoder reset, clock adjustment or audio replay was introduced.
If buffers empty, playback may pause until data returns; a dead TCP connection
is not transparently recreated by this change.

Host tests cover recovery after an eight-second gap inside a packet, the
30-second playback and unchanged 180-second startup limits, cancellation,
partial EOF and socket errors. A real PSP/long-stream test remains necessary.

The watchdog now snapshots the heartbeat before reading the clock, preventing
a concurrent newer heartbeat from producing a huge unsigned age and spurious
Memory Stick writes. Other stall reporting remains enabled.
