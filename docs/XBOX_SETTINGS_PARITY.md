# Xbox settings audit — 0.3.1

Compared against `psp-client/app_settings.h`, `visual_options.h` and the
Xbox `settings.h`, `net.h` and playback UI. This is an implementation audit,
not a claim that every new setting has passed a console test.

## Present

- Saved volume, autoplay next, repeat current, music folder shuffle.
- Preferred audio/subtitle language, per-file track selection and provider
  series preferences; source resume and chapter navigation.
- Two video encoding sizes. These are independent of physical output mode.
- Spectrum enabled/disabled; music X opens 12/24 bands, sensitivity 1–8,
  classic/gradient/whole-LED rendering and 8–32 LED segments.
- Server IPv4 address, port, password and output height are read from
  `server.cfg`, but **cannot yet be edited inside the Xbox app**.

## Useful missing settings, in suggested order

1. **Server configuration in-app:** address, port, masked password, connection
   test, save/cancel and rollback. Reuse the PSP keyboard/editor behavior with
   Xbox input/render adapters. Quiesce all HTTP workers before replacing their
   shared credentials; never replace an existing config during updates.
   DNS hostnames and HTTPS are separate transport work, not working toggles.
2. **General preferences:** English/German language selection using existing
   translation keys where appropriate, debug logging on/off, and optional
   next-episode countdown/cancel. Keep diagnostic errors available independently
   of frequent performance logging.
3. **Spectrum parity:** share the portable PSP signal analysis and visual
   option definitions instead of growing a divergent Xbox implementation.
   Still missing: 32/48/64 bands, legacy versus desktop-FFT analysis, dB gain,
   all five color styles, independent segment toggle and peak hold. Xbox does
   not need PSP LCD/TV settings; any separate profiles should follow Xbox
   output modes. Current Goertzel analysis is not desktop-FFT parity.
4. **Output settings:** expose supported RGB/Component modes, aspect handling
   and fullscreen preference. Start with apply-on-restart; runtime mode changes
   need renderer/texture lifecycle validation and 64 MB memory accounting.
5. **Encoding preferences:** expose appropriate Xbox audio codec/quality and
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
