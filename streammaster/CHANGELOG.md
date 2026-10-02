# StreamMaster firmware changelog

## 0.3.17 — Unblock Bluetooth motor output

- Fix the impossible admission condition: a four-slot HCI queue cannot have
  more than four free slots. Allow motor output with at least two free slots,
  retaining headroom for other traffic and the existing one-in-flight limit.
- Add a regression check tied to the queue's actual configured depth.
- Hardware 0.3.16 logs prove nonzero motor values reach the ESP32 but no
  Bluetooth motor write is attempted. This fix addresses that confirmed cause;
  physical vibration remains to be verified. PSP plugin update is not required.

## 0.3.16 — Rumble actuator-mask compatibility and end-to-end diagnostics

- Use SDL's all-actuator mask (0x0f), keeping trigger magnitudes zero. This
  avoids selecting only nonexistent trigger motors on reversed-mask clones.
- Retain sticky evidence for valid/nonzero USB commands, nonzero Bluetooth
  writes and successful host-stack write completion in existing metadata.
- Read the freshness clock under the receive-state lock, avoiding a cross-core
  race that could briefly mistake a just-arrived command for a future timestamp.
- Hardware 0.3.15 logs confirmed capture, 045e:02e0 recognition and repeated
  EP0 replies, but no vibration. 0.3.16 physical output remains to be tested.

## 0.3.15 — POPS Bluetooth rumble output test

- Return actual PS1 small/large motor values in the existing EP0 input exchange
  with a matching PSP Consolizer build. No extra USB host channel or task.
- Recognize the Xbox Bluetooth output-report layout used by the SF30 Pro in
  XInput mode. Validate identity and descriptor; unknown devices stay input-only.
- Coalesce motor states, bound output rate, use finite effects and stop stale
  input. Output errors do not disable the controller's ordinary input path.
- Include backend/VID/PID/output-fault diagnostics in existing PSP metadata.
- Keep QIO80/IRAM, Wi-Fi settings, bonds, button profiles and media transport.
- Builds/targeted host checks passed; physical rumble validation is pending.

## 0.3.14 — Wired USB HID controller test

- Accept a wired HID joystick/gamepad instead of the external Bluetooth dongle.
- Read its report descriptor asynchronously and reuse persistent button/axis/
  Home learning and the PSPStreamer/Consolizer input protocol.
- Reuse existing USB transfers and task; keep Wi-Fi and media transport intact.
- Validate descriptor bounds and release held controls on USB disconnect.
- One controller transport at a time. Rumble remains unimplemented pending a
  verified output protocol. Live hardware tests are still required.

## 0.3.13 — Per-controller PS / Home assignment

- Setup v2 exposes Home as a separate learnable target; store its source in an
  existing reserved profile byte, preserving bonds, Wi-Fi and previous mappings.
- Negotiate Home support with the PSP bridge and encode it in a spare EP0 bit.
  No extra transfers, threads, buffers or transport-frequency changes.
- Existing profiles leave Home unassigned. Update PSPStreamer and Consolizer
  together with the firmware to learn and use the new target.

## 0.3.12 — Isolate optional metadata from controller input

- Only send metadata to PSP bridges explicitly advertising revision 0x0102.
  Consolizer `metadata=0` retains the original 0x0101 input-only traffic.
- Due input/heartbeat packets take priority over name/battery updates.
- A failed metadata transfer disables metadata for that attachment, not gamepad
  input. Existing input-transfer failure handling and media transport are unchanged.
- Companion PSP build snapshots its input timestamp with a current clock under
  the same interrupt guard, avoiding false stale-input releases.
- This is a regression-isolation build, not a hardware-confirmed shutdown fix.

## 0.3.11 — PSP Consolizer status channel (hardware test)

- Push cached controller name, connection state, errors and optional HID Battery
  Strength over bounded EP0 metadata packets; keep the mapped-input wire format
  and four-deep media transfer unchanged.
- Request the actual remote name on connection, including saved devices, and
  update the Bluetooth device list when it becomes available.
- Report battery percentage only for descriptors exposing the standard input
  usage; unsupported controllers remain unknown, not empty.
- QIO80/IRAM, 240 MHz CPU and 80 MHz PSRAM remain unchanged. Flash separate images
  to retain NVS Wi-Fi profiles and pairing; no new throughput claim.

## 0.3.10 — Extended bulk startup hardening

- Hardware follow-up passed: repeated download starts, manual cancellations and
  one full verified 170143443-byte download in 222268 ms (747.5 KiB/s). Promote
  this exact QIO80-IRAM combination with the four-deep PSP app as the standard.
  No independent IRAM speed advantage is established; Bluetooth soak remains open.

- Allocate the extended 8 KiB USB reply terminator space at startup instead of
  growing the DMA buffer during the first full reply; just 64 extra bytes.
- Return the complete firmware version, including QIO80/IRAM suffixes, in the
  existing information response. No network/profile layout changes.
- Companion PSP build posts the whole receive window before sending a group
  and logs per-request completion details on failure. Four-deep transfers and
  the existing timeout remain unchanged. Startup stability test passed.

## 0.3.9-bt-qio80-iram — Optional transport code-placement experiment

- Place bulk read/checksum, ring-copy helper and USB completion callback in IRAM.
- 740 bytes additional IRAM text (768 bytes aligned); no buffer/clock/protocol
  changes. Default remains the tested QIO80 build pending throughput comparison.

## 0.3.9-bt-qio80 — Validated Onju default

- Separate Onju QIO 80 MHz flash build; CPU remains 240 MHz and Octal PSRAM
  remains 80 MHz. No controller, buffering or network behavior changes.
- Build with `bash build-bluetooth.sh qio80`; package with
  `bash package-bluetooth.sh qio80`. These scripts now default to QIO80 after
  successful hardware validation (700.5 KiB/s complete download, +6.2% versus
  QIO40). Flash the supplied component `flash_args` to keep NVS.
- Generic S3 Quad/Octal PSRAM and S2 gain separate QIO80 UNTESTED packages for
  compatible flash/wiring; conservative DIO40 packages remain available.
- Long-duration Bluetooth validation remains open; no CPU/PSRAM overclocking.

## 0.3.9-bt-dio / 0.3.9-bt-qio — Repair controller learning transport

- Fix immediate exit from learning: compact EP0 control events contain mapped
  buttons and X/Y only, not the raw HID inputs or connection session.
- Add a full raw-input snapshot command for a bounded, menu-only learning
  worker (maximum 20 requests/second). Normal playback input is unchanged.
- All 12 PSP buttons plus analog-stick selection remain individually learnable;
  show page numbers for the three-page mapping overview. Controller navigation
  works in the overview; capture uses physical PSP skip/cancel only.
- Update both PSP app and firmware. Existing saved profiles remain intact.

## 0.3.8-bt-dio / 0.3.8-bt-qio — PSP-centric controller learning

- Guided setup asks which controller input should act as each PSP button,
  followed by analog-stick selection using a circle and return to centre.
- Overview now shows PSP targets (`X = HID02`), with individual recapture,
  horizontal/vertical stick inversion, skip/cancel and explicit save.
- Store up to four controller profiles by Bluetooth address in NVS; load on
  connection and reject stale-session saves after a device change. Auto-reconnect
  remains a global option. Skipped steps retain previous/default assignments.
- Parse all six standard HID axes (X/Y/Z/Rx/Ry/Rz) and expose raw input for
  learning inside the existing 32-byte USB snapshot. Kernel bridge unchanged.
- No per-button RPC polling, new background task, USB buffer or timeout change.
- PSP EBOOT plus firmware update required. Builds precede focused tests for
  mapping, hat/axis capture, device storage and the production HID callback.

## 0.3.7-bt-dio / 0.3.7-bt-qio — HID connection event regression

- Treat OPEN/OK/CONNECTING as request acknowledgement, not a completed link.
  ESP-IDF supplies handle 0xff here; adopting it made the 0.3.6 handle guard
  disconnect the subsequent real link. Preserve the pairing deadline until
  OPEN/OK/CONNECTED with a valid handle.
- Retain handle ownership during the intermediate DISCONNECTING notification;
  release it only on final DISCONNECTED. Ignore stale events from other handles.
- User logs confirm internal memory headroom at roughly 38–42 KiB, but every
  apparent controller connection had zero input reports. No further changes
  to socket buffers, HID cache restoration, mappings or auto-reconnect policy.
- Firmware-only update; PSP files from 0.3.6 remain current. DIO/QIO built before
  the focused production-callback test covering complete connection sequences.

## 0.3.6-bt-dio / 0.3.6-bt-qio — Memory headroom, saved HID and button mapping

- Logs show live socket workers / connected Wi-Fi but only 1019 bytes of free
  internal RAM. Move four reserve channels' staging buffers (32 KiB) to PSRAM
  in Bluetooth builds. Keep the primary two channels and USB DMA buffers intact.
- Re-enable ESP-IDF's existing bonded HID descriptor restoration through a
  version-checked build-local replacement; the shared SDK stays untouched.
  Observed error 7 is HID SDP failure, not an authentication error.
- Reconnect known controllers automatically, without clearing bonds or repeated
  inquiry scans; outgoing attempts are spaced by 60 seconds. Explicit disconnect
  suspends reconnect. The behavior is configurable and saved in NVS.
- Add persistent HID button 1–16 assignments, including formerly unused buttons;
  PSP menu shows raw pressed buttons while keeping physical PSP navigation safe.
- Forget the highlighted device with Triangle then X; saved rows are marked `*`
  and remain visible during discovery. No implicit bond deletion on errors.
- PSP EBOOT update required; existing USB bridge remains compatible. Physical
  reconnection and concurrent server/controller stability still need testing.

## 0.3.5-bt-dio / 0.3.5-bt-qio — Concurrent controller/network diagnostics

- User confirmed discovery, pairing and PSP control with 0.3.4, followed by
  stalled server traffic. USB RPCs continued, without a growing PSP socket pool.
- PSP now limits unsuccessful write/connect status probes to one per socket
  per 20 ms, including nonblocking polls. Ready transfers retain their fast path.
- Add optional, bounded network snapshots: Wi-Fi state/reason/RSSI, internal
  free heap, socket task heartbeat, received/sent bytes, would-block counts,
  connection duration, last I/O result and errno. PSP debug logs collect these
  every 30 seconds alongside existing diagnostics; no credentials or payloads.
- Bluetooth menu refreshes its device list asynchronously every two seconds;
  refresh workers are reused/reaped, not left polling after exiting the menu.
- No changes to media timeouts, task priorities, Wi-Fi tuning or hub handling.
  Concurrent operation still requires the physical controller/playback test.

## 0.3.4-bt-dio / 0.3.4-bt-qio — PSP-first startup

- Defer claiming the Bluetooth HCI interface until the PSP streaming interface
  is claimed, avoiding competition for the S3's limited USB host channels during
  the PSP's USB-mode transition. No busy wait or new background poller.
- Expose the waiting phase and PSP interface-claim result in diagnostics.
- User logs already confirm CSR8510 host-ready and successful inquiry startup
  after hotplug. Controller discovery/pairing and cold-start fix remain unverified.

## 0.3.3-bt-dio / 0.3.3-bt-qio — USB adapter diagnostics

- Hub connection confirmed by user; Bluetooth still not active. Existing logs
  showed no claimed adapter, not a controller pairing failure.
- Report a recognized adapter's VID/PID and descriptor/interface claim errors
  even before HCI startup; these failures previously appeared as no adapter.
- Add an optional bounded USB-probe diagnostic command and PSP log output when
  opening/refreshing the Bluetooth menu. No new background polling or media changes.
- CSR8510 descriptors read directly on the PC match the implemented HCI interface
  and endpoints. Exact Onju rejection stage still needs the new hardware log.
- Updated EBOOT required for the detailed log; USB kernel bridge unchanged.

## 0.3.2-bt-dio / 0.3.2-bt-qio — Hub polling correction

- Fix the experimental hub poller injecting unconditional GET_STATUS actions
  into IDF's port state machine, including during reset/enumeration.
- Poll only after all port work is complete, discard unchanged raw status,
  and hand actual change bits back to IDF's normal port handling. This avoids
  duplicate RESET_COMPLETED events and interference with queued reset actions.
- PSP logs confirmed an attach timeout before network setup. Both firmware
  variants build; focused generated-code regression checks pass. Physical hub
  recovery still requires a retest. PSP application/driver remain unchanged.

## 0.3.1-bt-dio / 0.3.1-bt-qio — Experimental USB Bluetooth

- Add host-only Bluedroid with an external CSR8510/Barrot USB HCI adapter;
  standard Classic HID controller discovery, pairing, disconnect and bond deletion.
- Add the PSP StreamMaster controller submenu and bounded report-descriptor parsing.
- Deliver controller snapshots via existing USB EP0, separate from media RPCs;
  release input on disconnect/stale USB snapshots. Physical PSP controls remain active.
- Fit hub + PSP + Bluetooth within eight host channels using a version-checked,
  build-local hub status polling implementation; one hub only, hardware test pending.
- Provide DIO/QIO comparison builds at identical 40 MHz flash / 240 MHz CPU settings.
- Preserve the stable 0.3.0 firmware; no controller compatibility or speed gain
  claimed before hardware tests. See BLUETOOTH.md for wiring and license notices.

## 0.3.0 — Saved Wi-Fi networks

- Add separately packaged **UNTESTED** generic S3 Quad/Octal-PSRAM and S2
  SPI-PSRAM builds, with isolated configs and no Onju LED/amplifier GPIO writes.

- Store five independent WLAN profiles, including passwords and IP/DNS settings.
- Add profile selection, automatic network choice and confirmed deletion in the PSP app.
- Migrate the existing single-network configuration without erasing NVS.
- Prefer the strongest visible saved network/AP on connection setup; retry other
  saved networks after failed association, with no background scanning while connected.
- Keep the simpler 0.2.9 USB loops: the HTTP comparison measured 480.40 KiB/s
  with 0.2.8 optimizations versus 484.19 KiB/s without them (one run each, inconclusive).

## 0.2.9 — restore the pre-0.2.8 USB hot loops

- Restore the exact scalar FNV and ring-copy code from 0.2.7 on ESP and PSP.
  The 0.2.8 download measured 411.6 KiB/s versus 479.5 KiB/s previously;
  the logs do not establish that checksum/copy work caused the slowdown.
- Keep earlier internal buffers, asynchronous storage/logging and SHA work.
  Protocol/configuration remains compatible; no new tuning or buffer changes.

## 0.2.8 — compatible checksum/copy optimization

- Unroll FNV-1a four bytes at a time while preserving exact wire checksums,
  byte order, unaligned input support and protocol version 1.
- Avoid zero-length second ring-buffer copies on contiguous reads/writes.
- Keep internal reply buffers, PSRAM allocation, USB profiles, task scheduling,
  timeouts and recovery unchanged. No measured throughput gain claimed yet.
- Rebuild PSP app and StreamMasterUSB.prx for the matching checksum hot loop;
  old/new firmware and PSP components remain protocol-compatible.

Changes to the Onju Voice V3 firmware, starting with the first version.
Related PSP-side changes are explicitly identified. An unreleased entry does
not mean that the feature is available in the current firmware ZIP.

## 0.2.7 — 2026-09-29

- Restore internal-RAM reply processing for the default 8 KiB/two-request path:
  two fixed 8 KiB workspaces (plus short-packet padding), pointer-only ready/free
  queues and an initially 8 KiB USB DMA transfer buffer. No 32 KiB reply copies
  into/out of a queue, or full reply-buffer clearing on every response.
- Explicit ownership passes worker -> ready queue -> USB owner -> free queue.
  Only actual wire bytes are copied to the separately owned DMA buffer. Detach
  drains queued pointers without resetting the free pool or reclaiming a buffer
  still owned by the worker. Checksums and generation checks remain enabled.
- Larger comparison requests lazily allocate/grow bounded PSRAM scratch storage
  per pool slot and the internal DMA buffer as needed. Scratch storage is reused
  until reboot; subsequent small requests always select the internal workspace.
  Large network receive rings remain in PSRAM, as in the fast 0.2.4 baseline.
- No PSP application update required. Hardware speed/recovery tests pending.

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
