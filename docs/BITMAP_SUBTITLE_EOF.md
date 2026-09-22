# Bitmap subtitle end-of-stream fix (server 0.1.45)

The TV bitmap path burns PGS into video using FFmpeg's subtitle-to-video input
and `overlay`. The default overlay framesync behavior can extend the last main
video frame to the secondary subtitle timeline. The following `fps` filter then
produces additional pictures. Audio has already ended, but the PSP still receives
video packets, so it cannot report a natural end and advance to the next episode.

Use `overlay=eof_action=pass:repeatlast=0`. The subtitle input no longer drives
the remaining timeline, and video without further subtitles passes through.
Do not use `shortest=1`: sparse/short subtitle tracks must not truncate the movie.

## Targeted reproduction

Local FFmpeg comparison on 2026-09-22, using the original MKV of *Chillin' in
Another World with Level 2 Super Cheat Powers*, S01E06, Japanese audio, TV
720x480, H.264/MP3 FLV, 24000/1001 fps. Both German PGS tracks were checked.
The source video ends at approximately 1442.024 s and Japanese audio at
1442.015 s. Tests seek to 1438 s (the last four seconds).

The production command builder was used, with only input realtime throttling
removed and a 192-video-frame safety cap added for the otherwise extended old
path. No source files were changed. FLV packet timestamps were inspected using
ffprobe. The corrected runs finish naturally before reaching the cap.

| PGS track (zero-based) | Overlay | Video packets | Last video PTS | Last audio PTS |
| --- | --- | ---: | ---: | ---: |
| 0 | old defaults | 192 (safety cap) | 7.991 s | 4.023 s |
| 0 | corrected | 96 (natural EOF) | 3.987 s | 3.997 s |
| 1 | corrected | 96 (natural EOF) | 3.987 s | 3.997 s |
| 1 | old defaults | 192 (safety cap) | 7.991 s | 4.023 s |

This isolates the server's PGS filter behavior without a Jellyfin network
connection. The real Jellyfin-to-PSP episode transition remains a hardware test.
No PSP queue flushing, timeout changes, A/V clock changes or new client build
are involved. The separate delay while writing PSP debug traces is unchanged.

References: [FFmpeg framesync options](https://ffmpeg.org/ffmpeg-filters.html#Options-for-filters-with-several-inputs-_0028framesync_0029),
[dual-input framesync implementation](https://github.com/FFmpeg/FFmpeg/blob/master/libavfilter/framesync.c).
