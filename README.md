# PSP Streamer

### Covers and backgrounds (server 0.1.46)

The web library's **Cover view** button switches between the compact list and
a poster grid; the choice is remembered in that browser. Plex and Jellyfin supply
episode thumbnails, movie/series posters, album covers and series backgrounds.
Selecting an item shows its image and background in the remote-control view.
Missing images do not prevent browsing or playback. File shares and radio keep
their text view unless artwork is supplied by a media-server source.

**Preview theme song** requests the provider's theme music (including inherited
series themes) and plays it in the browser only. This is optional: not every
series has theme media. It does not extract an intro from the episode, transcode
the preview or start playback on the PSP. Browser-supported audio formats up to
16 MiB are accepted; theme videos are not implemented.

With the separate **PSPStreamerHA integration 0.1.1**, current-media artwork also
appears on Home Assistant's media-player entity. Update both server and
integration, then reload the integration. Images remain password-protected;
neither Plex/Jellyfin tokens nor the PSPStreamer password appear in browser URLs.
The server keeps a bounded image cache (24 MiB per provider) and loads grid
thumbnails lazily.

### PSP menu artwork (server 0.1.47)

With an updated PSP application, Plex/Jellyfin artwork also appears on LCD and
native TV menus. Pause the selection briefly: the menu requests images in the
background, dims the backdrop behind the main list/options and contains the
cover in the right-hand display. Episode menus use series artwork; songs use
their album. Text remains outside the cover. The information page shows a cover
only if there is enough space below its track list.

No setting or extra image files on the Memory Stick are needed. The server
prepares a bounded RGB565 packet with FFmpeg; the PSP does not initialize any
JPEG/MPEG decoder for it. Full artwork is about 130 KiB, released before media
playback. Artwork shares the cancellable idle-browser worker, waits 500 ms after
selection changes and never runs concurrently with playback. Missing images or
an older server leave the normal skin intact. Square/Refresh retries a failed
load. There is no artwork for ordinary SMB/local-file entries yet.

Image requests get a dedicated turn after a control reply, even if a slow TV
redraw overruns the usual idle polling interval. LCD backdrop bounds are
`x=36, y=29, width=312, height=123`; the cover remains separate from status text.
TV backdrops fit the inner glass at `x=27, y=61, width=506, height=231`, with
the skin's rounded corners preserved. While an image is downloading, automatic full-TV
redraws pause (navigation still redraws). Artwork alone has a cancellable
10-second request budget, retries failures after 15 seconds and empty image
responses after 60 seconds; it no longer permanently gives up after two tries.
Changing selection cancels the obsolete image request. Streaming timeouts are
unchanged.

With **server 0.1.48 and the matching PSP build**, selecting another episode of
the same series (or song from the same album) reuses the last image after a
68-byte server confirmation. The previous image stays hidden until confirmed;
only one image is retained, and playback releases it. Different accounts and
changed image tags do not share identities. Older server/client versions still
use full image packets. Cold requests share bounded image-conversion jobs.

Idle TV library updates redraw only the receiver controls, not the complete
menu/background. Full redraws present once, including connection notices.
The server also reuses successful local-file metadata/subtitle-codec probes with
file-change detection and bounded caches; remote/live probes are not retained.
Docker and the Home Assistant app include the same changes. No configuration
changes are required. See [optimization status](docs/OPTIMIZATION_NEXT_STEPS.md)
for the implementation review and remaining measurement-dependent work.

PSP Streamer makes a local or DynDNS-reachable media library available on a PSP-2000/3000 with custom firmware. The Python server browses allowed folders and transcodes with FFmpeg. Video is delivered in one FLV stream containing H.264 and MP3 audio, both decoded locally by the PSP.

### Plex library and playlists (server 0.1.37)

Directory navigation uses a background request on the PSP. While loading,
the status shows elapsed seconds; **Circle** requests cancellation. Failed or
cancelled loads keep the previous directory. Network transfers have a separate
30-second directory budget, independent of playback. Pending
remote-control requests are cancelled cooperatively without an unbounded UI
thread join. A worker that has not yet relinquished its resources is not killed
or replaced: the UI displays the stopping state until safe cleanup is possible.

Before playback, metadata and subtitle preparation also run in a worker with
an elapsed-time display and **Circle** to cancel. The menu stays on its current
LCD/TV output until preparation finishes; no decoder or playback clock is
started during that wait. Cold Plex subtitle extraction may scan the original
over the network and take considerably longer than a cached request. Preparation
has separate total budgets (metadata 60 s, text/type query 210 s, bitmap extraction
630 s), allowing the server's extraction deadlines without changing live stream
timeouts. TV bitmap burn-in skips the unused PSP sprite extraction entirely.

Update **both the server and PSP application**. Docker and the Home Assistant app
provide the same integration. No Plex password or token belongs in the PSP CFG.

1. Open **Settings → Plex** in the server web interface. Choose **Link Plex
   account**, then **Open Plex sign-in**. Authorize PSPStreamer on Plex's own
   website. The local page detects completion; the link expires after five minutes.
2. Select a discovered server connection and press **Use connection**. Prefer a
   reachable HTTPS connection; an HTTP LAN connection also works, but does not
   encrypt the token on that LAN. TLS verification is never disabled.
3. **Plex library** is enabled automatically after the connection is verified.
   Optionally disable **Filesystem library** and/or **Internet radio**, then
   **Save sources**. Refresh the PSP library with Square.
   Enter **Plex** to browse films, series/seasons, music/artists/albums or playlists.

**No path mapping or SMB mount is required.** PSP Streamer reads the original
file directly from the selected Plex HTTP(S) endpoint. Plex and PSP Streamer
can be on different networks, provided that endpoint is reachable and the Plex
account is allowed to read the original. Use a reachable public/remote HTTPS
connection from the server list when the LAN address is not accessible.
For a Plex/radio-only installation, `MEDIA_ROOTS` may be empty. Unavailable
filesystem roots produce an empty file library, not a server startup failure.

Plex does not transcode: FFmpeg on PSP Streamer keeps the established H.264/MP3
encoding and LCD/TV profiles. A private loopback bridge forwards byte-range
requests, so FFprobe and FFmpeg can seek without downloading the entire file
first. The Plex token remains in upstream HTTP headers in the server process;
it is not sent to the PSP/browser or placed in FFmpeg's command line. Neither
the extra ephemeral port nor a new Docker port mapping needs to be exposed.
TLS certificate verification and redirect blocking also apply to original files.

Audio/video inspection, embedded text subtitle extraction, PGS extraction and
offline conversion all support this direct path. Remote PGS uses lossless
FFmpeg stream-copy instead of mkvextract; local files keep their existing path.
Subtitle extraction may need to read much of the original, so a slow internet
link can still increase startup time. The Plex uplink must carry the **original
bitrate**, not merely the smaller PSP output bitrate.

If the same media is already mounted, **Optional: use existing local media
mounts** permits a path mapping to avoid the HTTP transfer. For example,
Plex `/volume1/video` → container `/media/video` (inside `MEDIA_ROOTS`). Missing
mapped files automatically fall back to Plex HTTP(S). Multipart originals are
reported as unsupported rather than silently playing only the first part; for
multiple versions, the first Plex media version is used.
Track selection currently covers tracks embedded in that original; external
subtitle sidecars managed only by Plex are not imported yet.

- Large item lists have 100-entry pages. Playlist order is preserved; automatic
  next/previous playback can cross item-page boundaries. Changed playlists stop
  advancement rather than guessing the next item. Music shuffle stays within
  the current Plex collection/playlist.
- The web information area shows title, series/artist, season/album, year,
  description, watched state and a **Resume at …** button. This button positions
  the seek slider; press Play to start. PSP Triangle information displays Plex
  title, series/artist and season/album. Poster rendering and an automatic resume
  prompt on the PSP are not included in this first version.
- During **online playback**, the updated PSP reports its actual played position
  in milliseconds and pause/stop state. A bounded background worker forwards
  Plex timeline updates at most every five seconds, plus state changes. The
  server never infers watched status from transcoding or download completion;
  Plex applies its own watched/resume rules. Reporting errors are shown under
  Media sources, without interrupting playback. Final Stop reporting is
  best-effort if the network fails. Offline playback progress is not synced yet.
- Credentials are stored only in `plex.json` under `PSP_STREAMER_SETTINGS_DIR`
  (`/data` in the containers; otherwise `~/.cache/psp-streamer`). The file has
  owner-only permissions. Back it up securely; do not commit it. Disconnect
  removes local credentials and restores the filesystem library. Revoke the
  device in your Plex account as well if you want to invalidate its authorization.

Protocol references: [Plex authentication flow](https://forums.plex.tv/t/authenticating-with-plex/609370)
and [Plex Media Server API](https://developer.plex.tv/pms/). The adapter is an
independent implementation; no Tautulli source was copied.

### Jellyfin (server/HA app 0.1.40)

Update both the server and PSP client. Open **Settings → Jellyfin**, enter your
server's HTTP(S) address, username and password, then **Connect Jellyfin**.
Connection enables the source; **Save sources** independently enables/disables
Files, Plex, Jellyfin and Radio. Refresh the PSP library with Square.
Use a dedicated Jellyfin user with access to the desired libraries and permission
to download originals. Use HTTPS for credentials outside a trusted local network.

Movies, series/seasons/episodes, music and playlists use the existing PSP browser.
Lists are paginated; next/previous and audio shuffle use the source collection
or playlist, including page boundaries. Web metadata includes description,
watched status and a resume-position button. Actual online playback reports
start/progress/pause/stop to Jellyfin in its native ticks; downloading a file
alone does not mark it watched. Reporting runs outside playback/UI threads.

Jellyfin supplies the original file over authenticated, range-capable HTTP(S).
Only PSPStreamer transcodes it; no shared media mount is necessary. Embedded
audio, text/PGS subtitle selection and offline conversion reuse the existing
pipeline. This first integration uses the item's default original version;
multipart files and external subtitle sidecars are not supported. Posters,
Jellyfin-device remote control and offline progress synchronization are not
included. Original-file bandwidth between Jellyfin and PSPStreamer still applies.
Embedded text subtitles are requested through Jellyfin's subtitle-export API
where its media-source mapping is available, avoiding a complete video transfer
just to extract text. With server 0.1.41 and the matching PSP client, text cues
are loaded in 256-entry pages in the background, including for Plex and files
on mounted SMB shares. There is no whole-episode cue-count cutoff. Seeking
loads the page matching the target timestamp. Identical neighbouring ASS cues
are merged; bitmap subtitle handling is unchanged. Older clients retain the
legacy response limit. Offline downloads still use whole-file overlays; new
conversions exceeding 960 normalized text intervals use burn-in instead of
silently truncating subtitles. Previously converted files need reconversion.

Subtitle choices show format and available Forced/SDH (hearing-impaired)/Default
flags alongside the title. Same-language alternatives are numbered. `smaller`
marks the smallest track only when all alternatives of that language have known
byte sizes and the same format. This does **not** identify its content as Forced
or signs/songs. No extra scan is performed to obtain missing statistics. The
labels apply to mounted files, Plex and Jellyfin on both PSP and web. After an
update, reselect a saved web preference if multiple tracks make it ambiguous.

The password is used only to obtain a user token, not saved. Tokens and device
identity live in owner-only `jellyfin.json` under `PSP_STREAMER_SETTINGS_DIR`
(`/data` in Docker/Home Assistant). Disconnect removes the local token; revoke
the device in Jellyfin as well to invalidate it upstream. Tokens never enter
PSP configuration, public API replies or FFmpeg arguments. Redirects carrying
credentials are blocked, and HTTPS certificates are verified.

The ordinary Docker image and Home Assistant app contain identical adapters.
Reference: [official Jellyfin API client](https://github.com/jellyfin/jellyfin-apiclient-python).

### Internet radio and music tags (server 0.1.33)

Update the server **and the PSP build**. In the web UI, open **Settings → Internet Radio**,
enter a name and a direct HTTP(S) audio URL, then save.
Stations appear in the PSP's **Internet radio / Internetradio** library folder;
press Square to refresh. Select a station, choose audio quality and start it.
The web remote can select/play stations and replace music/video playback too.

- Direct MP3, AAC, Ogg/Opus/Vorbis, FLAC and WAV inputs use the existing
  44.1-kHz stereo MP3 output and quality settings. Existing file encoding is
  unchanged. Simple URL-based `.m3u` and `.pls` lists resolve their first entry
  (up to 64 KiB, bounded nesting). HLS/M3U8 and HTML-page scraping are excluded.
- **Select** pauses by disconnecting. Select/X resumes at the live programme;
  **Start** stops. The web Pause/Resume/Stop buttons do the same. Radio has no
  seek, download, shuffle or automatic next-station playback. Spectrum and
  MilkDrop work as for music; fullscreen visualizations stay overlay-free.
- Interrupted radio playback shows a retry screen and reconnects after five
  seconds; Square/X can retry sooner, Start/Circle cancels. If Wi-Fi association
  itself fails, return to the library and use its existing Square reconnect.
  Silent senders are detected after 30 seconds without audio progress, not
  mistaken for the end of a song. No file/video timeouts were shortened.
- ICY/Shoutcast `StreamTitle` and `icy-name` are requested on the actual audio
  connection. Titles appear in the web remote and normal PSP music view.
  Missing metadata falls back to the saved station name. UTF-8 and legacy
  Windows-1252 text are handled; titles can lead audible playback by the small
  transcode/audio buffer. No exact ICY-title timestamp synchronization is claimed.
- Music files now display **title and artist** from ID3/container tags, falling
  back to the filename. The file-information screen (Triangle) also shows the
  album. MP3 ID3 and equivalent FFprobe-readable FLAC/Ogg/M4A tags are supported;
  embedded artwork and tag editing are not included.

Stations persist in `radio.json`: `/data/radio.json` in Docker and Home Assistant.
For standalone Python, use `PSP_STREAMER_RADIO_DIR`, otherwise
`PSP_STREAMER_SETTINGS_DIR`, otherwise `~/.cache/psp-streamer`. Keep the Docker
`/data` volume mounted. HA 0.1.33 and ordinary Docker ship identical server/UI
features. Sender edits do not interrupt an already playing connection; restart
that sender to apply its changed URL.

Radio management is protected by the existing server password and same-origin
JSON rules. Only configure trusted URLs: fetching stations intentionally permits
access to LAN senders, including HA-NetMD. Do not expose an unprotected server.
URLs stay on the server; PSP clients receive opaque station IDs. Basic auth
credentials inside sender URLs are not supported. Stream redirects are limited
to FFmpeg's HTTP(S) transport; local-file demux/protocol inputs are disabled.
The ICY adapter consumes the metadata events from
[FFmpeg's HTTP implementation](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/http.c),
without opening an additional receiver connection or logging arbitrary sender output.

### Software encoding / Main with CABAC (0.1.29)

All deployments use software `libx264` again; VA-API/NVENC support and its
configuration have been removed following PSP decoder failures. Encoding
uses H.264 **Main, level 3.0, CABAC enabled**, with B-frames
and weighted P prediction disabled. Resolution, selected frame rate, bitrates,
MP3 audio and container timestamp synchronization are unchanged. Docker and the
Home Assistant app use identical encoder commands. Update/rebuild the server;
this change does not require a new PSP executable. Main/CABAC subsequently
passed the user's real PSP playback test. Successful FFmpeg decoding alone
is not proof of PSP compatibility.

## Offline downloads (server/app 0.1.30 and matching PSP client)

Choose **Streaming** or **Download, then play** in the PSP video options.
The latter queues a complete file from its beginning, waits for conversion,
downloads and verifies it, then starts the existing FLV/PTS player from the
Memory Stick. It is not a general local-file browser.

**Music downloads (server/app 0.1.34 and matching PSP client):** music options
also offer **Streaming / Download, then play**. The server converts music files
to stereo 44.1-kHz MP3 at the selected CBR/VBR quality. Download progress,
resuming interrupted transfers, SHA256 checks and PC/USB ZIP export work just
like video. Radio/live streams cannot be downloaded this way.

Open downloaded songs in **Local storage**. Playback needs no server or WLAN,
retains title/artist metadata and uses the normal music decoder, volume, pause,
Spectrum/MilkDrop and fullscreen controls. Select pauses/resumes, Start stops.
There is no local-music seek control in this batch. End-of-track advance stays
within music entries; Shuffle visits each ready local song at most once per
browser playback run. Videos are not mixed into that music sequence.

The web **Prepare MP3 download** button converts music, without video-output
or subtitle options. For an album, queue songs on the web and use **R** in the
PSP server queue to transfer pending/ready entries. Starting a single download
directly from the music options plays that song; use the Local storage browser
for automatic multi-song playback. Keep the entire managed job folder even
for MP3: metadata/ready and tiny empty sidecars retain the existing bundle format.

For several episodes, use the Library checkboxes and **Convert selected**, or
**Convert this folder**. Subfolders are included only when explicitly enabled.
In Downloads, choose audio/subtitle language, quality, frame rate and output,
then **Check tracks**. The preview resolves language and title separately in
every file, rather than copying a stream index across episodes. Missing or
ambiguous tracks are shown and excluded; review the results, then **Queue
checked files**. At most 128 files and 256 visited folders are allowed per
batch, including Plex pages. All-music batches show only audio quality.
Single files still use **Convert for download** in the web remote. Each
job retains its own audio track, subtitle track, audio quality, frame rate and
LCD/TV profile. Conversions run sequentially, without real-time throttling;
the queue survives server restarts. A single conversion shares the server's
existing transcode limit with streaming. The web list reports conversion
state, percentage and produced size; cancel/delete controls affect only
server jobs/copies, not downloaded PSP files.

The web UI remembers audio/subtitle labels and languages, quality, frame rate
and output profile on the server across restarts. Tracks match by label first,
then language; a missing preferred track produces a warning to check the
fallback selection. Subtitle Off is remembered too. Queued jobs retain their
own settings; PSP config defaults are unchanged. FFmpeg/libx264 chooses its
thread count automatically, with no single-core limit imposed by the queue.

### Recommended: PC/USB transfer (server 0.1.31)

For a finished conversion, click **Download Memory Stick ZIP** (PC / USB is recommended).
Download and extract it on your PC, close PSP Streamer, then merge the contained
**PSP** folder into the Memory Stick root (not into another PSP folder).
Safely eject and open **Local storage**. The existing 0.1.30 PSP client needs
no update. This works with HA and Docker without SSH or internal filesystem access.

Copy the entire `PSP/VIDEO/PSPStreamer/<job-id>/` folder: FLV or MP3, `subtitles.ovl`,
`seek.idx`, compact `job.json` and `ready`, not just the video. Multiple ZIPs
can be merged thanks to unique job folders. Download ZIPs onto the PC first:
the combined archive can exceed FAT32's limit even when individual files fit.
The server streams the ZIP without creating another video-sized disk copy.
Interrupted ZIP downloads must be repeated. Direct Wi-Fi remains the fallback.

### Alternative: direct PSP Wi-Fi download

On the PSP, open **Local storage** at the library root, or press **Circle**
in any library folder. **Square** switches between local files and the server
queue. In the queue, **X** downloads the selected job (waiting if it is still
converting); **R** downloads all pending/ready jobs in the displayed list,
one after another. Use Square twice to refresh the queue. During transfer,
the screen shows file bytes/total size, KiB/s and estimated seconds remaining.
Size is final only after conversion. Subtitle/index sidecars follow the video;
the byte counter then refers to the current sidecar. SHA256 verification has
its own status. **Circle** cancels the PSP wait/transfer, keeping both the
server job and partial download. Select it again to resume via HTTP Range.

Local **X** plays a completed download or resumes an incomplete transfer.
**Triangle** offers deletion with confirmation; **Circle** goes back.
Playback supports pause, stop and keyframe-based seek using the existing
controls. Next/previous and end-of-file advance use the local list's filename
order (and stop at an incomplete entry). Local seeking also reads preceding
MP3 packets through the FLV backward links: it warms the decoder silently,
then publishes audio at/after the indexed video keyframe. This supplies the
MP3 bit reservoir without re-encoding existing downloads or changing PTS sync.
Decoder errors are tolerated only for silent lead-in frames whose required
preceding data has not yet been fed. After that, errors still stop playback.
Local playback makes no server or
remote-control requests, including for subtitles. Hold **R while launching**
to skip Wi-Fi association entirely; Square in the main library can connect later.

If Wi-Fi fails at startup or drops while browsing, press **Square** in the main
library to reconnect and reload without restarting the app. **L + Square** forces
a disconnect/reconnect even if the PSP still reports an IP address. Association
waits up to 30 seconds (plus at most 3 seconds to disconnect); **Circle** cancels.
Network modules initialize only once, including after a partially failed startup.
These controls do not change stream/subtitle preparation timeouts.

Files live under
`ms0:/PSP/VIDEO/PSPStreamer/<job-id>/<original-name>.flv`, with a subtitle
package, seek index and job metadata alongside. Unique job directories prevent
collisions between different series/language variants. Filesystem-unsafe
characters are replaced and exceptionally long names shortened. `+` identifies
a completed, verified entry; `~` an incomplete one. Local deletion is limited
to these managed files and never touches unrelated Memory Stick content.
For PC-copied files whose Unicode long name the PSP cannot open, the client
uses the FAT short-name alias inside that job folder. The fallback requires
one unambiguous FLV with the manifest's byte size; files are not renamed.
The original UTF-8 title remains visible. Existing exports need no conversion
or server update for this client-side compatibility fix.

Choose the output profile before converting: LCD (480×272) or TV (720×480).
A downloaded file requires its matching output; connect/disconnect the TV
cable accordingly. The PSP's direct-download option detects the cable.
`FFFFFA87` means this output/profile check failed, not that VLC-compatible
video is corrupt. The local list shows the resolution; the error screen shows
the required output and how to correct it. For TV playback, select the TV
profile on the server before converting; LCD copies cannot be reused as
native 720×480 TV copies (or vice versa).
Text subtitles use the existing offline overlay; supported LCD MKV/PGS cues
and sprites are packaged too. TV bitmap subtitles and unsupported/oversized
bitmap-overlay cases retain the existing server burn-in fallback. No network
fetches remain during playback. Current 960-cue subtitle bounds still apply.

Free Memory Stick space is checked before each file; individual files above
FAT32's 4 GiB-minus-one-byte limit are rejected. Keep free space on the server:
converted copies remain until explicitly deleted. Docker stores the queue in
its persistent `/data/downloads` volume; Home Assistant uses its app's own
`/data/downloads`. Native server default: `~/.cache/psp-streamer/downloads`.
Override with `PSP_STREAMER_DOWNLOAD_DIR` if needed. All offline endpoints use
the same password/HTTPS protection as streaming. The queue is capped at 128
jobs. This release requires real PSP testing of Memory Stick transfers,
offline playback and interruption/resume in addition to host tests.

Video profiles are 480×272 for LCD and 720×480 for component TV output, with 44.1 kHz MP3. The encoder currently produces 20 fps to limit decoder workload; that number is **not a playback clock or an A/V calibration value**. Text subtitles and LCD PGS bitmap subtitles use native PSP overlays. Video and common music formats use the same MP3 DAC path; music playback includes a receiver UI with live stereo VU meters and a real 12-band PCM spectrum display.

## Timestamp-based audio/video synchronization

The idle file browser polls remote commands in a cancellable background task,
so an HTTPS handshake or unavailable server does not block list navigation.
The task is joined before settings, other network requests or playback begin;
media and subtitle timeout budgets are unchanged.

Both LCD and TV playback follow the audio-master approach in [PMPlayer Advance](https://github.com/DavisDev/pmplayer-advance/tree/9ce494d020d3c909ee312bda1932f8d806a2f05d/ppa/mod):

- One FFmpeg process muxes both streams, including their timestamps. The PSP reads FLV tag timestamps and the signed H.264 composition offset to obtain video PTS.
- H.264 stays length-prefixed (AVCC) from the FLV reader to the Media Engine. Each aligned queue packet owns its SPS/PPS configuration and payload; no Annex-B round trip or second video encoding is involved. This applies to streaming and downloaded FLV on LCD and TV; existing downloads remain compatible.
- Decoded PCM buffers retain their first MP3 packet's PTS. The output worker publishes that timestamp immediately before its blocking DAC call, matching PPA's buffer-based audio clock.
- Video is decoded and composited with its subtitles in a RAM staging frame. The finished picture is compared with fresh audio PTS, again at VBlank before copying to the established LCD/TV framebuffer. It waits when ahead and discards late pictures. As in PPA, the tolerance is two video-frame durations; here the duration comes from adjacent packet PTS.
- Codec initialization, decoding and related cache operations share one Media Engine semaphore. Network reads, audio output and display waits remain independent. PCM ownership follows PPA: a successful submission releases the previous buffer; the last buffer is released after the DAC drains. Audio content is never repeated or skipped to synchronize video.
- Subtitle cues and progress use milliseconds. Seek and reconnect start a new container timeline at the requested source position. Video-only files use PTS differences on a monotonic clock.

The old 20.1/20.2-fps playback estimates and artificial start offsets are no longer used by the client. Existing hardware initialization, LCD/TV layouts, MP3 decoding and DAC block sizes remain in place. This is a streaming adaptation of PPA's scheduling, not a replacement of the working hardware drivers. Hardware validation is still required for actual DAC/display latency, rendering load and long-episode playback; host tests cannot establish those.

### Measuring a remaining offset

With **Debug diagnostics enabled** (`debug=1`), the client records a bounded
trace in RAM during video playback and writes it after Stop/end, once playback
threads have joined. Diagnostics are **off by default**, including when an old
CFG has no debug entry. Press SELECT in the library, select Debug diagnostics,
change with left/right and save with START. `debug=0` disables trace sampling,
diagnostic timing/packet snapshots, watchdog threads and diagnostic file writes,
and hides informational decoder/profile/frame/audio-state debug readouts.
The SELECT+L+R component test card is also disabled unless debug is enabled.
Normal errors, subtitle/MilkDrop validation and network recovery stay enabled.
Existing log files are not deleted. The separate optional OC plugin has its own
`report` setting since it runs independently in other apps.

When enabled, trace files are:

- TV: `ms0:/PSP/SYSTEM/PSPStreamer-sync-tv.csv`
- LCD: `ms0:/PSP/SYSTEM/PSPStreamer-sync-lcd.csv`

Each file is overwritten by the next completed playback session using that output (including seek/reconnect restarts). Copy it off the Memory Stick after stopping the test video, before starting another one. Enable `debug=1`; no server update is required for recording. There are no trace file writes during playback. The trace retains up to 4096 samples: the first 32 decisions and subsequently at most one per second, overwriting the oldest rows when full.

Rows contain elapsed wall time, video PTS, submitted audio-block PTS, remaining DAC samples, outstanding PCM blocks, preparation/copy durations, display/drop counters and non-increasing audio-PTS counts. `action` is `0=wait`, `1=display handoff`, `2=drop`. `decode_us` includes ME lock wait, decode and CSC; `prepare_us` also includes overlays; `copy_us` measures transfer to VRAM and the display API call. `dac_rest_samples=-1` means the sample was unavailable or crossed an audio timestamp change.

The audio PTS describes the submitted block, not an exact sample at the speaker. The video handoff is measured on the PSP, not at the TV panel. The trace can expose internal stalls and timestamp discontinuities; it cannot directly measure a television's processing delay. The staging frame uses approximately 544 KiB on LCD or 1.41 MiB on TV, plus 192 KiB for the trace. The measured copy duration helps assess its cost on real hardware.

### Optional MilkDrop phase timings (`debug=1`)

Enable **Debug diagnostics** in settings (or `debug=1` in the configuration).
Play music, select a preset such as Cauldron painterly 3, and leave it running
for 30–60 seconds. Stop playback with START, then copy
`ms0:/PSP/SYSTEM/PSPStreamer-watch-music.txt` before starting another song.
No server update is required. This report contains the preset filename.

The `MilkDrop profile` sections separate LCD/TV, window/fullscreen and preset.
They report completed frames, throttled calls and average/maximum microseconds
for setup, frame/shape formulas, pixel formulas, audio snapshot/FFT, custom-wave
evaluation, geometry/command submission and final GPU wait. Custom-wave
evaluation includes its CPU geometry generation. Up to 16 distinct contexts
are retained per music playback; `untracked_calls` reports capacity overflow.
Failed frames and initial texture loading are not included. Disable automatic
preset switching for a clean comparison.

These are wall-clock measurements: thread preemption is included and CPU/GPU
work overlaps. `gpu_wait` measures only the remaining wait after submission,
not the GPU's total execution time. With batched shapes, intermediate GPU waits
are included in `geometry_submit`. Throttled calls are not failed frames;
the existing scheduler deliberately leaves time for audio and controls.
Counters stay in RAM during playback and are appended only at music teardown.
With `debug=0`, profiling takes no timestamps and produces no report.

Direct FFT counters also cover built-in spectrum wave mode 8, whose work is
otherwise inside `geometry_submit`. `fft_prepare`, `fft_transform` and
`fft_magnitude` report time per warm FFT call; `cold_calls`/`cold_total_us`
separate initial window setup. These are nested costs, not additional frame
time; completed FFTs count even when a later frame stage fails. See the
[VFPU assessment and measurement instructions](docs/VFPU_REVIEW.md).

### Optional playback-stall diagnostics (`debug=1`)

An FLV reader failure (including `FFFFFAD8` / `-1320`) also creates
`ms0:/PSP/SYSTEM/PSPStreamer-stream-error.txt`. This does not wait for the
eight-second stall threshold: the reader captures the failure in RAM, and
the UI saves it after joining that reader, before the remaining teardown.
It records the failing phase, TCP EOF versus socket error versus inactivity
timeout, socket errno/poll events, partial-read sizes, received bytes, last
audio/video tag timestamps, time since received data and APCTL state at failure.
Parser/allocation failures are reported separately from network read failures.
No video bytes, credentials or media filenames are included. A new FLV error
replaces the previous report; healthy playback does not erase it. Save it with
the sync CSV and watchdog file after an unexpected video stop. These changes
do not adjust streaming/startup/subtitle timeouts or implement new recovery.

With diagnostics enabled, the client watches both music and video, including teardown. Before
playback it creates `ms0:/PSP/SYSTEM/PSPStreamer-watch-music.txt` or
`ms0:/PSP/SYSTEM/PSPStreamer-watch-video.txt` and checks that its worker can append
`monitor running`. A failed storage/monitor check stops playback with a
`Diagnostic file` error instead of silently losing diagnostics. Each new playback
replaces that media kind's previous file: copy evidence before starting another.
After eight seconds without a main-loop heartbeat, playback progress or a
successful remote poll, the
monitor records the stage, queues, codec state and remote HTTP phase/counters.
Music progress is counted in DAC blocks, video progress in presented PTS.
At most four reports are written, at least 30 seconds apart. Healthy playback
only writes at startup and shutdown, not on rendering/audio ticks.

After a hang, wait about 10 seconds before leaving via the PS button, then
copy both files. A higher-priority CPU lockup can still prevent a stall report,
but the startup record should already exist. This is diagnosis, not a claim
that every hang is fixed. Decoder, PTS and subtitle preparation timeouts remain
unchanged. Remote commands alone use a cancellable two-second HTTP budget and
the already resolved server address. HA app 0.1.22 adds server-session identity
so restarting the server cannot strand the client's command counter. This is
separate from investigating commands that only execute after a PSP app restart.
HA app 0.1.21 fixes the
web remote incorrectly treating subtitle index 0 as Off; update the app and
reload the browser page to receive that fix.

## Requirements

- Python 3.11 or later
- FFmpeg with `libx264` and `libmp3lame`
- A PSP with working infrastructure Wi-Fi and CFW for the homebrew app

## Run the server directly

`MEDIA_ROOTS` is a colon-separated list of allowed media roots. The server never follows paths outside these roots.

```bash
cd /path/to/PSPStreamer
MEDIA_ROOTS='/srv/media/Serien:/srv/media/Filme' PORT=8091 \
  python3 -m psp_streamer.server
```

The test interface is then available at `http://SERVER:8091`. It is useful for browsing and checking transcoding; reliable playback happens in the PSP app.

`psp_streamer.server` is the module in `psp_streamer/server.py`. The root-level
`server.py` is now only a compatibility launcher (`python3 -m server` also
starts the current implementation). The obsolete synthetic A/V calibration
streams have been removed in server 0.1.32; ordinary media is unaffected.

An independent, disabled-by-default experimental overclock plugin is documented
in [psp-overclock/README.md](psp-overclock/README.md). It is not required for
PSP Streamer and must not run alongside another clock controller.

| Variable | Default | Purpose |
| --- | --- | --- |
| `MEDIA_ROOTS` | `/media` | Allowed video directories, separated by `:` |
| `PORT` | `8091` | HTTP-Port |
| `MAX_TRANSCODES` | `4` | Concurrent FFmpeg processes; one current PSP playback needs one |
| `PGS_CACHE_TRACKS` | `1` | Number of decoded PGS tracks retained in host RAM |
| `FFMPEG_PRESET` | `veryfast` | x264-Preset |

## Docker

The SMB/NFS mount belongs on the Docker host. Mount it read-only only.

```bash
MEDIA_ROOT_PATH=/srv/media PSP_STREAMER_PORT=8091 docker compose up -d --build
```

Do not put SMB credentials in `compose.yaml`.

## Home Assistant integration (HACS)

The **custom integration** adds a `media_player` entity, a media browser and
playback controls to Home Assistant. It connects to an existing PSP Streamer
server (Docker, the Home Assistant app/add-on, or standalone Python); it does
not replace that server or run FFmpeg itself.

In HACS, add `https://github.com/dreimer1986/PSPStreamerHA` as a **custom repository**
of type **Integration**, download **PSP Streamer**, and restart Home Assistant.
Then open **Settings → Devices & services → Add integration → PSP Streamer**
and enter the server URL and its password. Requires Home Assistant **2026.3+**,
server **0.1.44+**, and the matching updated PSP application for playback telemetry.

See [Home Assistant integration setup, controls and limitations](https://github.com/dreimer1986/PSPStreamerHA/blob/master/docs/HOME_ASSISTANT.md).
The HACS integration has its own repository, independent of PSP application
releases. This repository remains the source for the server app/add-on and PSP
application. Adding it to the app/add-on store does **not** install the integration.

## Home Assistant add-on

PSP Streamer can run as a Home Assistant add-on, keeping FFmpeg and the media
server off the desktop PC. In Home Assistant, open **Settings → Add-ons → Add-on
Store**, open the menu, choose **Repositories**, and add:

```
https://github.com/dreimer1986/PSPStreamer
```

Refresh the store, select **PSP Streamer**, and install it. The add-on exposes
Home Assistant's `/media` directory read-only at port `8091`; mount the SMB/NFS
library into that directory on the Home Assistant host. Configure `port` (default
`8091`) and `max_transcodes` (default `4`) in the add-on configuration, then
start it. Point the PSP configuration at the Home Assistant host or its DynDNS
name with `server=…` and `port=8091`.

**Update the server/add-on to 0.1.20 or later for the current client.** Version 0.1.19 introduced FLV streaming; 0.1.20 also provides automatic continuation for remotely started playback. Refresh the add-on store, install the update and restart the add-on. Media mounts and options stay unchanged. The updated server retains the old raw H.264/MP3 endpoints for older clients; the new video client does not fall back to their estimated timing.

### Automatic continuation from the web remote

After a remote seek, the PSP consumes both the seek request and its pending
resume flag before restarting playback. Update the PSP application to include
this fix: older clients could return to the browser at the later natural video
end instead of advancing. This applies equally to the web and Home Assistant
remotes; server 0.1.45's separate PGS end-of-stream fix remains necessary for TV
bitmap burn-in. With debugging enabled, the video watchdog log now includes a
final `playback outcome` line with EOF/seek/resume flags.

After a remotely started episode ends naturally, the PSP requests the next video in the same folder, using natural filename order (episode 2 before episode 10). This does not depend on the folder currently open in the PSP browser or its visible page. Playback stops at the last video; it does not descend into other folders or switch to music. Stop, playback errors and seeking do not trigger next-episode playback.

Music continues through audio files in the same folder. The PSP's saved shuffle option also applies to remotely started music; shuffle excludes the current track, while sequential playback stops at the folder end. Requested audio/subtitle track indices are reused for the next video and checked against its available tracks.

The updated PSP client also accepts Pause, Resume, Stop and Seek during
music, including tracks started using the PSP buttons. Seek restarts the
stream at the requested position and resumes playback. Selecting another
file and pressing Play during music **or video** first stops and releases
the current playback resources, then starts the requested file (including
switching between audio and video). A Stop or replacement Play does not
trigger automatic next-track playback. These client changes use the existing
server API; no server/add-on update is needed.

Commands are polled in background workers, not in the music drawing or DAC
loop. Allow normal network/polling and teardown/startup time for a command to
take effect. The existing server stores the latest command, not a playlist
or a queue of rapid button presses. The browser's progress control is a seek
target, not a live report of the PSP playback position.

The read-only endpoint is `GET /api/media-next/<media-id>?shuffle=0` (`shuffle=1` for shuffled music). It returns `{"id":"...","kind":"video"}` or `{"id":"...","kind":"audio"}`, and `{}` when there is no successor. It does not enqueue remote commands. Update both the server and PSP app to use remote continuation. The web page's selected-file details still describe the file selected in the browser, not a live report of the automatically selected successor.

## Install and configure the PSP app

After a build, install the complete contents of `psp-client/release/PSPStreamer/` to:

```
ms0:/PSP/GAME/PSPStreamer/
```

The first time a playback option is saved, the app creates this file:

```
ms0:/PSP/SYSTEM/PSPStreamer.cfg
```

The server is configurable without recompiling or a PC: press **Select in the
file browser** for Settings. Up/Down selects, Left/Right changes values,
Cross opens text/number entry, Start saves and Circle cancels. TV startup mode
applies on the next app launch. You can also edit the config file directly:

```ini
server=streamer.example.net
port=8091
server_password=
https=0
debug=0
music_cpu_mhz=0
milkdrop_cpu_mhz=0
video_cpu_mhz=0
idle_cpu_mhz=0
screen_idle=0
audio=0
subtitle=-1
quality=2
video_fps=20
play_mode=stream
music_preset=active.milk
preset_auto=0
preset_seconds=60
preset_fade_ms=1500
milkdrop_high_resolution=1
volume=24
shuffle=0
language=en
tv_ui=off
```

`server` accepts an IPv4 address or DNS/DynDNS name; an `http://` or `https://`
prefix is also allowed in the file. `https=0` selects HTTP, `https=1` TLS;
when both are present the last setting wins. `port` is explicit in either mode.
Enter just the hostname/IP in the app's Server host field, not a URL.

Optional CPU profiles in Settings: `music_cpu_mhz` is music with the spectrum
display, `milkdrop_cpu_mhz` is music with MilkDrop, `video_cpu_mhz` is video,
and `idle_cpu_mhz` is the media browser. Each accepts `0` (default: plugin's
INI target) or 66–471 MHz. Left/Right changes by **1 MHz**; Cross opens direct
number entry. The optional [StreamerOC plugin](psp-overclock/README.md) needs
`enabled=1` and `app_control=1`. Above 333 MHz, requests are limited to the
plugin's configured `target_mhz`; set that only to an overclock you have tested.
Without the plugin no clocks are changed. All profiles default to disabled.
Track/episode changes, seeks and radio reconnects keep the current clock while
loading. The next active renderer requests its profile only if it differs;
there is no intermediate return to the INI target or browser clock. Returning
to the media browser applies its idle profile directly (`0` releases to the
INI target). An explicitly disabled next playback profile (`0`) also releases
the previous override. App exit retains the coordinated plugin shutdown.
Start underclock testing at 222 MHz, not 66 MHz: low clocks can slow the
browser, decoding and storage/network I/O. Requested MHz are not guaranteed
exact physical frequencies. Enable plugin `overlay=1` to see changes.

**Select → StreamerOC plugin → X** opens the optional plugin's own settings.
Up/Down selects a row; Left/Right changes it. The submenu exposes `enabled`,
`target_mhz`, `enforce`, `enforce_unlimited`, `app_control`, `report` and `overlay`.
**Start saves this INI independently of the outer app settings; Circle cancels.**
Changes take effect at the **next application start**, not in the running plugin.
The file is `ms0:/SEPLUGINS/StreamerOC/StreamerOC.ini` (an existing `ef0:` file
is used if the Memory Stick file is absent). A missing INI starts disabled;
install the plugin/directory first. Invalid INIs are not overwritten. Saving
validates all values, writes a temporary file and keeps the previous INI as
`StreamerOC.ini.bak`. Comments are replaced with the standard seven-key layout.
The editor does not install/enable ARK's plugin entry or test clock stability.
Overlay offers `0=Off`, `1=polling` (the previous default) and
`2=framebuffer hook` (experimental, opt-in). Mode 2 follows standard user-mode
framebuffer submissions; apps using direct/kernel presentation can bypass it.
Switch back to 1 if an app has display problems. Restart the application after
changing the mode; this is not an in-session display patch switch.

`screen_idle=0` keeps the display awake as before; `1` allows normal LCD
power saving during music; `2` allows it throughout the app. Dimming/off
timing comes from the PSP's power-saving settings. Buttons wake the screen
and retain their usual functions. System standby is always prevented while
the app runs. TV output always stays awake so video/sync and external audio
continue. These options also have pages in the built-in Settings help.

DNS is refreshed by library/media setup; remote polling uses that cached address.
`audio` (0–7) and `subtitle` (-1–31) store preferred video-track indices
(`subtitle=-1` disables subtitles); `quality` means `0=96k`, `1=128k`, `2=160k`,
`3=V6`, `4=V5`, `5=V4`, `6=V3` MP3; `volume` ranges from `0` to `30`;
`shuffle=1` randomly continues with another audio file from the current folder
(`0` keeps its listed order). See [settings and HTTPS](docs/HTTPS_AND_SETTINGS.md)
for text entry, password changes, certificates and deployment examples.

`play_mode=stream` (default) or `play_mode=download` selects the PSP's video
playback workflow. Change it in the video options dialog; music and immediate
web-remote Play commands continue streaming. Web conversion jobs have their
own explicit button and per-job settings.

MilkDrop automation: `preset_auto=0` disables automatic changes (default);
`1` selects in order, `2` randomly and `3` randomly weighted by each preset's
`fRating`. `preset_seconds` accepts 30–600 seconds (default 60).
`preset_fade_ms` accepts 0–5000 (default 1500; 0 means a hard cut).

`milkdrop_high_resolution=1` selects 512×512 feedback (default); `0` restores
the faster 512×256 mode, including the original 32-bit LCD feedback. Change it
in **Settings → MilkDrop resolution** and save with Start. No app restart is
needed: it applies when music/visualization is next started. The setting affects
both LCD and TV, windowed and fullscreen, not video playback or spectrum mode.
In the Circle preset browser, **Square** cycles modes and **Triangle** cycles
30/60/120-second intervals. Settings are saved when leaving the browser.
An optional `presets/playlist.txt` limits automation to one exact filename per
line; otherwise it uses the sorted preset directory. See
[automation, instances and large waves](docs/MILKDROP_AUTOMATION.md).

### Audio quality and film frame rate (server/HA app 0.1.25)

The PSP playback-options dialog and browser remote offer CBR 96/128/160 kbit/s
and VBR V6/V5/V4/V3 for music and video. V5 is the balanced VBR choice; V6
favours smaller streams and V4/V3 favour quality. VBR controls average quality,
not a strict bandwidth ceiling; individual frames can reach 320 kbit/s.
Existing installations keep their CBR preference (default: 160 kbit/s).

For videos, choose **20 fps** (compatibility/default) or **23.976 fps**
(`24000/1001`, film/anime). The config key is `video_fps=20` or
`video_fps=24000/1001`. This applies to both LCD and TV output, without changing
the audio clock or container-timestamp synchronization. Matching a 23.976 fps
source avoids the frame dropping needed for 20 fps, but requires about 20%
more video decoding and presentation work. It does not eliminate 60 Hz display
cadence or network stalls. Video bitrate limits remain unchanged.

Local PSP choices are saved for subsequent playback. The browser saves its own
preferences in local storage and sends them with Play; those choices also apply
to following files in that remote playback session. Commands from older remotes
without these fields retain the PSP's current choices. Update both the server
and PSP application before using the new options. Music hides the frame-rate
selector, just as it hides video-track/subtitle selectors.

`language` selects the PSP interface language: `en` (default) or `de`. The PSP's bundled Latin-1 font supports direct German `ä`, `ö`, `ü`, and `ß` characters.

`tv_ui=off` (default) preserves LCD menus with TV video playback. Set `tv_ui=auto`
to use the separate native 720×480 TV interface when a supported component
cable is connected at app startup. This includes the library, loading screen,
file information, playback options and music receiver/spectrum. Without that
cable, or if TV initialization fails, menus remain on the LCD. Hold **L while
starting the app** to bypass automatic TV menus for that session without
changing the saved setting. Restart the app after changing `tv_ui`.

You can also switch the menu and music display **after startup**: stop playback,
open **SELECT → Switch menu to TV/LCD** (one Down press, then **X**).
**Circle** returns to the library. Connect the component cable before switching
to TV; returning to LCD works even after unplugging it. This action takes effect
immediately, requires no Save or restart, and leaves `tv_ui` and any unsaved
settings unchanged. Video still follows the cable at its next start, including
the existing LCD-menu/TV-video mode. Switching during playback is not supported.

The TV artwork is a separate embedded asset, not an enlarged LCD screenshot.
Its volume dial is prepared for a 16:9 display; use the corresponding TV/OSSC
aspect setting. The startup check detects a cable, not whether the TV is
powered on. Automatic hotplug switching during playback is not provided.
The TV interface adds a 1.41 MiB RAM drawing buffer only when enabled; its
embedded artwork adds about 1.32 MiB to the executable. The current tested
video staging/PTS scheduler is unchanged. Host tests verify rendering bounds
and mode ownership, but physical PSP/OSSC transition tests are still required.

### Adding a PSP interface language

Create `psp-client/lang_xx.h` by copying `lang_en.h`. Each visible text has its own named `TXT_*` entry and related entries are grouped by interface screen, so translations can be edited without relying on array order. Preserve printf placeholders such as `%d`, `%s`, and `%.48s`. Include the new file in `psp-client/language.c`, then register its code and table in the `languages[]` array there, for example `{"fr", lang_fr}`. Rebuild the EBOOT and set `language=fr` in `PSPStreamer.cfg`.

For the illustrated manual, also copy `help_en.h` to `help_xx.h`, translate the
named title/step fields, and include/register the table in `help_pages.h`'s
`help_translation()` list. Unregistered help languages fall back to English.
Keep each step to one short title and one explanation line. The help layout
test measures all English/German pages with the bundled LCD and TV glyphs;
add new languages to the same test when translating.

### Illustrated help on the PSP

In the library, press **Select → Help → X**. Help is the first settings entry,
not a configuration value. **Square** in playback options opens the relevant
music/options topic; **Select** in Local storage or the server queue opens the
download topic. Help works offline, does not save or discard pending settings,
and needs no extra image files on the Memory Stick.

**L/R** selects a topic, **Up/Down** selects its subpage, and **Circle** returns
to the screen you came from. The 17 pages cover browsing, playback options,
video controls, music, visualizations/presets, downloads, settings/text entry,
TV output and connection recovery. Each page shows a PSP diagram with the
relevant buttons in gold and three short instructions. Both LCD and native TV
render text and the diagram directly; help does not switch video-output modes.
Routine footer hints now show only the essential actions and help entry point.

Fullscreen is simpler: **Triangle alone** toggles it during music or LCD video.
The old **Cross+Triangle** gesture still works; holding Triangle does not repeat
the toggle. In the preset chooser, Triangle still changes the preset interval.

Browser controls: Cross opens a folder or playback options; Triangle opens the media information page for a file; Circle exits the options screen; Left goes to the parent folder; held L/R pages through the list; Square reloads; Start exits the app.

Playback controls: In fullscreen video (including TV), Select opens/closes the
transport overlay without pausing. Left/Right selects a button, Cross activates
it and Circle closes the overlay. Buttons are previous file, -30 seconds,
-10 seconds, Pause/Play, +10 seconds, +30 seconds and next file. File switches
use the existing complete decoder/audio teardown, stay in the same folder and
do not wrap; at a folder boundary the player returns to the browser. They also
work for remotely started video with server/HA app 0.1.24 or newer. Outside
fullscreen, Select retains direct pause/resume. Music controls are unchanged.
L/R still seek ±10 seconds and Start returns to the browser. Track titles such
as `Forced` or `Full` appear beside language labels when available.

Receiver controls: Circle shows/hides the receiver strip, Up/Down adjusts and stores volume (hold either direction for a slow repeat), and Triangle toggles fullscreen (Cross+Triangle also works). Fullscreen works for video and for the audio spectrum display.

With the normal spectrum selected (Square cycles back from MilkDrop),
Triangle fills the LCD or native TV canvas with spectrum bars, without
the receiver strip. The existing 20-Hz visual update limit and changed-area-only
writes remain in place. Pause lets the bars decay; volume and Stop still work.
Fullscreen preference remains active across music tracks.

Server/HA app **0.1.23** hides audio-track and subtitle selectors for music and
uses their neutral values when sending a music Play command. Video selectors
return when selecting a video. Update the HA app and reload the browser page.
Remote commands have a **15-second validity window from submission**; polling
does not extend it. After expiry, the server returns Idle with the current
sequence number, so starting the PSP app cannot replay an old command. Expiry
does not stop playback that has already started, nor limit subtitle preparation.

## Subtitles and limitations

ASS/SSA, SRT, WebVTT, and other FFmpeg-readable text tracks are converted once into compact, timed cues. The PSP overlays a compact outlined DejaVu Sans bitmap locally. HDMV PGS subtitles use native palette-indexed sprites, preserving their original colour and outline without burning them into video. Other bitmap formats (VobSub/DVDSUB, DVB, XSUB) continue to use the server fallback.

Keep `subtitle_font.raw` and `cooleyesBridge.prx` beside `EBOOT.PBP`. The compact DejaVu Sans Latin-1 atlas is loaded only after the AVC decoder is ready; if it is missing, video playback remains safe and text subtitles are simply not drawn. The receiver artwork is embedded in `EBOOT.PBP`; no separate `menu_skin.raw` is required.

Component TV playback uses native 720×480 output. By default, the browser and options stay on the PSP LCD and video switches to the TV. With `tv_ui=auto`, TV menus and video share the same output mode: Stop/end returns to the TV menu without an LCD mode reset. The existing Select+L+R TV check is available from either browser and returns to its originating output. Text subtitles remain local overlays; TV bitmap subtitles use server-side burn-in to avoid sprite-transfer stalls at the higher resolution. The server website can select media and send play/pause, stop and seek commands. This GUI update does not require a server/add-on update.

During music, **Square** cycles **spectrum analysis → MilkDrop → Cave → spectrum**.
MilkDrop uses the selected preset file (initially `presets/active.milk`). Copy
the supplied `psp-client/presets` folder beside EBOOT. The three early built-in
test effects are no longer part of the user-facing selection.

**Cave** is a separately selectable Monkey geometry study: an independently
implemented Marching Cubes surface over compact radial fields and three-octave
noise, following the algorithm identified in the DLL. It adds branching walls,
depth testing, procedural rock texture and distance fog. Octave rotations and
offset ranges now follow the recovered generator, and interpolated field-gradient
normals give the walls smooth lighting. Sixteen contributors now use the
reconstructed base oscillator controller, with changing radii and cached profiles.
Small, distance-weighted random variations now affect odd side paths, without
moving the main camera path; values are cached rather than regenerated each frame.
The recovered alternate cubic paths now blend with the oscillators; the camera
looks six profiles ahead, and continuous base texture coordinates replace
per-triangle projections. This is **not yet a complete reproduction of Monkey**:
the camera now uses chained profile curvature, source sway, roll impulses and
distance-dependent banking. Classic cube topology, weighted material fields,
source material normals, spatial RGB/alpha envelopes and two-light shading follow
the recovered formulas. Nine styles include displaced wireframe and Hair;
the two texture banks blend with separate UV coordinates. PSP geometry budgets,
fallback replacement textures, random sequence, audio analysis and some
camera/projection configuration branches still differ. See the
[comparison audit and adaptations](docs/MONKEY_GEOMETRY.md).
Only one new depth slab is built per visual update; completed geometry is cached.
Three rear slabs remain visible for camera turns without shortening the forward horizon.
Adjacent slabs reuse their shared field/gradient plane, and trigonometric path
parameters are prepared once per plane rather than once per sample.
The render target is fixed at 512×256 RGB565, including on TV, to fit a real depth
buffer alongside the scanout. MilkDrop's resolution setting remains unchanged
for MilkDrop. No new assets or server update are needed.
Triangle toggles fullscreen; the selected mode survives song changes. From
spectrum, press Square twice for Cave, once more to return to spectrum.
Circle opens Cave's effects settings while Cave is selected; in MilkDrop it
opens the preset list. Select inside that list opens additional automatic-switch
options. Start stops normally. Audio clocks and queues
are untouched. The original tube prototype was removed after the cave renderer
passed the LCD/TV hardware test. Its source remains available in Git history.

### Visualization settings

While music plays, press Circle in Cave. Use Up/Down to select a row,
Left/Right to change it, and Circle to save and return. The music keeps playing.
These settings are saved in the existing `pspstreamer.cfg`:

| Key | Default | Meaning |
| --- | --- | --- |
| `cave_fog` | `1` | Distance fog, 0/1 |
| `cave_multitexture` | `1` | Two texture banks, 0/1 |
| `cave_hair` | `1` | Hair in the corresponding style, 0/1 |
| `cave_transparent_hair` | `1` | Hair transparency, 0/1 |
| `cave_beat` | `1` | Music-triggered responses, 0/1 |
| `cave_sensitivity` | `8` | Beat detection sensitivity, 0–16 |
| `cave_amplitude` | `8` | Beat response strength, 0–16 |
| `cave_style` | `-1` | Automatic styles; 0–8 fixes a style (7 = Hair) |
| `cave_speed` | `100` | Travel speed, 10–200 percent; affects automatic and Easter-egg flight |
| `cave_invert_y` | `0` | Invert the flight stick's vertical axis, 0/1; when enabled, pushing up dives |
| `preset_random_seconds` | `10` | Additional random automatic-switch delay, 0–120 seconds |
| `preset_hard_cuts` | `0` | Enable music-triggered immediate preset switches, 0/1 |
| `preset_hard_threshold` | `250` | Hard-cut sensitivity threshold, 125–400 percent |
| `preset_hard_seconds` | `60` | Hard-cut threshold recovery parameter, 5–180 seconds |

MilkDrop's four new options are available with Select inside the playing-music
preset list. Automatic mode must be enabled for timed or hard-cut switches.
Existing interval/fade controls remain; ordinary transitions still use a snapshot
fade, not two simultaneously running presets. No new resolution options were added.

Use Up/Down to reach the second page of Cave settings (speed and inverted flight).
Try 30–50 percent for a calmer tunnel ride; the default 100 keeps the existing
speed. The shoulders still temporarily slow/accelerate Easter-egg flight on top
of this setting. Geometry preparation continues to bound the maximum speed.

Cave's background now follows the recovered animated, brightness-limited palette
even with fog disabled. When enabled, fog blends distant walls into that same
color, rather than always into black. Black-material modes retain the original
exception: with fog off, they clear to black. Fog depth is scaled to the PSP's
shorter prepared horizon, not the desktop's longer view distance.
The original additional ambient-light term for enabled multitexture is included.

### Installing original Monkey textures (optional)

Use the images from your own original Winamp Monkey installation. They are not
included in PSPStreamer. No extraction from the DLL or manual conversion is needed:

1. On your PC, open `Winamp/Plugins/monkey/`.
2. On the Memory Stick, create `PSP/GAME/PSPStreamer/monkey/` beside `EBOOT.PBP`.
3. Copy these seven files into that folder, keeping their names:

   ```text
   supertex_a1.jpg
   supertex_a2.jpg
   supertex_a3.jpg
   supertex_a4.jpg
   supertex_a5.jpg
   supertex_b1.jpg
   supertex_b2.jpg
   ```

4. Start Cave again to load them. No configuration entry is required.

For example, the first file must be at
`ms0:/PSP/GAME/PSPStreamer/monkey/supertex_a1.jpg`, not inside `presets/`.
The original 512×512 JPEGs work unchanged: the loader downsizes them to 256×256
for PSP memory limits. Replacement JPEGs may be up to 1024×1024; PNGs must have
power-of-two dimensions from 16 to 256. Each file is limited to 1 MiB.
The extensions `.jpg`, `.png`, then `.jpeg` are tried in that order per slot.

Missing or invalid files use generated replacements individually, so partial
sets also work. Only use images you are entitled to use; do not redistribute the
original assets with the application. Seven maximum-size images use 1.75 MiB of
additional memory, released when leaving the renderer. With diagnostics enabled,
`Cave external textures: 7F` confirms that all seven slots loaded successfully.

<details>
<summary>Cave Easter egg</summary>

During Cave playback, press **L+R together** to enable or disable flight.
The analog stick steers, L alone slows down and R alone accelerates. The tunnel
still follows its generated path; a bounded density check restricts lateral
camera movement. This is not a game with enemies or mesh-accurate collisions.
Triangle still switches fullscreen and Start stops playback.

Flight starts disabled, is not saved, and is reset when the renderer is closed
(including opening its options). When disabled, flight performs no field checks
or mesh draw calls and does not alter the normal camera, timing or random state.
The ship mesh is embedded; no extra file needs copying to the memory stick.
“Low Poly Spaceships” is by Samuel Metters, CC BY 4.0; see
[asset attribution and conversion details](psp-client/assets/cave_ship.CREDITS.md).

</details>

Legacy `fShader` hue shading now uses fixed-function corner colors; compare
`legacy-shading-demo.milk` and `legacy-shading-off-demo.milk`. Both also exercise
echo zoom below 1. This is not HLSL shader support.
Numbered formula entries now compile together, including multiline loops,
comments and split tokens. Try `multiline-formula-demo.milk`.
The [collection audit](docs/MILKDROP_MULTILINE_AUDIT.md) separates successful
loading from host-side formula execution and actual PSP playback testing.
The [parser/density update](docs/MILKDROP_PARSER_DENSITY.md) adds larger compiled
formula blocks, empty-statement/point-input compatibility, a 16×16 warp grid
with a budget-aware fallback, and up to 1024 custom-wave points. Test with
`extended-formula-demo.milk`, `fine-mesh-demo.milk`, and `dense-wave-demo.milk`.
The [phase-budget update](docs/MILKDROP_PHASE_BUDGETS.md) accelerates recognized
init zero-fill loops, separates once-per-frame work from point-call limits,
and reduces dense custom-wave point counts within the unchanged total frame
budget. Try `phase-memory-demo.milk` and `wave-budget-demo.milk`.
The latest [import/resource update](docs/MILKDROP_RESOURCE_LIMITS.md) raises
file/formula capacity, adds sparse local memory and compact uniform fills,
and gives expensive setup/main-frame formulas a separate, bounded allowance.
Shared memory has 16384 resident floats; each context has 8192 local floats.
Both address spaces span 1048576 logical cells. Writes requiring another page
after the resident pool fills are discarded, without aliasing existing data;
this deliberate PSP approximation can change a preset's appearance.
Try `local-sparse-fill-demo.milk` and `sparse-global-demo.milk`.
No server or OC-plugin update is required.
The **[current MilkDrop compatibility guide](docs/MILKDROP_COMPATIBILITY.md)**
is the authoritative feature/limit summary; older linked guides describe
historical batches. The latest batch adds EEL operators, assignment expressions,
loops, shared registers, local/global memory, engine dimensions and monitor
state. Try `eel-memory-orbit-demo.milk`, `eel-grid-logic-demo.milk` and
`eel-wave-loop-demo.milk`. External PNG textures now have a bounded,
fixed-function baseline: try `external-texture-demo.milk` together with
`presets/textures/checker.png`. See the compatibility guide for supported
dimensions and the `psp_texture_0`–`psp_texture_3` extension. Shaders and their
arbitrary texture sampling remain excluded. Numbered warp/composite shader
source is silently skipped so supported non-shader parts can run. Other preset
errors still report normally; shader-heavy presets can look different or blank.
Try `shader-fallback-demo.milk` to check this fallback. Building the PSP client now also
requires PSP libpng, libjpeg and zlib (host rendering tests require their host
libraries; texture fixture tests also use Python Pillow). JPEG/JPG textures are
supported alongside PNG via `psp_texture_0=example.jpg` in the preset and a
`textures/example.jpg` file beside it. JPEG images are resized once on loading;
see the texture limits in `docs/MILKDROP_COMPATIBILITY.md`.
During music, **Circle** opens the in-app preset browser: Up/Down selects,
L/R pages, Cross opens folders or applies a file, Circle cancels. The `..` entry
goes up one folder. The same folder browser is available without playback at
**Select → MilkDrop preset → X**; **Start** in the outer settings saves that
selection. Paths are relative to `PSPStreamer/presets/`, e.g.
`music_preset=Geiss/Hyperdrive.milk`. Absolute paths and `..` components are
rejected. Automatic changes use the selected preset's folder and its optional
`playlist.txt`, not the entire folder tree. Textures remain relative to each
preset's own directory. Manual `active.milk` replacement is no longer required.
The browser lists up to 128 entries per directory (including `..`) and warns
when that limit is exceeded. All built-in waveform modes 0–8, a real FFT waveform,
bounded motion vectors and `frame`/`fps` inputs are now available. See the
[waveform pack, controls and test presets](docs/MILKDROP_WAVE_PACK.md).
Frame formulas can also animate `wave_mode` and all nine `mv_*` fields.
Try `wave-switch-demo.milk` in the preset browser; see
[dynamic waveforms and motion vectors](docs/MILKDROP_DYNAMIC_WAVES.md).
Custom shapes now have separate bounded init/frame formula contexts, including
`t1`–`t8`, current preset `q` inputs and persistent named variables. Try
`shape-orbits-demo.milk`; see [shape formulas and limits](docs/MILKDROP_SHAPE_FORMULAS.md).
Custom PCM waves support separate init/frame/point formulas, stereo inputs,
per-point colors, dots, thick lines and additive drawing. Try
`custom-wave-demo.milk` and `custom-wave-fft-demo.milk`; see
[custom-wave controls and compatibility limits](docs/MILKDROP_CUSTOM_WAVES.md).
Custom waves also accept stereo FFT data (`bSpectrum=1`). Line waves now use
MilkDrop-style spline subdivision; dots retain their original points. The
fixed-function image effects (brighten, darken, solarize, invert and darken
center) are available as static fields and frame formulas. See the
[spectrum, smoothing and image-effects batch](docs/MILKDROP_SPECTRUM_EFFECTS.md)
for test presets, memory changes and the remaining compatibility gaps.
The [native-input audit and current work list](docs/MILKDROP_NATIVE_INPUTS.md)
describe context-local writable EEL inputs and the `native-inputs-demo.milk`
test preset. Older milestone reports retain their historical scope.
For shapes crossing the image boundary, see [offscreen shape clipping](docs/MILKDROP_OFFSCREEN_SHAPES.md)
and test `offscreen-shapes-demo.milk`.
The completed [wave/transform range package](docs/MILKDROP_GEOMETRY_RANGES.md)
has one combined hardware test: `geometry-range-demo.milk`.
The next batch adds up to eight instances per shape, 512-point custom waves,
read-only `instance`/`instances`/`progress` inputs, optional ordered/random/rated
preset cycling, playlists and snapshot-to-live crossfades. Try
`shape-instances-demo.milk` and `large-wave-demo.milk`; details and resource
limits are in the [automation guide](docs/MILKDROP_AUTOMATION.md).
See [supported fields, time formulas and limits](docs/MILKDROP_PRESETS.md).
For animated rotation and colors, copy `presets/time-demo.milk` over
`presets/active.milk` on the PSP, then stop and restart music.
For music-reactive transforms and colors, use `presets/music-demo.milk`
instead. It uses the existing PSP spectrum/VU snapshots, with optional
time-based smoothing; no additional audio analysis is required.
`presets/relative-demo.milk` uses the original `bass/mid/treb` and `*_att`
names with relative-to-history envelopes. Their PSP frequency-analysis
approximation and `min/max/sqrt` formula support are documented in the subset guide.
The [Hyperdrive target subset](docs/HYPERDRIVE_TARGET.md) additionally supports
the real circular PCM waveform (mode 0), `dx/dy`, texture clamp and gamma
brightness. Copy your original preset as `presets/active.milk`; third-party
presets are not bundled in Git. This is not full MilkDrop compatibility.
Visualization feedback now renders at **512×512**, using RGB565 on LCD and TV.
TV preserves raw feedback in a 512 KiB main-RAM buffer using a GPU copy; if that
allocation fails it retains 512×256 feedback. LCD uses two EDRAM surfaces.
Echo/brightness are composed offscreen before presentation;
see [memory layout and hardware-test notes](docs/MILKDROP_PRESENTATION.md).
The [extended fixed-function subset](docs/MILKDROP_FIXED_FUNCTION.md) adds
animated transform centers/stretch, wave styling, echo, borders and four
static custom shapes (including feedback-textured shapes). Try
`receiver-fx-demo.milk` and `echo-dots-demo.milk` as the active preset.

Custom shapes also support `thickOutline=1`. The `outline-demo.milk` preset
compares thin (left) and thick (right) cyan outlines. See
[shape-outline rendering and testing](docs/MILKDROP_SHAPE_OUTLINES.md).

`nWaveMode=4` adds the stereo-driven horizontal script waveform alongside
the existing mode 0 circle. Try `script-wave-demo.milk`; see
[waveform behavior and hardware checks](docs/MILKDROP_SCRIPT_WAVE.md).

`nWaveMode=1` adds the stereo spiral; try `spiral-wave-demo.milk`.
See [spiral waveform details](docs/MILKDROP_SPIRAL_WAVE.md).
The [conditional formula subset](docs/MILKDROP_CONDITIONS.md) adds lazy `if`,
Boolean functions and additional math; `branch-beat-demo.milk` demonstrates
music-reactive changes of wave style, color, zoom and echo intensity.
The [initialization/q-variable subset](docs/MILKDROP_INIT.md) adds
`per_frame_init_*` and `q1`–`q32`; try `init-orbit-demo.milk` as the active file.
Up to 128 [named persistent variables](docs/MILKDROP_COMPATIBILITY.md) per context can retain
state between frames; `memory-pulse-demo.milk` holds and releases bass impulses.
Bounded [per-grid-point formulas](docs/MILKDROP_GRID.md) add local transforms;
try `grid-twist-demo.milk`. This is an interpolated 8×8 mesh, not pixel shaders.
**Triangle** (or **Cross + Triangle**) toggles receiver view and true visualization fullscreen
(480×272 LCD / 720×480 TV). Fullscreen hides the receiver controls, but
pause, volume and remote commands still work. Without an active visualization,
the existing enlarged spectrum/receiver layout is unchanged.
This adapts MilkDrop 2's no-shader
warp equations to PSP GU, not its complete desktop engine: arbitrary `.milk`
files and shader presets are not guaranteed compatible. The custom slot now
supports the reference's public expression function/operator families within
documented PSP limits. Music remembers the selected effect
and fullscreen across track changes, including autoplay/shuffle and remote
selection. Resources are still released and recreated per track. Video uses
its own presentation mode; restarting the app resets music visualization to
off. No configuration change or server update is required.
Further references are in [docs/VISUALIZATIONS.md](docs/VISUALIZATIONS.md).

## Build the PSP client

A PSPDEV/PSPSDK toolchain is required. The active video path uses firmware AVC, not OpenH264:

```bash
cd psp-client
make
cp EBOOT.PBP release/PSPStreamer/EBOOT.PBP
```

Keep the firmware bridge and TV-out PRX files from the working installation alongside the new EBOOT. The Makefile preserves the MPEG import-library order and embeds the receiver artwork.

The default optimization is `-O3 -G0`, following the successful PSP comparison.
A conservative comparison build uses `make clean && make OPT_LEVEL=-O2`;
clean first because make does not track compiler-flag changes. No fast-math
options are enabled. Return to O3 with `make clean && make`.
Copy the matching EBOOT/PRX pair, keep your config,
presets and firmware modules, and compare the same presets on LCD and TV.
The reported improvement is subjective, not a measured speedup for every preset.
See [the O2/O3 comparison and test checklist](docs/OPTIMIZATION_COMPARISON.md).

## Tests

See [music rendering ownership and performance](docs/MUSIC_RENDERING.md)
for the shared LCD/TV helpers, hardware-validated baseline, rendering
invariants and boundaries for a future visualization engine.

LCD and TV music views update only dynamic regions, at most 20 times per
second, and give the existing audio-output worker priority over GUI work.
No extra LCD framebuffer is needed. The initial scene is drawn before audio
starts; switching fullscreen redraws once. Video timestamps and audio sample
timing are unchanged. Native renderer tests compare incremental updates with
full redraws, including volume, spectrum, VU decay and both layouts.

Host integration tests require `cc`, FFmpeg and FFprobe. They compile the same FLV parsing/sync helpers used by the PSP with undefined-behavior checks, compare every parsed PTS with FFprobe (including a five-minute stream), decode the extracted H.264/MP3, and exercise seek, video-only HTTP output and millisecond subtitle cues.

Client regression tests also exercise the first-picture/DAC startup barrier, late decisions after a 600-ms preparation stall, and the actual audio output worker with a simulated asynchronous DAC and immediate producer reuse of released buffers. They cover single-block EOF, ring wraparound, cancellation and output failure; they do not emulate real firmware decoding.

```bash
python3 -m unittest discover -s tests -v
```

Optional browser regression tests require Node.js, Playwright and its Chromium
browser. Run `node tests/web_ui_browser.cjs` and
`node tests/web_login_browser.cjs` (or set `PLAYWRIGHT_MODULE` to an installed
Playwright module). The first uses deterministic media fixtures; the second
starts an isolated local Python server and exercises real login/logout,
password changes, navigation and remote commands. Neither contacts your
personal server or Plex installation.

Before treating a build as hardware-validated, test one complete episode on both LCD and TV, then pause/resume, seek, stop/start another file, subtitles and a WLAN disconnect/reconnect. A successful host test or build alone is not a claim of perfect real-device synchronization.

## License and reference

GPL-2.0-or-later. See [LICENSE](LICENSE) and [NOTICE](NOTICE) for the PPA attribution and pinned reference revision; the original BSD notice is preserved in [LICENSE.BSD](LICENSE.BSD).

The adapted MilkDrop 2 equations retain Nullsoft's
[BSD-3-Clause notice](licenses/MilkDrop2.txt). Include that notice when
redistributing binaries containing the prototype.

## Security

### One shared password (server/HA app 0.1.24)

Set the HA app option `password`, or use **Server settings** in the Docker WebUI.
`PSP_STREAMER_PASSWORD` bootstraps Docker; a saved WebUI password takes precedence
and survives restarts in the `/data` volume. HA still manages its password via
its app options and requires a restart after changes there.
All media, metadata, subtitle and control routes require authentication when
the password is nonempty. Only the login page and its static assets are public.
An empty password preserves unauthenticated LAN use; it is **not safe for WAN**.

Put the identical password in the PSP's Settings screen or `server_password=` in
`ms0:/PSP/SYSTEM/PSPStreamer.cfg` (restart after manual file edits). Use a single-line
password of at most 128 UTF-8 bytes; a long random ASCII password is easiest to
enter consistently. The client preserves this setting when saving volume or
other preferences. The password is stored in plaintext on the Memory Stick.
Do not commit your real config or put credentials into URLs.

The website has a password-only login page and **Sign out**. Browser sessions
expire after 12 hours and are invalidated by logout, a WebUI password change,
or server restart. Cookies use HttpOnly, SameSite=Strict and Secure on HTTPS;
state-changing requests require same-origin JSON and the session's CSRF token.
Protected responses are non-cacheable. Login attempts are limited per connecting
IP (a reverse proxy may share this allowance). The native PSP and API clients
continue to use HTTP Basic with fixed username **psp** and the same password.

### Long playback pauses (server/HA app 0.1.39)

Update both the server and PSP client for long MP3/video pauses. The client
reports its actual pause state through the existing remote-control poll, for
both filesystem and Plex media. A fresh report lets the server wait while the
PSP's receive buffer is full, instead of closing the stream after 180 seconds.
Resume continues the same byte stream without restarting the decoder. A pause
report expires after 45 seconds without updates; ordinary write-inactivity
limits then apply again. This does not change subtitle startup timeouts and
cannot prevent a router or reverse proxy from enforcing its own idle timeout.
Radio retains its separate pause/reconnect-to-live behavior.

### Web navigation (server/HA app 0.1.38)

The PSP retries transient directory-loading failures up to three attempts,
with a 15-second budget per attempt including bounded DNS lookup and a one-second
pause between attempts. The UI shows the attempt and total elapsed time;
Circle cancels and failed loads retain the previous folder listing. Permanent
HTTP client errors (such as 401/404) are not retried. This directory-only policy
does not shorten metadata, subtitle-preparation or playback timeouts. Firmware
cleanup must finish before another request starts; threads are never forcibly killed.

**Library** groups mounted media roots under **Files**, alongside **Plex**, **Jellyfin** and
**Internet Radio**. Sources can be disabled in **Settings**. The Sources button
always returns to that overview; returning from a filesystem folder no longer
loses other sources. DLNA is not implemented yet.

**Remote control** shows Pause/Resume/Stop even without a selected file, but
track/quality/seek/download options appear only after selection. Music hides
video and subtitle controls; live radio also hides seeking and downloads.
**Downloads** contains conversion previews and the persistent queue.
**Settings** contains source switches, Plex/Jellyfin connections, radio stations and the password.
The language selector switches between English and German and is remembered
in that browser; the PSP language setting is independent. Web translations
are in `static/i18n.js`. Docker and Home Assistant ship identical web assets.

**A password is not transport encryption.** HTTP remains available and is the
default. Basic credentials are only Base64 encoded on HTTP
([RFC 7617](https://www.rfc-editor.org/rfc/rfc7617)). Optional PSP HTTPS now covers
media, subtitles, metadata and remote polling. It automatically records and
accepts server certificates, showing a notice when they change, as requested
for this project. This is encrypted transport, **not verified server identity**:
CA trust, hostname and expiration are not enforced. An active interceptor can
present a replacement certificate and receive credentials. No silent downgrade
to HTTP occurs. Use a trusted/VPN path when that risk is unacceptable.

See [HTTPS and settings setup](docs/HTTPS_AND_SETTINGS.md). Browser HTTPS should
use a normally trusted certificate. Docker and HA media sources/web assets are
checked for equality in the test suite; both include mkvtoolnix for MKV PGS.
