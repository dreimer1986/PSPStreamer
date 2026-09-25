# 0.1.56

- Add web favorites, recently played media and continue-watching shortcuts.
  History uses existing PSP telemetry; state is saved atomically in the durable
  settings directory. No extra playback polling is introduced.
- The matching PSP build synchronizes server-scoped shortcuts when opening
  Comfort and after favorite/history edits. Local downloads and connection
  profiles/passwords never leave the Memory Stick. The PSP still holds 64 records.
- Add background name search across enabled Files/SMB, Plex, Jellyfin, DLNA and
  radio sources. Film/series folders and music are included. Progress and partial
  results are shown; explicit time/folder/result limits prevent unlimited scans.
- Docker and Home Assistant ship identical server/web code. No HACS update needed.

# 0.1.55

- Slow virtual-controller polling to a two-second gap after each response or
  failure, including held keys, to reduce repeated PSP TLS connection overhead.
- Keep acknowledged tap/text events for at most 15 seconds so slower polls do
  not lose quick clicks. Held-button leases remain short; a new PSP session or
  controller owner clears old input. Existing media-remote polling is unchanged.
- Update both PSP files and the server. This is settings control, not real-time
  gameplay: held buttons/analog updates can be intermittent between polls.

# 0.1.54

- Separate short virtual-button impulses from real held keys. A click releases
  locally after 60 ms, without waiting for another PSP HTTP/TLS round trip.
- Renew held keys only after a 350 ms hold delay; idle polls never revive taps.
- Matching PSP client also supports START to exit from local storage through
  the normal application cleanup path. Update both PSP files and the server.

# 0.1.53

- Add an authenticated virtual PSP controller and UTF-8 text entry to the web
  remote. Includes analog input, held buttons, combinations and short leases.
- Text is accepted only for the currently open PSP field; passwords are not
  read back. Requires the matching new PSP EBOOT/PRX pair.
- Docker and Home Assistant contain the same server and web assets. HACS
  integration 0.1.3 remains compatible without an integration update.

# 0.1.52

- Proxy standard DLNA album-art covers to web, PSP LCD/TV and HA media-player
  artwork; keep host validation, bounded image loading and authenticated access.
- Keep Docker and Home Assistant server implementations identical.
- New PSP client adds persistent resume/favorites/history/server profiles,
  session playback limits and local download inventory/preflight space checks.
  Client state stays in PSP/SYSTEM and is not replaced by server/app updates.

# 0.1.45

- Fix bitmap-subtitle burn-in extending video beyond its real end: the overlay no longer repeats the main picture while waiting for the PGS timeline.
- Keep the video playing when subtitles finish early; do not use `shortest=1`.
- Shared fix for file, Plex and Jellyfin sources, in Docker and the Home Assistant app. No PSP application update needed.

# 0.1.44

- Add authenticated, read-only playback telemetry for the new HACS custom integration.
- Expose confirmed PSP state independently of queued commands; stale clients become unavailable.
- Add the matching PSP update for file/music position, buffering and stop reports. Playback clocks and decoding are unchanged.
- Docker and Home Assistant use identical server code. Integration installation is separate from the app/add-on.

# 0.1.43

- Automatically enable the Plex source after successfully selecting and verifying a server, matching Jellyfin setup.
- Failed connections leave source settings unchanged; manually disabling Plex remains supported.
- Identical Docker/Home Assistant update; no PSP update required.

# 0.1.42

- Distinguish subtitle tracks using titles, Forced/SDH/Default flags and codec names, on both PSP and web UI.
- Number same-language alternatives; mark the smallest only when comparable same-codec byte statistics exist. No media scan or subtitle extraction is added.
- Do not silently restore the first same-language web selection when a saved label is ambiguous.
- Server-only update, compatible with existing PSP clients. Docker and Home Assistant contain identical changes.

# 0.1.41

- Page complete text-subtitle timelines for Jellyfin, Plex and mounted file sources; matching PSP client required.
- Merge repeated ASS text and bound each PSP page to 256 cues; seeking requests the appropriate page.
- Use burn-in for new offline conversions whose text timeline exceeds the legacy offline overlay capacity.
- Matching client fixes an end-of-episode demux deadlock that prevented automatic next-episode playback. Normal audio-master synchronization and network timeouts remain unchanged.

# 0.1.40

- Jellyfin connection and independent source selection in web Settings; user tokens stay on the server.
- Browse libraries, episodes, music and playlists with pagination, metadata, resume information and automatic successors.
- Read authenticated originals directly; reuse PSP transcoding, embedded subtitles and offline downloads without a shared mount.
- Report actual PSP playback start/progress/pause/stop asynchronously to Jellyfin.
- Propagate confirmed pauses through the original-file bridge as well as the PSP output stream.
- Requires the matching PSP update. Docker and Home Assistant ship identical source code.

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
# 0.1.46

- Plex/Jellyfin cover grid, episode artwork and series backgrounds in the web UI.
- Authenticated current-media artwork for the separate HA integration 0.1.1.
- Optional browser-only theme music preview; no PSP playback or artwork changes.
- Bounded image caches and lazy thumbnails; no provider credentials in URLs.
# 0.1.47

- Optional bounded PSP menu artwork packets for Plex and Jellyfin.
- Series/album covers and backdrops are resized server-side with FFmpeg; no
  new Python dependencies, playback changes or PSP decoder initialization.
- Requires the matching PSP application; older clients keep working.

# 0.1.48

- Canonical series/album artwork identities let the matching PSP client reuse
  its last image with a 68-byte response; v1 clients remain compatible.
- Coalesce concurrent artwork preparation, cache converted image planes and
  bound FFmpeg conversion concurrency; isolate account/source identities.
- Reuse successful local ffprobe results with file-change detection, bounded
  caches and in-flight deduplication; remote/live sources remain uncached.
- Docker and Home Assistant ship the same server implementation.
# 0.1.49

- Follow PSP playback telemetry in the browser seek bar, without overwriting a dragged or pending seek.
- Show Plex and Jellyfin intro/credits skip controls only within the supplied marker interval; gracefully handle Jellyfin without segment support.
- Add chapter ticks and a chapter selector from container/Plex metadata. Keep timeline payloads out of compact PSP metadata replies.
- Identical server and web assets for Docker and the Home Assistant app.

# 0.1.50

- Persist HA Plex/Jellyfin credentials, source configuration and player identity
  in `/data` rather than the disposable container cache.
- Copy surviving legacy settings without overwriting persistent files; retain
  HA-managed password options and existing download/radio paths.
- Document volume reuse, backups and the one-time sign-in needed if an older
  update already discarded credentials. Docker and HA share the implementation.

# 0.1.51

- Add persisted DLNA servers, bounded SSDP discovery and manual description URLs,
  ContentDirectory browsing, original-resource preference, range proxy and next/shuffle.
- Use host networking in HA for multicast; the port app option controls the
  listener. Ordinary Docker offers an optional host-network Compose deployment.
- Select Plex/Jellyfin originals through version folders on web and PSP, retaining
  stable identities across provider list reordering and downloads.
- Add selected-version external text and single-file PGS subtitles to metadata,
  streaming and offline overlays/burn-in. Unsupported sidecars fail explicitly.
- No PSP executable or A/V synchronization changes. Docker/HA sources remain equal.
