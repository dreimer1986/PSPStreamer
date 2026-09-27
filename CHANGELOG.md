# Changelog

## Additional unreleased changes

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
