# Xbox settings audit — 0.4.0

Compared against `psp-client/app_settings.h`, `visual_options.h` and the
Xbox `settings.h`, `net.h` and playback UI. This is an implementation audit,
not a claim that every new setting has passed a console test.

## Present

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

## Useful missing settings, in suggested order

1. **Language:** read Xbox dashboard language as initial default, allow manual
   selection, reuse existing translations. Controller help needs Xbox labels.
   Still English-only; explicitly queued after MPEG-2/output work.
2. **Server transport:** DNS hostnames and HTTPS are separate work, not
   working toggles. Current editor supports trusted-LAN HTTP with IPv4.
3. **Output tests:** verify runtime mode changes/rollback, anamorphic aspect
   and SD/HD performance on the console. Do not claim tested HD decoding yet.
4. **Encoding preferences:** expose appropriate Xbox audio codec/quality and
   cadence choices only once the separate Xbox encoder supports them. Do not
   copy PSP MP3/VBR or 20 fps labels onto the current MP2/MPEG-1/2 stream.
   AC-3/DTS passthrough is deferred until an S/PDIF receiver is available.

## Dependent on later ports

- MilkDrop preset selection, automatic changes, timing, live transitions,
  resolution and hard cuts; Monkey options, flight controls and rumble.
  Reuse existing portable engine/parser code; replace PSP GU/input bindings.
- Offline download/cache controls, editable queue and repeat-all, favorites
  editing and search/text entry. These are not enabled by settings alone.
- Xbox-specific HA entity; the web Xbox remote is present, the HACS entity
  still represents the PSP.

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
