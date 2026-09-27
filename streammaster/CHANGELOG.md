# StreamMaster firmware changelog

Changes to the Onju Voice V3 firmware, starting with the first version.
Related PSP-side changes are explicitly identified. An unreleased entry does
not mean that the feature is available in the current firmware ZIP.

## Unreleased — transport integration in progress

- Work in progress: independent TCP/TLS channels for concurrent media,
  metadata, subtitle and remote-control requests over USB.
- Work in progress: PSP transport selection, cancellation and reconnection.
- These changes are not part of firmware 0.1.2. That version provides setup
  and diagnostics, not USB media playback.

## 0.1.2 — 2026-09-27

- Added an extended network-information command with the current subnet mask,
  SSID and DHCP/DNS configuration flags alongside IP, gateway and DNS addresses.
- Preserved the original status command for older clients.
- Matching PSP app: DHCP-managed fields display `(DHCP)` and the actual leased
  values, or `-` while no applicable lease is available.
- Matching PSP app: DHCP fields are read-only; selecting one refreshes network
  status instead of opening the keyboard. Manually configured DNS stays editable.
- Matching PSP app: live lease values never overwrite saved static settings;
  an unsaved SSID change does not display the previous network's lease.

## PSP bridge fix between 0.1.1 and 0.1.2 — 2026-09-27

- Fixed missing I/O-driver initialization and exit callbacks in
  `StreamMasterUSB.prx`, addressing startup error `80020002`.
- Added PSP bridge startup diagnostics. This was a PSP-side correction, not a
  separate ESP firmware version; it did not require reflashing Onju.

## 0.1.1 — 2026-09-27

- Enabled the six onboard GRB LEDs on GPIO11: dim white power indication,
  Wi-Fi connection/retry/failure states and four signal-strength bars.
- Added a cyan indicator while the PSP USB interface is claimed.
- Added RSSI threshold hysteresis to reduce flicker near signal boundaries.
- Used hardware RMT output, low brightness and a low-priority display task;
  unchanged LED frames are not retransmitted.
- LED initialization/output failures disable the indicators without aborting
  USB or Wi-Fi operation. No PSP app update is required for these indicators.

## 0.1.0 — 2026-09-27

- Initial ESP-IDF firmware for Onju Voice PCB V3: ESP32-S3, 16 MB flash and
  8 MB octal PSRAM; USB host mode with full-speed hub support.
- Added a private PSP USB protocol with fixed 4096-byte frames, sequence
  validation, payload bounds and checksums.
- Separated USB event handling from network/configuration work; connection
  generations prevent stale responses from crossing USB reconnections.
- Added Wi-Fi scanning, connection status, bounded reconnect attempts and
  explicit connect/disconnect commands.
- Added persistent network settings in NVS: SSID/password, DHCP or static
  IPv4, gateway, subnet mask and automatic or manual DNS.
- Added password-preserving configuration updates without returning the
  stored password to the PSP.
- Added HTTP/HTTPS GET diagnostics, trusted-root certificate validation and
  SNTP time synchronization. Media playback still uses the PSP's own Wi-Fi.
- Added USB echo/data-integrity diagnostics and a saved-server health check.
- Added the companion PSP USB kernel driver and settings/diagnostics menu,
  including cancellation and USB reconnect support.
- Disabled the speaker amplifier; retained UART diagnostics while native USB
  operates as host. Audio, microphones and touch controls are not implemented.
- Added build/flash documentation and a firmware package with split update
  binaries, a factory image, checksums and license notices. Split updates
  preserve NVS with the same partition layout; the factory image resets it.

