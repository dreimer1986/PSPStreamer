# StreamMaster firmware changelog

Changes to the Onju Voice V3 firmware, starting with the first version.
Related PSP-side changes are explicitly identified. An unreleased entry does
not mean that the feature is available in the current firmware ZIP.

## 0.2.2 — 2026-09-27

- Negotiate compact USB framing: a socket read request now occupies 40 bytes
  rather than 4096; empty replies use 32 bytes. Full data replies remain 4096.
  Short-packet termination handles 64-byte boundaries explicitly. Packet length,
  checksum, sequence, ownership and cancellation validation remain enabled.
- Fall back to fixed framing with 0.2.1 firmware or an older PSP kernel bridge.
  Matching new app, bridge and ESP firmware are required for compact framing.
- Batch Memory Stick download writes into the existing 32 KiB buffer. Resume
  uses committed file length; cancelled uncommitted tails are downloaded again.
- Add transport measurements and log the selected framing (`compact=1`).
- Firmware stays at ESP-IDF performance `-O2`. Real throughput and an `-O3`
  comparison remain hardware-test follow-ups, not claimed performance gains.

## PSP client follow-up — 2026-09-27

- Add opt-in transport timing, read-throughput and sampled ESP receive-buffer
  diagnostics, plus persisted USB benchmark results. Firmware remains 0.2.1.

- Route library hostname resolution through StreamMaster instead of attempting
  native PSP DNS without a PSP WLAN connection. Firmware 0.2.1 is unchanged.
- Reload saved ESP configuration and current DHCP information automatically
  when reopening StreamMaster settings; leaving the menu only clears the local
  configuration draft, not the ESP's saved settings.

## 0.2.1 — 2026-09-27

- Enabled ESP-IDF's supported performance build (`-O2`), replacing the debug
  optimization level (`-Og`) actually used by the firmware sources.
- Matching PSP app: readable polling now fetches the next data block directly,
  removing a separate full-size USB status exchange before each block.
- Added adaptive empty-read backoff (5–100 ms) to avoid flooding USB while a
  server prepares a response. Successful reads clear the backoff immediately;
  subtitle/server response deadlines are unchanged.
- Matching PSP app: a temporarily full channel pool is retried within the
  caller's connection deadline instead of immediately failing the request.
- Matching PSP app: isolated socket errors per worker, independent of the PSP
  C library's shared errno; release these fixed error slots when workers exit,
  including failed audio socket allocation.
- Failed audio socket allocation now also releases native PSP network-thread
  bookkeeping; cleanup no longer depends on obtaining a valid descriptor.
- Reject old queued USB work after disconnect and release connections created
  by work that completed after its USB session ended.
- Retain USB device/interface handles until release succeeds; preserve quick
  reattach notifications received during old-device cleanup.
- Abort cancelled/failed TCP connections with zero linger so dead peers cannot
  retain send queues and TCP control blocks. Clean EOF still drains normally.
- Focused sanitizer tests include 10,000 PSP connection/worker cycles, 4,000 ESP
  owner runs (EOF, cancellation, handshake failure and read failure), partial
  TLS-write retries, empty-poll traffic and stale USB work. These are host tests
  with mocked USB/TLS I/O, not a measured hardware-throughput claim.

## 0.2.0 — 2026-09-27

- Added six independent TCP/TLS channels for concurrent media, metadata,
  subtitle, download and remote-control requests over USB.
- Moved connection setup, DNS and TLS handshakes into independent channel
  workers. Slow responses do not block the USB command handler or other channels.
- Added bounded per-channel PSRAM buffers, backpressure, clean EOF draining
  and owner-only socket/TLS cleanup. TLS allocations also use PSRAM.
- Added channel cancellation/reset and generation checks to reject stale
  connections after USB interruption or reconnection.
- Matching PSP app: selectable `wifi` or `streammaster` network transport in
  settings and CFG; changes take effect after saving and restarting the app.
- Matching PSP app: shared USB access for diagnostics and playback, buffered
  reads, short socket-command USB waits and integration with playback recovery.
- Native PSP Wi-Fi remains the default; USB does not install global network
  hooks. Decoder and audio/video timing are unchanged.
- Matching firmware, PSP app and USB driver are required. Firmware 0.1.2 does
  not support this media transport. Hardware playback validation is pending.

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
