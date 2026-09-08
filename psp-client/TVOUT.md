# Component TV output

The established video output is 720×480, 32-bit ABGR, stride 768 pixels,
using the real EDRAM address `0x04000000` (uncached alias `0x44000000`).
LCD uses 480×272, stride 512. Never substitute the extra-RAM `0x0A` window;
it caused the old repeated/garbled LCD-stride pictures on ARK-5 PSP-3000.

## Optional native GUI

In `ms0:/PSP/SYSTEM/PSPStreamer.cfg`:

```ini
tv_ui=auto
```

Default `off` preserves LCD menus and TV video. `auto` checks for a component
cable at startup, allocates a separate RAM menu canvas, and enters TV mode.
Hold L while starting to bypass TV menus for that session. Missing/unsupported
cables or startup failure fall back to LCD menus. Cable presence does not
prove an active TV/OSSC signal. Hotplug during playback is not a supported
automatic mode switch; stop and restart with the desired cable connected.

The native layout includes all browser/loading/info/options/music screens,
12 entries per page and original-resolution Latin-1 font glyphs. Music's
Cross+Triangle fullscreen keeps analogue VUs and the volume dial visible.
Both GUI skins are embedded in EBOOT; no new file must be copied separately.
The TV dial is authored for anamorphic 16:9 presentation.

`display_output.h` owns mode transitions. Entering video while already in
TV mode is a no-op for DVE/framebuffer mode selection. Video retains its
existing staging, timestamps and decoder layout. Only after playback workers
have joined can the GUI present another frame. Native TV return does not call
`pspDebugScreenInit`, which could silently restore the LCD stride.

Menus draw off-screen at stride 768 then copy to the same EDRAM scanout at
VBlank. Two 32-bit 768×480 buffers do not fit in the real 2 MiB EDRAM, so the
second buffer is RAM, **not** an assumed second VRAM framebuffer. Host tests
do not establish transfer timing, tearing behaviour or physical TV sync.

## Calibration and first hardware test

Select+L+R enters the existing component test card; repeat to return to the
originating menu output. `dvemgr.prx` exposes PMPlayer Advance's historical
`pspDveManager` interface and remains unchanged. It is distributed under
PMPlayer Advance's GPL-2.0-or-later licensing, as before.

1. Keep `tv_ui=off`: verify the existing LCD-menu/TV-video behaviour first.
2. Enable `auto`, connect the tested component cable and restart. Verify
   library navigation, held L/R paging, Triangle information and X options.
3. Start video locally with/without subtitles. Stop, seek, start another
   video, and allow a natural next-episode transition. The TV must retain
   signal and show one picture; no LCD-stride GUI may flash during playback.
4. Repeat using the web remote. Then test music, volume/held repeat, pause,
   fullscreen and the live VUs/spectrum.
5. Restart without the cable, and once with L held. Menus must be on LCD.

Report the exact transition if an issue appears. Keep the existing sync CSV
diagnostics; there are no new playback clock estimates or fixed offsets.
