# PSP Streamer

PSP Streamer makes a local or DynDNS-reachable media library available on a PSP-2000/3000 with custom firmware. The Python server browses allowed folders and transcodes with FFmpeg. Video is delivered in one FLV stream containing H.264 and MP3 audio, both decoded locally by the PSP.

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
The latter queues a full episode from its beginning, waits for conversion,
downloads and verifies it, then starts the existing FLV/PTS player from the
Memory Stick. It is not a general local-file browser. Music streaming is
unchanged; offline jobs currently support video only.

For several episodes, use **Convert for download** in the web remote. Each
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

For a finished conversion, click **Download Memory Stick ZIP (PC / USB — recommended)**.
Download and extract it on your PC, close PSP Streamer, then merge the contained
**PSP** folder into the Memory Stick root (not into another PSP folder).
Safely eject and open **Local storage**. The existing 0.1.30 PSP client needs
no update. This works with HA and Docker without SSH or internal filesystem access.

Copy the entire `PSP/VIDEO/PSPStreamer/<job-id>/` folder: FLV, `subtitles.ovl`,
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
to skip Wi-Fi association entirely; restart normally to return online.

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
audio=0
subtitle=-1
quality=2
video_fps=20
play_mode=stream
music_preset=active.milk
preset_auto=0
preset_seconds=60
preset_fade_ms=1500
volume=24
shuffle=0
language=en
tv_ui=off
```

`server` accepts an IPv4 address or DNS/DynDNS name; an `http://` or `https://`
prefix is also allowed in the file. `https=0` selects HTTP, `https=1` TLS;
when both are present the last setting wins. `port` is explicit in either mode.
Enter just the hostname/IP in the app's Server host field, not a URL.
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

Receiver controls: Circle shows/hides the receiver strip, Up/Down adjusts and stores volume (hold either direction for a slow repeat), and Cross+Triangle toggles fullscreen. Fullscreen works for video and for the audio spectrum display.

With the normal spectrum selected (Square cycles back from MilkDrop),
Cross+Triangle now fills the LCD or native TV canvas with spectrum bars, without
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

During music, **Square** toggles between **spectrum analysis and MilkDrop**.
MilkDrop uses the selected preset file (initially `presets/active.milk`). Copy
the supplied `psp-client/presets` folder beside EBOOT. The three early built-in
test effects are no longer part of the user-facing selection.
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
L/R pages, Cross applies, Circle cancels. The chosen filename is saved as
`music_preset=filename.milk` in the PSP config; manual `active.milk` replacement
is no longer required. The browser lists up to 128 `.milk` files and warns when
that limit is exceeded. All built-in waveform modes 0–8, a real FFT waveform,
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
Visualization feedback now renders at **512×256**, using 32-bit color on LCD
and RGB565 on TV. Echo/brightness are composed offscreen before presentation;
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
Up to 64 [named persistent variables](docs/MILKDROP_COMPATIBILITY.md) per context can retain
state between frames; `memory-pulse-demo.milk` holds and releases bass impulses.
Bounded [per-grid-point formulas](docs/MILKDROP_GRID.md) add local transforms;
try `grid-twist-demo.milk`. This is an interpolated 8×8 mesh, not pixel shaders.
**Cross + Triangle** toggles receiver view and true visualization fullscreen
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
All GET/POST routes, including media, metadata, subtitles, the website
and remote control, require authentication when the password is nonempty.
An empty password preserves unauthenticated LAN use; it is **not safe for WAN**.

Put the identical password in the PSP's Settings screen or `server_password=` in
`ms0:/PSP/SYSTEM/PSPStreamer.cfg` (restart after manual file edits). Use a single-line
password of at most 128 UTF-8 bytes; a long random ASCII password is easiest to
enter consistently. The client preserves this setting when saving volume or
other preferences. The password is stored in plaintext on the Memory Stick.
Do not commit your real config or put credentials into URLs.

The website uses the browser's HTTP Basic login dialog: username **psp**, plus
the shared password. There are no user accounts; `psp` is a fixed protocol
label. Wrong/missing credentials return HTTP 401 before any library/transcode
work. Browser control commands require same-origin JSON. Protected responses
are marked non-cacheable. Clear the browser's saved authentication or restart
its session after changing passwords.

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
