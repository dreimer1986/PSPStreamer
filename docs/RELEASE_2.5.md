# 2.5 — System-Wide Optical Audio, Smoother TV Gaming and a Cyberpunk XMB

Changes since **2.4**. PSP/plugin/S/PDIF behavior, XMB animations and the optional
Memory Stick FTP service were accepted on the user's Onju/PSP test setup.
Generic ESP32 firmware variants remain explicitly hardware-UNTESTED.

## Wireless Memory Stick file transfers

- New **PSPStreamFTP 0.4** package: upload, download, browse and manage Memory
  Stick files through StreamMaster Wi-Fi while in VSH. No flash access.
- Own username/password in an INI, optional read-only mode, staged uploads and
  download resume. Disabled by default; plain FTP for trusted LAN use only.
- Correct PSP file sizes and root navigation; configure FileZilla for one
  simultaneous connection per site.
- **StreamMaster 0.3.26** includes the inactive-until-requested FTP transport
  across all four distributed firmware variants. Existing BT/S/PDIF retained.

## Optical audio beyond the player

- PSP Consolizer can mirror stereo PCM from PSP games, VSH and PS1/POPS to the
  optional StreamMaster/Onju optical output. PSPStreamer retains its separate
  AC-3/DTS passthrough and conversion path; system mirroring is not passthrough.
- Added verified native SRC/Output2 capture for titles that change audio paths,
  including GTA VCS, and a guarded POPS capture path.
- Reduced copying and allocation pressure, improved USB batching and audio-worker
  scheduling, and corrected watchdog handling after silent intervals.
- Deferred VSH overlay setup to leave startup audio alone. Kept the established
  loader delay; complete playback of the boot sound is not guaranteed.
- Corrected optical stream reuse and next-media preparation. MP3 bitrate controls
  are hidden when not used by the selected optical playback mode.

## Games, controllers and TV

- Overlay pixel backups now use on-demand user RAM instead of scarce kernel
  RAM, addressing AVCODEC module allocation failure 800200D9 with both plugins
  enabled. Controller/clock services remain available if an OSD cannot allocate
  its backup. This memory-layout correction was confirmed on the user's PSP.

- Multiple named health-based rumble profiles per title, supporting compilations
  and separate monitored values. Added confirmed EU profile examples for Mortal
  Kombat and Street Fighter; no universal address compatibility is implied.
- In-app management for global optical PCM settings and rumble configuration.
- FuSa Fullscreen refreshes persistent framebuffers at its output cadence rather
  than the old 100 ms fallback limit, improving affected GTA cutscenes.
- Confirmed Consolizer overlays operate without StreamerOC. StreamerOC remains
  GAME-only. GTA VCS passed cold starts at 333 MHz with Enforce; 366 MHz did not.

## A new first impression

- Cyberpunk XMB icon and static backdrop, a moving film ribbon, city display and
  detailed spaceship flyby with an original futuristic stereo sound ident.
- Native ICON1.PMF and SND0.AT3 embedded directly in the EBOOT. No plugin needed;
  executable and application metadata are unchanged by the asset repack.
- Short, feature-focused README with separate setup, build and historical docs.

## Not included / update notes

- Optional animated full-screen XMB background in Consolizer, default enabled
  with `xmb_animation=1` and the separate `xmb-background.xba` asset. The guarded
  6.61 renderer keeps icons/text above the animation and native PIC1 as fallback.
  First hardware animation confirmed; follow-up doubles the asset to 12 fps
  and protects the automatic TV-button hold against background-I/O stalls.
  The smoother animation is user-confirmed. Per-background on-demand banks
  and a packaged video/EBOOT-to-XBA converter now allow other supported apps
  to use their own animations; multi-app loading/playback is user-confirmed.
  A further cold-start TV fix starts the long-press deadline after blocking log
  writes rather than before them; hardware retest pending.
- Preserve INIs, profiles, credentials, downloaded media and server data volumes.
  Update matching PSP application files together; reboot after plugin changes.
- Generic ESP32 variants remain UNTESTED. No new USB speed claim or forced
  overclocking is part of this release.
