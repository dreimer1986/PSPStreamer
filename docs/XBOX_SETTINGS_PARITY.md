# Xbox settings audit — 0.5.0

Compared against `psp-client/app_settings.h`, `visual_options.h` and the
Xbox `settings.h`, `net.h` and playback UI. This is an implementation audit,
not a claim that every new setting has passed a console test.

## Present

- Saved NV2A hardware-overlay / software video renderer choice; automatic
  software fallback on allocation or persistent GPU-buffer availability failure.
  Hardware presentation confirmed; this is not MPEG hardware decode.
- Saved volume, autoplay next, repeat current, music folder shuffle.
- Preferred audio/subtitle language, per-file track selection and provider
  series preferences; source resume and chapter navigation.
- Six video sizes (480x272 through 1920x1080), MPEG-1/2, stereo/Dolby
  Surround/Pro Logic II downmix. Independent of physical output mode.
- Runtime output selector with cable/region/EEPROM-compatible modes,
  confirmation/15-second rollback, saved selection and 16:9/4:3 TV shape.
  nxdk does not expose native 576-line output; 720x576 is a video profile only.
- Spectrum enabled/disabled; music X opens the shared PSP analyzer/painter
  options: legacy/desktop FFT, 12/24/32/48/64 bands, dB gain, all five palettes,
  separate LED toggle, 8–32 LED segments and peak hold. PSP implementation
  headers are compiled unchanged; Xbox only adapts PCM/clock/texture delivery.
- Server IPv4/port/password editor with masked controller keyboard, connection
  test, save/cancel and backup. HTTP workers are quiesced before editing.
  Legacy server.cfg output_height remains a fallback; confirmed UI output
  selections in preferences.cfg take precedence.
- Debug logging toggle, 0–30 second next-episode countdown with immediate
  advance/cancel, and real-position fullscreen progress bar.
- Dashboard-default English/German UI with saved manual override and Xbox help.
- MP2/48-kHz bitrate selection: 128/192/256/320/384 kbit/s, also via web remote.
- Shared favorites editing, provider-wide progressive search, editable playlist,
  queue repeat off/one/all and queue shuffle independent of folder shuffle.
- Bounded HD artwork (1280x720 backdrop, 240x336 cover); no PSP changes.
- Separate stable Xbox media-player entity in HACS 0.2.0; shared web control panel.

## Useful missing settings, in suggested order

1. **Server transport:** DNS hostnames and HTTPS are separate work, not
   working toggles. Current editor supports trusted-LAN HTTP with IPv4.
2. **Encoding preferences:** expose additional Xbox audio codecs and
   cadence choices only once the separate Xbox encoder supports them. Do not
   copy PSP MP3/VBR or 20 fps labels onto the current MP2/MPEG-1/2 stream.
   AC-3/DTS passthrough is deferred until an S/PDIF receiver is available.

## Dependent on later ports

- MilkDrop preset selection, automatic changes, timing, live transitions,
  resolution and hard cuts; Monkey options, flight controls and rumble.
  Reuse existing portable engine/parser code; replace PSP GU/input bindings.
- Offline download/cache controls, ICY titles and local text/bitmap subtitles.
  Subtitle overlays require PTS paging, seek/cancellation and both NV2A color-key
  and software rendering paths; server burn-in remains the working fallback.

## Do not copy PSP-specific controls

PSP CPU/PLL clocks, LCD power behavior, StreamMaster Wi-Fi/USB/Bluetooth
management, PSP plugin INIs and LCD/TV cable switching have no equivalent in
this native Ethernet Xbox client. Xbox overclocking is neither required nor
changed by the app. Physical output modes must follow the Xbox cable/EEPROM.

## Preservation rule

Share platform-neutral logic and settings semantics where useful. Keep PSP
decoder, audio clock, input hooks and hardware paths unchanged. Xbox and
browser must not consume the PSP remote mailbox or overwrite PSP playback
status. Existing provider APIs, metadata, chapters and artwork are shared.
