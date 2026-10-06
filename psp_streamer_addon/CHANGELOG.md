# 0.1.80

- Batch S/PDIF PCM into 960-sample (20 ms) packets instead of forwarding tiny
  decoder frames individually. Preserve timestamps and the unpadded final tail.
- AC-3/DTS passthrough and the standard PSP/Xbox/browser paths are unchanged.

# 0.1.79

- Optional PSP/StreamMaster optical audio output: AC-3/DTS core passthrough,
  stereo PCM, and explicit PCM or Dolby Digital conversion fallback.
- Preserve one source timeline for AVC and optical audio. Default PSP, Xbox,
  browser and offline encoders are unchanged. Requires Onju firmware 0.3.18.
- Add the output selector to the PSP web remote. Hardware verification pending.

# 0.1.78

- Optional PSP Dolby Surround / Pro Logic II stereo downmix for streams and new downloads.
- Persisted web selection and separate offline cache identity for each downmix.
- Unchanged default stereo, PSP sample rate and synchronization; Xbox settings remain separate.

# 0.1.76

- Xbox 0.5.0 library adapters for shared favorites, search and editable queue.
- Xbox MP2 bitrate selection and bounded HD artwork; PSP routes unchanged.
- One common PSP/Xbox web playback panel, including current-item adoption.
- Xbox metadata/status for the separate HACS 0.2.0 media-player entity.
- Docker and Home Assistant app use identical server code.

# 0.1.75

- Add all six Xbox video resolutions to the web remote, plus "Use Xbox setting".
  Remember the selection in the browser; apply it on Play with Xbox 0.4.6.
- Keep Xbox video quality separate from TV output and PSP download profiles;
  hide the selection for music and other playback targets. English/German labels.

# 0.1.74

- Raise only the Xbox MPEG packet bound from 256 KiB to 1 MiB, including PES
  header allowance. Complex 1080p I pictures could otherwise abort the stream.
- Use Xbox client 0.4.5 for the matching bound and DMA end-marker fix.
- PSP and browser encoding/transport remain unchanged; Docker and HA match.

# 0.1.73

- Xbox 0.4.0: selectable MPEG-2/MPEG-1 and six video sizes through 1920x1080.
- Anamorphic SD, aspect-correct pillarboxing and optional Dolby Surround /
  Pro Logic II stereo downmix. Existing PSP and browser encoding is unchanged.
- Keep legacy Xbox requests on MPEG-1; the new client explicitly requests
  its codec, size and matrix mode. Update the server before testing HD profiles.

# 0.1.72

- Make the playback target selector available without a media selection.
- Use Xbox status in the main remote/current-playback view when Xbox is selected;
  retain the target across browser sessions and hide PSP-specific controller input.
- Keep the existing PSP remote behavior and command mailbox independent.

# 0.1.71

- Add an independent Xbox playback target and remote status/controls to the
  web interface. Commands expire after 15 seconds and do not consume PSP commands.
- Requires Xbox client 0.3.1 for remote playback, seek and transport controls.

# 0.1.70

- Browser/Xbox playback progress, pause and stop report to Plex/Jellyfin and
  shared history, isolated from the PSP remote queue and playback state.
- Browser autoplay next/previous, retaining languages instead of track indices.
- Same-series season continuation for providers and numbered local/DLNA folders.
- Xbox 0.3.0 metadata/resume, shared favorites/history and radio support.
- Docker and HA contain identical server and web code; PSP codec paths unchanged.

# 0.1.69

- Prepare Jellyfin text subtitles via its API for browser/Xbox burn-in instead
  of scanning the remote original twice; preserve ASS formatting.
- Keep text-subtitle timing correct after browser/Xbox seek or resume.
- PSP encoding and overlays are unchanged. Docker/HA code is identical.

# 0.1.68

- Add an independent original Xbox preview transport: MPEG-1 video and MP2
  audio with actual container timestamps, plus bounded library pages.
- Reuse source/track/subtitle selection without changing PSP stream encoding
  or PSP remote control. Same server code in Docker and Home Assistant.
- Native Xbox GUI/playback preview is separate from the PSP release packages;
  it requires this server version. Hardware playback verification pending.

# 0.1.67

- Add This browser as a playback target for selected video, music and radio.
- H.264/AAC fragmented MP4, MP3 audio, selected audio/subtitles and explicit
  seek/pause/resume controls. Release encoder resources on Stop/disconnect.
- Keep PSP commands/status and browser playback independent. Browser playback
  is single-item/tab-local; PSP playlists and watched-state reporting unchanged.
- Docker and Home Assistant ship the same implementation. No PSP or firmware
  update required; the HACS media_player still represents the PSP.

# 0.1.66

- Optional automatic server-side reserve of the next N unwatched episodes,
  with persistent per-series/folder rules, language matching and a storage limit.
- Clean only explicitly managed reserve packages. Manual conversion requests
  and requested downloads are pinned; original media and PSP files are untouched.
- Optional read-only Plex account Watchlist, mapped by exact GUID to the selected
  server, separate from the episode playlist. Unavailable titles are identified
  in the web UI; available items also appear in the PSP provider shelf.
- Same server and web implementation in Docker and Home Assistant. No PSP or
  StreamMaster firmware update required.

# 0.1.65

- Prepare LCD PGS sprites at display resolution, preserving palette, timing and
  the client's previous nearest-neighbour sampling. This avoids oversized
  full-HD subtitle transfers freezing LCD playback.
- Package new offline LCD bitmap overlays at the same bounded resolution.
- Update the PSP client too: bitmap fetching now runs outside the video renderer.
- Docker and Home Assistant use the same server implementation.

# 0.1.64

- Show the next queued title and total duration for one pass; unknown durations
  remain explicit and can be retried without blocking playback.
- Reorder web playlist rows by dragging their handle; arrow buttons remain.
- Store audio quality and video frame rate per queue entry, with PSP-default
  inheritance. The matching PSP client applies choices on start and continuation
  without changing its saved global quality settings.
- Keep Docker and Home Assistant server/web code identical.

# 0.1.63

- Add Play next in the web library, media controls and shared playlist, with
  persistent order and independent shuffle handling; no immediate playback command.
- Expose compact Plex/Jellyfin shelves through the PSP catalogue.
- Persist series audio/subtitle preferences by language/title; direct files use
  folder scope. Settings survive updates in the existing state volume.
- Same implementation and web assets in Docker and the Home Assistant app.

# 0.1.62

- Reuse identical offline conversion jobs and intact ready packages instead of
  duplicating work. Different track/quality/output choices remain separate.

# 0.1.61

- Add a Cover view toggle directly to all new Plex/Jellyfin provider shelves.
  The saved preference is shared with the library. Switching views uses already
  loaded entries, without another provider request or changing playback.
- Keep collection covers and playlist actions correctly arranged in the grid.
- Same web assets in Docker and Home Assistant; no PSP update required.

# 0.1.60

- Persistent playlist repeat off/one/all and independent playlist shuffle, in
  the web UI and matching PSP client. Manual Next leaves Repeat One.
- Fresh Plex/Jellyfin Continue Watching, recently added, unwatched and
  collections in the web Provider library. Continue Watching includes other
  clients on the same provider account; Jellyfin also includes Next Up.
- Open previous/next media details without starting or changing playback.
- Docker and Home Assistant server/web implementations remain identical.

# 0.1.59

- Persistent shared playlist: add selected library items or a configured media
  entry in the web UI; play, reorder, remove and clear from the Playlist tab.
- Matching PSP client browses, starts, reorders and removes entries; mixed
  video/music continuation respects each entry's audio/subtitle selection.
- Revision checks prevent stale clients overwriting a newer edit. Playlist
  state uses the existing persistent state directory in Docker and HA.

# 0.1.58

- Show current PSP playback across browser sessions, including playback started
  on the PSP or another controller. Open its existing controls without sending Play.
- Synchronize position when opening the currently playing library entry; follow
  automatic media changes while viewing current playback, preserving other selections.

# 0.1.57

- Add bounded-rate stream transport diagnostics: FFmpeg output versus bytes sent
  to the client, idle durations and termination reason. No media URLs or secrets
  are logged. Streaming and pause timeouts remain unchanged.
- Keep the Docker and Home Assistant server implementations identical.

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
# 0.1.77

- Separate Xbox menu-button and text-entry commands with acknowledgements and expiry.
- Xbox radio now packetizes MPEG-TS into the client transport; forwards changing ICY titles.
- Matching Docker/Home Assistant server and web assets. PSP playback/input unchanged.
