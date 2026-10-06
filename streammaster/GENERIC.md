# Generic StreamMaster firmware — UNTESTED

These builds are compiled for ESP32-S3 or ESP32-S2 boards with working external
PSRAM. They have **not been tested on physical generic boards**. Successful
compilation is not a claim of board compatibility, throughput or stability.
The tested Onju Voice V3 package remains separate and unchanged.

**Current consolidated release:** all generic targets are freshly built from
the same **0.3.18** source as Onju, in `StreamMaster/ESP32-…-UNTESTED/`.
The Onju optical-audio output is disabled on generic boards (no assumed GPIO).
Only the QIO80 variants below are published there; older DIO40 commands
describe optional developer builds. Generics remain **UNTESTED**, network-only,
with no USB-Bluetooth claim. Use each folder's `flash_args` for updates.

## Choose the matching package

| Package | Chip | External RAM interface |
| --- | --- | --- |
| StreamMaster-ESP32-S3-QUAD-UNTESTED | ESP32-S3 | Quad SPI PSRAM |
| StreamMaster-ESP32-S3-OCTAL-UNTESTED | ESP32-S3 | Octal PSRAM |
| StreamMaster-ESP32-S2-UNTESTED | ESP32-S2 | SPI PSRAM |

All builds use 240 MHz CPU, DIO flash at 40 MHz, a **4 MB flash layout**, and
80 MHz PSRAM. Provide at least 4 MB flash and 2 MB PSRAM; 8 MB PSRAM on S3 is
preferable for headroom. These are target requirements, not measured generic
board minimums. Boards with larger flash may use the same 4 MB layout; extra
space is unused. Boards without PSRAM are not supported. Match PSRAM type to
the module datasheet, not merely its capacity. Other memory wiring/speeds may
require a custom build. S2 has less CPU/internal-memory headroom than S3;
six TLS channels and peak transfer performance require hardware testing.

### Optional QIO 80 MHz packages (S3 and S2)

Packages with `-QIO80-UNTESTED` use Quad flash at 80 MHz instead of DIO 40 MHz.
Use these only when the board's flash supports QIO at 80 MHz and IO2/IO3 are
correctly connected. Octal **PSRAM** does not imply Octal flash. CPU/PSRAM clocks,
4 MB partition layout and USB behavior are unchanged. S2 supports QIO 80 MHz
too, but has less CPU/internal RAM headroom; no Onju throughput is promised.
Unknown boards retain the conservative DIO40 choice; neither generic variant
has been hardware-tested. These generic packages do not add USB Bluetooth.

Build: `bash build-generic.sh s3-quad qio80` (also `s3-octal` or `s2`).
For factory flashing use `--flash_freq 80m`, keeping the intentional DIO boot
header. When switching clock variants, update the **bootloader as well as the
application**: bootloader.bin at 0x0 for S3 or 0x1000 for S2, partition-table.bin
at 0x8000, and the matching app at 0x10000. These split writes preserve NVS.
Do not use the app-only update instructions below to change flash clock.

There are **no status LED writes, amplifier controls or board-specific VBUS
GPIO controls** in generic builds. The UART console remains enabled; keep its
default TX/RX pins free. Native USB uses GPIO19 (D-) and GPIO20 (D+) on S2/S3.
Use the native USB port, not a USB-to-UART programming port. The ESP is host and
the PSP is device. Supply regulated USB VBUS through appropriate host hardware
or a correctly powered adapter/hub. Do not join independent 5 V supplies or
backfeed the PC; a USB-C receptacle alone does not guarantee host power/role
wiring. Boards needing a VBUS-enable pin require board-specific setup first.

## Initial installation

Back up the board before replacing existing firmware. Select the matching
factory file and chip; all merged factory files are flashed at **0x0**:

```sh
# Example: S3 with Quad PSRAM. Adjust port for your board.
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash \
  --flash_mode dio --flash_size 4MB --flash_freq 40m \
  0x0 streammaster-s3-quad-untested-factory.bin
```

For S3 Octal use `streammaster-s3-octal-untested-factory.bin`.
For S2 use `--chip esp32s2` and `streammaster-s2-untested-factory.bin`.
The S2 bootloader is embedded at **0x1000 inside the merged image**; do not
shift the merged file to 0x1000. Split images instead use 0x1000 for the S2
bootloader (0x0 for S3), 0x8000 for partitions and 0x10000 for the application.
Factory flashing overwrites NVS and its saved network profiles.

For subsequent updates with the same partition layout, preserve settings by
flashing only the matching `*-untested-app.bin` at **0x10000**. Do not flash
an S2 binary on S3 or interchange Quad/Octal builds.

Install the current PSPStreamer app and StreamMasterUSB.prx. The same PSP
protocol and five-network settings are used; no generic-specific PSP build is
needed. Network status identifies these builds as **SM S2/S3 UNTESTED**.
Configure Wi-Fi in Settings > StreamMaster and select the StreamMaster transport.

## First hardware checks

1. Confirm PSRAM initialization and absence of boot/reset errors on UART.
2. Check USB attachment and network status from the PSP settings menu.
3. Save a Wi-Fi profile; test DHCP, server access and power-cycle persistence.
4. Run the USB integrity test, then a small HTTP download with verification.
5. Test playback, reconnect and sustained transfers before relying on the board.
   HTTPS has additional memory/CPU cost; no Onju throughput claim applies here.

## Rebuild

With the project's ESP-IDF v5.5.1 environment activated, from the repository root:

```sh
bash streammaster/build-generic.sh s3-quad
bash streammaster/build-generic.sh s3-octal
bash streammaster/build-generic.sh s2
```

Each build has a separate directory and sdkconfig, and does not change the Onju
configuration. Outputs and ZIP packages are under `streammaster/release/`.
USB/network code is shared; memory configuration and nonessential board I/O
are selected at build time. No flashing is performed by this script.
