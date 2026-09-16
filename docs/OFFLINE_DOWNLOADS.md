# Offline transport and ownership

The server's `OfflineQueue` owns a persistent, bounded list of jobs and one
conversion worker. Each job snapshots track numbers and encoding options.
It acquires an existing transcode slot and calls the same Main/CABAC command
builder as streaming, removing only the input real-time throttle. Streaming
commands, clocks, codec initialisation and timeout policy are unchanged.

`PSP_STREAMER_DOWNLOAD_DIR` overrides the cache directory. Defaults are
`/data/downloads` in both container variants and
`~/.cache/psp-streamer/downloads` for a native server without a settings path.
Job JSON is atomically replaced. Interrupted conversions restart after server
restart; completed files retain their job ID and immutable download content.
The queue resumes in creation order. Error/cancelled jobs remain visible until
deleted; create a new job to retry a failed conversion.

## Authenticated API

- `POST /api/offline/jobs`: JSON `id`, `audio`, `subtitle`, `audio_quality`,
  `video_fps`, `profile` (`normal`, `low`, `tv`). Full video from the beginning.
- `GET /api/offline/jobs`: web list of states and conversion progress.
- `GET /api/offline/catalog`: bounded tab-separated PSP catalogue.
- `GET /api/offline/job/<id>`: compact JSON manifest, including ordered files,
  byte lengths and SHA256 hashes when ready.
- `GET /api/offline/file/<id>/<number>`: immutable file, optional
  `Range: bytes=N-`; returns 206 with Content-Range or 416 for invalid ranges.
- `POST /api/offline/cancel`, `POST /api/offline/delete`: JSON `job` ID.
  Cancel active work before deleting it. Deletion never addresses source media.

POST uses the same JSON/origin checks as the remote and settings APIs; every
endpoint uses the existing password/TLS policy. The IDs are server-issued UUID
hex strings. The client never supplies a cache path.

## PSP state and files

The offline UI and its worker are active only outside playback. The worker
owns HTTP/TLS sessions, hashing and file writes. Its UI cancellation flag is
checked during connection, association, receiving, waiting and hashing. A
network failure keeps the partial file; explicit reselection reconnects and
requests the remaining bytes. The worker validates status, Content-Length,
resume Content-Range, final length and SHA256 before renaming a `.part` file.
The final `ready` marker is written only after all three files pass. A failed
verification removes only the invalid partial, so the next attempt can retry.

Each PSP directory `ms0:/PSP/VIDEO/PSPStreamer/<job-id>/` contains:

- `<source-stem>.flv`: complete H.264/MP3 media with original container PTS.
- `subtitles.ovl`: `OVL1`, little-endian uint32 JSON length, compact UTF-8 cue
  JSON, followed by palette+pixel blocks in cue order for LCD PGS overlays.
  Text cue JSON is identical to the existing millisecond subtitle endpoint.
  TV bitmap burn-in stays server-side, with an empty text-cue package.
- `seek.idx`: repeated little-endian uint32 timestamp/uint32 file-offset pairs
  for AVC keyframes. Positions point at FLV tag headers.
- `job.json`, and `ready` after successful verification.

Local playback uses the actual `timed_reader` with file reads in place of
socket reads. Seek reloads AVC configuration from the start, then jumps to the
nearest preceding indexed keyframe. Audio/video retain their absolute PTS;
subtitle lookup uses that same source timeline. No remote worker starts during
local playback. LCD/TV output must match the preselected conversion profile.

No import of arbitrary local files, shader changes, automatic background
downloads during playback or network-dependent local subtitles are included.
Deletion is confined to the selected managed job's known files and directory.

## Validation

`tests/test_offline.py` exercises real FFmpeg conversions, subtitle variants,
range responses, cancellation, persistence, origin checks and PGS packaging.
Native harnesses compile the actual PSP download/FLV-reader functions with
POSIX file/socket shims. They test partial reads, range resume, seek decoding,
low space, damaged complete-length partials, hash validation and ready-marker
ownership. With `MBEDTLS_HOST_SOURCE` and `MBEDTLS_HOST_BUILD` pointing at a
host mbedTLS 2.28 build, the complete PSP download worker is also executed.

These checks do not replace real PSP Memory Stick, WLAN, decoder and TV-out
tests. Start with a short video, then test two subtitle/language variants,
offline launch with R held, download interruption/resume, seek, Stop and
streaming playback after returning from the local library.
