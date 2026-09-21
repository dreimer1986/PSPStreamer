# Episode-end liveness and text subtitle paging

The captured stall had 128 queued video packets, zero audio packets, and all
13,798 published PCM blocks played. The shared FLV reader could not enqueue
more video; video waited for the exhausted audio clock; audio waited for that
same reader to reach another audio packet or real EOF. Remote control remained
responsive. This was not evidence of a WLAN timeout.

The client now detects reader backpressure together with an empty compressed
audio queue, an audio decoder awaiting input, and a fully played PCM queue.
Only in that state, after playback has started, video continues on a monotonic
clock anchored to the end of the last submitted audio block. A new submitted
audio PTS restores audio-master synchronization. Pauses freeze the continuation
clock. This does not manufacture EOF: the reader must reach the real end before
normal automatic successor handling runs. No audio replay, FPS estimate, codec
reset or network-timeout change is involved.

Text subtitles use the same server endpoint for mounted files, Plex and
Jellyfin. The complete extracted timeline is normalized into disjoint intervals,
preserving the existing first-active-cue display policy. Identical adjacent text
is merged. The client requests `page=1&timebase=ms&at_ms=...` initially, then
`offset=...` for subsequent pages. Each response contains at most 256 cues,
`next` (-1 at end), and `until` (next page start). Cue text remains bounded to
159 UTF-8 bytes without splitting a character.

Two page banks use approximately 86 KiB, replacing the old 960-cue allocation
for online text. The existing control worker prefetches within 30 seconds of a
boundary, using its own control connection and the now-unused preparation
response buffer. The render thread performs only a bank switch. A failed page
request retries after two seconds without interrupting video/audio; late pages
can temporarily leave subtitles absent. Stop joins the worker before freeing
the banks. Seeking restarts with the target timestamp, avoiding stale pages.

Legacy clients retain capped responses; bitmap subtitles are unchanged. Offline
OVL1 files remain self-contained: new conversions with more than 960 normalized
text intervals burn in the selected track. Existing downloads are not rewritten.

Focused host regressions cover 15,000 cues, overlaps, UTF-8, seek boundaries,
shared source/cache behavior, the actual C page parser and drain-clock helper,
and the offline fallback. PSP verification must check long subtitles across page
boundaries and automatic progression after a previously affected episode.
