# Xbox settings audit — 0.6.9

Update: DNS, session stop timer, ICY updates, separate web menu/text control,
series/folder preference editing and illustrated contextual help are now
implemented (console acceptance pending). See `XBOX_NEXT_STEPS.md` for the
current checklist; the historical gap table below records the 0.6.0 baseline.
Downloads, client-side subtitle overlays and HTTPS are still open work.

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
- Monkey/MilkDrop shared engines, presets, settings, live transitions and flight
  game through the NV2A adapter. Build/host checks pass; Xbox hardware test pending.

## Useful missing settings, in suggested order

1. **Server transport:** DNS hostnames and HTTPS are separate work, not
   working toggles. Current editor supports trusted-LAN HTTP with IPv4.
2. **Encoding preferences:** expose additional Xbox audio codecs and
   cadence choices only once the separate Xbox encoder supports them. Do not
   copy PSP MP3/VBR or 20 fps labels onto the current MP2/MPEG-1/2 stream.
   AC-3/DTS passthrough is deferred until an S/PDIF receiver is available.

## Remaining portable feature gaps and alternatives

| Feature | Xbox gap | Available alternative / implementation direction |
|---|---|---|
| Offline storage | No download manager, local library, cache/reserve or local resume UI | Stream over LAN now; later server-generated Xbox-compatible files copied by FTP, then local playback. PSP FLV is not a drop-in Xbox format. |
| Client subtitles | Text/bitmap overlays, paging, seek lifecycle and styling absent | Working server burn-in; later share subtitle transport/layout with an Xbox presentation adapter. |
| Server addressing/security | IPv4 + trusted-LAN HTTP only; no DNS/HTTPS | Local server/reverse-proxy HTTP endpoint now; add DNS/TLS separately, not an insecure pretend-HTTPS toggle. |
| Live radio metadata | Radio playback exists, changing ICY song title display absent | Station name now; relay current title through a bounded server metadata request. |
| Browser input | Playback remote works, arbitrary menu buttons/text injection absent | Native Xbox controller now; add a separate Xbox input mailbox instead of reusing PSP commands. |
| Playback limits | No PSP-style stop-after-N-files or timed session stop | Manual stop/web/HA stop now; reuse portable timer/count policy later. |
| Help | Compact Xbox help rather than PSP's illustrated topic/subpage guide | Build an Xbox-controller diagram and contextual topics; do not display PSP button instructions. |
| Series preferences | Reads server recommendations but has no series-specific editor | Edit on the web; later reuse server save API from Xbox options. |
| Encoding controls | MP2/48 kHz and MPEG-1/2; not PSP MP3/CBR/VBR/frame-rate options | Existing bitrate/size/downmix choices; add codecs/cadence only with an actual Xbox decoder/transport path. |
| Diagnostics | Current startup log is overwritten, unlike PSP retained/rotated logs | Copy log before restart; later bounded rotation and equivalent stall reporting. |

These are implementation gaps, not claims that the hardware cannot support
them. S/PDIF passthrough remains a separately deferred Xbox feature, not an
already working PSP feature to copy. Native high-resolution effects rendering
would likewise be an Xbox enhancement, not required PSP parity.

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
