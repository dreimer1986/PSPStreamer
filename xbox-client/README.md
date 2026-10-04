# Original Xbox client — connection preview, NOT a media player yet

This is a native, open-source **nxdk** XBE, not an XBMC skin or a PSP executable.
It starts a 640×480 display, controller input and Ethernet; A tests the existing
authenticated `/api/health` endpoint, X displays a bounded preview of the library
JSON. The reported RAM size comes from the Xbox kernel. Back reboots to the
configured dashboard. Requests have a ten-second total timeout; input waits
while a diagnostic request runs. No console files are changed by the program.

**Built successfully; not tested on a physical Xbox. There is no audio/video
decoder, navigable library GUI or HTTPS implementation in this preview.**

## Hardware test

1. Copy `bin/default.xbe` and a copy of `server.cfg.example` renamed to
   `server.cfg` into the same application directory on a homebrew-capable Xbox.
2. Set `host` to the server's LAN IPv4 address (not a URL), `port`, and optionally
   the server password. Use the direct HTTP port, not an HTTPS reverse proxy.
   HTTP/Basic authentication is only suitable for a trusted local network.
3. Launch the XBE from your dashboard. Note the reported RAM and network result.
4. Press **A**, then **X**. HTTP 200 and JSON are expected. 401 means the password
   is missing/wrong. Back reboots. Do not post your password when reporting.

Ethernet setup uses nxdk's automatic configuration. This preview intentionally
requires an IPv4 server address to avoid hiding DNS/TLS issues in the first test.

## Reproducible build

The SDK is not committed. Reference revision used for the first successful build:
`XboxDev/nxdk@14d5ee97e73347c973f1f57b68b79ec08c9e77f2` (including its pinned submodules).
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

Output: `xbox-client/bin/`, including the XBE, example config, source and notices.
Include a copy of the GPLv3 license when distributing the linked executable.
This experimental target is deliberately
not mixed into the five stable PSP/firmware release packages.

## What is needed for the real port?

- **Your hardware:** actual CPU frequency, 64/128 MB RAM, BIOS/dashboard and XBMC
  version, controller, TV cable and desired output mode (PAL/NTSC/480p first).
  The preview's RAM readout helps, but does not measure CPU frequency.
- **Deployment:** how you copy/start XBE files (usually an existing FTP-enabled
  dashboard) and a reachable LAN server. No Xbox password or full disk image is
  required for source development.
- **Decoder choice and measurement:** port and benchmark a software decoder.
  PSP Media Engine, Sony AVC/MP3 APIs, GU, VFPU and kernel plugins cannot be
  reused on Xbox. Start by comparing modest-resolution MPEG-1/2 or MPEG-4/MP3
  streams against H.264; do not promise HD or hardware H.264 decoding. Codec
  choice, server output format and licenses need review together.
- **Platform backend:** network ring buffers, demuxing with actual PTS, audio
  output/sample clock, texture uploads and video scheduling. Reuse the current
  timestamp design, not historic guessed FPS synchronization.
- **UI:** SDL2/controller-based library and settings, then media options,
  subtitle rendering, artwork, playlists and resume. The server already handles
  filesystem, Plex, Jellyfin and DLNA discovery/authentication/metadata.
- **Later:** offline cache, visualization ports and Xbox-specific optimizations.
  Do not port PSP USB bridges/OC/Consolizer plugins. An XBMC script could be an
  independent shortcut using its decoder, but is not this native client.

Next acceptance step: confirm the preview boots and reaches the library, then
benchmark one video/audio decoder on the actual machine before committing to
the full playback architecture. Preserve the working PSP paths throughout.

## Sources and licenses

- [nxdk](https://github.com/XboxDev/nxdk): original Xbox SDK, BSD sockets and SDL2
  input/audio/graphics. nxdk and its dependencies keep their own notices.
- New client source follows this repository's GPL-2.0-or-later license.
- The preview links nxdk runtime/USB/network components and SDL2. See the SDK's
  `LICENSES/`, SDL2 `COPYING.txt`, lwIP `COPYING`, and source SPDX headers.
  A distributed preview directory includes these notices and its complete
  project source/build instructions; upstream dependencies are pinned above.
  The linked executable uses the GPL's **version 3** option to accommodate
  Apache-2.0 components (USB); the rest of PSPStreamer is not relicensed.
