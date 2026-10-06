# 2.4 — Original Xbox Playback, MilkDrop & Monkey, Web Streaming and Dolby Surround

Changes since tag **2.3**, including the optional optical-audio implementation.
Includes Xbox preview **0.7.3**, Home Assistant server app **0.1.79** and
StreamMaster **0.3.18** (optical hardware verification pending).

## Optional Onju V3 optical audio

- Stereo PCM and AC-3/DTS core passthrough through a wired TOSLINK transmitter.
- Explicit PCM or Dolby Digital conversion fallback for incompatible sources.
- Output selection in PSP settings/CFG and web remote; actual ESP DMA progress
  provides the audio clock. The default PSP audio path remains unchanged.
- Initially for network media files, not cached local media or live radio.
  Passthrough volume is controlled by the receiver; compressed audio is not
  analyzed for music visualizations. See `streammaster/SPDIF.md` for wiring,
  setup and limitations. No automated or hardware test runs for this change.

## Original Xbox player

- New native player for the original Xbox, including stock 64 MB systems.
  Reuses the PSP receiver-style interface, VU meters, artwork and shared code
  where practical, without replacing the PSP playback implementation.
- Browse the server library, select audio/subtitle tracks, inspect metadata,
  resume playback and use shared favorites, history, search and queue controls.
- Video, music and internet radio playback, with ICY station/title information.
- MPEG-2 and MPEG-1 video with MP2 audio. Synchronization uses actual stream
  timestamps and the audio DMA sample position, not estimated frame timing.
- Runtime display-mode selection from modes enabled in the Xbox configuration
  and supported by the cable. Independent video profiles: 480×272, 640×360,
  720×480, 720×576, 1280×720 and 1920×1080.
- Widescreen/anamorphic SD handling and corrected 1080i presentation geometry.
  Selecting 1080i output does not require decoding 1080p video.
- Optional Dolby Surround / Pro Logic II matrix downmix and MP2 bitrate settings.
- Video transport menu, pause/resume, seeking, chapter navigation and fullscreen
  progress display. Sequential/shuffled continuation and corrected EOF draining.
- Native text subtitles and bounded PGS bitmap overlays, timed to the displayed
  video. Unsupported tracks retain server burn-in; complex ASS styling can use
  burn-in explicitly. Subtitle workers are cancelled on stop, seek and restart.
- English/German interface with dashboard-language default and saved settings.
- In-app server connection settings, DNS hostnames, series/folder track
  preferences, a session stop timer and illustrated contextual controller help.
- Separate web remote control, including playback, menu buttons and text entry.
  Current playback can be adopted without having started it in that browser tab.
- Shared Spectrum Analyzer options, MilkDrop presets/automatic switching/live
  transitions, and Monkey with its flight Easter egg, combat, ships and scores.
  External Monkey textures are supported; copyrighted original textures are not
  bundled. Monkey's tunnel view extends from 16 to 20 sections.

## Xbox performance and compatibility

- O3/LTO builds and optimized MPEG decoding with NV2A PVIDEO presentation.
- Faster YUY2 packing, SSE1 block copies and matrix transforms, cached texture
  conversion and GPU command batching; no SSE2 requirement.
- Direct GPU visualization presentation, reduced fullscreen/menu redraw work
  and smaller dirty regions for receiver instruments.
- Spectrum decay follows elapsed time rather than redraw count. Removed extra
  post-frame waits that unnecessarily reduced visualization cadence.
- Audio refills between MPEG slices and visualization work batches reduce long
  gaps without changing preset equations or audio/video timestamp semantics.
- Fixed graphics-context, framebuffer ownership, aligned allocation and SDK
  math/parser placeholder failures encountered during hardware testing.
- Larger bounded HD packet handling, clean audio tail completion, monotonic
  DMA progress and recovery from interrupted stream tails.
- Version 0.7.3 adds pre-refill queue minimum, service-gap, starvation and DMA
  halt diagnostics for intermittent syllable repetition. This is diagnostic
  instrumentation, **not a claim that the reported audio stalls are fixed**.

## Server, browser and Home Assistant

- Browser playback target for video, music and radio, independent of PSP/Xbox.
- Browser and Xbox progress/pause/stop reporting to Plex/Jellyfin and shared
  playback history. Browser next/previous and automatic continuation.
- Continue into the next season of the same series, including supported
  numbered local/DLNA folder layouts, instead of always stopping at season end.
- Prepare Jellyfin text subtitles separately for browser/Xbox burn-in to avoid
  redundant scans of the remote original; preserve timing after seek/resume.
- Unified PSP/Xbox web playback panel and separate per-target command/state
  handling. Expose Xbox resolution settings without changing PSP profiles.
- Higher-quality bounded Xbox artwork and metadata/status integration for the
  separate HACS media-player integration.
- Normal server and Home Assistant app include matching server/web changes.
  The server app and HACS integration remain separate installations.

## PSP and StreamerOC

- Optional **Dolby Surround / Dolby Pro Logic II** downmix for PSP streams and
  new offline conversions. Select in PSP settings or the web interface, or set
  `audio_matrix=none`, `dolby` or `dplii` in the PSP configuration.
- Stereo remains the default. PSP MP3 sample rate and timestamp synchronization
  are unchanged. Matrix decoding requires a compatible receiver; this is not
  AC-3/DTS passthrough. Different downmixes have separate offline cache identities.
- StreamerOC accepts experimental **472–500 MHz** requests through INI/title
  rules and plugin settings. Above 471 it uses a separate denominator-18 PLL
  ramp; requesting 500 corresponds to a register-derived **499.5 MHz**.
- Existing targets/defaults are unchanged. The extended range is **not hardware
  validated** and can freeze or shut down a PSP. Automatic PSPStreamer media
  profiles retain their existing ceiling. No user's INI is raised automatically.

## Packaging and known limits

- Canonical component folders/ZIPs and integrity manifests replace duplicated
  release copies. StreamMaster firmware targets share the 0.3.18 source base;
  generic hardware variants remain explicitly untested.
- `PSPStreamerXbox.zip` contains the current 0.7.3 XBE, themes/fonts, licenses,
  shared source snapshot, existing Xbox demo presets and the familiar PSP
  release preset collection. Xbox-safe filenames have an original-name map.
- Preserve existing configuration, scores, custom presets and external textures
  when updating. Install server app 0.1.79 for optical output and PSP downmix.
- Xbox is still a preview: offline downloads/local playback and HTTPS are not
  complete; AC-3/DTS passthrough remains deferred. No Xbox StreamMaster/Bluetooth
  relay implementation is claimed. HLSL shaders are not implemented by this port.
- HD decoding speed depends on scene complexity and CPU speed. Successful
  playback on the development Xbox is not a guarantee of full-speed 1080p on a
  stock Xbox. Native subtitle paths and the latest audio diagnostics still need
  further console testing.
