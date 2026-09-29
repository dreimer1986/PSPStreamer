# StreamMaster — Onju Voice V3 USB/Wi-Fi bridge

For other PSP homebrews, see the [developer integration guide](INTEGRATION.md):
source dependencies, build setup, socket/TLS semantics, ownership, configuration
and transport limitations. This is an application-local adapter, not a global
replacement for the PSP WLAN driver.

## Saved Wi-Fi profiles (firmware 0.3.0)

Five networks can be stored on the Onju, each with its own password and DHCP,
static IP, gateway and DNS settings. In PSP settings > StreamMaster:

1. Press X on **Profile (X: next)** to cycle through the five slots. Changing
   slots discards unsaved edits but does not change the live connection.
2. Scan/select an SSID (or type a hidden SSID), enter the password and optionally
   adjust IP/DNS, then choose **Save to Onju + connect**. Save before changing slots.
3. **Auto network** is enabled by default. At startup or explicit reconnect the
   firmware scans saved networks and selects the strongest visible signal;
   equal signals prefer the selected network. Hidden networks are tried directly
   if no saved visible network is found. After exhausted connection retries it
   tries another saved profile; once all have failed it waits 30 seconds and retries.
4. For a particular network, turn Auto network off, select its profile, then
   choose **Reconnect Wi-Fi**. Manual selection survives a power cycle.
5. **Delete profile** requires X twice. The old single-network configuration is
   imported into slot 1 automatically. Passwords are never returned to the PSP;
   a retained password belongs only to its original slot and SSID.

There is no periodic scanning or roaming while a connection is healthy. Within
one SSID, connection setup scans all channels and prefers the strongest AP.
This cannot guarantee uninterrupted roaming or override changing radio conditions.
Profiles persist in NVS when updating the application binary at **0x10000**:

```sh
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 40m \
  0x10000 streammaster_onju_v3.bin
```

Full flash erasure **or flashing the merged factory image at 0x0** removes them
(the factory image spans the NVS area). Use the split app binary for upgrades
with the existing partition layout. The legacy single-profile key is retained for firmware rollback and is not
kept in sync with edits made by 0.3.0. The older PSP app still works with the new
firmware; the new app falls back to single-network setup on older firmware.

## Transport measurements

### Experimental O3 comparison

The standard release remains `-O2`. The separate `StreamMaster-Onju-V3-O3`
package reports `0.2.2-o3` and uses the same protocol and settings. Rebuild from
the repository root after activating ESP-IDF:

```sh
idf.py -C streammaster -B build-o3 -DSTREAMMASTER_O3=ON build
bash streammaster/package.sh ../build-o3 StreamMaster-Onju-V3-O3
```

The comparison app and SDK sources use `-O3`, except IDF 5.5.1's
`vfs_semihost.c` and ESP32-S3 `rtc_clk.c`: these stay at `-O2` because GCC emits
uninitialized-variable errors at `-O3`. Warnings are not suppressed. Bootloader,
prebuilt vendor libraries and ROM routines are not changed to O3.
Use the same PSP app, server, HTTP URL and media file for both tests. Run the USB
echo test several times and compare full media-download elapsed time separately.
Playback rate alone is not a maximum-throughput benchmark. Flashing only
`streammaster_onju_v3.bin` at `0x10000` preserves NVS configuration; do not erase
flash or use the factory image for this comparison. Revert with the standard
package's app binary at the same offset.

With PSP debugging enabled, `PSP/SYSTEM/PSPStreamer-recovery.txt` records
`USB throughput` and `ESP RX samples` approximately every 30 seconds while
network diagnostics are running. Counters are cumulative for the app session:
USB exchanges, read payload bytes, elapsed time, exchange time, successful-lock
waiting time, longest exchange and its operation, busy replies and errors.
`KiB_s` is the session-average read payload rate, including idle time; use
differences in `rx_B` and `span_ms` for interval rates. This is not a USB speed limit.
ESP buffer samples include ordinary socket-status replies and one additional
status request per five seconds per reading socket. Empty/full counts are sampled
observations, not time percentages; buffers from all sockets are aggregated.
The USB echo test also records its rate, byte count and elapsed time. These
measurements need no firmware update from 0.2.1. Disabling debugging disables
extra sampling and logs. HTTP and HTTPS can be compared without changing codecs.

See [CHANGELOG.md](CHANGELOG.md) for changes since the first firmware version,
including matching PSP-side changes and work not yet released.

Version **0.2.7** provides an opt-in USB network transport for PSPStreamer, with
six independent TCP/TLS channels. Library browsing, media, subtitles, remote
control and downloads can use Onju's Wi-Fi instead of the PSP's Wi-Fi.
Native PSP Wi-Fi remains the default. This is the first media-transport build:
compilation and host checks pass, but physical playback validation is pending.

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
or measured throughput. With native PSP Wi-Fi selected, the USB LED goes out
when leaving the StreamMaster submenu. With USB transport selected, the
interface stays active for browsing/playback until app exit or disconnection.

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
   driver to ARK's global plugin list. It is loaded by this app only, either
   from this menu or at startup when USB transport is selected.
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

## Enable USB playback (0.2.2)

Version 0.2.2 negotiates compact transfers with the matching app and
`StreamMasterUSB.prx`. Read requests shrink from 4096 to 40 bytes, empty replies
to 32 bytes, while full payload replies remain 4096 bytes. Older peers retain
fixed framing. `compact=1` in the receive-buffer log confirms negotiation.
Memory Stick downloads batch writes into the existing 32 KiB buffer; cancellation
may discard an uncommitted tail, which is fetched again on resume. No additional
large buffer is allocated. Benchmark, playback and file-download rates must be
measured separately; reduced wire traffic is not a measured speed multiplier.

1. Flash firmware **0.2.7** and copy all three matching PSP files listed above.
   Updating only Onju or only the app/PRX is insufficient.
2. Configure Onju and verify **Test saved server** succeeds. The server address
   must be reachable from Onju's network. HTTP is a useful first transport test.
3. In the parent settings menu select **Network transport → StreamMaster USB**,
   save with Start, then exit and restart PSPStreamer. In German the setting is
   **Netzwerkweg**. The saved CFG line is `network_transport=streammaster`.
4. Browse folders, play music, then video with subtitles. While media plays,
   test the web remote, pause/resume, stop, and selecting another file.
5. Test a brief USB interruption and reconnect. The existing recovery flow
   attempts to restore playback; Circle/Start can cancel it. Square retries
   from the browser; L+Square requests a full reconnect, including Onju Wi-Fi.
6. To return to native PSP Wi-Fi, select **PSP Wi-Fi**, save and restart. Or edit
   `ms0:/PSP/SYSTEM/PSPStreamer.cfg` to `network_transport=wifi`.

The transport setting deliberately does not switch live workers mid-session.
Server URL, port and password remain the existing app settings. Onju resolves
the hostname and performs TLS. HTTPS requires a certificate trusted by its root
bundle and a synchronized clock; the PSP's certificate-pinning cache is not
used and self-signed certificates are not silently accepted. The server does
not require changes. LCD/TV decoding and container-PTS synchronization are
unmodified.

## Architecture and current boundaries

- A small **kernel PRX**, using SDK padded USB descriptors, exposes `stm:` I/O
  control calls. No global WLAN hooks and no changes to MPEG import order.
- A shared PSP transport serializes USB requests from independent socket owners
  and the diagnostics worker. DMA buffers remain allocated
  until both USB completion callbacks have returned; they are not recycled after
  a timeout. Retry deactivates/re-enumerates USB. The PRX refuses to take over an
  already active PSP USB application.
- ESP-IDF's USB client task owns transfers; a command worker handles Wi-Fi,
  NVS and diagnostics. Each of six TCP/TLS channels has an independent owner
  for DNS/connect/handshake and socket I/O. Device
  generation numbers reject responses from an earlier cable connection.
- Up-to-4096-byte little-endian messages have bounded payloads, sequence numbers
  and a checksum. These detect corruption; they are not encryption or an
  authentication mechanism. USB is a physically trusted connection.
- WLAN reconnect is bounded to five retries; **Reconnect Wi-Fi** starts a new
  attempt. HTTP reads and USB cancellation have their own bounded waits.
- No passwords are deliberately logged. NVS is not encrypted: physical flash
  access can recover saved credentials. HTTPS validates server certificates,
  does not silently downgrade and does not forward credentials across redirects.
- Each channel has a bounded 64 KiB receive ring and 8 KiB transmit ring in
  PSRAM; a full ring applies backpressure rather than dropping stream bytes.
  TLS allocations use PSRAM too. PSP reads cache up to 4064 bytes per socket,
  avoiding a USB transaction for every HTTP-header byte or small FLV field.
- Readable polling fetches into that cache directly: one USB request/reply per
  data block rather than an extra status request/reply. Empty reads back off
  from 5 to at most 100 ms; data arrival clears the backoff. This reduces idle
  USB traffic without changing cold-subtitle timeouts. Physical throughput
  still depends on the PSP, adapter, Wi-Fi and server and must be measured.
- Firmware uses ESP-IDF's `CONFIG_COMPILER_OPTIMIZATION_PERF=y` (`-O2`).
  Assertions and protocol validation remain enabled. The build-system message
  `USING O3` from mbedTLS alone does not describe the whole firmware's flags.
- Socket commands return promptly; pending reads/writes report busy. The
  kernel driver waits at most 500 ms per USB transfer for these commands;
  slower setup diagnostics retain their separate timeout. This does not shorten
  server-side subtitle preparation deadlines.
- Closing/resetting a channel asks its owner to stop. A slot cannot be reused
  until that owner has destroyed its connection. USB generations invalidate
  stale PSP handles and buffered data after transport failure.
- Cancelled/error connections use zero TCP linger; normal EOF keeps buffered
  response bytes. Stale USB commands cannot leave orphaned connections behind.
  PSP worker errors use a fixed, explicitly released per-thread table rather
  than the C library's shared errno storage.

Validation: ESP32-S3 firmware, PSP driver and PSP app compile; host tests cover
frame corruption/bounds, configuration, six-channel isolation, ring wraparound,
PSP read caching, EOF/error draining, cancellation, stale handles, old-firmware
rejection, native fallback and settings under sanitizers.
The transport test runs 10,000 PSP connection/worker cycles and 4,000 ESP owner
lifecycles with mocked network/TLS I/O, checking cleanup and retry semantics.
Those tests do not emulate USB enumeration, kernel driver timing or physical
adapter behavior. No hardware success is claimed until the above test is run.
