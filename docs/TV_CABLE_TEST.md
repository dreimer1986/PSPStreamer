# TV cable test candidate — PSPStreamer and plugins

New paths are **UNTESTED ON HARDWARE**. Keep the previous working packages.
No ESP32 firmware, server, Xbox, FTP or audio-clock change is required.

## Install

The combined test ZIP has `PSP/GAME/PSPStreamer/` and `SEPLUGINS/` folders.
Replace **both EBOOT.PBP and PSPStreamer.prx**, and update both Consolizer
files (`PSPConsolizer.prx` and `PSPConsolizerUSB.prx`), StreamerOC and
FuSaFullscreen. Keep existing INIs, CFG, rules, profiles and plugin activation
entries. `.example` files do not overwrite settings. The app carries its
existing codec/display helpers. On Go internal storage use `ef0:` in entries.

For a new installation activate only plugins you actually need. StreamerOC
clock policy remains unchanged; leave OC off for this display test. Do not
enable OC in VSH/POPS just to test the picture. Consolizer needs its existing
StreamMaster hardware for controller input; the player and FuSa do not.

## Hardware matrix

| Model | LCD | Composite / S-video | Component / D-terminal |
|---|---|---|---|
| 1000 / Street | Existing path | No physical output | No physical output |
| 2000 | Existing path | New 480i; FuSa bypass for PSP games | Existing 480p, new 480i |
| 3000 | Existing path | New 480i | Existing 480p, new 480i |
| Go | Existing path | New composite 480i, Go cable required | Go cable: 480p / 480i |

No 576i, RGB-SCART or HD signal is promised. A composite-to-SCART adapter
still carries composite. The CRT must accept the actual 480i signal. The
existing TV layout is intended for 16:9 presentation; CRT overscan/aspect may
need TV settings. Record exact model, CFW, cable plugs, TV and adapter model.

## Test separately, then together

1. **PSPStreamer alone:** connect the cable before launch. Start with LCD
   menus (`tv_ui=off`), then video; verify picture, sound and subtitles.
   For composite/S-video `tv_scan=auto` suffices. For component CRT use
   `tv_scan=interlaced` (app setting 1). Setting 2 never forces composite to 480p.
2. Enable `tv_ui=auto`; check every menu, artwork, playback controls, pause,
   seek, stop, next episode, return to menu and LCD/TV switching. After changing
   scan preference save and toggle LCD/TV, or restart. Hold L at launch to
   bypass TV menus. Do not swap cable types in the middle of playback.
3. Test spectrum, MilkDrop and Monkey, windowed and fullscreen, including text
   and game HUD. Watch for alternating missing rows, wrong colors or audio load.
4. **FuSa alone:** enable Sony TV output, then NOTE+R; repeat to disable.
   For a PSP-2000 composite game NOTE+R can activate from LCD. Auto mode is
   optional. For component CRTs set FuSa's separate `tv_scan=1` (also in the
   plugin settings menu); this allows direct activation from LCD as well.
   Restart the game after plugin INI changes. Test game scene/movie
   transitions, HOME, save dialogs and exit.
5. **Consolizer:** automatic TV at boot/game start, controller input and HOME.
   Test overlay=1 and overlay=2 separately. Repeat with FuSa active.
6. **StreamerOC:** test both overlays separately and with Consolizer/FuSa,
   without changing previously validated clock settings.
7. Recheck component 480p and LCD on the existing hardware. Their rendering
   paths are retained, but this candidate still needs regression confirmation.

If black/garbled: try the physical SCREEN button to restore LCD. Do not keep
blindly toggling if the console has frozen. Save app diagnostics and
`SEPLUGINS/FuSaFullscreen/fullscreen.log`, plus Consolizer/OC logs if enabled.
Include a photo/video and the exact transition. A 503 height in logs is the
interlaced storage geometry, not an accidental resolution.

## References and verification boundary

- PPA `videomode.cpp`, `common/graphics.c`, `mod/gu_draw.c`, reference revision
  `9ce494d020d3c909ee312bda1932f8d806a2f05d`:
  https://github.com/PSP-Archive/PMPlayer-Advance
- Original FuSa source distinguishes cable classes 1/2 and interlaced DVE mode.
- Shared host tests check row mapping, RGB conversion, dirty rectangles,
  scaler equivalence, VRAM limits and reversible overlays. They do not emulate
  the PSP display controller, cable electronics, CRT timing or actual GU DMA.
