# PSP-Client

This directory contains the native PSP application: timestamped FLV with H.264 Baseline video through the Media Engine, MP3 audio through the PSP DAC, subtitle/audio-track selection, receiver-style menus, and the AVC bridge. Install `subtitle_font.raw`, `cooleyesBridge.prx` and `dvemgr.prx` next to `EBOOT.PBP`. The menu skin is embedded in the application.

Installation, the `ms0:/PSP/SYSTEM/PSPStreamer.cfg` configuration file, and the build command are described in the [project README](../README.md).

For a local build, put PSPDEV/PSPSDK on PATH:

```bash
make
```

`flv.h` contains portable framing and PTS comparison helpers, and `timed_stream.h` contains the bounded network packet queues. Both display outputs share `play_h264()` in `main.c`. Video timing comes from container PTS and the output audio buffer timestamp; no LCD/TV frame-rate calibration is used. Update the server/add-on together with the client.
