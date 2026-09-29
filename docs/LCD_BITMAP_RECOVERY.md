# LCD bitmap subtitle recovery

## Diagnosis

The captured watchdog stopped at `subtitle/overlay`, position 91,258 ms,
with a full video queue (128), no queued audio and successful remote requests.
The first German PGS cue in the affected Chillin' S01E02 starts at 91.091 s:
1920×1079 indexed pixels plus palette, 2,072,704 bytes. The old synchronous
renderer downloaded into a 512 KiB response buffer, rejected the truncated cue,
then repeated the request every frame. This is independent of O3 versus LTO.

## Fix

- Optional `lcd=1` on bitmap metadata and sprite endpoints returns display-sized
  geometry and pixels. The affected cue becomes 480×271, 131,104 bytes including
  its palette. Sampling matches the previous LCD renderer, including clipping.
- Two private banks and one worker prefetch cues up to three seconds ahead.
  The renderer only consumes completed buffers; it performs no network/file I/O.
- Fetches have a three-second budget and cancellation; failed cues are attempted
  once per playback, logged and skipped, not allowed to stall the movie.
- Allocate banks after AVC/staging initialization. Join the worker before freeing
  buffers or cue metadata. Validate geometry and offline offset arithmetic.
- Old endpoint requests remain supported; TV burn-in is unchanged. New offline
  LCD packages use small sprites. Existing packages are not regenerated silently.

## Verification and hardware test

Normal O3 and isolated O3+LTO builds completed before targeted tests.
Five focused tests cover exact LCD sampling, metadata/sprite consistency, legacy
API responses, offline packaging/TV policy, and the actual C worker with host
thread mocks (publication, cancellation, failed fetches, renderer responsiveness).
The C harness uses AddressSanitizer/UBSan; leak detection is disabled in this
sandbox. No unrelated full suite was run.

Update server to 0.1.65 and install **both** EBOOT.PBP and PSPStreamer.prx from
either build. Test the same episode on LCD from its beginning, with the German
PGS track, past 1:31 and 1:36. Check visible subtitles, continuous audio/video,
and Stop during playback. Repeat with the other build. Hardware confirmation
is still pending; no ESP or OC plugin update is needed.
