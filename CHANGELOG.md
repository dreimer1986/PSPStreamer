# Changelog

## Additional unreleased changes

- Add optional damage-scaled game rumble with a configurable full-power
  threshold (120 HP default), motor cap and pulse range (60–500 ms default).
  Stronger hits can upgrade running pulses without queued effects; preserve
  fixed-mode profiles and use integer-only float-health decoding in the plugin.

- Add optional PSP game hit rumble: read-only health profiles by title ID/path,
  edited in PSPStreamer, with integer/float types, optional pointer/battle flag,
  range checks, finite pulses and cooldown. Soul Calibur ULES01298 confirmed
  by the user. Add guided CWCheat-line / PPSSPP RetroArch-result import, with
  explicit type/full-health confirmation and disabled drafts; never run cheats.
  See `psp-controller/RUMBLE.md` for setup and import limitations.
- Add Monkey hull collision damage (player 30%, enemy 60%) and independently
  adjustable native bass/beat and impact rumble, both off by default. Reuse
  the existing StreamMaster motor channel without extra network polling.

- Rename all Consolizer plugin files, configuration paths and ARK entries to
  PSPConsolizer; keep the unrelated StreamMaster app transport name unchanged.
- Add per-controller PS/Home learning with firmware 0.3.13, preserving existing
  bonds and mapping storage. Update English/German help for StreamMaster,
  Wi-Fi profiles, Bluetooth learning, Home and VSH USB hand-off.

- Add explicit VSH USB release/resume (physical NOTE + Volume Down, 2 s) so
  Consolizer can yield the port for PC memory-stick access.
- Add controller Start + Select (1 s) as a bounded, system-only PS/HOME pulse;
  `home_combo=0` disables the shortcut. No ESP firmware update required.

- Recheck VSH USB activation after startup instead of trusting a stale started
  flag; log state transitions and preserve the previous plugin launch's logs.

- Reuse an already-running idle USB bus in VSH without stopping another
  component's bus on shutdown. Keep active USB functions protected.
- Add Consolizer `report=0` to disable both loader and runtime diagnostic writes.

- Pack the two-color overlay history in both StreamerOC and PSP Consolizer
  into one bit per pixel, preserving original framebuffer pixels losslessly.
  Save about 107 KiB in StreamerOC and 72 KiB for Consolizer's three backups.
  Re-enable optional Consolizer features for hardware testing after the
  overlay-off Soul Calibur regression test passed.

- Remove unconditional Consolizer overlay backup allocation from kernel BSS;
  disabled overlays now allocate no pixel backups, polling uses one backup.
  Add kernel-memory diagnostics for the ongoing game/network regression test.

- Add PSP Consolizer: PSP-wide controller
  input, optional VSH/POPS, connection/name/battery diagnostics, cooperative OC
  presentation overlay and optional cable-gated Sony TV activation. New modes
  require hardware testing; the initial Soul Calibur input test passed.

- Consolidate the validated StreamMaster release: Onju firmware
  0.3.10-bt-qio80-iram with QIO80, CPU 240 MHz and PSRAM 80 MHz; 8 KiB/four
  outstanding requests are now the PSP default, with legacy negotiation intact.
- Fix bulk startup hazards through preallocated terminator space and a fully
  posted receive window. Confirm repeated starts, manual cancellation and a
  complete verified download at 747.5 KiB/s (two-request baseline: 700.5 KiB/s).
- Display/log the full firmware version and preserve per-request USB failure
  details. Keep checksum verification and all existing timeout bounds.
- Remove the temporary 8x4 build switch; archive superseded test/fallback
  packages outside the release folder. No additional IRAM speedup is claimed.

- Add an opt-in server-side next-N unwatched episode reserve, persistent language
  preferences and a bounded, explicitly owned cache; preserve manual downloads.
- Add the optional Plex account Watchlist with exact local GUID mapping, keeping
  it separate from the episode playlist. Server/Add-on version: 0.1.66.

- Monkey flight: alternate drone/turret spawns with higher fallback positions
  for drones; retain one pending spawn without rerolling the requested type.
- Add the Astro Shield pickup with a green honeycomb glow: fly through for
  +66 shield points, capped at 100. Embed its 42-triangle mesh and small texture.
- Reuse the player's particle explosion for enemy kills.
- Add an optional model-2 autopilot ship to normal Monkey visualization, without
  activating gameplay or changing the camera. Disabled by default.

- Add web Play next, compact Plex/Jellyfin shelves on PSP, persistent per-series
  language/subtitle preferences and an optional cancellable next-video countdown.
- Add floor-hovering waypoint drones using non-player ships and inverted colors,
  retaining bounded combat, firing rate, damage and scoring rules.
- Confirm previous recovery, StreamMaster profile and flight tests; generic
  ESP32-S2/S3 images remain untested. New conveniences await PSP feedback.

- Prevent network-recovery failures from advancing the playlist: track clean
  music EOF separately from transport failures, reject premature video ends,
  and recheck reader errors after queue waits and worker teardown. Recovery
  clears stale end-of-media state; successful natural completion still advances.

- Flight: enemies destroyed after exactly three shots; enemy fire interval
  500 ms with player damage unchanged at 10. Disable Square visualization
  switching throughout the Easter egg and use the prepared turret model.
- StreamMaster 0.2.9 restores pre-0.2.8 checksum/copy code after a slower hardware
  run; earlier transport, storage and SHA improvements remain intact.
- Monkey: retain at most one missed enemy spawn, retry once per second with
  hull-sized clearance on uneven terrain; show selected ship in the flight menu.
- StreamMaster 0.2.8: exact-compatible unrolled FNV checksums and skip empty
  ring-wrap copies; retain current buffering and scheduling defaults.
- Monkey flight: seven selectable ships, bounded stationary enemy encounters,
  yellow blaster projectiles (player 500 ms, enemy 750 ms), 10-point projectile
  damage, barrel-roll protection and 100-point kill rewards. Import all supplied
  GLBs with CC-BY credits; prepare but do not spawn the turret model.
- Add an isolated VAAPI Main/CABAC test-package generator with a software control,
  shared production FLV/MP3 timing, header checks and PSP offline manifests. No
  production encoder changes; Intel encoding and PSP validation pending.
- Offline SHA-256: use a 16-word rolling schedule with grouped rounds to reduce
  schedule storage and loop overhead. Reference digest/state tests pass; PSP
  verification-speed comparison pending. Transport and TLS remain unchanged.
- PSP offline downloads: move periodic diagnostic card writes to a bounded,
  dedicated writer so reception can continue. Join and drain on exit; retain
  double-buffered media writes and full read-back verification. Add separate
  write-submission, reporting and log-I/O timings; StreamMaster firmware unchanged.
- StreamMaster 0.2.7: internal 8 KiB reply pool and pointer queues remove repeated
  full-buffer PSRAM copies/clears. Larger comparison buffers are allocated on
  demand; default USB DMA starts at 8 KiB. PSP app and hash optimization unchanged.
- StreamMaster 0.2.6 fixes USB-startup RAM pressure introduced in 0.2.5: bulk
  queue storage/workspaces explicitly use PSRAM; DMA and queue controls stay internal.
- StreamMaster 0.2.5: negotiated 8/16/32 KiB read profiles with 1/2/4 requests
  (maximum approximately 64 KiB per group), retaining 8 KiB/two by default and
  legacy fallback. Add ESP queue, checksum, copy and USB transfer timing.
- Batch offline SHA-256 compression with one work-buffer wipe per batch; preserve
  full Memory Stick read-back, the expected digest and the library's TLS path.
  Performance remains subject to on-device comparison.
- Batch recovery logs during offline transfers, overlap reception with bounded
  asynchronous card writes and prefetch verification reads while hashing. Full
  on-card SHA-256 verification and committed-prefix resume remain enabled.
- Add opt-in download bottleneck timings for receive, storage, verification and
  per-socket StreamMaster read-ahead/ESP occupancy without extra USB queries.
- Report StreamMaster firmware capability bits and kernel driver probe results
  to distinguish legacy firmware from driver negotiation failures.
- StreamMaster 0.2.4: two outstanding 8 KiB bulk replies, negotiated legacy
  fallback, FIFO host scheduling and reduced payload copies through buffer swaps.
- Pipeline StreamMaster bulk reads through asynchronous PSP USB begin/finish
  operations. One speculative block per socket, one USB transaction in flight,
  generation-checked delivery, and synchronous fallback for older bridge drivers.
  ESP firmware 0.2.3 remains compatible; update the PSP USB PRX with the app.
- StreamMaster 0.2.3: larger TCP receive window/mailbox and finer socket-worker
  scheduling; PSP uses faster active read retries with retained idle backoff.
  Add five-second file-download progress diagnostics for throughput comparison.
- Reuse matching queued, running and intact completed offline conversions instead
  of creating duplicates; distinguish server queue waiting from conversion on PSP.
- Downloads now share playback's nonblocking connection/send/receive handling,
  including native WLAN would-block retries. Debug logs record download phases,
  HTTP status, transport results, byte counts, resume offsets and elapsed time.
  Playback behavior, download idle deadlines and resumable storage are unchanged.
- LED spectrum uses whole, single-color segments and grid-aligned peak markers;
  configure 8..32 vertical LEDs per bar (default 20) without changing FFT bands.
- Spectrum color presets (Original, Rainbow, Classic VU, Ice, Fire), optional
  LED segments and time-based peak hold; persistent settings for LCD/TV,
  windowed/fullscreen, with incremental rendering and unchanged audio analysis.
- Normalize FFT analyzer height to a fixed -60..0 dBFS range; adjustable
  display-only gain (-24..+24 dB), without changing MilkDrop values or volume.
- Desktop FFT mode now gives Monkey its reference input filtering, frequency
  groups, history and adaptive beat detector; retain the light-mode detector.
- Optional desktop MilkDrop music analysis; retain the light 12-band default.
- Select 12/24/32/48/64 FFT analyzer bars independently for LCD and TV, windowed or fullscreen,
  with persistent in-app settings and no restart required.

## Unreleased — since 1.8

Summary of 36 commits after tag `1.8`, through `9011a1d` (server/add-on 0.1.61).

### Playback and reliability

- Fixed network-worker resource leaks and socket cleanup, improving long-session
  stability and folder browsing responsiveness.
- Added cancellable stream recovery, WLAN reassociation and clean restarts for
  recoverable AVC errors; bounded stalled DNS and subtitle preparation.
- Reduced remote-control polling overhead and corrected short button presses.
- Improved startup feedback, persistent diagnostic history and server-side
  transport diagnostics; fixed START-to-exit in local storage.

### Library, playlists and remote control

- Added DLNA sources and cover artwork for the web, PSP and Home Assistant.
- Added Plex/Jellyfin alternative media versions and external subtitle support.
- Added provider-wide Continue Watching, recently added, unwatched and collection
  views, with list/cover switching; Jellyfin also includes Next Up.
- Added a persistent shared music/video playlist with per-entry track choices,
  reorder/remove controls, repeat off/one/all and independent playlist shuffle.
- Added favorites, recent history, resume positions, server profiles, sleep/file
  limits and local-download inventory/free-space checks on the PSP.
- Added shared web favorites/history/resume and cross-source media search.
- Added a virtual PSP controller and remote text entry in the web interface.
- Restored current playback controls across browser sessions; added adjacent-file
  detail navigation without interrupting playback.
- Added provider-supported intro/credits controls and chapter navigation.
- Preserved media-server settings across container updates and kept Docker/HA
  server features aligned.

### Visuals and flight mode

- Added live shaderless MilkDrop transitions between two running presets, with
  a snapshot fallback when extra state cannot be allocated.
- Improved desktop formula compatibility for Hexcollie and Clouded Bottle presets.
- Reduced formula dispatch, renderer state copying, geometry submission and
  transition overhead without intentionally simplifying preset behavior.
- Refined ship collision clearance; added wall recoil, blinking recovery,
  double-L/R barrel rolls and gradual ship materialization.
- Added four music-reactive exhaust lights with dynamic brightness and color.
- Corrected Monkey title rendering, fitted LCD/TV screen boundaries and improved
  translated text layout.
- Added an optional always-visible StreamerOC overlay (`overlay_always=1`).

### Upgrade notes

- Update the server and both PSP files (`EBOOT.PBP` and `PSPStreamer.prx`) together
  when upgrading from 1.8. Keep the existing persistent server volume and PSP state.
- The latest 0.1.61 cover-view update is web-only; the 0.1.60 PSP build remains current.
- Newly delivered features have been confirmed working by the user. Enemies,
  wall damage and a health bar remain deferred and are not part of this release.
