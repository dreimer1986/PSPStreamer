# PSPStreamer

**Your media library. Your music. A new life for the PSP.**

Turn Sony's handheld into a networked media player — at home on the TV or on the
move with downloaded episodes. Browse Plex, Jellyfin, DLNA and your own files in
a receiver-inspired interface with analog VU meters, cover art and music visualizations.

![PSPStreamer cyberpunk artwork](psp-client/assets/xmb-cyberpunk-concept/background-master.png)

*Promotional artwork. The player uses its own receiver-style interface.*

## Watch, listen, take it with you

- **Films and series:** timestamp-synchronized video and audio, selectable
  soundtracks, subtitles, chapters, resume and automatic next-episode playback.
- **Music and radio:** music libraries, ID3/ICY metadata, a configurable spectrum
  analyzer, MilkDrop presets and the Monkey tunnel — with a playable flight Easter egg.
- **Your library, together:** Plex, Jellyfin, DLNA and server-accessible folders;
  search, favorites, recently played, continue watching and editable playlists.
- **Ready for travel:** convert on the server, download to the Memory Stick or
  transfer a prepared ZIP by PC. Play without a network connection.
- **On the big screen:** LCD and TV-out layouts, fullscreen playback, artwork,
  remote controls and in-app settings with illustrated help.

The server handles transcoding; the PSP handles playback. No need to manually
convert your whole collection first. Choose streaming or prepare media for offline use.

## More than a handheld player

**A browser and an original Xbox can play too.** The web player and native
Xbox client use separate playback paths; PSP support stays independent.
The Xbox port is a preview — [see its capabilities and limitations](xbox-client/README.md).

**Home Assistant fits in naturally.** Run the server as a Home Assistant app,
or add a media-player entity through the separate HACS integration. These are
two different components; see [Home Assistant setup](docs/SETUP.md#home-assistant-integration-hacs).

**Optional hardware opens more possibilities.** StreamMaster provides USB
networking through an ESP32 board. The tested Onju Voice V3 configuration also
supports external Bluetooth/USB controllers and a wired optical S/PDIF output.
None of this hardware is required for ordinary PSP playback.

## Get started

1. Run the server with **Docker**, the **Home Assistant app**, or **Python + FFmpeg**.
2. Enable your media sources and set a server password.
3. Copy the PSP release to `PSP/GAME/PSPStreamer/` on a homebrew-capable PSP.
4. Enter the server address in the app, connect and pick something to play.

[Installation and configuration →](docs/SETUP.md) ·
[Release contents →](docs/RELEASE_LAYOUT.md) · [Changelog →](CHANGELOG.md)

Keep the server's persistent data volume when updating. On the PSP, update the
matching application files together and retain your configuration, presets and
downloaded media. Use HTTPS outside a trusted network.

## Optional PSP companions

| Component | What it adds |
| --- | --- |
| [PSP Consolizer](psp-controller/README.md) | StreamMaster controller input for games and XMB, TV activation, overlays, configurable game rumble and optical audio mirroring. |
| [FuSa Fullscreen](psp-fusa-probe/FULLSCREEN.md) | Fullscreen TV scaling for PSP games, with menu and overlay handling. |
| [StreamerOC](psp-overclock/README.md) | Optional clock control and per-title profiles. Overclocking is hardware- and game-dependent, not required. |
| [StreamMaster](streammaster/README.md) | ESP32 firmware, board support, wiring and controller/network setup. |

## Know the limits

PSPStreamer is homebrew, not an official Sony product. Compatible custom firmware
is required; TV-out and optional hardware features depend on your PSP and setup.
Server-side FFmpeg transcoding is intentional. MilkDrop is adapted to PSP hardware:
shader code is ignored and complex presets can be slow. Generic ESP32 firmware
targets marked **UNTESTED** have not been hardware-verified.

## Explore or contribute

- [Detailed setup, configuration and security](docs/SETUP.md)
- [Build and test from source](docs/BUILDING.md)
- [PSP client](psp-client/README.md) · [Xbox client](xbox-client/README.md)
- [Documentation directory](docs/) · [Earlier development notes](docs/DEVELOPMENT_HISTORY.md)
- [License](LICENSE) and [references / asset credits](docs/SETUP.md#license-and-reference)

Bug reports with relevant logs, console/controller details and reproducible steps
are welcome. Screenshots of the real LCD/TV interface are welcome too.
