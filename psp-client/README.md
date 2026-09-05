# PSP-Client

This directory contains the native PSP application: H.264 Baseline video through the Media Engine, MP3 audio through the PSP DAC, subtitle/audio-track selection, receiver-style menus, and the bridge for the AVC path. Install `subtitle_font.raw` and `menu_skin.raw` next to `EBOOT.PBP`.

Installation, the `ms0:/PSP/SYSTEM/PSPStreamer.cfg` configuration file, and the build command are described in the [project README](../README.md).

For a local build, `OPENH264_DIR` must point to the PSP-compiled OpenH264 library:

```bash
make OPENH264_DIR=/path/to/openh264
```
