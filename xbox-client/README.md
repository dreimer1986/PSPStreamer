# Original Xbox — native player preview 0.2

The connection-only preview has been tested successfully on a real Xbox.
This next preview adds the familiar receiver GUI, analog VU meters, library
navigation, music and video playback. **Playback still needs a physical Xbox
test.** It is a native nxdk XBE, not an XBMC skin or a PSP executable.

## Install and test

1. Update the PSPStreamer **server to 0.1.68 or newer** (Docker or Home Assistant).
2. Copy `bin/default.xbe`, `bin/theme.png` and `bin/font.ttf` to the same Xbox
   application directory. Keep the existing `server.cfg`, or create it from
   `server.cfg.example`. The font and image are required.
3. Use the direct LAN HTTP port. `host` is an IPv4 address without a scheme/path;
   `port` defaults to 8091, `password` is the existing server password. HTTP/Basic
   authentication is appropriate only on a trusted LAN. No TLS implementation yet.
4. Begin with `output_height=480` and standard video quality. Play one music
   track, then one video; check VU response, start sync and sync after several
   minutes. Test pause/resume, +/-30 seconds, Stop and a second title.
5. The app writes `xbox-player.log` beside the XBE (overwritten at startup).
   Copy it from the dashboard after the test. It records PTS, presented/dropped
   frames, underruns and queue occupancy, not credentials.

### Controls

| Context | Buttons |
|---|---|
| Library | D-pad up/down: entry; left/right: page; A: open; B: parent; X: reload |
| Media options | Up/down: row; left/right: audio/subtitle/quality; A on Play: start |
| Playback | A or Start: pause/resume; B: stop; left/right: -/+30 seconds |
| Playback | Up/down: volume; Y: fullscreen/receiver view |
| Network request | B: cancel, including stalled requests |
| Menu | Back: return to dashboard |

A long pause releases the stream/encoder. Resume requests a fresh stream at
the recorded audio position; it can therefore take a moment. Network failures
remain visible errors, not automatic playlist advancement. No automatic next
item, artwork, watched reporting, radio, visualizations or offline cache yet.

## Display and performance

- Target: ordinary **64 MB / stock-clock Xbox**. No overclock requirement or
  overclock changes. Actual performance is subject to the hardware test.
- Default output: 640x480. `output_height=576`, `720` or `1080` selects a matching
  mode only when nxdk lists it for the connected cable and console video
  configuration; otherwise 480 remains. RGB is not forced into Component modes.
- The existing 720x480 PSP TV theme and needle/control coordinates share one
  scaling transform. Video is aspect-fitted. No AI repainting alters the artwork.
- Video decoding choices: **480x272** and **640x360**. A 720p/1080i output mode does
  not imply HD decoding. Software upscaling increases memory traffic; test 480
  output first. RGB/Component combinations beyond the connection test are untested.
- First decoder: MIT **pl_mpeg**, MPEG-1 video / MP2 stereo audio at 48 kHz,
  192 kbit/s; video target 1.5 Mbit/s. Decoding and YUV conversion are software,
  not an unverified claim of Xbox MPEG-2 hardware acceleration.
- **Next candidate: libmpeg2 / MPEG-2**, after this foundation works. No H.264
  decoding or hardware codec acceleration is claimed for this preview.

## Synchronization and isolation

Separate `/api/xbox-stream/<id>` output encodes MPEG-TS, extracts actual 90 kHz
PES timestamps and sends bounded access units in an `XSM1` stream. Header:
`XSM1` plus little-endian flags (video=1, audio=2). Each record is `<c3xIq>` plus
payload (type, length, signed PTS). `V` is one MPEG-1 picture, `A` one 1152-sample
MP2 frame, `E` clean EOF. B pictures are disabled.

The client uses only pl_mpeg's low-level decoders, **not its frame-count playback
clock**. Audio is master: the AC97 DMA descriptor and remaining sample count
locate the actual playback position inside the packet's PTS. Video waits for
its PTS; an excessively late image can be omitted without omitting predictive
decoding. Audio is never repeated to catch up. There are no 20.1/20.2 correction
factors or guessed output-latency offsets. The 23.976 encoding cadence limits
decoder load; it is not the sync clock. Video-only playback uses a monotonic
clock anchored to its first PTS. After a shorter audio track ends, video can
continue from the last audio time.

PC checks cover timestamps against FFprobe, real low-level decoding, packet
bounds, HTTP authentication and encoder cleanup. **DMA timing, cable/display
latency and sustained frame rate require the real Xbox test.**

PSP encoder options and playback/control protocols stay unchanged. Xbox and
browser streams have their own formats and never send PSP control commands.
They share source/track/subtitle resolution and server capacity only. Xbox
subtitles are server-burned, including supported bitmap tracks. The paged
library exposes Files/Plex/Jellyfin/DLNA, 64 entries per page.

## Build and licenses

Requirements: Git, make, Clang/LLD, flex, bison, CMake; no proprietary XDK.

```sh
git clone --recursive https://github.com/XboxDev/nxdk.git .toolchain/nxdk
git -C .toolchain/nxdk checkout 14d5ee97e73347c973f1f57b68b79ec08c9e77f2
git -C .toolchain/nxdk submodule update --init --recursive
export NXDK_DIR="$PWD/.toolchain/nxdk"
export PATH="$NXDK_DIR/bin:$PATH"
make -C xbox-client -j4
python3 xbox-client/package_preview.py
```

The packaging helper uses DejaVu Sans under `/usr/share/fonts/TTF/` and its
license under `/usr/share/licenses/ttf-dejavu/`; adapt those paths for other
distributions. Output is **one** `xbox-client/bin/` directory, separate from
stable PSP/firmware releases. Preserve licenses and sources when distributing.

- [nxdk](https://github.com/XboxDev/nxdk), pinned above, supplies the Xbox HAL,
  network, controllers and SDL2/SDL_image/SDL_ttf/FreeType dependencies.
- [pl_mpeg](https://github.com/phoboslab/pl_mpeg), vendored unchanged at
  `c871f2be022ece7ef4f64230b4fb8e1fb9eb6023`, and [jsmn](https://github.com/zserge/jsmn)
  retain their MIT notices in `vendor/`.
- New source is GPL-2.0-or-later. The linked binary uses the **GPLv3** option
  because nxdk USB includes Apache-2.0 components; other targets are not relicensed.
  Font and dependency notices are included in `licenses/`.
