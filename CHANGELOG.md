# Changelog

## Additional unreleased changes

- PSP Consolizer: opt-in read-only system-audio probe (`audio_probe=1`, also
  requiring `report=1`). Capture the running audio driver's text/signature
  evidence and normal/SRC queue activity per title. No audio hooks or game
  S/PDIF output yet; a hardware trace is required before the capture stage.

- StreamMaster 0.3.23: retain the bounded optical DMA/ring resources across
  media changes to avoid repeated ESP_ERR_NO_MEM startup failures. Stop still
  disables output; reopen resets sessions, queued data and clock configuration.
- PSP / server 0.1.81: consistently label the circle button O in StreamMaster
  menus and web keyboard help. Optical playback confirmed by the user;
  server-triggered next-track regression fix awaits hardware verification.

- PSP / StreamMaster 0.3.22 / server 0.1.80: batch optical PCM into 20 ms
  packets, increase PSRAM-only buffering, reduce interrupt-lock work and
  status polling. Show actual server audio output in video controls/music;
  retain the established timestamp sync and standard PSP audio path.

- StreamMaster 0.3.21: reduce optical audio's internal DMA buffering from
  18 KiB to 6 KiB to address the logged S/PDIF startup allocation failure.
  Preserve channel-status continuity and all network/USB buffer sizes.

- StreamMaster 0.3.20 / PSP: detailed S/PDIF startup diagnostics, bounded
  BUSY retry on audio open, and PSRAM-backed temporary silence preparation.

- StreamMaster 0.3.19: postpone automatic Bluetooth reconnection to sleeping
  controllers during substantial TCP/TLS transfers and a 30-second quiet grace
  period. Manual connections and incoming reconnections remain available.
  Firmware-only diagnostic fix; controller-standby playback test pending.

- PSP / server 0.1.79 / StreamMaster 0.3.18: opt-in Onju V3 TOSLINK audio,
  stereo PCM, AC-3/DTS core passthrough and explicit conversion fallback.
  Audio output selection in PSP settings/CFG and web remote; a shared media
  timeline and ESP DMA completion reports drive optical A/V synchronization.
  Existing PSP DAC, Xbox, browser and offline formats stay unchanged.
  Network media files only initially; hardware verification is pending.

- Xbox 0.7.3: record audio service gaps, minimum pre-refill queue depth,
  starvation and DMA halt events to investigate intermittent syllable repeats.
  No audio timing, decoding or synchronization changes in this diagnostic build.

- StreamerOC: experimental 472–500 MHz target range in INI/title rules and
  plugin settings, with an 18-denominator PLL ramp and restoration path.
  500 requests approximately 499.5 MHz; hardware stability is untested.
  Existing targets and defaults remain unchanged.

- PSP / server 0.1.78: optional Dolby Surround and Pro Logic II matrix downmix
  for streaming and offline conversion, selectable in the PSP settings and web
  remote. Stereo remains the default; audio rate and timestamp synchronization
  are unchanged. Home Assistant app includes the same server and web changes.

- Xbox 0.6.5: fix MilkDrop preset-load assertion in the SDK's unimplemented
  strtof. Reuse musl's floating-point conversion through a string adapter;
  cover exponents, end pointers, range errors and preset loading in focused tests.

- Xbox 0.6.4: fix 0.6.3's virtual/physical framebuffer-pointer assertion on
  stopping effects; stop audio DMA before GPU/network cleanup. Add a shared
  row-cached opaque presentation scaler for Monkey, MilkDrop and Spectrum,
  retaining the existing render resolution and preset algorithms.

- Xbox 0.6.3: remove hidden full-menu rendering under fullscreen music, limit
  effect texture transfers to the visible rectangle, explicitly return HAL
  scanout after GPU teardown and log visualization stop stages. Serial inspection
  of the reported black screen showed completed playback cleanup and an active
  SDL render loop; console recovery/performance still need verification.

- Xbox 0.6.2: correct NV2A color/depth DMA contexts for custom offscreen buffers;
  size depth storage and pitch for the widest composition target. Fix the
  reported zeta-buffer limit error; add target-switch command regression checks.

- Xbox 0.6.1: implement missing SDK `exp2*` / `expm1*` math functions used by
  Monkey and audio smoothing. Fix the photographed startup assertion without
  disabling assertions or changing shared formulas. Add numerical regression
  checks and a link-map audit for unimplemented SDK math objects.

- Xbox 0.6.0 test build: reuse the PSP Monkey/MilkDrop engines through an NV2A
  adapter, including presets, live transitions, persistent options, flight,
  combat, scores and native controller rumble. Bounded offscreen targets avoid
  full-HD feedback buffers. Console rendering/performance need verification;
  PSP source and playback timing remain unchanged.

- Xbox 0.5.0 / server 0.1.76: dashboard-default EN/DE UI; shared favorites,
  cross-provider search, editable playlist with repeat/shuffle; MP2 bitrate
  settings and higher-resolution artwork. Unify Xbox/PSP web playback controls
  without mixing their mailboxes. Separate HACS 0.2.0 Xbox entity with artwork,
  position and controls; retain the existing PSP entity identity. PSP playback
  and timing are unchanged. Local subtitle rendering remains deferred.

- Xbox 0.4.6 / server 0.1.75: web video-resolution selection for all six sizes,
  optional client default, browser persistence and music/PSP isolation. Prevent
  audio completion counters/sample clock from regressing after a DMA stall.
  Episode autoplay is now confirmed; full-HD decode throughput remains limited.

- Xbox 0.4.5 / server 0.1.74: allow bounded 1 MiB MPEG packets for complex
  1080p I pictures (formerly 256 KiB); preserve original stream errors. Docker
  and HA use the same transport. End-of-track audio uses a silent DMA completion
  marker, preserving real sample PTS and clean-EOF-only autoplay.
- Xbox 0.4.2–0.4.4: NV2A YUY2 presentation with repaired startup ownership,
  two-row MMX packing, partial GUI copies and bounded text caching; substantial
  speedup confirmed on console. Correct 1080i field geometry, also confirmed.
  PSP rendering and synchronization remain unchanged.

- Xbox 0.4.1: fix runtime output changes returning to the dashboard. Supply
  the missing SDL Xbox window-destruction callback, retaining the pinned
  upstream backend, and log each output transition/recovery stage. Build first;
  a focused test reproduces the upstream failure and checks five recreations.

- Xbox 0.4.0 / server 0.1.73: libmpeg2 with Pentium III MMX/MMXEXT, real
  picture PTS and explicit final-frame drain for clean autoplay. MPEG-2 is
  default; MPEG-1 remains selectable. Six encoding sizes through 1080p,
  anamorphic SD, 16:9/4:3 TV shape, live cable-compatible output switching
  with confirmation/rollback, optional Dolby Surround/Pro Logic II downmix.
  Build with -O3/LTO; do not change console clocks, EEPROM or PSP playback.
  HD performance and receiver matrix decoding still require hardware tests.

- Xbox 0.3.2 / server 0.1.72: reuse the PSP FFT analyzer and spectrum painter
  unchanged, including band counts, dB gain, palettes, whole LEDs and peak hold.
  Add in-app server editing/testing with masked password and recovery backup,
  debug toggle, next-episode countdown and fullscreen progress bar. Make the
  web target selector global and connect Xbox to the main remote/status view.

- Xbox 0.3.1 / server 0.1.71: on-screen transport/chapter menu, persistent
  spectrum settings, separate Xbox web remote with expiring commands and
  current playback status. Fix seek/log number formatting; increase DMA audio
  reserve and move routine log writes out of the playback thread. Console
  verification of the reported rare audio stalls remains pending.

- Xbox 0.3.0 / server 0.1.70: readable track languages, persisted playback
  preferences, source resume, next/previous, chapters, same-title reconnect,
  compact help/info, bounded provider artwork, shared favorites/history rows,
  radio playback and a 24-band music spectrum. Native 0.2.5 video/audio is
  confirmed on hardware; the new feature set awaits its combined console test.
- Browser and Xbox report progress/pause/stop to Plex/Jellyfin and shared
  history without consuming or replacing PSP remote commands. Separate client
  keys and ordered reports protect concurrent clients and late unload messages.
- Browser automatically advances at clean EOF (optional), with language-based
  track carry-over. Sequential video can cross seasons of the same series on
  all clients: provider season metadata, or numbered files/DLNA season folders.
  Explicit provider playlists, folder shuffle and music album boundaries stay
  unchanged. Browser season transition was verified in Chromium.
- Xbox preview 0.2.5: avoid nxdk's assertion-only `strtod` stub when reading
  media duration; support decimal/exponent durations and report SDK assertions
  over serial as well as on screen and in the log.
- Xbox preview 0.2.4: fix the serial-debugger-confirmed startup access violation
  in nxdk PDCLib's suppressed string scan; parse HTTP status lines explicitly.
  Add optional Linux serial KD diagnostics and build-specific symbol maps.
  PSP playback and server transcoding are unchanged.
- Xbox preview 0.2.3: initialize controllers before GUI/network workers,
  deduplicate connection events, restore Back-to-dashboard during loading
  and playback, and capture SDK assertions. Hardware startup retest pending.
- Xbox preview 0.2.2: expose catalog loading before worker startup and add
  stage diagnostics for the post-network-init stall; live catalog/parser
  check passes on host, console diagnosis remains pending.
- Fix Xbox black startup: explicitly enqueue viewport before clip changes in
  the pinned SDL backend after temporary text textures are freed. Initialize
  audio only on playback; log startup stages and rendering assertions.
- Server 0.1.69: prepare Jellyfin text subtitles via its API for browser/Xbox
  burn-in, preserving ASS styles; restore source subtitle time when seeking.
  Aharen-san S01E01 with German ASS starts in the browser test in ~2.3 s.
  PSP encoding and subtitle overlay delivery remain unchanged.
- Extend the confirmed Xbox connection preview into a player test build:
  receiver theme, analog VU meters, library, track/subtitle selection, music,
  video, pause/resume, seek and fullscreen. Separate MPEG-1/MP2 output carries
  real PTS; video follows the AC97 sample cursor. Server 0.1.68 required;
  console playback verification pending. PSP paths unchanged.
- Add a separate browser playback target with fragmented H.264/AAC video,
  MP3 audio/radio, selected tracks/subtitles, seek and pause/resume; isolate
  browser controls and encoder cleanup from PSP status/commands. Same code
  in Docker and Home Assistant server 0.1.67.
- Add a buildable native nxdk original Xbox connection preview (controller,
  RAM report, authenticated server health/library diagnostics), plus porting
  requirements. Connection preview confirmed on hardware; playback added above.

- Consolidate release packaging: one current folder/ZIP per app or plugin,
  one StreamMaster package with current 0.3.17 Onju/S3/S2 hardware variants,
  verified embedded versions and file hashes. Archive historical test copies
  outside the published release; preserve generic UNTESTED labels.
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
