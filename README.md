# PSP Streamer

PSP Streamer makes a local or DynDNS-reachable video library available on a PSP-2000/3000 with custom firmware. The Python server browses allowed folders and transcodes with FFmpeg. The native PSP app receives a compact H.264 Baseline video stream and a separate MP3 audio stream, both decoded locally by the PSP.

The proven target profile is 480×272, H.264 Baseline at 20 fps, and 44.1 kHz MP3. Burned-in text subtitles, audio-track selection, a kept-awake display, and large directory listings are supported.

## Requirements

- Python 3.11 or later
- FFmpeg with `libx264`, `libmp3lame`, and `libass`
- For subtitles: Fontconfig and DejaVu fonts (included in the Docker image)
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
| `MAX_TRANSCODES` | `2` | Concurrent FFmpeg processes; one PSP playback needs two |
| `FFMPEG_PRESET` | `veryfast` | x264-Preset |
| `FFMPEG_FONTS_DIR` | DejaVu system directory | Font directory for burned-in text subtitles |

## Docker

The SMB/NFS mount belongs on the Docker host. Mount it read-only only.

```bash
MEDIA_ROOT_PATH=/srv/media PSP_STREAMER_PORT=8091 docker compose up -d --build
```

Do not put SMB credentials in `compose.yaml`.

## Install and configure the PSP app

After a build, the release file is at `psp-client/release/PSPStreamer/EBOOT.PBP`. Copy it and `cooleyesBridge.prx` to:

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
```

`server` accepts an IPv4 address or DNS/DynDNS name; an `http://` prefix is also allowed. The PSP resolves the name for every new connection. `quality` means `0=96k`, `1=128k`, `2=160k` MP3.

Controls: Cross opens a folder or playback options; Circle exits the options screen; Left goes to the parent folder; L/R pages through the list; Square reloads; Start exits the app.

## Subtitles and limitations

ASS/SSA, SRT, and other text subtitles are burned in with libass. PGS subtitles use FFmpeg's bitmap-overlay path. Other bitmap formats such as VobSub/DVB may need a separate handling path. Starting with subtitles enabled can take longer while FFmpeg prepares fonts or subtitle data.

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
