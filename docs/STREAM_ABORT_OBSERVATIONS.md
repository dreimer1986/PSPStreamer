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
