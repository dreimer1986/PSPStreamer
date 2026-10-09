# Setup and configuration

Detailed installation, configuration, controls and security. See the [project overview](../README.md), [Build guide](BUILDING.md) and [earlier development notes](DEVELOPMENT_HISTORY.md).

## Requirements

- Python 3.11 or later
- FFmpeg with `libx264` and `libmp3lame`
- A PSP with working infrastructure Wi-Fi and CFW for the homebrew app

## Run the server directly

`MEDIA_ROOTS` is a colon-separated list of allowed media roots. The server never follows paths outside these roots.

```bash
cd /path/to/PSPStreamer
MEDIA_ROOTS='/srv/media/Serien:/srv/media/Filme' PORT=8091 \
  python3 -m psp_streamer.server
```

The test interface is then available at `http://SERVER:8091`. It is useful for browsing and checking transcoding; reliable playback happens in the PSP app.

`psp_streamer.server` is the module in `psp_streamer/server.py`. The root-level
`server.py` is now only a compatibility launcher (`python3 -m server` also
starts the current implementation). The obsolete synthetic A/V calibration
streams have been removed in server 0.1.32; ordinary media is unaffected.

An independent, disabled-by-default experimental overclock plugin is documented
in [psp-overclock/README.md](../psp-overclock/README.md). It is not required for
PSP Streamer and must not run alongside another clock controller.

| Variable | Default | Purpose |
| --- | --- | --- |
| `MEDIA_ROOTS` | `/media` | Allowed video directories, separated by `:` |
| `PORT` | `8091` | HTTP-Port |
| `MAX_TRANSCODES` | `4` | Concurrent FFmpeg processes; one current PSP playback needs one |
| `PGS_CACHE_TRACKS` | `1` | Number of decoded PGS tracks retained in host RAM |
| `FFMPEG_PRESET` | `veryfast` | x264-Preset |

## Docker

The SMB/NFS mount belongs on the Docker host. Mount it read-only only.

```bash
MEDIA_ROOT_PATH=/srv/media PSP_STREAMER_PORT=8091 docker compose up -d --build
```

Do not put SMB credentials in `compose.yaml`.

## Home Assistant integration (HACS)

The **custom integration** adds a `media_player` entity, a media browser and
playback controls to Home Assistant. It connects to an existing PSP Streamer
server (Docker, the Home Assistant app/add-on, or standalone Python); it does
not replace that server or run FFmpeg itself.

In HACS, add `https://github.com/dreimer1986/PSPStreamerHA` as a **custom repository**
of type **Integration**, download **PSP Streamer**, and restart Home Assistant.
Then open **Settings → Devices & services → Add integration → PSP Streamer**
and enter the server URL and its password. Requires Home Assistant **2026.3+**,
server **0.1.44+**, and the matching updated PSP application for playback telemetry.

See [Home Assistant integration setup, controls and limitations](https://github.com/dreimer1986/PSPStreamerHA/blob/master/docs/HOME_ASSISTANT.md).
The HACS integration has its own repository, independent of PSP application
releases. This repository remains the source for the server app/add-on and PSP
application. Adding it to the app/add-on store does **not** install the integration.

## Home Assistant add-on

PSP Streamer can run as a Home Assistant add-on, keeping FFmpeg and the media
server off the desktop PC. In Home Assistant, open **Settings → Add-ons → Add-on
Store**, open the menu, choose **Repositories**, and add:

```
https://github.com/dreimer1986/PSPStreamer
```

Refresh the store, select **PSP Streamer**, and install it. The add-on exposes
Home Assistant's `/media` directory read-only at port `8091`; mount the SMB/NFS
library into that directory on the Home Assistant host. Configure `port` (default
`8091`) and `max_transcodes` (default `4`) in the add-on configuration, then
start it. Point the PSP configuration at the Home Assistant host or its DynDNS
name with `server=…` and `port=8091`.

**Update the server/add-on to 0.1.20 or later for the current client.** Version 0.1.19 introduced FLV streaming; 0.1.20 also provides automatic continuation for remotely started playback. Refresh the add-on store, install the update and restart the add-on. Media mounts and options stay unchanged. The updated server retains the old raw H.264/MP3 endpoints for older clients; the new video client does not fall back to their estimated timing.

### Automatic continuation from the web remote

After a remote seek, the PSP consumes both the seek request and its pending
resume flag before restarting playback. Update the PSP application to include
this fix: older clients could return to the browser at the later natural video
end instead of advancing. This applies equally to the web and Home Assistant
remotes; server 0.1.45's separate PGS end-of-stream fix remains necessary for TV
bitmap burn-in. With debugging enabled, the video watchdog log now includes a
final `playback outcome` line with EOF/seek/resume flags.

After a remotely started episode ends naturally, the PSP requests the next video in the same folder, using natural filename order (episode 2 before episode 10). This does not depend on the folder currently open in the PSP browser or its visible page. Playback stops at the last video; it does not descend into other folders or switch to music. Stop, playback errors and seeking do not trigger next-episode playback.

Music continues through audio files in the same folder. The PSP's saved shuffle option also applies to remotely started music; shuffle excludes the current track, while sequential playback stops at the folder end. Requested audio/subtitle track indices are reused for the next video and checked against its available tracks.

The updated PSP client also accepts Pause, Resume, Stop and Seek during
music, including tracks started using the PSP buttons. Seek restarts the
stream at the requested position and resumes playback. Selecting another
file and pressing Play during music **or video** first stops and releases
the current playback resources, then starts the requested file (including
switching between audio and video). A Stop or replacement Play does not
trigger automatic next-track playback. These client changes use the existing
server API; no server/add-on update is needed.

Commands are polled in background workers, not in the music drawing or DAC
loop. Allow normal network/polling and teardown/startup time for a command to
take effect. The command mailbox stores only the latest command; it is separate
from the persistent playlist added in 0.1.59. Commands expire after 15 seconds.
The browser displays the reported playback position and allows a seek target.

An enabled shared playlist takes precedence for its entries when resolving
remote playback or playback started from the PSP's Playlist folder. It can
cross between music and video, uses saved per-entry tracks and stops at the
list end. Ordinary folder playback remains available; disable playlist order
in the Playlist view to restore the server's folder-based continuation.

The read-only endpoint is `GET /api/media-next/<media-id>?shuffle=0` (`shuffle=1` for shuffled music). It returns `{"id":"...","kind":"video"}` or `{"id":"...","kind":"audio"}`, and `{}` when there is no successor. It does not enqueue remote commands. Update both the server and PSP app to use remote continuation. The web page's selected-file details still describe the file selected in the browser, not a live report of the automatically selected successor.

## Install and configure the PSP app

### StreamMaster USB / Onju Voice V3

Also available: **UNTESTED** generic ESP32-S3 builds for Quad/Octal PSRAM and
ESP32-S2 with PSRAM. These require matching memory hardware and USB host power;
they do not imply support for every ESP32 board. See the
[generic firmware guide](../streammaster/GENERIC.md). Onju V3 remains the tested target.

Firmware **0.3.0** adds five saved Wi-Fi profiles, selectable or automatic by
signal strength, with separate passwords and IP/DNS settings. Configure them
under **Settings → StreamMaster USB → Profile**. See the
[profile and update instructions](../streammaster/README.md#saved-wi-fi-profiles-firmware-030).
To retain profiles when upgrading, flash `streammaster_onju_v3.bin` at `0x10000`,
not the merged factory image at `0x0` (which overwrites the NVS area).

An optional ESP32-S3 bridge and PSP USB driver are available for the original
Onju Voice PCB V3. **Select → Settings → StreamMaster USB** configures its Wi-Fi,
DHCP/static IPv4 and DNS, and provides USB integrity/throughput and server tests.
Network credentials are saved on Onju. With firmware **0.2.2** and the matching
PSP app/USB driver, **Settings → Network transport → StreamMaster USB** routes
browsing, playback, subtitles, downloads and remote control through Onju.
Save and restart the app to switch transports. The CFG equivalent is
`network_transport=streammaster`; `network_transport=wifi` is the default and
restores native PSP Wi-Fi. The existing server URL, port and password apply to
both transports. USB HTTPS uses Onju's trusted certificate bundle and clock;
it does not import the PSP's certificate cache or accept untrusted certificates.
Six independent TCP/TLS channels share the USB link; hardware playback and
throughput depend on the adapter; the Onju V3 combination is hardware-tested.
Onju's status LEDs indicate firmware operation, Wi-Fi connection/signal strength
and the claimed PSP USB connection; see the LED legend in the firmware guide.
See [firmware, flashing and first-test instructions](../streammaster/README.md).
Developers can integrate the adapter into other PSP homebrews using the
[StreamMaster integration guide](../streammaster/INTEGRATION.md).
Firmware 0.2.5 also supports advanced CFG-only throughput comparison settings:
`streammaster_bulk_kib=8` (8, 16 or 32 KiB per response) and
`streammaster_bulk_depth=4` (1, 2 or 4 outstanding requests). Save the CFG and
restart the app. The validated default is 8 KiB/four requests. Zero or missing
values select the default; explicit depth=2 retains the older two-request route.
32 KiB/four is limited to two requests; unsupported values use the defaults.
Older firmware/drivers retain their negotiated legacy route. Keep the same ready
file, HTTP route, card, hub and clock when comparing settings. No speedup is assumed.

The current Onju firmware is in `StreamMaster/Onju-V3` (firmware
`0.3.18-bt-qio80-iram`). Install all three companion PSP files: `EBOOT.PBP`,
`PSPStreamer.prx` and `StreamMasterUSB.prx`. Four-deep HTTP downloads measured
747.5 KiB/s versus 700.5 KiB/s for two requests on the tested setup. IRAM alone
has no demonstrated mean-speed advantage. Firmware version is visible in the
StreamMaster settings and debug logs. See [release layout](../docs/RELEASE_LAYOUT.md).
Download diagnostics include the actual selected profile and, on 0.2.5, ESP timing
to separate queuing and processing from USB transfer time.
The bridge replaces Onju's current firmware and requires a powered USB host
connection; it is not a globally installed ARK Wi-Fi replacement plugin.

### PSP installation

After a build, install the complete contents of `psp-client/release/PSPStreamer/` to:

```
ms0:/PSP/GAME/PSPStreamer/
```

The first time a playback option is saved, the app creates this file:

```
ms0:/PSP/SYSTEM/PSPStreamer.cfg
```

The server is configurable without recompiling or a PC: press **Select in the
file browser** for Settings. Up/Down selects, Left/Right changes values,
Cross opens text/number entry, Start saves and Circle cancels. TV startup mode
applies on the next app launch. You can also edit the config file directly:

```ini
server=streamer.example.net
port=8091
server_password=
https=0
debug=0
music_cpu_mhz=133
milkdrop_cpu_mhz=266
video_cpu_mhz=333
idle_cpu_mhz=222
download_cpu_mhz=333
screen_idle=0
audio=0
subtitle=-1
quality=2
audio_matrix=none
audio_output=psp
video_fps=20
play_mode=stream
music_preset=active.milk
preset_auto=0
preset_seconds=60
preset_fade_ms=1500
preset_live_transitions=1
milkdrop_high_resolution=1
spectrum_analysis=0
spectrum_bands_lcd=32
spectrum_bands_tv=32
spectrum_gain_db=0
# Colors: 0 Original, 1 Rainbow, 2 Classic VU, 3 Ice, 4 Fire.
spectrum_style=1
spectrum_segments=0
# Whole vertical LEDs per bar: 8..32, shared by LCD and TV.
spectrum_led_count=20
spectrum_peak_hold=1
volume=24
shuffle=0
language=en
tv_ui=off
```

`server` accepts an IPv4 address or DNS/DynDNS name; an `http://` or `https://`
prefix is also allowed in the file. `https=0` selects HTTP, `https=1` TLS;
when both are present the last setting wins. `port` is explicit in either mode.
Enter just the hostname/IP in the app's Server host field, not a URL.

Optional CPU profiles in Settings: `music_cpu_mhz` is music with the spectrum
display (133 MHz), `milkdrop_cpu_mhz` is music with MilkDrop **or Monkey**
(266 MHz), `video_cpu_mhz` is video (333 MHz), and `idle_cpu_mhz` is the media
browser (222 MHz). `download_cpu_mhz` controls the download workflow including
the final SHA-256 check (333 MHz); completion, cancellation and failure restore
the browser profile. Existing saved choices are preserved. Each accepts `0`
(no profile override) or 66–471 MHz. Left/Right changes by **1 MHz**; Cross opens direct
number entry. The optional [StreamerOC plugin](../psp-overclock/README.md) needs
`enabled=1` and `app_control=1`. Above 333 MHz, requests are limited to the
plugin's configured `target_mhz`; set that only to an overclock you have tested.
Without the plugin no clocks are changed. Defaults above apply when a CFG key
is absent; an explicit `0` remains respected.
Track/episode changes, seeks and radio reconnects keep the current clock while
loading. The next active renderer requests its profile only if it differs;
there is no intermediate return to the INI target or browser clock. Returning
to the media browser applies its idle profile directly (`0` releases to the
INI target). An explicitly disabled next playback profile (`0`) also releases
the previous override. App exit retains the coordinated plugin shutdown.
Start underclock testing at 222 MHz, not 66 MHz: low clocks can slow the
browser, decoding and storage/network I/O. Requested MHz are not guaranteed
exact physical frequencies. Enable plugin `overlay=1` to see changes.

**Select → Plugins → X** opens settings for **StreamerOC**, **PSP Consolizer**
and **FuSa Fullscreen**. Up/Down selects a row; Left/Right changes its value.
X also opens numeric entry for MHz/delay. All main numeric INI options are
available, including always-on overlays, TV policy, metadata and POPS rumble.
**Start saves this INI independently of the outer app settings; Circle cancels.**
Changes take effect at the **next application start**, not in the running plugin.
The files are `ms0:/SEPLUGINS/<plugin>/<plugin>.ini`. StreamerOC alone also uses
an existing `ef0:` INI if its Memory Stick INI is absent. Install the plugin and
INI first. Unreadable, oversized or invalid supported settings are not overwritten.
Saving verifies a temporary file and keeps the previous INI as `.ini.bak`.
Unchanged keys and comments are preserved; editing a key replaces its old lines.
Per-title and exact launch-path overrides live separately in
`StreamerOC-rules.ini` and `PSPConsolizer-rules.ini`, beside each plugin's main
INI. Open **Title / path rules** to add an actual DISC_ID or full launch path,
X to edit values, Square to rename, and Triangle twice to delete a rule.
Inside a rule, Square removes the selected override ("Global"). Circle returns
to the rule list without saving; **Start in the list saves all rule edits**;
Circle there discards the draft. Title ID takes precedence over path, with the
first matching section winning ties. FuSa has no per-title rule engine.
Consolizer's **Path filters** edits up to eight allowed and eight excluded
case-sensitive path substrings. Triangle twice deletes a filter; Start saves.
These global filters still apply even when a title rule enables the controller.
No new kernel hooks or resident plugin buffers are added by these menus.
See the
[OC rules](../psp-overclock/README.md#per-game-and-homebrew-rules) and
[Consolizer rules](../psp-controller/README.md#per-title-and-exact-path-settings)
for supported keys, precedence and examples.
The editor does not install/enable ARK's plugin entry or test clock stability.
Overlay offers `0=Off`, `1=polling` (the previous default) and
`2=framebuffer hook` (experimental, opt-in). Mode 2 follows standard user-mode
framebuffer submissions; apps using direct/kernel presentation can bypass it.
Switch back to 1 if an app has display problems. Restart the application after
changing the mode; this is not an in-session display patch switch.

`screen_idle=0` keeps the display awake as before; `1` allows normal LCD
power saving during music; `2` allows it throughout the app. Dimming/off
timing comes from the PSP's power-saving settings. Buttons wake the screen
and retain their usual functions. System standby is always prevented while
the app runs. TV output always stays awake so video/sync and external audio
continue. These options also have pages in the built-in Settings help.

DNS is refreshed by library/media setup; remote polling uses that cached address.
`audio` (0–7) and `subtitle` (-1–31) store preferred video-track indices
(`subtitle=-1` disables subtitles); `quality` means `0=96k`, `1=128k`, `2=160k`,
`3=V6`, `4=V5`, `5=V4`, `6=V3` MP3; `volume` ranges from `0` to `30`;
`shuffle=1` randomly continues with another audio file from the current folder
(`0` keeps its listed order). See [settings and HTTPS](../docs/HTTPS_AND_SETTINGS.md)
for text entry, password changes, certificates and deployment examples.

`play_mode=stream` (default) or `play_mode=download` selects the PSP's video
playback workflow. Change it in the video options dialog; music and immediate
web-remote Play commands continue streaming. Web conversion jobs have their
own explicit button and per-job settings.

MilkDrop automation: `preset_auto=0` disables automatic changes (default);
`1` selects in order, `2` randomly and `3` randomly weighted by each preset's
`fRating`. `preset_seconds` accepts 30–600 seconds (default 60).
`preset_fade_ms` accepts 0–5000 (default 1500; 0 means a hard cut).
`preset_live_transitions=1` (default) keeps both presets running during automatic
soft changes. Set it to `0` for the cheaper snapshot fade. In music, open the
MilkDrop preset list with Circle, then Select for **Live transitions**.
Manual selection from the stopped preset browser still starts a fresh renderer.

`milkdrop_high_resolution=1` selects 512×512 feedback (default); `0` restores
the faster 512×256 mode, including the original 32-bit LCD feedback. Change it
in **Settings → MilkDrop resolution** and save with Start. No app restart is
needed: it applies when music/visualization is next started. The setting affects
both LCD and TV, windowed and fullscreen, not video playback or spectrum mode.
In the Circle preset browser, **Square** cycles modes and **Triangle** cycles
30/60/120-second intervals. Settings are saved when leaving the browser.
An optional `presets/playlist.txt` limits automation to one exact filename per
line; otherwise it uses the sorted preset directory. See
[automation, instances and large waves](../docs/MILKDROP_AUTOMATION.md).

### Audio quality and film frame rate (server/HA app 0.1.25)

The PSP playback-options dialog and browser remote offer CBR 96/128/160 kbit/s
and VBR V6/V5/V4/V3 for music and video. V5 is the balanced VBR choice; V6
favours smaller streams and V4/V3 favour quality. VBR controls average quality,
not a strict bandwidth ceiling; individual frames can reach 320 kbit/s.
Existing installations keep their CBR preference (default: 160 kbit/s).

For videos, choose **20 fps** (compatibility/default) or **23.976 fps**
(`24000/1001`, film/anime). The config key is `video_fps=20` or
`video_fps=24000/1001`. This applies to both LCD and TV output, without changing
the audio clock or container-timestamp synchronization. Matching a 23.976 fps
source avoids the frame dropping needed for 20 fps, but requires about 20%
more video decoding and presentation work. It does not eliminate 60 Hz display
cadence or network stalls. Video bitrate limits remain unchanged.

Local PSP choices are saved for subsequent playback. The browser saves its own
preferences in local storage and sends them with Play; those choices also apply
to following files in that remote playback session. Commands from older remotes
without these fields retain the PSP's current choices. Update both the server
and PSP application before using the new options. Music hides the frame-rate
selector, just as it hides video-track/subtitle selectors.

`language` selects the PSP interface language: `en` (default) or `de`. The PSP's bundled Latin-1 font supports direct German `ä`, `ö`, `ü`, and `ß` characters.

`tv_ui=off` (default) preserves LCD menus with TV video playback. Set `tv_ui=auto`
to use the separate native 720×480 TV interface when a supported component
cable is connected at app startup. This includes the library, loading screen,
file information, playback options and music receiver/spectrum. Without that
cable, or if TV initialization fails, menus remain on the LCD. Hold **L while
starting the app** to bypass automatic TV menus for that session without
changing the saved setting. Restart the app after changing `tv_ui`.

You can also switch the menu and music display **after startup**: stop playback,
open **SELECT → Switch menu to TV/LCD** (one Down press, then **X**).
**Circle** returns to the library. Connect the component cable before switching
to TV; returning to LCD works even after unplugging it. This action takes effect
immediately, requires no Save or restart, and leaves `tv_ui` and any unsaved
settings unchanged. Video still follows the cable at its next start, including
the existing LCD-menu/TV-video mode. Switching during playback is not supported.

The TV artwork is a separate embedded asset, not an enlarged LCD screenshot.
Its volume dial is prepared for a 16:9 display; use the corresponding TV/OSSC
aspect setting. The startup check detects a cable, not whether the TV is
powered on. Automatic hotplug switching during playback is not provided.
The TV interface adds a 1.41 MiB RAM drawing buffer only when enabled; its
embedded artwork adds about 1.32 MiB to the executable. The current tested
video staging/PTS scheduler is unchanged. Host tests verify rendering bounds
and mode ownership, but physical PSP/OSSC transition tests are still required.

### Adding a PSP interface language

Create `psp-client/lang_xx.h` by copying `lang_en.h`. Each visible text has its own named `TXT_*` entry and related entries are grouped by interface screen, so translations can be edited without relying on array order. Preserve printf placeholders such as `%d`, `%s`, and `%.48s`. Include the new file in `psp-client/language.c`, then register its code and table in the `languages[]` array there, for example `{"fr", lang_fr}`. Rebuild the EBOOT and set `language=fr` in `PSPStreamer.cfg`.

For the illustrated manual, also copy `help_en.h` to `help_xx.h`, translate the
named title/step fields, and include/register the table in `help_pages.h`'s
`help_translation()` list. Unregistered help languages fall back to English.
Keep each step to one short title and one explanation line. The help layout
test measures all English/German pages with the bundled LCD and TV glyphs;
add new languages to the same test when translating.

### Illustrated help on the PSP

In the library, press **Select → Help → X**. Help is the first settings entry,
not a configuration value. **Square** in playback options opens the relevant
music/options topic; **Select** in Local storage or the server queue opens the
download topic. Help works offline, does not save or discard pending settings,
and needs no extra image files on the Memory Stick.

**L/R** selects a topic, **Up/Down** selects its subpage, and **Circle** returns
to the screen you came from. The 17 pages cover browsing, playback options,
video controls, music, visualizations/presets, downloads, settings/text entry,
TV output and connection recovery. Each page shows a PSP diagram with the
relevant buttons in gold and three short instructions. Both LCD and native TV
render text and the diagram directly; help does not switch video-output modes.
Routine footer hints now show only the essential actions and help entry point.

Fullscreen is simpler: **Triangle alone** toggles it during music or LCD video.
The old **Cross+Triangle** gesture still works; holding Triangle does not repeat
the toggle. In the preset chooser, Triangle still changes the preset interval.

Browser controls: Cross opens a folder or playback options; Triangle opens the media information page for a file; Circle exits the options screen; Left goes to the parent folder; held L/R pages through the list; Square reloads; Start exits the app.

Playback controls: In fullscreen video (including TV), Select opens/closes the
transport overlay without pausing. Left/Right selects a button, Cross activates
it and Circle closes the overlay. Buttons are previous file, -30 seconds,
-10 seconds, Pause/Play, +10 seconds, +30 seconds and next file. File switches
use the existing complete decoder/audio teardown, stay in the same folder and
do not wrap; at a folder boundary the player returns to the browser. They also
work for remotely started video with server/HA app 0.1.24 or newer. Outside
fullscreen, Select retains direct pause/resume. Music controls are unchanged.
L/R still seek ±10 seconds and Start returns to the browser. Track titles such
as `Forced` or `Full` appear beside language labels when available.

Receiver controls: Circle shows/hides the receiver strip, Up/Down adjusts and stores volume (hold either direction for a slow repeat), and Triangle toggles fullscreen (Cross+Triangle also works). Fullscreen works for video and for the audio spectrum display.

With the normal spectrum selected (Square cycles back from MilkDrop),
Triangle fills the LCD or native TV canvas with spectrum bars, without
the receiver strip. The existing 20-Hz visual update limit and changed-area-only
writes remain in place. Pause lets the bars decay; volume and Stop still work.
Fullscreen preference remains active across music tracks.

Server/HA app **0.1.23** hides audio-track and subtitle selectors for music and
uses their neutral values when sending a music Play command. Video selectors
return when selecting a video. Update the HA app and reload the browser page.
Remote commands have a **15-second validity window from submission**; polling
does not extend it. After expiry, the server returns Idle with the current
sequence number, so starting the PSP app cannot replay an old command. Expiry
does not stop playback that has already started, nor limit subtitle preparation.

## Subtitles and limitations

ASS/SSA, SRT, WebVTT, and other FFmpeg-readable text tracks are converted once into compact, timed cues. The PSP overlays a compact outlined DejaVu Sans bitmap locally. HDMV PGS subtitles use native palette-indexed sprites, preserving their original colour and outline without burning them into video. Other bitmap formats (VobSub/DVDSUB, DVB, XSUB) continue to use the server fallback.

Keep `subtitle_font.raw` and `cooleyesBridge.prx` beside `EBOOT.PBP`. The compact DejaVu Sans Latin-1 atlas is loaded only after the AVC decoder is ready; if it is missing, video playback remains safe and text subtitles are simply not drawn. The receiver artwork is embedded in `EBOOT.PBP`; no separate `menu_skin.raw` is required.

Component TV playback uses native 720×480 output. By default, the browser and options stay on the PSP LCD and video switches to the TV. With `tv_ui=auto`, TV menus and video share the same output mode: Stop/end returns to the TV menu without an LCD mode reset. The existing Select+L+R TV check is available from either browser and returns to its originating output. Text subtitles remain local overlays; TV bitmap subtitles use server-side burn-in to avoid sprite-transfer stalls at the higher resolution. The server website can select media and send play/pause, stop and seek commands. This GUI update does not require a server/add-on update.

During music, **Square** cycles **spectrum analysis → MilkDrop → Cave → spectrum**.
MilkDrop uses the selected preset file (initially `presets/active.milk`). Copy
the supplied `psp-client/presets` folder beside EBOOT. The three early built-in
test effects are no longer part of the user-facing selection.

**Cave** is a separately selectable Monkey geometry study: an independently
implemented Marching Cubes surface over compact radial fields and three-octave
noise, following the algorithm identified in the DLL. It adds branching walls,
depth testing, procedural rock texture and distance fog. Octave rotations and
offset ranges now follow the recovered generator, and original material-field
normals give the walls smooth lighting. Sixteen contributors now use the
reconstructed base oscillator controller, with changing radii and cached profiles.
Small, distance-weighted random variations now affect odd side paths, without
moving the main camera path; values are cached rather than regenerated each frame.
The recovered alternate cubic paths now blend with the oscillators; the camera
looks six profiles ahead, and continuous base texture coordinates replace
per-triangle projections. This is a PSP adaptation, not a pixel-identical port:
the camera now uses chained profile curvature, source sway, roll impulses and
distance-dependent banking. Classic cube topology, weighted material fields,
source material normals, spatial RGB/alpha envelopes and two-light shading follow
the recovered formulas. Nine styles include displaced wireframe and Hair;
the two texture banks blend with separate UV coordinates. The original RNG,
scene initialization, animated movement/FOV/sway gates, wall-noise coordinates
and distance-based texture transitions are now used. PSP geometry budgets,
fallback replacement textures, PCM-based beat detection and the fixed-function
display still differ. See the
[comparison audit and adaptations](../docs/MONKEY_GEOMETRY.md).
Only one new depth slab is built per visual update; completed geometry is cached.
Three rear slabs remain visible for camera turns without shortening the forward horizon.
Adjacent slabs reuse their shared field/gradient plane, and trigonometric path
parameters are prepared once per plane rather than once per sample.
The render target is fixed at 512×256 RGB565, including on TV, to fit a real depth
buffer alongside the scanout. MilkDrop's resolution setting remains unchanged
for MilkDrop. No new assets or server update are needed.
Triangle toggles fullscreen; the selected mode survives song changes. From
spectrum, press Square twice for Cave, once more to return to spectrum.
Circle opens Cave's effects settings while Cave is selected; in MilkDrop it
opens the preset list. Select inside that list opens additional automatic-switch
options. Start stops normally. Audio clocks and queues
are untouched. The original tube prototype was removed after the cave renderer
passed the LCD/TV hardware test. Its source remains available in Git history.

### Visualization settings

While music plays, press Circle in Cave. Use Up/Down to select a row,
Left/Right to change it, and Circle to save and return. The music keeps playing.
These settings are saved in the existing `pspstreamer.cfg`:

| Key | Default | Meaning |
| --- | --- | --- |
| `cave_fog` | `1` | Distance fog, 0/1 |
| `cave_multitexture` | `1` | Two texture banks, 0/1 |
| `cave_hair` | `1` | Hair in the corresponding style, 0/1 |
| `cave_transparent_hair` | `1` | Hair transparency, 0/1 |
| `cave_beat` | `1` | Music-triggered responses, 0/1 |
| `cave_sensitivity` | `8` | Beat detection sensitivity, 0–16 |
| `cave_amplitude` | `8` | Beat response strength, 0–16 |
| `cave_style` | `-1` | Automatic styles; 0–8 fixes a style (7 = Hair) |
| `cave_speed` | `100` | Travel speed, 10–200 percent; affects automatic and Easter-egg flight |
| `cave_invert_y` | `0` | Invert the flight stick's vertical axis, 0/1; when enabled, pushing up dives |
| `cave_noise` | `0` | Original wall-noise amount, 0–16; 0 disables it, matching the desktop default |
| `cave_flight_sensitivity` | `50` | Flight steering strength, 10–100; lower gives gentler steering and rolling |
| `cave_flight_inertia` | `65` | Flight response smoothing, 0–100; higher responds and settles more slowly |
| `cave_rumble_music` | `0` | Monkey bass/beat vibration strength, 0–100%; 0 disables it |
| `cave_rumble_game` | `0` | Monkey impacts/scraping vibration strength, 0–100%; 0 disables it |
| `preset_random_seconds` | `10` | Additional random automatic-switch delay, 0–120 seconds |
| `preset_hard_cuts` | `0` | Enable music-triggered immediate preset switches, 0/1 |
| `preset_hard_threshold` | `250` | Hard-cut sensitivity threshold, 125–400 percent |
| `preset_hard_seconds` | `60` | Hard-cut threshold recovery parameter, 5–180 seconds |

MilkDrop's four new options are available with Select inside the playing-music
preset list. Automatic mode must be enabled for timed or hard-cut switches.
Existing interval/fade controls remain. Automatic soft transitions now evaluate
both presets, blend their warp grids with cosine easing and crossfade their live
waves and shapes on shared feedback, following the shaderless MilkDrop approach.
Echo, gamma, borders, motion vectors and legacy color shading blend too; discrete
effects switch at the midpoint. These are uniform transitions, not shader-based
or spatial wipe patterns. Hard cuts remain immediate.

The outgoing state costs about 937 KiB plus its retained bytecode and texture
assets; it does not require another full-screen buffer. Allocation failure falls
back to the existing snapshot fade. Formula and asset errors are still reported.
During demanding transitions, visual frame rate may fall; audio clocks, decoding
and renderer idle/yield rules are unchanged. The old state is released at fade
completion, stop, hard cut, another preset replacement or an output-layout rebuild.

For a clear hardware test, choose `live-transition-test/01 - Amber orbit.milk`,
set automatic mode to sequential and the interval to 30 seconds. The catalog uses
the selected folder, so these two fixtures alternate. Keep hard cuts off.
The amber polygon and cyan wave should continue moving during the overlap.
For a longer inspection, set `preset_fade_ms=4000` before starting the app.

Use Up/Down to reach the second page of Cave settings (speed, inverted flight,
flight sensitivity and inertia). The flight defaults are deliberately gentle;
sensitivity changes steering strength, whereas inertia changes response time.
That page also offers wall noise: higher values add geometric wall detail and
cost more computation. This is separate from textures and from animated path jitter.
Try 30–50 percent for a calmer tunnel ride; the default 100 keeps the existing
speed. In Easter-egg flight, Up/Down adjust a separate 20–200 percent throttle
on top of this setting. Geometry preparation continues to bound maximum speed.

Cave's background now follows the recovered animated, brightness-limited palette
even with fog disabled. When enabled, fog blends distant walls into that same
color, rather than always into black. Black-material modes retain the original
exception: with fog off, they clear to black. Fog depth is scaled to the PSP's
shorter prepared horizon, not the desktop's longer view distance.
The original additional ambient-light term for enabled multitexture is included.

### Installing original Monkey textures (optional)

Use the images from your own original Winamp Monkey installation. They are not
included in PSPStreamer. No extraction from the DLL or manual conversion is needed:

1. On your PC, open `Winamp/Plugins/monkey/`.
2. On the Memory Stick, create `PSP/GAME/PSPStreamer/monkey/` beside `EBOOT.PBP`.
3. Copy these seven files into that folder, keeping their names:

   ```text
   supertex_a1.jpg
   supertex_a2.jpg
   supertex_a3.jpg
   supertex_a4.jpg
   supertex_a5.jpg
   supertex_b1.jpg
   supertex_b2.jpg
   ```

4. Start Cave again to load them. No configuration entry is required.

For example, the first file must be at
`ms0:/PSP/GAME/PSPStreamer/monkey/supertex_a1.jpg`, not inside `presets/`.
The original 512×512 JPEGs work unchanged: the loader downsizes them to 256×256
for PSP memory limits. Replacement JPEGs may be up to 1024×1024; PNGs must have
power-of-two dimensions from 16 to 256. Each file is limited to 1 MiB.
The extensions `.jpg`, `.png`, then `.jpeg` are tried in that order per slot.

Missing or invalid files use generated replacements individually, so partial
sets also work. Only use images you are entitled to use; do not redistribute the
original assets with the application. Seven maximum-size images use 1.75 MiB of
additional memory, released when leaving the renderer. With diagnostics enabled,
`Cave external textures: 7F` confirms that all seven slots loaded successfully.

<details>
<summary>Cave Easter egg</summary>

During Cave playback, press **L+R together** to open the flight intro with its
Monkey-inspired logo. Choose **Game Start / Hall of Fame / Exit** with Up/Down
and confirm with X; Circle leaves the menu. **Left/Right selects one of seven
ships** with a model preview before starting (Low Poly 2 is the original/default ship). During flight, hold **L+R for five
seconds** to leave (a progress indicator appears). The entry press must be
released first; continuing to hold it does not immediately leave again.
The Easter egg uses fullscreen on LCD and TV and restores your previous view
when leaving. It does not change the saved music fullscreen preference.
The analog stick steers the forward flight direction; release it to fly parallel
to the local tunnel axis. L/R roll the ship and its steering axes. Hold Up/Down
to increase/decrease speed (20–200 percent, retained until leaving flight).
These two buttons do not change volume while flying. The existing Cave speed
setting also applies, but automatic beat-driven acceleration does not.

The camera follows the player across the generated cross section, without pulling
back to the automatic centerline. Connected, forward-going branches can be entered;
this does not create new branches or permit backward flight. A tight, rotating
bounding box encloses the visible hull and wings with 0.006 world units of padding
per side and checks the generated wall triangles in small movement steps,
including edges and corners. The ship now uses the tunnel's world coordinates,
projection and depth buffer rather than a separate camera-space projection.
Contacts remove the inward movement component, allowing tangential sliding;
genuinely blocked passages can still stop forward progress. The following camera
shortens its chase distance before entering walls. The box still encloses empty
space around the tapered nose; it is not an exact hull mesh.

The SNES Star Fox-inspired **SHIELD** gauge is at the bottom left, with points
at the top left. A flight starts at 100 shield. A wall hit costs 20, followed by
one second of damage protection; scraping cannot apply damage every frame.
The initial 0.85-second ship materialization is protected too. Existing recoil,
sliding and escape assistance remain active. Each active survival second earns
one point; pausing music freezes the flight and scoring.

At zero shield, the ship disappears in a short particle explosion and **Game
Over** appears. Five seconds after the fatal hit the ten best scores are shown;
X or Circle returns to ordinary Monkey. Music continues throughout. The intro's
Hall of Fame can be viewed without starting a flight and returns to the intro.
Abandoned flights do not enter the table. Start still stops music. Square cannot
switch visualizations while the flight intro/game/results are open; leave with
L+R (five seconds), or use the menu, before changing the visualization.

Scores are stored locally in `ms0:/PSP/SYSTEM/PSPStreamer-flight-a.dat` and
`PSPStreamer-flight-b.dat`. Alternating checksummed generations preserve the
previous valid table if a write is interrupted. Updates do not replace these
files. A write failure is shown on the result screen; the in-memory table still
works. No server update is required.

Hold **X** for yellow pulsing blaster bolts: one shot every **500 ms** at most.
There is no delayed burst after a slow frame. The player has 100 health and
takes **10 damage per enemy projectile**. Enemies take **exactly three hits**
to destroy (one third per hit, no rounded 1% remainder); a kill earns **100 points**. The full
double-tap barrel roll protects against projectiles, not wall damage.

Every 15–20 simulation seconds, the game attempts to place an enemy with room
to pass. Successful spawns alternate between a moving ship drone (first) and
a stationary turret. Failed attempts keep the same requested type instead of
rerolling it. Drones prefer the floor but can spawn higher if the floor is too
tight; they use a ship other than the player's, with inverted colors.
An unsuitable site leaves **one pending spawn**, retried at most once per second.
Missed opportunities never accumulate; a successful spawn restarts the 15–20
second interval. At most one enemy and 32 projectiles exist
at once; enemies left behind are removed. Each enemy
turns toward the player after a short line-of-sight delay, and fires at most once
every **500 ms** when aligned. Walls block shots. Music pause freezes combat.
All selectable ships currently share health, speed and weapons; hull collision
bounds follow the chosen model. These are choices, not upgrades.

Destroyed enemies use the player's 2.5-second particle explosion at the kill
position. They still award 100 points exactly once.

Colliding with an enemy removes **30 player shield points** and **60% of the
enemy's maximum health**, once per contact rather than every frame. A swept
hull check catches crossings between frames. Existing damage protection is
respected; barrel rolls still block shots, not physical hull collisions.
Three blaster hits still destroy a full-health enemy. Ram kills use the same
explosion and scoring path as blaster kills.

Monkey's Circle options include separate **Music rumble (%)** and **Impact
rumble (%)** sliders, initially 0 (off). Music drives a soft bass/beat response;
wall scraping is light, frontal wall impacts stronger, and enemy collisions
strongest. Received blaster hits also vibrate. The variable motor follows the
configured strength; the binary small motor adds a kick only for strong impacts.
Pause, leaving Monkey and stopping playback stop output. Stalled updates expire
after 250 ms in the bridge; firmware motor effects are also time-limited.

Update **EBOOT.PBP, PSPStreamer.prx and StreamMasterUSB.prx together**, plus
**PSPConsolizerUSB.prx** if the resident plugin is installed, and restart the PSP.
The existing POPS-rumble-capable ESP firmware is sufficient; no new flash is
needed. Supported controllers are the same as for POPS rumble (tested backend:
SF30 Pro in XInput mode); generic wired adapters do not gain guessed output
protocols. The native app submits an 8-byte cached command at most 20 times/s;
the existing controller USB reply carries it, without extra network requests,
blocking USB exchanges, worker threads or audio-thread work. Native and POPS
motor values have separate caches, so the resident POPS service cannot overwrite
the app's output. This is deliberate native-app feedback, not PSP game rumble
emulation.

A neon-green **Astro Shield** pickup floats on the automatic tunnel path. Fly
through its face to restore **66 percentage points of shield**, capped at 100.
The first placement is attempted after 25 simulation seconds; collecting or
passing it starts a 35-second wait. If the tunnel is too narrow, placement
retries every three seconds, keeping at most one item. Music pause freezes these
timers. The imported rest-pose model has 42 triangles and an embedded 128×128
honeycomb texture; no additional PSP asset file is needed. See
[Astro Shield attribution](../psp-client/assets/shield/CREDITS.md).

In normal Monkey mode, Circle → **Autopilot ship** optionally displays model 2
with its music-reactive engines. `cave_autopilot_ship=0` is the default; set it
to `1` to enable the cosmetic ship. Camera motion stays unchanged, and this does
not activate enemies, pickups, damage, scoring or flight controls. The chosen
Easter egg ship is preserved independently.

All eight supplied GLBs were imported. The enemy turret uses **613 triangles**
and a model scale of 0.60 versus 0.45 for the player (one third larger relative
to the common normalized maximum dimension). It comes from the original
Kirilllucas asset, the same source as the user's turretMin variants.
The seven playable ships use 234–592 triangles.
Large meshes are simplified; embedded texture colours are baked into vertex
colours, not full-resolution textures. Normal/metallic maps and partial alpha
are not reproduced. The original ship retains its four music-reactive exhausts;
the other models do not yet have individually tagged exhaust locations.
Original GLBs, attribution and per-model conversion counts are in
[flight model credits](../psp-client/assets/ships/CREDITS.md).
Regenerate with `python3 tools/import_cave_models.py psp-client/assets/ships psp-client/cave_models_data.h`
(requires NumPy and Pillow). The meshes are embedded in EBOOT/PRX, so there are
no additional model files to copy to the PSP.

Flight starts disabled, is not saved, and is reset when the renderer is closed
(including opening its options). When disabled, flight performs no field checks
or mesh draw calls and does not alter the normal camera, timing or random state.
The ship mesh and generated intro logo are embedded; no extra image needs
copying to the memory stick. See [logo provenance](../psp-client/assets/monkey-flight-logo.md).
“Low Poly Spaceships” is by Samuel Metters, CC BY 4.0; see
[asset attribution and conversion details](../psp-client/assets/cave_ship.CREDITS.md).

</details>

Legacy `fShader` hue shading now uses fixed-function corner colors; compare
`legacy-shading-demo.milk` and `legacy-shading-off-demo.milk`. Both also exercise
echo zoom below 1. This is not HLSL shader support.
Numbered formula entries now compile together, including multiline loops,
comments and split tokens. Try `multiline-formula-demo.milk`.
The [collection audit](../docs/MILKDROP_MULTILINE_AUDIT.md) separates successful
loading from host-side formula execution and actual PSP playback testing.
The [parser/density update](../docs/MILKDROP_PARSER_DENSITY.md) adds larger compiled
formula blocks, empty-statement/point-input compatibility, a 16×16 warp grid
with a budget-aware fallback, and up to 1024 custom-wave points. Test with
`extended-formula-demo.milk`, `fine-mesh-demo.milk`, and `dense-wave-demo.milk`.
The [phase-budget update](../docs/MILKDROP_PHASE_BUDGETS.md) accelerates recognized
init zero-fill loops, separates once-per-frame work from point-call limits,
and reduces dense custom-wave point counts within the unchanged total frame
budget. Try `phase-memory-demo.milk` and `wave-budget-demo.milk`.
The latest [import/resource update](../docs/MILKDROP_RESOURCE_LIMITS.md) raises
file/formula capacity, adds sparse local memory and compact uniform fills,
and gives expensive setup/main-frame formulas a separate, bounded allowance.
Shared memory has 16384 resident floats; each context has 8192 local floats.
Both address spaces span 1048576 logical cells. Writes requiring another page
after the resident pool fills are discarded, without aliasing existing data;
this deliberate PSP approximation can change a preset's appearance.
Try `local-sparse-fill-demo.milk` and `sparse-global-demo.milk`.
No server or OC-plugin update is required.
The **[current MilkDrop compatibility guide](../docs/MILKDROP_COMPATIBILITY.md)**
is the authoritative feature/limit summary; older linked guides describe
historical batches. The latest batch adds EEL operators, assignment expressions,
loops, shared registers, local/global memory, engine dimensions and monitor
state. Try `eel-memory-orbit-demo.milk`, `eel-grid-logic-demo.milk` and
`eel-wave-loop-demo.milk`. External PNG textures now have a bounded,
fixed-function baseline: try `external-texture-demo.milk` together with
`presets/textures/checker.png`. See the compatibility guide for supported
dimensions and the `psp_texture_0`–`psp_texture_3` extension. Shaders and their
arbitrary texture sampling remain excluded. Numbered warp/composite shader
source is silently skipped so supported non-shader parts can run. Other preset
errors still report normally; shader-heavy presets can look different or blank.
Try `shader-fallback-demo.milk` to check this fallback. Building the PSP client now also
requires PSP libpng, libjpeg and zlib (host rendering tests require their host
libraries; texture fixture tests also use Python Pillow). JPEG/JPG textures are
supported alongside PNG via `psp_texture_0=example.jpg` in the preset and a
`textures/example.jpg` file beside it. JPEG images are resized once on loading;
see the texture limits in `docs/MILKDROP_COMPATIBILITY.md`.
During music, **Circle** opens the in-app preset browser: Up/Down selects,
L/R pages, Cross opens folders or applies a file, Circle cancels. The `..` entry
goes up one folder. The same folder browser is available without playback at
**Select → MilkDrop preset → X**; **Start** in the outer settings saves that
selection. Paths are relative to `PSPStreamer/presets/`, e.g.
`music_preset=Geiss/Hyperdrive.milk`. Absolute paths and `..` components are
rejected. Automatic changes use the selected preset's folder and its optional
`playlist.txt`, not the entire folder tree. Textures remain relative to each
preset's own directory. Manual `active.milk` replacement is no longer required.
The browser lists up to 128 entries per directory (including `..`) and warns
when that limit is exceeded. All built-in waveform modes 0–8, a real FFT waveform,
bounded motion vectors and `frame`/`fps` inputs are now available. See the
[waveform pack, controls and test presets](../docs/MILKDROP_WAVE_PACK.md).
Frame formulas can also animate `wave_mode` and all nine `mv_*` fields.
Try `wave-switch-demo.milk` in the preset browser; see
[dynamic waveforms and motion vectors](../docs/MILKDROP_DYNAMIC_WAVES.md).
Custom shapes now have separate bounded init/frame formula contexts, including
`t1`–`t8`, current preset `q` inputs and persistent named variables. Try
`shape-orbits-demo.milk`; see [shape formulas and limits](../docs/MILKDROP_SHAPE_FORMULAS.md).
Custom PCM waves support separate init/frame/point formulas, stereo inputs,
per-point colors, dots, thick lines and additive drawing. Try
`custom-wave-demo.milk` and `custom-wave-fft-demo.milk`; see
[custom-wave controls and compatibility limits](../docs/MILKDROP_CUSTOM_WAVES.md).
Custom waves also accept stereo FFT data (`bSpectrum=1`). Line waves now use
MilkDrop-style spline subdivision; dots retain their original points. The
fixed-function image effects (brighten, darken, solarize, invert and darken
center) are available as static fields and frame formulas. See the
[spectrum, smoothing and image-effects batch](../docs/MILKDROP_SPECTRUM_EFFECTS.md)
for test presets, memory changes and the remaining compatibility gaps.
The [native-input audit and current work list](../docs/MILKDROP_NATIVE_INPUTS.md)
describe context-local writable EEL inputs and the `native-inputs-demo.milk`
test preset. Older milestone reports retain their historical scope.
For shapes crossing the image boundary, see [offscreen shape clipping](../docs/MILKDROP_OFFSCREEN_SHAPES.md)
and test `offscreen-shapes-demo.milk`.
The completed [wave/transform range package](../docs/MILKDROP_GEOMETRY_RANGES.md)
has one combined hardware test: `geometry-range-demo.milk`.
The next batch adds up to eight instances per shape, 512-point custom waves,
read-only `instance`/`instances`/`progress` inputs, optional ordered/random/rated
preset cycling, playlists and live shaderless transitions (optional snapshot fallback). Try
`shape-instances-demo.milk` and `large-wave-demo.milk`; details and resource
limits are in the [automation guide](../docs/MILKDROP_AUTOMATION.md).
See [supported fields, time formulas and limits](../docs/MILKDROP_PRESETS.md).
For animated rotation and colors, copy `presets/time-demo.milk` over
`presets/active.milk` on the PSP, then stop and restart music.
For music-reactive transforms and colors, use `presets/music-demo.milk`
instead. It uses the existing PSP spectrum/VU snapshots, with optional
time-based smoothing; no additional audio analysis is required.
`presets/relative-demo.milk` uses the original `bass/mid/treb` and `*_att`
names with relative-to-history envelopes. Their PSP frequency-analysis
approximation and `min/max/sqrt` formula support are documented in the subset guide.
The [Hyperdrive target subset](../docs/HYPERDRIVE_TARGET.md) additionally supports
the real circular PCM waveform (mode 0), `dx/dy`, texture clamp and gamma
brightness. Copy your original preset as `presets/active.milk`; third-party
presets are not bundled in Git. This is not full MilkDrop compatibility.
Visualization feedback now renders at **512×512**, using RGB565 on LCD and TV.
TV preserves raw feedback in a 512 KiB main-RAM buffer using a GPU copy; if that
allocation fails it retains 512×256 feedback. LCD uses two EDRAM surfaces.
Echo/brightness are composed offscreen before presentation;
see [memory layout and hardware-test notes](../docs/MILKDROP_PRESENTATION.md).
The [extended fixed-function subset](../docs/MILKDROP_FIXED_FUNCTION.md) adds
animated transform centers/stretch, wave styling, echo, borders and four
static custom shapes (including feedback-textured shapes). Try
`receiver-fx-demo.milk` and `echo-dots-demo.milk` as the active preset.

Custom shapes also support `thickOutline=1`. The `outline-demo.milk` preset
compares thin (left) and thick (right) cyan outlines. See
[shape-outline rendering and testing](../docs/MILKDROP_SHAPE_OUTLINES.md).

`nWaveMode=4` adds the stereo-driven horizontal script waveform alongside
the existing mode 0 circle. Try `script-wave-demo.milk`; see
[waveform behavior and hardware checks](../docs/MILKDROP_SCRIPT_WAVE.md).

`nWaveMode=1` adds the stereo spiral; try `spiral-wave-demo.milk`.
See [spiral waveform details](../docs/MILKDROP_SPIRAL_WAVE.md).
The [conditional formula subset](../docs/MILKDROP_CONDITIONS.md) adds lazy `if`,
Boolean functions and additional math; `branch-beat-demo.milk` demonstrates
music-reactive changes of wave style, color, zoom and echo intensity.
The [initialization/q-variable subset](../docs/MILKDROP_INIT.md) adds
`per_frame_init_*` and `q1`–`q32`; try `init-orbit-demo.milk` as the active file.
Up to 128 [named persistent variables](../docs/MILKDROP_COMPATIBILITY.md) per context can retain
state between frames; `memory-pulse-demo.milk` holds and releases bass impulses.
Bounded [per-grid-point formulas](../docs/MILKDROP_GRID.md) add local transforms;
try `grid-twist-demo.milk`. This is an interpolated 8×8 mesh, not pixel shaders.
**Triangle** (or **Cross + Triangle**) toggles receiver view and true visualization fullscreen
(480×272 LCD / 720×480 TV). Fullscreen hides the receiver controls, but
pause, volume and remote commands still work. Without an active visualization,
the existing enlarged spectrum/receiver layout is unchanged.
This adapts MilkDrop 2's no-shader
warp equations to PSP GU, not its complete desktop engine: arbitrary `.milk`
files and shader presets are not guaranteed compatible. The custom slot now
supports the reference's public expression function/operator families within
documented PSP limits. Music remembers the selected effect
and fullscreen across track changes, including autoplay/shuffle and remote
selection. Resources are still released and recreated per track. Video uses
its own presentation mode; restarting the app resets music visualization to
off. No configuration change or server update is required.
Further references are in [docs/VISUALIZATIONS.md](../docs/VISUALIZATIONS.md).

## Playlist quality and comparison build (server 0.1.64)

The web **Playlist** now shows its next entry and total duration for one pass,
with unknown durations marked separately. Drag the row handle to reorder, or
keep using the arrow buttons. Each row can save audio quality and video frame
rate; music rows hide frame rate. **PSP default** inherits the player's settings
without carrying over the previous entry's override. Saved choices apply to
web/PSP starts and automatic continuation. See [Playlist](../docs/PLAYLIST.md).

The PSP release now defaults to **O3 + LTO**, following successful hardware
tests. The plain O3 fallback uses the same source, settings and assets; no
firmware/plugin changes are required. Build LTO in an isolated object tree with
`bash tools/build_psp_lto.sh /path/to/PSPStreamer-LTO` (PSP SDK on `PATH`). Copy
**both** its `EBOOT.PBP` and `PSPStreamer.prx` into the normal PSPStreamer folder
to install; preserve your CFG/plugins/assets. A plain O3 build uses
`make clean` followed by `make LTO=0`. Do not mix object files between build
modes; clean before changing compiler flags. See the historical
[comparison and focused verification](../docs/PLAYLIST_LTO_0164.md).

The GUI now shows a compact connected-network name and four signal bars in
the upper right on LCD and TV. Long names are shortened. Native Wi-Fi uses
the PSP's signal percentage; StreamMaster uses live SSID/RSSI from firmware
supporting `SM_NETWORK_INFO`. Bars are qualitative, not directly comparable
measurements between adapters. Queries are throttled to at most once per five
seconds in the existing GUI worker; there is no AP scan or extra HTTP poll.
They pause during media playback, and stale samples display `Wi-Fi --` when
redrawn. The indicator is not a fullscreen video overlay.
LCD menu text retains its 6×8 footprint and all existing layout bounds, but
now blends the font atlas's alpha edges instead of applying a binary threshold.

This version also batches tiny PSP-side TLS reads and uses Allegrex word-byte
swapping for offline SHA-256 input. Complete read-back verification stays intact;
StreamMaster firmware, ESP TLS, USB buffers and clock profiles are unchanged.

## New queue and episode conveniences (server 0.1.63)

- **Play next** in the web library, media controls or playlist schedules one
  item after the currently reported track. It does not stop playback. The shared
  queue is enabled; a current folder item is added to it when necessary. Existing
  entries are moved, not duplicated. The explicit choice takes precedence over
  Repeat One once, and shuffle keeps the adjusted order. Subsequent playback
  follows the shared queue, not the original folder; disable queue mode to return
  to folder continuation. Local-only PSP files and live radio are not supported
  as the current queue anchor.
- Open **Plex/Jellyfin → Provider views** on the PSP for Continue Watching,
  Recently Added, Unwatched and Collections. The latter three first ask for a
  library. Pages reuse the cancellable browser request, without playback polling.
- In a video's PSP options, **Triangle** saves its audio/subtitle choices for
  the series (or the containing folder for direct files). With unchanged saved
  choices, Triangle removes that preference; changing tracks and pressing
  Triangle updates it. The web media page also has explicit Save/Forget buttons.
  Languages and track titles are matched in each episode, not old stream indices.
  Missing audio falls back to the first track, missing subtitles to Off. Explicit
  playlist track choices take precedence. DLNA lacks reliable series identity
  and keeps its existing global defaults.
- **Next video delay** in PSP settings / `next_episode_seconds=0` in the config:
  zero keeps immediate continuation; **1–30** enables a countdown after natural
  video completion. **X** starts immediately, **O or Start** cancels. Manual
  next/previous, reconnect and music do not wait. It works for online and local
  videos; new server playback commands take precedence for online playback.
- The flight Easter egg now mixes stationary turrets with bounded, floor-hovering
  waypoint drones. A drone uses another selectable ship, with inverted colors.
  Firing rate, three-hit enemy health, points and barrel-roll protection remain
  unchanged. There is still only one active enemy and one deferred spawn.

`series-preferences.json` is saved atomically alongside `playlist.json` in the
existing persistent server state directory. Keep that Docker volume; the HA
app uses its persistent data directory. No StreamMaster firmware update is
needed for these conveniences.

### PSP Dolby Surround downmix

Settings → **Audio downmix** offers Stereo (default), Dolby Surround and Dolby
Pro Logic II. The CFG key is `audio_matrix=none`, `audio_matrix=dolby` or
`audio_matrix=dplii`. The server folds a multichannel source into matrix-encoded
stereo; the PSP still receives its established 44.1-kHz MP3 stream. This is not
Dolby Digital/DTS passthrough and does not create discrete surround from stereo.
A compatible receiver must decode the stereo signal in its Surround/Pro Logic
mode. Headphones and the PSP speakers do not provide discrete surround.

The web PSP controls also offer Audio downmix and remember the selection.
New offline conversions include the choice and keep different mixes separate;
existing downloads must be reconverted. Server/Home Assistant app 0.1.78 and a
matching new PSP build are required. Decoder, PTS clock, volume and video settings
are unchanged. Xbox retains its separate downmix setting.


## License and reference

GPL-2.0-or-later. See [LICENSE](../LICENSE) and [NOTICE](../NOTICE) for the PPA attribution and pinned reference revision; the original BSD notice is preserved in [LICENSE.BSD](../LICENSE.BSD).

The adapted MilkDrop 2 equations retain Nullsoft's
[BSD-3-Clause notice](../licenses/MilkDrop2.txt). Include that notice when
redistributing binaries containing the prototype.

## Security

### One shared password (server/HA app 0.1.24)

Set the HA app option `password`, or use **Server settings** in the Docker WebUI.
`PSP_STREAMER_PASSWORD` bootstraps Docker; a saved WebUI password takes precedence
and survives restarts in the `/data` volume. HA still manages its password via
its app options and requires a restart after changes there.
All media, metadata, subtitle and control routes require authentication when
the password is nonempty. Only the login page and its static assets are public.
An empty password preserves unauthenticated LAN use; it is **not safe for WAN**.

Put the identical password in the PSP's Settings screen or `server_password=` in
`ms0:/PSP/SYSTEM/PSPStreamer.cfg` (restart after manual file edits). Use a single-line
password of at most 128 UTF-8 bytes; a long random ASCII password is easiest to
enter consistently. The client preserves this setting when saving volume or
other preferences. The password is stored in plaintext on the Memory Stick.
Do not commit your real config or put credentials into URLs.

The website has a password-only login page and **Sign out**. Browser sessions
expire after 12 hours and are invalidated by logout, a WebUI password change,
or server restart. Cookies use HttpOnly, SameSite=Strict and Secure on HTTPS;
state-changing requests require same-origin JSON and the session's CSRF token.
Protected responses are non-cacheable. Login attempts are limited per connecting
IP (a reverse proxy may share this allowance). The native PSP and API clients
continue to use HTTP Basic with fixed username **psp** and the same password.

### Long playback pauses (server/HA app 0.1.39)

Update both the server and PSP client for long MP3/video pauses. The client
reports its actual pause state through the existing remote-control poll, for
both filesystem and Plex media. A fresh report lets the server wait while the
PSP's receive buffer is full, instead of closing the stream after 180 seconds.
Resume continues the same byte stream without restarting the decoder. A pause
report expires after 45 seconds without updates; ordinary write-inactivity
limits then apply again. This does not change subtitle startup timeouts and
cannot prevent a router or reverse proxy from enforcing its own idle timeout.
Radio retains its separate pause/reconnect-to-live behavior.

### Web navigation (server/HA app 0.1.38)

Server/HA app **0.1.49** adds a playback-following seek bar for the selected
media item. It uses PSP position reports, interpolates briefly while playing,
and stops extrapolating when reports become stale. Paused/buffering reports do
not advance. Dragging the slider or waiting for a seek is not overwritten by
old telemetry; choosing a different file still allows preparing its start position.

Chapter ticks and a chapter selector offer jumps during playback, or select the
start position before Play. Chapters come from the original container (including
mounted files) or Plex/Jellyfin metadata. **Skip intro** and **Skip credits** appear
only while the current position lies inside a supplied segment. No automatic
skipping or local intro detection is performed. Plex must supply analyzed markers;
Jellyfin must expose Intro/Outro entries through its MediaSegments API (for example
from a segment provider). Older Jellyfin servers or missing segments simply offer
no skip button. Optional Jellyfin segment lookup is bounded and browser-only;
PSP metadata responses exclude these larger timeline arrays.

Protocol references: [PlexAPI marker/chapter fields](https://python-plexapi.readthedocs.io/en/latest/_modules/plexapi/media.html)
and [Jellyfin MediaSegments controller](https://github.com/jellyfin/jellyfin/blob/master/Jellyfin.Api/Controllers/MediaSegmentsController.cs).
Docker and the Home Assistant app contain the same implementation.

The PSP retries transient directory-loading failures up to three attempts,
with a 15-second budget per attempt including bounded DNS lookup and a one-second
pause between attempts. The UI shows the attempt and total elapsed time;
Circle cancels and failed loads retain the previous folder listing. Permanent
HTTP client errors (such as 401/404) are not retried. This directory-only policy
does not shorten metadata, subtitle-preparation or playback timeouts. Firmware
cleanup must finish before another request starts; threads are never forcibly killed.

**Library** groups mounted media roots under **Files**, alongside **Plex**, **Jellyfin** and
**Internet Radio**. Sources can be disabled in **Settings**. The Sources button
always returns to that overview; returning from a filesystem folder no longer
loses other sources. DLNA is not implemented yet.

**Remote control** shows Pause/Resume/Stop even without a selected file, but
track/quality/seek/download options appear only after selection. Music hides
video and subtitle controls; live radio also hides seeking and downloads.
**Downloads** contains conversion previews and the persistent queue.
**Settings** contains source switches, Plex/Jellyfin connections, radio stations and the password.
The language selector switches between English and German and is remembered
in that browser; the PSP language setting is independent. Web translations
are in `static/i18n.js`. Docker and Home Assistant ship identical web assets.

**A password is not transport encryption.** HTTP remains available and is the
default. Basic credentials are only Base64 encoded on HTTP
([RFC 7617](https://www.rfc-editor.org/rfc/rfc7617)). Optional PSP HTTPS now covers
media, subtitles, metadata and remote polling. It automatically records and
accepts server certificates, showing a notice when they change, as requested
for this project. This is encrypted transport, **not verified server identity**:
CA trust, hostname and expiration are not enforced. An active interceptor can
present a replacement certificate and receive credentials. No silent downgrade
to HTTP occurs. Use a trusted/VPN path when that risk is unacceptable.

See [HTTPS and settings setup](../docs/HTTPS_AND_SETTINGS.md). Browser HTTPS should
use a normally trusted certificate. Docker and HA media sources/web assets are
checked for equality in the test suite; both include mkvtoolnix for MKV PGS.

## Optional Onju V3 optical audio

Settings → **Audio output** selects the existing PSP output (default), S/PDIF
stereo PCM, or AC-3/DTS core passthrough with an explicit PCM/AC-3 fallback.
The same selector is in the server's web PSP controls. This needs the wired
TOSLINK transmitter, the matching app, server **0.1.79** and Onju firmware
**0.3.18**. Initial scope: network media files, not cached local files or radio.
It is not a system-wide PSP sound plugin. Hardware verification is pending.

CFG: `audio_output=psp`, `spdif_pcm`, `spdif_auto_pcm` or `spdif_auto_ac3`.
Compressed passthrough uses receiver volume and does not feed decoded audio
to visualizations. The PCM mode supports PSP volume and visualizations.
See [wiring, setup, formats and timing](../streammaster/SPDIF.md).

## Optional PSP game hit feedback

PSP Consolizer also offers opt-in health-triggered rumble profiles, edited in
PSPStreamer under **SELECT -> Plugins -> PSPConsolizer: Game rumble**.
This reads a verified health value; it does not modify games or automatically
discover addresses. [Configuration and address-finding guide](../psp-controller/RUMBLE.md)
explains all fields, optional pointer/battle checks and the disabled defaults.
