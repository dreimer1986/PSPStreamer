# PSP Streamer

PSP Streamer makes a local or DynDNS-reachable media library available on a PSP-2000/3000 with custom firmware. The Python server browses allowed folders and transcodes with FFmpeg. Video is delivered in one FLV stream containing H.264 Baseline and MP3 audio, both decoded locally by the PSP.

Video profiles are 480×272 for LCD and 720×480 for component TV output, with 44.1 kHz MP3. The encoder currently produces 20 fps to limit decoder workload; that number is **not a playback clock or an A/V calibration value**. Text subtitles and LCD PGS bitmap subtitles use native PSP overlays. Video and common music formats use the same MP3 DAC path; music playback includes a receiver UI with live stereo VU meters and a real 12-band PCM spectrum display.

## Timestamp-based audio/video synchronization

Both LCD and TV playback follow the audio-master approach in [PMPlayer Advance](https://github.com/DavisDev/pmplayer-advance/tree/9ce494d020d3c909ee312bda1932f8d806a2f05d/ppa/mod):

- One FFmpeg process muxes both streams, including their timestamps. The PSP reads FLV tag timestamps and the signed H.264 composition offset to obtain video PTS.
- Decoded PCM buffers retain their first MP3 packet's PTS. The output worker publishes that timestamp immediately before its blocking DAC call, matching PPA's buffer-based audio clock.
- Video is decoded and composited with its subtitles in a RAM staging frame. The finished picture is compared with fresh audio PTS, again at VBlank before copying to the established LCD/TV framebuffer. It waits when ahead and discards late pictures. As in PPA, the tolerance is two video-frame durations; here the duration comes from adjacent packet PTS.
- Codec initialization, decoding and related cache operations share one Media Engine semaphore. Network reads, audio output and display waits remain independent. PCM ownership follows PPA: a successful submission releases the previous buffer; the last buffer is released after the DAC drains. Audio content is never repeated or skipped to synchronize video.
- Subtitle cues and progress use milliseconds. Seek and reconnect start a new container timeline at the requested source position. Video-only files use PTS differences on a monotonic clock.

The old 20.1/20.2-fps playback estimates and artificial start offsets are no longer used by the client. Existing hardware initialization, LCD/TV layouts, MP3 decoding and DAC block sizes remain in place. This is a streaming adaptation of PPA's scheduling, not a replacement of the working hardware drivers. Hardware validation is still required for actual DAC/display latency, rendering load and long-episode playback; host tests cannot establish those.

### Measuring a remaining offset

The client records a bounded trace in RAM during video playback and writes it after Stop/end, once playback threads have joined:

- TV: `ms0:/PSP/SYSTEM/PSPStreamer-sync-tv.csv`
- LCD: `ms0:/PSP/SYSTEM/PSPStreamer-sync-lcd.csv`

Each file is overwritten by the next completed playback session using that output (including seek/reconnect restarts). Copy it off the Memory Stick after stopping the test video, before starting another one. No recording configuration or server update is required. There are no trace file writes during playback. The trace retains up to 4096 samples: the first 32 decisions and subsequently at most one per second, overwriting the oldest rows when full.

Rows contain elapsed wall time, video PTS, submitted audio-block PTS, remaining DAC samples, outstanding PCM blocks, preparation/copy durations, display/drop counters and non-increasing audio-PTS counts. `action` is `0=wait`, `1=display handoff`, `2=drop`. `decode_us` includes ME lock wait, decode and CSC; `prepare_us` also includes overlays; `copy_us` measures transfer to VRAM and the display API call. `dac_rest_samples=-1` means the sample was unavailable or crossed an audio timestamp change.

The audio PTS describes the submitted block, not an exact sample at the speaker. The video handoff is measured on the PSP, not at the TV panel. The trace can expose internal stalls and timestamp discontinuities; it cannot directly measure a television's processing delay. The staging frame uses approximately 544 KiB on LCD or 1.41 MiB on TV, plus 192 KiB for the trace. The measured copy duration helps assess its cost on real hardware.

## Requirements

- Python 3.11 or later
- FFmpeg with `libx264` and `libmp3lame`
- A PSP with working infrastructure Wi-Fi and CFW for the homebrew app

## Run the server directly

`MEDIA_ROOTS` is a colon-separated list of allowed media roots. The server never follows paths outside these roots.

```bash
MEDIA_ROOTS='/srv/media/Serien:/srv/media/Filme' PORT=8091 \
  python3 -m psp_streamer.server
```

The test interface is then available at `http://SERVER:8091`. It is useful for browsing and checking transcoding; reliable playback happens in the PSP app.

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

The server is configurable without recompiling. Edit the file on a PC:

```ini
server=streamer.example.net
port=8091
audio=0
subtitle=-1
quality=2
volume=24
shuffle=0
language=en
tv_ui=off
```

`server` accepts an IPv4 address or DNS/DynDNS name; an `http://` prefix is also allowed. The PSP resolves the name for every new connection. `audio` and `subtitle` store the preferred video-track indices (`subtitle=-1` disables subtitles); `quality` means `0=96k`, `1=128k`, `2=160k` MP3; `volume` ranges from `0` to `30`; and `shuffle=1` randomly continues with another audio file from the current folder (`0` keeps its listed order).

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

Playback controls: Select pauses/resumes, L/R seek ±10 seconds, and Start returns to the browser. Track titles such as `Forced` or `Full` appear beside language labels when the source provides them.

Receiver controls: Circle shows/hides the receiver strip, Up/Down adjusts and stores volume (hold either direction for a slow repeat), and Cross+Triangle toggles fullscreen. Fullscreen works for video and for the audio spectrum display.

## Subtitles and limitations

ASS/SSA, SRT, WebVTT, and other FFmpeg-readable text tracks are converted once into compact, timed cues. The PSP overlays a compact outlined DejaVu Sans bitmap locally. HDMV PGS subtitles use native palette-indexed sprites, preserving their original colour and outline without burning them into video. Other bitmap formats (VobSub/DVDSUB, DVB, XSUB) continue to use the server fallback.

Keep `subtitle_font.raw` and `cooleyesBridge.prx` beside `EBOOT.PBP`. The compact DejaVu Sans Latin-1 atlas is loaded only after the AVC decoder is ready; if it is missing, video playback remains safe and text subtitles are simply not drawn. The receiver artwork is embedded in `EBOOT.PBP`; no separate `menu_skin.raw` is required.

Component TV playback uses native 720×480 output. By default, the browser and options stay on the PSP LCD and video switches to the TV. With `tv_ui=auto`, TV menus and video share the same output mode: Stop/end returns to the TV menu without an LCD mode reset. The existing Select+L+R TV check is available from either browser and returns to its originating output. Text subtitles remain local overlays; TV bitmap subtitles use server-side burn-in to avoid sprite-transfer stalls at the higher resolution. The server website can select media and send play/pause, stop and seek commands. This GUI update does not require a server/add-on update.

An optional [MilkDrop warp prototype](docs/MILKDROP_PROTOTYPE.md) is available
during music: **Square** cycles three built-in effects, the custom file
`presets/active.milk`, then the normal spectrum. Copy the supplied
`psp-client/presets` folder beside EBOOT to try the fourth slot.
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
The [conditional formula subset](docs/MILKDROP_CONDITIONS.md) adds lazy `if`,
Boolean functions and additional math; `branch-beat-demo.milk` demonstrates
music-reactive changes of wave style, color, zoom and echo intensity.
The [initialization/q-variable subset](docs/MILKDROP_INIT.md) adds
`per_frame_init_*` and `q1`–`q32`; try `init-orbit-demo.milk` as the active file.
**Cross + Triangle** toggles receiver view and true visualization fullscreen
(480×272 LCD / 720×480 TV). Fullscreen hides the receiver controls, but
pause, volume and remote commands still work. Without an active visualization,
the existing enlarged spectrum/receiver layout is unchanged.
This adapts MilkDrop 2's no-shader
warp equations to PSP GU, not its complete engine: arbitrary `.milk` files,
full EEL and shader presets are not supported. A bounded arithmetic/time
subset is available in the custom slot. Music remembers the selected effect
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

The server has no authentication. Do not expose it directly to the public internet; DynDNS access should use a VPN, a trusted firewall rule, or a separate home network.
