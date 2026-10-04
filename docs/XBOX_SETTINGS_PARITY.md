# Xbox settings audit — 0.3.2

Compared against `psp-client/app_settings.h`, `visual_options.h` and the
Xbox `settings.h`, `net.h` and playback UI. This is an implementation audit,
not a claim that every new setting has passed a console test.

## Present

- Saved volume, autoplay next, repeat current, music folder shuffle.
- Preferred audio/subtitle language, per-file track selection and provider
  series preferences; source resume and chapter navigation.
- Two video encoding sizes. These are independent of physical output mode.
- Spectrum enabled/disabled; music X opens the shared PSP analyzer/painter
  options: legacy/desktop FFT, 12/24/32/48/64 bands, dB gain, all five palettes,
  separate LED toggle, 8–32 LED segments and peak hold. PSP implementation
  headers are compiled unchanged; Xbox only adapts PCM/clock/texture delivery.
- Server IPv4/port/password editor with masked controller keyboard, connection
  test, save/cancel and backup. HTTP workers are quiesced before editing.
  Output height remains a `server.cfg` setting.
- Debug logging toggle, 0–30 second next-episode countdown with immediate
  advance/cancel, and real-position fullscreen progress bar.

## Useful missing settings, in suggested order

1. **Language:** English/German selection using existing translation keys;
   Xbox controller help needs platform-specific labels. Still English-only.
2. **Server transport:** DNS hostnames and HTTPS are separate work, not
   working toggles. Current editor supports trusted-LAN HTTP with IPv4.
3. **Output settings:** expose supported RGB/Component modes, aspect handling
   and fullscreen preference. Start with apply-on-restart; runtime mode changes
   need renderer/texture lifecycle validation and 64 MB memory accounting.
4. **Encoding preferences:** expose appropriate Xbox audio codec/quality and
   cadence choices only once the separate Xbox encoder supports them. Do not
   copy PSP MP3/VBR or 20 fps labels onto the current MP2/MPEG-1 stream.

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
