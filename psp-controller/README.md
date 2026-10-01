# StreamMasterPad 0.1 — experimental PSP-wide controller input

First hardware-test build for PSP 6.61 / ARK. **Not yet hardware-validated.**
No Rumble, video-output changes, CPU clock changes or ESP firmware update.
Keep the tested Onju `0.3.10-bt-qio80-iram` firmware and its saved controller.

## Install and first test

1. Pair/map the controller in PSPStreamer first, as before.
2. Copy `SEPLUGINS/StreamMasterPad/` onto the Memory Stick. Keep both PRX files
   together with `StreamMasterPad.ini` in that directory.
3. Use the companion test PSPStreamer app: copy its EBOOT.PBP, PSPStreamer.prx
   and StreamMasterUSB.prx into the app directory. Do not copy the plugin's
   resident bridge into the app directory: the two variants share source, but
   only the plugin variant starts the global controller service.
4. Add this line to ARK's active PLUGINS.TXT, without replacing existing lines:

   ```text
   game, ms0:/SEPLUGINS/StreamMasterPad/StreamMasterPad.prx, on
   ```

   GAME covers PSP games and homebrew, including PSPStreamer. Initially test one
   game; do not enable VSH/POPS globally. ARK per-title plugin rules can narrow
   activation further. Do not combine with RemoteJoy or other controller/USB
   replacement plugins. In-game USB features may conflict; exclude those apps.
5. Restart the app/game after changing plugin activation. With the powered
   StreamMaster adapter/hub attached, turn on the saved controller. Allow a few
   seconds for USB and Bluetooth startup. Test d-pad, face/shoulder buttons,
   Start/Select and analog movement with no PSPStreamer running.
6. Disconnect/reconnect the controller. Buttons must release; physical PSP
   controls must remain usable. Test leaving the game through the physical PS
   menu. Then test PSPStreamer pairing-menu capture, playback and an HTTP download.

**Emergency:** hold the physical PSP NOTE + VOLUP buttons for two seconds.
External input is disabled until the next application launch, including the
app's direct snapshot path. This does not forcibly interrupt streaming USB.
If a title fails, disable the ARK plugin entry and restart; the untouched normal
PSPStreamer release is the app rollback. The firmware does not need reflashing.

Logs: `ms0:/SEPLUGINS/StreamMasterPad/loader.log` and `last.log`. They are reset
on each plugin launch. Collect them before launching another GAME application
if the earlier game is the failing case. No per-frame log writes.

## Settings and limitations

`StreamMasterPad.ini` is read at each game/app launch. No INI editor in PSPStreamer
yet; pairing, button mapping, analog selection and device reconnect remain in
PSPStreamer and are persisted by the unchanged ESP firmware.

- `enabled=1`: global injection/automatic bridge start; `0` disables those.
- `vsh=0`: XMB is disabled for the first test. An explicit VSH plugin entry and
  `vsh=1` are both needed to opt in later; XMB compatibility is untested.
- `allow_path=...`: up to eight case-sensitive substrings of the launch path;
  if present, only matches receive global input. No wildcard syntax.
- `exclude_path=...`: up to eight launch-path substrings; exclusion takes priority.
  Disc boot paths need not contain the game ID: use ARK's per-title rules for
  ID-based selection rather than assuming this path filter is an ID filter.
- POPS/PS1 is deliberately disabled. Analog output follows the game's sampling
  mode. Centered external analog input leaves physical analog input alone;
  off-centre external input takes priority. No second-stick mapping is added.
- Physical PS, brightness and volume buttons remain physical; the present wire
  format carries the twelve mapped game buttons and one analog stick.
- Sony emulation slot 3 is used. Do not use another emulation plugin on that slot.

## Design and evidence

The ESP already sends mapped input over vendor EP0 requests. The resident bridge
uses those cached values; it creates no network requests or per-button workers.
One 100 Hz service refreshes finite Sony emulation values and handles stale data,
physical escape and suspend. It does not patch syscalls, game code or framebuffers.

The loader loads Sony USB before the resident bridge's USB imports. The bridge
is compiled directly from the existing transport source. PSPStreamer identifies
it through a versioned local devctl, claims transport lifecycle ownership and
does not unload the resident module. While PSPStreamer is present, global
injection is suppressed; its original direct input/learning path remains owner.
The resident service does not force another USB function off to take the bus.

API semantics/NIDs were checked against the uOFW reference:
- https://github.com/uofw/uofw/blob/master/include/ctrl.h
- https://github.com/uofw/uofw/blob/master/src/kd/ctrl/ctrl.c
- https://github.com/uofw/uofw/blob/master/src/kd/ctrl/exports.exp

Related external-controller reference (reviewed, not copied):
https://github.com/crozone/PSP-EmulatedControllerTest

Sony API resolution is checked at runtime; missing exports or power callback
availability disable global injection rather than attempting blind patches.
This does not establish universal game/CFW compatibility. Suspend/resume and
USB-using games still require hardware validation.

## Status overlay

`overlay=1` (default) displays connection changes and USB-start error codes for
five seconds in the upper-right corner, clear of StreamerOC's upper-left OSD.
`overlay=0` disables it; `overlay_always=1` keeps it visible. The cached input
channel supplies connected/disconnected status, not the controller's name, so
this first version uses the label `STREAMMASTER BT`. Pairing names remain in
PSPStreamer's Bluetooth menu. No extra USB requests are used for the overlay.

This is a best-effort framebuffer overlay without a display hook: some games
may overwrite it or cause flicker. It checks framebuffer layout and suspension
before drawing and never waits for VBlank. Early loader/API failures remain in
the logs if the service cannot start. TV placement still requires hardware testing.

## Build

With PSPSDK on PATH:

```sh
make -C psp-controller
make -C psp-controller/bridge
make -C psp-client/streammaster_usb
make -C psp-client
```

The two plugin modules use O2, as other kernel modules do; the app retains O3/LTO.
The power-callback slot helper is reused from StreamerOC (MIT); the new plugin
and common USB bridge are GPL-2.0-or-later. See the repository licenses.
