# 0.1.39

- Keep file-based MP3/FLV streams alive during long pauses confirmed by the matching PSP's remote-control heartbeat, including Plex playback.
- Preserve the existing inactivity timeout for missing clients; expired pause heartbeats do not hold transcoding slots indefinitely.
- Resume partial writes without repeating bytes. Requires the matching PSP client update; Docker and Home Assistant use identical server code.

# 0.1.38

- Password login page, bounded browser sessions, logout and CSRF protection; PSP Basic authentication stays compatible.
- Responsive Library / Remote control / Downloads / Settings navigation, English/German selection and existing app artwork.
- Files, Plex and Internet Radio share a stable source overview; mounted roots are grouped under Files, including correct parent navigation.
- Convert selected files or entire folders, optionally including subfolders. Preview matches language/title per file and excludes missing or ambiguous tracks.
- Hide irrelevant controls for music and until a media file is selected. Preserve subtitle index zero and saved playback preferences.
- Identical server and web assets in Home Assistant and ordinary Docker.

# 0.1.37

- Plex originals stream directly over the selected HTTP(S) connection: no path mappings or shared network required.
- Private range-capable loopback bridge keeps Plex tokens out of FFmpeg arguments and client responses.
- Direct originals support audio/subtitle inspection, text and PGS subtitles, seeking and offline conversion.
- Local path mappings remain an optional optimization, with HTTP fallback for unavailable mounts.
- Display web metadata failures instead of apparently missing tracks; matching PSP build does not open empty options on metadata errors.
- Home Assistant and plain Docker contain identical server/web changes.

# 0.1.36

- Optional Plex account linking, discovered server selection and source switches.
- Browse Plex libraries and playlists; map original files to existing media mounts.
- Preserve the software transcoder, subtitle and offline download paths.
- Matching PSP build reports real playback position, pause and stop to Plex.
- Web metadata/resume controls and PSP title/series/album information.
- Same implementation in Home Assistant and ordinary Docker; see README for setup and limitations.

# 0.1.35

- Fix CSS overriding hidden controls: music no longer shows frame rate or LCD/TV download output.
- Label music conversion explicitly as "Prepare MP3 download"; completed jobs offer Memory Stick ZIP export or PSP Wi-Fi download.
- Identical web interface fixes for Home Assistant and ordinary Docker.

# 0.1.34

- Music conversion/download jobs: stereo 44.1-kHz MP3 with CBR/VBR quality.
- Resume/checksum/Memory Stick ZIP support for music, with title/artist metadata.
- Matching PSP client plays managed music downloads without Wi-Fi.
- Same implementation and web controls as the ordinary Docker deployment.

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
