# PSP Streamer

PSP Streamer makes a local or DynDNS-reachable video library available on a PSP-2000/3000 with custom firmware. The Python server browses allowed folders and transcodes with FFmpeg. The native PSP app receives a compact H.264 Baseline video stream and a separate MP3 audio stream, both decoded locally by the PSP.

The proven target profile is 480×272, H.264 Baseline at 20.1 fps, and 44.1 kHz MP3. Text subtitles and PGS bitmap subtitles are rendered as native PSP overlays. Video and common music formats stream through the same native MP3 DAC path; audio playback includes a receiver UI with live stereo VU meters and an amplified, real 12-band PCM spectrum display.

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
| `MAX_TRANSCODES` | `4` | Concurrent FFmpeg processes; one PSP playback needs two |
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
```

`server` accepts an IPv4 address or DNS/DynDNS name; an `http://` prefix is also allowed. The PSP resolves the name for every new connection. `audio` and `subtitle` store the preferred video-track indices (`subtitle=-1` disables subtitles); `quality` means `0=96k`, `1=128k`, `2=160k` MP3; `volume` ranges from `0` to `30`; and `shuffle=1` randomly continues with another audio file from the current folder (`0` keeps its listed order).

`language` selects the PSP interface language: `en` (default) or `de`. The PSP's bundled Latin-1 font supports direct German `ä`, `ö`, `ü`, and `ß` characters.

### Adding a PSP interface language

Create `psp-client/lang_xx.h` by copying `lang_en.h`. Each visible text has its own named `TXT_*` entry and related entries are grouped by interface screen, so translations can be edited without relying on array order. Preserve printf placeholders such as `%d`, `%s`, and `%.48s`. Include the new file in `psp-client/language.c`, then register its code and table in the `languages[]` array there, for example `{"fr", lang_fr}`. Rebuild the EBOOT and set `language=fr` in `PSPStreamer.cfg`.

Browser controls: Cross opens a folder or playback options; Triangle opens the media information page for a file; Circle exits the options screen; Left goes to the parent folder; held L/R pages through the list; Square reloads; Start exits the app.

Playback controls: Select pauses/resumes, L/R seek ±10 seconds, and Start returns to the browser. Track titles such as `Forced` or `Full` appear beside language labels when the source provides them.

Receiver controls: Circle shows/hides the receiver strip, Up/Down adjusts and stores volume (hold either direction for a slow repeat), and Cross+Triangle toggles fullscreen. Fullscreen works for video and for the audio spectrum display.

## Subtitles and limitations

ASS/SSA, SRT, WebVTT, and other FFmpeg-readable text tracks are converted once into compact, timed cues. The PSP overlays a compact outlined DejaVu Sans bitmap locally. HDMV PGS subtitles use native palette-indexed sprites, preserving their original colour and outline without burning them into video. Other bitmap formats (VobSub/DVDSUB, DVB, XSUB) continue to use the server fallback.

Keep `subtitle_font.raw` and `cooleyesBridge.prx` beside `EBOOT.PBP`. The compact DejaVu Sans Latin-1 atlas is loaded only after the AVC decoder is ready; if it is missing, video playback remains safe and text subtitles are simply not drawn. The receiver artwork is embedded in `EBOOT.PBP`; no separate `menu_skin.raw` is required.

Video output and a dedicated 480p mode are deliberately not part of this release yet.

## Build the PSP client

A PSP SDK and an OpenH264 library built for PSP are required. The library path is deliberately not hard-coded to a personal location:

```bash
cd psp-client
make OPENH264_DIR=/pfad/zu/openh264
cp EBOOT.PBP release/PSPStreamer/EBOOT.PBP
```

The library must provide `libopenh264_dec_psp.a` and its headers.

## Tests

```bash
python3 -m unittest discover -s tests -v
```

## Security

The server has no authentication. Do not expose it directly to the public internet; DynDNS access should use a VPN, a trusted firewall rule, or a separate home network.
