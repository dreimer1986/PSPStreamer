# Consolidated release — 2026-10-01

Use these packages:

- `PSPStreamer/` or `PSPStreamer.zip`: app, both PRX companions, assets/presets.
  Copy EBOOT.PBP, PSPStreamer.prx and StreamMasterUSB.prx together. Preserve your
  existing config, local media and saved state. USB defaults to 8 KiB x 4;
  missing/zero settings use that default, explicit depth=2 remains supported.
- `StreamMaster-Onju-V3/` or its zip: tested 0.3.10-bt-qio80-iram, Bluetooth,
  QIO flash 80 MHz, CPU 240 MHz, PSRAM 80 MHz. This is the only Onju choice here.
- `SEPLUGINS/` or its zip: optional StreamerOC, unchanged by this release.
- `UNTESTED/`: generic S2/S3 board alternatives. Check exact flash/PSRAM wiring
  and GENERIC.md; Onju validation does not validate these hardware variants.

Flash from the Onju folder using the supplied components, not the factory image:

```sh
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash @flash_args
```

This preserves NVS Wi-Fi profiles, Bluetooth bonds and controller mapping.
The DIO image header is intentional: the bootloader enables QIO. Do not override
flash_args. The merged factory image is for fresh installations, not updates.

Build the standard PSP app with ordinary `make` (O3/LTO). Build/package Onju with
`bash build-bluetooth.sh` / `bash package-bluetooth.sh` after activating ESP-IDF.
No special PSP test flag is necessary. Onju firmware remains byte-identical to
the verified 0.3.10 test image; only packaging/default selection is consolidated.

Old test packages, QIO40/DIO fallbacks, stale release copies and stray diagnostic
files are moved to a dated sibling archive, never deleted. Historical comparison
documents remain evidence, not an active list of installation options.

Validation: full HTTP payload 170143443 bytes in 222268 ms = 747.5 KiB/s; subsequent
read-back verification and download completion succeeded. Both earlier partial
downloads were user-cancelled; no additional transport errors during downloads.
Two early diagnostic errors predate downloading and did not increase afterward.
This is a successful hardware test, not a guarantee against all future faults.
Bluetooth long-duration testing and generic hardware validation remain open.
