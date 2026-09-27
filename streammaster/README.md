# StreamMaster — Onju Voice V3 USB/Wi-Fi bridge

Version 0.1.2 is the **hardware bring-up build**. It includes a native ESP-IDF
firmware, a PSP USB device driver, and a PSP settings/diagnostics menu. It does
**not yet route media playback, library browsing or remote control through
USB**. Existing playback continues to use the PSP's Wi-Fi. Test USB enumeration,
data integrity and Onju's server connection before enabling it as a transport.

## Hardware

Target: the original **Onju Voice PCB revision V3**, ESP32-S3R8, 16 MB SPI flash,
8 MB octal PSRAM. This is not the separately named Onju Voice 2 board. Native
USB is D− GPIO19 / D+ GPIO20. The ESP32 is the USB **host**, the PSP is its USB
**device**. The USB peripheral is therefore unavailable as a USB serial console
while this firmware runs. UART0 remains available for diagnostics at 115200 baud.
The speaker amplifier is disabled; microphone, touch controls and speaker
functions from the previous firmware are not implemented here.

Use your powered USB adapter for the host connection. Onju's V3 USB VBUS circuit
is an **input**, not a switched host-power output. Use a correctly powered 5 V
USB data connection; do not connect the Nest supply voltage to USB VBUS or use
a passive power-combining cable. Do not connect the PC and PSP simultaneously
as competing hosts. Full-speed hubs are supported by the firmware, but the
particular powered adapter/hub still needs a hardware test.

Sources: [Onju hardware and schematic](https://github.com/justLV/onju-voice),
[ESP-IDF USB host documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/usb_host.html),
[PSPLINK USB descriptor reference](https://github.com/pspdev/psplinkusb/blob/master/usbhostfs/main.c).
The PSPLINK BSD notice is included in `PSPLINK-license.txt`.

The S3 provides 2.4 GHz Wi-Fi (including WPA2/WPA3 Personal), not 5 GHz Wi-Fi.
Its USB host is full-speed, 12 Mbit/s raw, not high-speed USB. Actual throughput
must be measured. Bluetooth on the S3 is BLE only; ordinary phone Bluetooth
PAN tethering is **not** supported. A phone's Wi-Fi hotspot remains an option.

## Status LEDs

The six onboard GRB pixels (GPIO11, 800 kHz) show:

| LEDs | Meaning |
| --- | --- |
| First outer LED, dim white | Firmware running / power indication after boot |
| Four central LEDs, dim blue | No Wi-Fi connection configured or deliberately disconnected |
| Central amber running light | Connecting / reconnecting to Wi-Fi |
| Central red pulse | Connection failed / waiting for another retry |
| 1–4 central steady bars | Wi-Fi connected and an IPv4 address obtained |
| Other outer LED, cyan | PSP USB interface claimed by StreamMaster |

Signal bars: 1 below −80 dBm, 2 from −80, 3 from −70, 4 from −60 dBm.
One or two bars are amber/yellow; three or four are green. A 3 dB downward
hysteresis and one RSSI reading per second prevent rapid threshold flicker.
This indicates the **access-point signal**, not server/Internet reachability
or measured throughput. The USB LED goes out when the PSP leaves the
StreamMaster submenu and releases its USB interface.

Brightness is deliberately low (maximum 24/255 per colour). An independent
low-priority task checks display state four times per second and sends only
changed frames. RMT generates the LED timing in hardware; the complete six-pixel
frame fits its allocated symbol memory. LED errors disable the display rather
than aborting USB/Wi-Fi. The white LED is a firmware indication, not a supply
voltage measurement or watchdog guarantee. No PSP app update is required for
these indicators.

## Building the firmware

Use **ESP-IDF v5.5.1**, target `esp32s3`. From a checkout of ESP-IDF:

```sh
./install.sh esp32s3
. ./export.sh
```

From the PSPStreamer repository root:

```sh
idf.py -C streammaster build
bash streammaster/package.sh
make -C psp-client/streammaster_usb
make -C psp-client
```

The last two commands require PSPSDK on `PATH`. `streammaster/sdkconfig.defaults`
selects V3 flash/PSRAM, 240 MHz, USB hub support, UART console and the TLS root
bundle. No ESPHome component is required. Firmware output:
`streammaster/release/StreamMaster-Onju-V3.zip`.

## Flashing and recovery

Flashing replaces the existing Onju application. **Back up the old firmware and
configuration first.** With the board connected to your PC and in its ROM
download mode, using esptool 4.12:

```sh
python -m esptool --chip esp32s3 --port /dev/ttyACM0 read_flash 0 0x1000000 onju-v3-backup.bin
```

Adjust the port for your computer. ROM download mode requires GPIO0 low during
reset; use the board's documented boot/reset controls. USB serial disappearing
after normal boot is expected because StreamMaster takes over the USB hardware
as host. Return to ROM download mode to flash again. See the
[ESP32-S3 boot-mode guide](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html).

Extract the firmware ZIP. For **first installation**, run from that directory:

```sh
python -m esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m 0x0 streammaster-onju-v3-factory.bin
```

The factory image includes erased padding over the NVS configuration area and
therefore resets saved settings. For later **StreamMaster updates with the same
partition layout**, preserve network settings using the separate binaries:

```sh
python -m esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash --flash_mode dio --flash_size 16MB --flash_freq 40m 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 streammaster_onju_v3.bin
```

No automated flash command is run by the build or packaging scripts. OTA updates
are not included in this initial firmware.

## First PSP test

1. Copy the matching `EBOOT.PBP`, `PSPStreamer.prx` and **`StreamMasterUSB.prx`**
   beside the existing files in `ms0:/PSP/GAME/PSPStreamer/`. Do not add the USB
   driver to ARK's global plugin list. It is loaded only from this menu.
2. Boot Onju normally, connect its USB host data connection to the PSP, launch
   PSPStreamer. Do **not** enter the XMB's USB mass-storage mode or run USBHostFS.
3. Open **Select → Settings → StreamMaster USB → Connect / retry USB**. On first
   setup, an empty SSID is normal. A missing adapter times out after about ten
   seconds; Circle cancels without blocking the GUI.
4. Select **Find Wi-Fi networks**, choose your SSID, edit **Wi-Fi password** and
   use the keyboard's Start button to accept. DHCP and automatic DNS are default.
   Hidden networks can be entered manually. SSIDs are limited to 32 UTF-8 bytes;
   personal-network passwords are 8–63 bytes or a 64-digit hexadecimal key.
   An empty password selects an open network.
5. For a fixed IPv4 address, turn DHCP off and enter address, subnet mask,
   gateway and DNS. Automatic DNS is disabled in this case. `0.0.0.0` is allowed
   for an intentionally absent gateway; a primary DNS address is required.
   DHCP can also be combined with manually chosen DNS servers.
   DHCP-managed fields show `(DHCP)` followed by the actual leased address,
   subnet mask, gateway or DNS value. They are read-only: X refreshes the status
   instead of opening the keyboard. **Network status** refreshes all fields too.
   Until a lease is available, or after changing to a different unsaved SSID,
   they show `(DHCP) -`. Manual DNS stays unmarked even when IPv4 uses DHCP.
   Live values do not overwrite saved manual addresses. Full lease details
   require firmware 0.1.2 and the matching PSP app; older app/firmware combinations
   retain the original status command but cannot show all new fields.
6. Choose **Save to Onju + connect**, wait a few seconds and select **Network
   status**. Expect “Connected”, an IP address and RSSI. The settings persist in
   Onju's NVS, not in the PSP CFG. Returning with Circle discards unsaved edits.
   Changing the SSID clears the previous password choice. Otherwise “Keep saved”
   preserves the stored password without revealing it to the PSP.
7. Run **USB transfer test**. It checks every byte of 128 echoed 4064-byte payloads
   and reports verified-payload KiB/s (not the doubled USB round-trip traffic).
8. Run **Test saved server**. It requests `/api/health` through Onju using the
   server URL and authentication last saved in the main PSP settings. Expect
   `HTTP 200` and a nonzero response length. Unsaved changes in the parent settings
   dialog are not used. HTTPS uses Onju's bundled trusted roots and SNTP time;
   allow time synchronization after Wi-Fi connection. Private/self-signed roots
   and the PSP's certificate-pinning cache are not imported in this build.
9. Test cancel and retry with the USB cable disconnected, then reconnect.
   Leave the submenu and verify existing PSP Wi-Fi playback still works.

Report the USB result/rate, network status and server test result. Keep passwords
out of screenshots/logs. If USB initialization fails, include the full hexadecimal
code. `FFFFFFFC` is a timeout, `FFFFFFFB` means busy, `FFFFFFFD` is a transport
or HTTP I/O failure. Other values can be original PSP module/USB errors.

## Architecture and current boundaries

- A small **kernel PRX**, using SDK padded USB descriptors, exposes `stm:` I/O
  control calls. No global WLAN hooks and no changes to MPEG import order.
- One cancellable PSP worker serializes requests. DMA buffers remain allocated
  until both USB completion callbacks have returned; they are not recycled after
  a timeout. Retry deactivates/re-enumerates USB. The PRX refuses to take over an
  already active PSP USB application.
- ESP-IDF's USB client task owns transfers; a separate worker handles Wi-Fi,
  NVS and HTTP so connection setup never blocks USB event processing. Device
  generation numbers reject responses from an earlier cable connection.
- Fixed 4096-byte little-endian messages have bounded payloads, sequence numbers
  and a checksum. These detect corruption; they are not encryption or an
  authentication mechanism. USB is a physically trusted connection.
- WLAN reconnect is bounded to five retries; **Reconnect Wi-Fi** starts a new
  attempt. HTTP reads and USB cancellation have their own bounded waits.
- No passwords are deliberately logged. NVS is not encrypted: physical flash
  access can recover saved credentials. HTTPS validates server certificates,
  does not silently downgrade and does not forward credentials across redirects.
- One HTTP GET stream is available to diagnostics. Multiplexed media/metadata
  requests, the actual player transport switch and long playback/recovery tests
  remain the next integration step **after this hardware check**.

Validation: ESP32-S3 firmware, PSP driver and PSP app compile; host tests cover
frame corruption/bounds and network configuration validation under sanitizers.
Those tests do not emulate USB enumeration, kernel driver timing or physical
adapter behavior. No hardware success is claimed until the above test is run.
