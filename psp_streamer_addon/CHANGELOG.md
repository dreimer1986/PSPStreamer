# 0.1.33

- Persistent Internet radio station management and virtual radio library.
- Direct HTTP(S) audio and bounded M3U/PLS resolution, live MP3 transcoding.
- Same radio/server/web features as ordinary Docker; stations persist in /data.
- ICY/Shoutcast current titles and sender names; ID3/container music tags.

# 0.1.32

- Remove obsolete synthetic A/V calibration streams and their special routes.
- Reject invalid/negative library root identifiers consistently.
- Same server implementation as the ordinary Docker deployment.

# 0.1.31

- Persist web track/language, quality, frame rate and output preferences.

- Add authenticated Memory Stick ZIP exports for completed conversion jobs.
- Recommend PC/USB copying; keep PSP Wi-Fi downloads as the portable fallback.
- Include FLV, subtitles, seek index, compact metadata and completion marker.
- Stream archives without storing an extra video-sized ZIP on the server.
- Compatible with the existing 0.1.30 PSP client; no new PSP build required.

# 0.1.30

- Persistent offline conversion queue with per-episode track and quality settings.
- Resumable downloads, SHA256 manifests and packaged offline subtitles.
- Matching PSP client adds Local storage, download progress, playback and deletion.
- Cache lives in /data/downloads; remove server copies from the web queue.
- Main/CABAC has passed the user's real PSP playback test.

# 0.1.29

- Restore software-only libx264 encoding; remove VA-API/NVENC options.
- Test Main profile with CABAC, no B-frames and no weighted P prediction.
- Keep audio, timestamps, frame-rate options and bitrates unchanged.
- Requires real PSP LCD/TV validation; no PSP executable update needed.
