# StreamMaster firmware changelog

Changes to the Onju Voice V3 firmware, starting with the first version.
Related PSP-side changes are explicitly identified. An unreleased entry does
not mean that the feature is available in the current firmware ZIP.

## 0.2.6 — 2026-09-27

- Fix the 0.2.5 USB-startup memory regression: FreeRTOS dynamic queues allocate
  internal RAM, not PSRAM. Explicitly place command/reply storage and the two
  CPU-side reply workspaces in PSRAM, saving approximately 208 KiB internally.
- Keep queue controls, task stacks and actual USB DMA transfers in internal RAM.
  Packet ownership, checksum validation, profiles and detach handling are unchanged.
- Log free/largest internal and DMA blocks at USB initialization and readiness;
  report storage allocation failures explicitly. No PSP app update required.
- PSP logs from the failed run show three `wait USB attach` timeouts before any
  Wi-Fi/server operation. Hardware recovery still needs confirmation after flashing.

## 0.2.5 — 2026-09-27

- Add separately negotiated extended bulk reads: 8/16/32 KiB frames and 1/2/4
  outstanding requests, capped at about 64 KiB per group (32 KiB/four clamps to
  two in the app). Default 8 KiB/two still uses the unchanged legacy bulk opcode.
- Bound the ESP command/reply queues to four entries, use a 32 KiB TX DMA buffer
  and retain sequence/checksum validation, detach generations and cancellation.
- Add boot-cumulative ESP metrics for command queue, worker, ring copy, checksum,
  reply wait, host copy, transfer completion and within-group transfer gaps.
  The matching PSP app logs start/end deltas around downloads when debugging is on.
- Preserve old 8 KiB framing and the old PSP result-buffer ABI. Extended framing
  is enabled only with support from both firmware and kernel bridge. Short replies
  use a terminating short packet even at an 8/16 KiB boundary.
- PSP configuration: `streammaster_bulk_kib=8`, `streammaster_bulk_depth=2`;
  changes take effect after an app restart. These are comparison controls, not
  evidence that larger/deeper groups are faster.
- PSP fixed socket buffers now reserve approximately 1.5 MiB across 12 slots
  (about 1.125 MiB extra); kernel bulk buffers about 144 KiB. No per-packet allocation.
- PSP-only offline SHA-256 batching clears its temporary workspace once per
  batch instead of per block; TLS remains unchanged. Full on-card verification
  and cancellation/resume handling are retained. Hashes checked against Mbed TLS.

## 0.2.4 — 2026-09-27

- Negotiate two outstanding bulk-read requests, each with an 8 KiB USB frame
  (8,160 payload bytes). Up to 16,320 bytes are collected per read-ahead group.
- ESP USB owner receives the next request while the previous response is on the
  bus. Two-entry FIFO queues replace overwrite queues; replies retain their own
  wire size and generation. Detach flushes DMA before buffer/queue reuse.
- PSP bridge queues two independent send/receive pairs, verifies both sequence
  numbers, lengths and checksums, and drains/cancels outstanding DMA safely.
- PSP rotates cache/mailbox ownership instead of copying cached payloads. Bulk
  results go directly from kernel buffers into the owning socket's mailbox;
  compact legacy replies copy only their header and actual payload.
- Larger replies are sealed once on ESP and checked once in the PSP kernel.
  No checksum or cancellation safeguard is removed. Legacy 4 KiB operation is
  retained when either firmware or kernel bridge lacks the negotiated feature.
- Requires firmware 0.2.4 plus matching EBOOT.PBP, PSPStreamer.prx and
  StreamMasterUSB.prx for bulk pairs. `bulk=1` in diagnostics confirms selection.
- Fixed PSP user-space buffers now reserve about 383 KiB across 12 slots, about
  287 KiB more than the previous buffers. Additional kernel DMA buffers use about
  24 KiB. No allocation per packet. Hardware performance is not yet measured.

## 0.2.3 — 2026-09-27

- Matching PSP update adds asynchronous USB read-ahead, overlapping one next
  block with caller processing/storage. This is a PSP-local driver ABI extension:
  firmware wire format is unchanged and no additional ESP flash is required.
  Copy EBOOT.PBP, PSPStreamer.prx and StreamMasterUSB.prx together. Older bridges
  fall back to synchronous reads. `ahead` diagnostics count completed prefetches.

- Increase TCP receive window from 5,760 to 32,768 bytes and receive mailbox
  from 6 to 26 entries. Application rings remain bounded at 64 KiB per channel.
- Set FreeRTOS tick to 1 kHz: the socket owner's mandatory one-tick yield now
  takes 1 ms instead of 10 ms. USB/LED timeouts expressed in milliseconds retain
  their durations. Six independent socket owners and cancellation remain intact.
- Matching PSP app: reduce active empty-read backoff to 1–10 ms (was 5–100 ms),
  retaining up to 100 ms after 250 ms without data to avoid flooding idle channels.
  Poll virtual sockets at 1 ms and log active download progress every five seconds.
- Standard build remains O2. Throughput and audio stability require hardware
  validation; the previous release remains the measured baseline.

## 0.2.2 — 2026-09-27

- Optional separately packaged `0.2.2-o3` compiler comparison; standard stays
  O2. Two SDK source exceptions, unchanged vendor binaries and reproducible
  build/flash instructions are documented in the README. No speedup claimed
  until measured on hardware.

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
