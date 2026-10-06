# PSPStreamer 2.4 — release contents

One current folder and matching ZIP per component. No combined plugin bundle,
duplicate PSP application, experimental clock packages or old test releases.
Choose either the extracted folder **or** its ZIP.

| Package | Install / purpose |
| --- | --- |
| `PSPStreamer/` + `.zip` | Copy to `ms0:/PSP/GAME/PSPStreamer/`. App, PRX companions, fonts, presets and Monkey assets. |
| `PSPStreamerXbox/` + `.zip` | Copy to the Xbox application folder; launch `default.xbe`. Preview 0.7.3, runtime assets, demo and familiar PSP presets, licenses and a host-readable source ZIP. |
| `PSPConsolizer/` + `.zip` | Copy to `ms0:/SEPLUGINS/PSPConsolizer/`. Controller bridge, TV activation and rumble. |
| `StreamerOC/` + `.zip` | Copy to `ms0:/SEPLUGINS/StreamerOC/`. Clock plugin and per-title rule examples. |
| `FuSaFullscreen/` + `.zip` | Copy to `ms0:/SEPLUGINS/FuSaFullscreen/`. Fullscreen scaler and TV helper. |
| `StreamMaster/` + `.zip` | All firmware targets, from the same source and **0.3.17** version base. No PSP app/plugins inside. |

Plugin folders contain `.ini.example` files. For a **new installation**, copy
the main example to the corresponding `.ini` filename. On update, **preserve
existing INIs**, profiles, local media and state. Keep the app EBOOT/PRX files
together. Restart the PSP after updating resident plugins. See plugin READMEs
for ARK registration and dependencies.

## Firmware targets

- `StreamMaster/Onju-V3/`: ESP32-S3 Onju Voice V3,
  `0.3.17-bt-qio80-iram`, USB Bluetooth/HID and rumble. QIO flash 80 MHz,
  16 MB layout, CPU 240 MHz, Octal PSRAM 80 MHz. Hardware-tested configuration,
  freshly rebuilt from current source; no new performance claim.
- `StreamMaster/ESP32-S3-QUAD-UNTESTED/`: generic S3, Quad PSRAM.
- `StreamMaster/ESP32-S3-OCTAL-UNTESTED/`: generic S3, Octal PSRAM.
- `StreamMaster/ESP32-S2-UNTESTED/`: generic S2, SPI PSRAM.

Generic versions are `0.3.17-generic-qio80`, 4 MB layout, QIO flash 80 MHz and
PSRAM 80 MHz. **UNTESTED** means compiled, not physically validated. S2 has less
internal-memory/CPU headroom. Generics retain their network-only feature set
(no external USB Bluetooth); equal versions do not imply equal capabilities.
Read `StreamMaster/GENERIC.md` for wiring and memory requirements.

From the chosen firmware folder, update using the included split images:

```sh
# Use esp32s2 for S2; adjust the port as needed.
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash @flash_args
```

This preserves NVS Wi-Fi profiles and controller bonds. The DIO header is
intentional: the bootloader enables QIO. `factory.bin` is merged for **fresh
installation** at offset 0x0; writing it also overwrites the NVS gap. Split and
factory images are two formats of the **same build**, not different versions.
Likewise `dvemgr.prx` is included wherever needed for independent installation.

## Release notes and integrity

- `RELEASE-2.4.md`: English changelog since tag 2.3; older notes remain historical.
- `RELEASE-TITLE.txt`: one-line English release title.
- `CHANGELOG.md`: detailed history.
- `MANIFEST.json`: source revision, embedded firmware versions, configuration
  fingerprints and ZIP SHA256 hashes.
- Each component has `SHA256SUMS` for its files.
- `Probleme und Ideen.txt`: current ToDo, not another release.

## Packaging future updates

Build the PSP app, USB companion and all plugins first. Under ESP-IDF v5.5.1,
build Onju with `streammaster/build-bluetooth.sh`, then each generic target
with `streammaster/build-generic.sh s3-quad qio80` (also `s3-octal`, `s2`).
The version base is defined once in `streammaster/CMakeLists.txt`.

Run the canonical packager under the ESP-IDF Python environment:

```sh
python tools/package_release.py --seed /path/to/current/release --output /path/to/new-staging-release
```

Output must be a new directory. The seed provides licensed runtime helpers and
the preset/texture collection, never old app/plugin/firmware binaries. The
packager checks embedded firmware versions and hardware flags, creates ZIPs
from scratch and verifies them. After success, archive the previous release
**outside** the release directory and publish staging in its place. Firmware-
specific scripts produce development staging only; never copy their mixed
test directories over the canonical release.
