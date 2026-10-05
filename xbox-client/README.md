# Original Xbox — native player preview 0.4.4

GUI navigation and native video **with audio** were confirmed on a real Xbox
with 0.2.5 and 0.3.0. Version 0.4.4 extends that working foundation; its new features
still require console testing. It is a native nxdk XBE, not an XBMC skin or a
PSP executable. The proven PTS/sample-position clock is unchanged.

## Install and test

1. Update the PSPStreamer **server to 0.1.73 or newer** (Docker or Home Assistant).
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
| Library | D-pad up/down: entry; left/right: page; A: open; B: parent; Start: reload |
| Library | X: settings; Y: help |
| Media options | Up/down: row; left/right: audio/subtitle/quality; A on Play: start |
| Playback | A: pause/resume; Start: controls menu; B: stop; left/right: -/+30 seconds |
| Video | X also opens the controls menu: pause, seek, chapters, previous/next file, stop |
| Music | X: shared PSP spectrum settings (legacy/FFT, bands, gain, palette, LEDs, peaks) |
| Playback | Up/down: volume; Y: fullscreen/receiver view |
| Network request | B: cancel, including stalled requests |
| Menu | Back: return to dashboard |

A long pause releases the stream/encoder. Resume requests a fresh stream at
the recorded audio position; it can therefore take a moment. Network failures
never count as a clean end: interrupted streams retry the same position up to
three times, with B to cancel. Live radio reconnects at the live position.

## New in 0.3.0

- Options X toggles source resume/from-start; Y shows duration, track counts,
  artist/album and a compact synopsis. Music hides video/subtitle controls.
- Playback LB/RB selects previous/next file; triggers select previous/next
  chapter. Clean EOF can automatically advance; errors never skip a title.
- Settings: autoplay next, repeat current, music folder shuffle, spectrum,
  video quality, volume, preferred audio language and subtitle language/off.
  Existing server series preferences take precedence over global languages.
- `preferences.cfg` beside the XBE stores these settings, with a `.bak`
  recovery copy. Updating the executable never replaces personal settings.
- Provider resume points and shared direct-file history are available. Root
  rows expose existing shared Favorites and Recently played (read-only).
- Plex/Jellyfin/DLNA artwork reuses the bounded server RGB565 image service:
  320x180 dimmed backdrop and 80x112 cover; it does not decode unbounded images
  on the console. Artwork failure never prevents Play; Play cancels a pending
  optional image request. This is not yet native-resolution Xbox artwork.
- Music has a 24-band windowed frequency display plus the analog VU meters.
  Y toggles its fullscreen display; frequency analysis is disabled for video.
- Configured radio stations are playable; resume returns to live radio.
  ICY title display on Xbox is still pending.
- Playback progress (15 seconds), pause and stop go to Plex/Jellyfin and
  shared history in a bounded background queue, not the render/audio thread.
  Browser playback reports the same events. Client/session identities and
  monotonic report sequence numbers prevent cross-client queue interference
  and stale unload reports. These clients never consume PSP remote commands.
- Sequential video playback crosses numbered season folders of the same
  series. Plex/Jellyfin use their season metadata; files/DLNA recognize
  `Season 1`, `Staffel 1`, `S01`, etc. No arbitrary recursive folder traversal,
  music-album continuation, or modification of explicit provider playlists.

## New in 0.3.1

- Visible playback menu (Start/X) with chapter and file navigation. Music X
  opens persistent spectrum settings; LED segments light only as whole units.
- Web playback target **Xbox**, with its own current-title/status/seek and
  previous/pause/resume/stop/next controls. One Xbox mailbox, three-second
  asynchronous polls, one outstanding request, commands expire after 15 seconds.
  PSP command/state storage remains independent; this is not an Xbox HACS entity.
- Audio reserve increased from 10 to 24 existing DMA slots (576 ms), starting
  after 16 slots (384 ms). Hardware sample-position/PTS sync is unchanged.
  Routine log writes now run in the reporting worker rather than the playback
  thread. The rare audible stalls still need hardware verification.
- Fixed unsupported floating-point printf formatting in seek URLs and logs.
  Old `pos=` log columns were shifted and cannot be read as underrun counts.
  New diagnostics use `pos_ms=` and integer formatting.

## New in 0.3.2

- Reuse **unmodified** PSP `spectrum_analysis_impl.h` and `spectrum_paint.h`:
  desktop FFT (default) or original 12-bin legacy analysis; 12/24/32/48/64 FFT
  bands, -24..+24 dB display gain, Original/Rainbow/VU/Ice/Fire colors, separate
  LED toggle, 8–32 whole segments and peak hold. An Xbox adapter supplies the
  DMA-current PCM snapshot and an SDL texture. Analysis runs in the UI at up
  to 20 Hz, not during audio submission. The old 24-bin approximation is gone.
  One Xbox band-count preference replaces PSP-specific LCD/TV preferences.
- Main menu **X → Server connection**: edit IPv4/port/password, test without
  saving, then Save and connect; B cancels. The controller keyboard follows
  the PSP editor: A types, X deletes, Y clears, Start accepts, B cancels.
  Passwords remain masked. All HTTP workers are quiesced before credentials
  change. `server.cfg` is atomically replaced via a `.bak` recovery copy.
  HTTP/trusted LAN only; DNS and HTTPS are not silently emulated.
- Settings include debug logging and a 0–30 second next-episode countdown
  (0 keeps immediate continuation). A advances now; B cancels the countdown.
  Error/assertion diagnostics remain available when routine logging is off.
- A thin real-position progress bar returns at the bottom of fullscreen video.
- Web target selector is visible even without selecting a media file and is
  remembered per browser. Current playback, position and Remote control use
  the selected Xbox/PSP; PSP-only controller buttons hide for other targets.

### Still separate porting work

Accelerated 3D rendering, MilkDrop/Monkey, native high-resolution
artwork, Xbox offline storage, editable Xbox playlists, favorites editing,
library search, translations and a distinct
Xbox HA remote entity are **not implemented** in this build. No parity with
all PSP features is claimed. Existing PSP plugins cannot run as Xbox plugins.

## Display and performance

0.4.4 corrects vertically stretched video in 1080i output: the PVIDEO destination
uses 540 field lines, while the RGB GUI/color-key rectangle remains in 1080
framebuffer lines. Both fullscreen and embedded video use the correction,
following XBMC4Xbox ComboRenderer's 1080i handling. Source quality, SD, 720p,
software presentation and timestamp synchronization are unchanged. The 0.4.3
speedup was confirmed by the user; this geometry correction needs a console test.

At track end, AC97's halted-at-last-valid-descriptor status now takes precedence
over a residual sample count (only while RUN is set and current equals last).
This avoids an endless drain wait in that hardware state. The reported autoplay
stall still needs hardware confirmation: older logs lacked the DMA status needed
to establish its exact cause. End diagnostics now include network/clean-EOF,
decoder/pending-frame state and both DMA cursors. No timeout forces completion;
only a clean, fully drained stream may advance to another episode.

0.4.3 repairs the PVIDEO startup handoff: reset only the overlay engine, clear
the inherited pending-buffer state and release STOP before first submission.
The 0.4.2 console log showed **zero GPU submissions and software fallback**;
it was not evidence of successful GPU playback. The new hardware behavior
requires another console test. Ongoing buffer ownership checks remain active.

GUI updates now copy damaged rectangles only; static screen content remains
in SDL's existing backing surface. Textures for text use a bounded 96-entry /
2 MiB LRU cache, cleared on output changes and shutdown. The YUY2 packer handles
two luma rows per chroma load. PTS/audio synchronization and the decoder are
unchanged. `gui_kib` and `gui_copies` quantify actual framebuffer copies.
If playback sticks at Buffering after a dashboard/warm restart, cold boot first:
the previously observed audio-DMA startup issue is separate and remains open.

0.4.2 introduced **NV2A PVIDEO hardware video presentation** as the default. The CPU
still decodes MPEG-1/2 with libmpeg2, but packs YUV420 into YUY2 with Pentium III
MMX/SSE1; the video unit performs YUV-to-RGB conversion and scaling. Two packed
buffers replace per-frame RGB conversion/scaling/copying. This is not MPEG
hardware decoding, nor a GPU renderer for the GUI or visualizations.

Settings → **Video renderer** selects **NV2A overlay (auto)** or **Software**;
`video_hardware=1` / `0` in `preferences.cfg` stores that choice. Allocation or
persistent buffer-busy failures fall back to software for the stream. The
color-keyed GUI keeps playback controls and the progress bar visible, including
when video fills the screen. Progress/receiver updates run at 4 Hz while video
retains its timestamp cadence; control selection and fullscreen changes redraw
on the next video frame. Pause/seek/stop disable and release the overlay.
No server update beyond 0.1.73 is needed, and PSP code is unchanged.

Test a familiar episode at 720x480 first, then 720p if desired. Check color,
aspect, fullscreen/windowed, controls, pause/seek, stop and next episode. Compare
Software with NV2A at the **same video and display sizes**. Debug logs include
`renderer`, `gpu_frames`, `gpu_busy`, cumulative `decode_ms`, `pack_ms` and
`gui_ms`, alongside audio underruns and dropped frames. Console scanout and
performance remain unverified until this hardware test. Implementation and
focused test details: [GPU video notes](../docs/XBOX_GPU_VIDEO.md).

0.4.1 fixes output changes returning to the dashboard: the pinned SDL Xbox
driver retains its singleton window pointer after destruction. The app now
compiles that same upstream driver with a narrow DestroyWindow adapter that
clears the pointer. The SDK checkout is unchanged. The actual upstream failure
and repeated recreation across five mode sizes are covered by
`tests/xbox_video_lifecycle.c` (host hardware-boundary stubs; not a TV test).
Logs now identify hardware switching, window/renderer creation and theme load.
No server update beyond 0.1.73 is required for this client-only repair.

- Target: ordinary **64 MB / stock-clock Xbox**. No overclock requirement or
  overclock changes. Actual performance is subject to the hardware test.
- Default output: 640x480 (or the console's enabled 480p mode). Settings →
  Display output lists the cable/region/EEPROM-compatible modes, including
  enabled 720p and 1080i. A applies immediately; A again keeps it, B restores
  the old mode. Without confirmation it reverts after 15 seconds. Selection
  is saved in preferences.cfg; no region/EEPROM changes are performed.
  The pinned nxdk has 480-line PAL framebuffers, not native 576i/576p modes.
  Its 480p flag selects progressive instead of interlaced 480 output; the app
  does not change that dashboard setting. RGB is never forced into HD modes.
- The existing 720x480 PSP TV theme and needle/control coordinates share one
  scaling transform. Video is aspect-fitted. No AI repainting alters the artwork.
- Video choices: **480x272, 640x360, 720x480, 720x576, 1280x720, 1920x1080**.
  Both SD 720-wide profiles are anamorphic 16:9; 4:3 sources are pillarboxed.
  Settings → TV shape selects 16:9 or 4:3 letterbox (set the TV accordingly).
  Output and encoding resolution are independent; 1080i output does not
  promise smooth HD decoding on a 64 MB console. HD profiles are experimental.
- Decoder: **libmpeg2 0.5.1**, with Pentium III MMX/MMXEXT (not SSE2), shared
  by selectable MPEG-2 (default) and MPEG-1. No B pictures; real packet PTS are
  attached to the decoded pictures, including the final delayed reference
  picture. Clean EOF drains every picture and the PCM queue before autoplay;
  interrupted transport never counts as EOF. Decoder frame allocations fail
  gracefully rather than relying on unchecked internal allocations.
- MP2 stereo remains 48 kHz / 192 kbit/s. Audio downmix offers Stereo,
  Dolby Surround and Dolby Pro Logic II, performed on the server. Select the
  corresponding decoder on your receiver. Matrix stereo is not discrete
  AC-3/DTS passthrough; passthrough remains deferred. Existing stereo sources
  are not turned into genuine discrete surround channels.
- Video target rates by size: 1.5 / 2 / 4 / 5 / 9 / 16 Mbit/s. The server
  requires the new Xbox profile parameters; old clients retain MPEG-1 defaults.
  Native build uses **-O3 and LTO**, without fast-math or clock changes.
  Decoding stays on the CPU; NV2A accelerates presentation, not MPEG decoding.
- No H.264 decoding or hardware codec acceleration is claimed for this preview.

## Synchronization and isolation

The early file log misleadingly ended around controller initialization.
Serial KD subsequently proved the HTTP `sscanf` access violation (fixed in
0.2.4); the screen then exposed nxdk's unimplemented `strtod` (fixed in 0.2.5).
Do not treat the old controller hypothesis as a confirmed hardware fault.
SDK C assertions are saved to disk, sent over serial KD and displayed before
the fatal halt. Keep `xbox-player.log` if a new SDK assertion occurs.

Preview 0.2.1 fixes the first-menu black screen caused by a clip command
without a preceding viewport after SDL flushes temporary text textures.
`tests/xbox_clip.c` reproduces the crash with the pinned SDL source on a host
and verifies the corrected command order. Audio initializes only on Play;
startup stages and assertions are now recorded in `xbox-player.log`.

Separate `/api/xbox-stream/<id>` output encodes MPEG-TS, extracts actual 90 kHz
PES timestamps and sends bounded access units in an `XSM1` stream. Header:
`XSM1` plus little-endian flags (video=1, audio=2). Each record is `<c3xIq>` plus
payload (type, length, signed PTS). `V` is one MPEG-1/2 picture, `A` one 1152-sample
MP2 frame, `E` clean EOF. B pictures are disabled.

The client uses libmpeg2 for video and pl_mpeg for MP2 audio, **not a frame-count
playback clock**. Audio is master: the AC97 DMA descriptor and remaining sample count
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

Preview 0.2.5 also replaces the SDK's unimplemented `strtod` duration conversion
with a JSON decimal/exponent parser and sends SDK assertions over serial KD.
Preview 0.2.4 fixes a hardware-confirmed startup access violation in the pinned
SDK's `sscanf`: suppressed `%*s` still consumed/wrote an argument. HTTP status
parsing now uses a bounded, explicit parser. This affects only the Xbox client.

### Serial diagnostics (optional)

With a compatible Xbox serial debug port and kernel debugger enabled, run
`python3 xbox-client/tools/kd_serial.py /dev/serial/by-id/YOUR_ADAPTER` with
permission to access that port. The transport is 115200 baud, 8N1. Startup
messages include the thread address and stage; `bin/player.map` maps this
specific build's addresses to functions. Do not use a map from another build.

The small KD client acknowledges debug output and continues module-load
notifications, but **does not automatically continue exceptions**. Commands:
`break`, `context`, `read HEX_ADDRESS BYTE_COUNT`, `version`, `continue`,
`resume-break`, `reboot`, `quit`. `resume-break` advances EIP only over a verified
INT3 breakpoint; never use it to bypass an application fault. `reboot` requests
a console reset and may require a manual restart. Keep the client connected
while debugging: a stopped kernel can otherwise wait for its debugger.
Protocol layouts follow the [ReactOS KD definitions](https://github.com/reactos/reactos/blob/master/sdk/include/reactos/windbgkd.h).

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
