# 2.5 — System-Wide Optical Audio, Smoother TV Gaming and a Cyberpunk XMB

Changes since **2.4**. Existing PSP/plugin/S/PDIF behavior was accepted on the
user's test hardware; the newly packaged XMB animation and sound still need
their first real-PSP check before publishing this release.

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
  This follow-up still needs a hardware test before publishing.
- Preserve INIs, profiles, credentials, downloaded media and server data volumes.
  Update matching PSP application files together; reboot after plugin changes.
- Generic ESP32 variants remain UNTESTED. No new USB speed claim or forced
  overclocking is part of this release.
