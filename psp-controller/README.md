# PSP Consolizer 0.2 — StreamMaster controller plugin

### Current regression diagnostic build

The disabled overlay previously still reserved three framebuffer backups in
kernel BSS (152,688 bytes). The resident bridge's fixed footprint had grown from
228,756 bytes in 0.1 to 339,192 bytes in 0.2. It is now 187,340 bytes: OSD storage
is allocated only when enabled (one backup for polling, three for presentation).
Allocation failure disables the OSD rather than controller input. Storage is
released only after the worker and registered callbacks have stopped.

The latest PSPStreamer log failed at Sony `Network Common` module loading
(`800208D9`), while StreamMaster settings exchanges, a transfer benchmark and
HTTP succeeded. That is not evidence of a failed USB connection. The plugin now
logs free kernel memory and the largest block before OSD initialization, plus
free memory afterwards. These values are hexadecimal byte counts.

This fixes a confirmed unconditional-memory regression, not yet a confirmed
explanation of the game shutdowns. Keep the current `overlay=0`, `tvout=0` and
`metadata=0` for the first hardware test. No clock, controller emulation or
firmware changes accompany this build. Existing INIs are preserved.

Use a controller paired with StreamMaster in PSP games, homebrew, and optionally
VSH or POPS. **0.1 input/reconnection passed the user's Soul Calibur test. The
0.2 status, overlay cooperation, TV activation, VSH and POPS need hardware tests.**
No Rumble. Wi-Fi/media transport remains available to PSPStreamer.

The legacy `StreamMasterPad` filenames and directory deliberately remain valid.
The module/display name is now PSP Consolizer. Do not install it twice under
different names. Pairing and button/analog learning remain in PSPStreamer.

## Installation

Copy both files into `ms0:/SEPLUGINS/StreamMasterPad/`:

- `StreamMasterPad.prx` (loader)
- `StreamMasterUSB.prx` (resident USB bridge/controller service)

Copy `StreamMasterPad.ini.example` as `StreamMasterPad.ini` and adjust it. Use
the companion PSPStreamer EBOOT.PBP, PSPStreamer.prx and **app** StreamMasterUSB.prx.
Do not interchange the app and resident bridge binaries. Install the companion
StreamerOC.prx too if using both plugins with presentation-hook overlays.

Add the desired contexts to ARK's active `SEPLUGINS/PLUGINS.TXT`:

```text
game, ms0:/SEPLUGINS/StreamMasterPad/StreamMasterPad.prx, on
vsh, ms0:/SEPLUGINS/StreamMasterPad/StreamMasterPad.prx, on
pops, ms0:/SEPLUGINS/StreamMasterPad/StreamMasterPad.prx, on
```

GAME includes homebrew. VSH and POPS are optional, newly enabled test targets,
not proven compatible. Keep a way to disable plugins through ARK recovery if
VSH fails. Do not combine with another USB/controller replacement plugin using
the same USB function or Sony emulation slot 3. Exclude USB-using titles if needed.

For controller name/battery metadata flash StreamMaster **0.3.12-bt-qio80-iram**
and opt in with `metadata=1` (currently off for regression isolation).
Basic input continues to work with 0.3.10, but names/battery are unavailable.
Use the separate bootloader/partition/app `flash_args` to preserve NVS pairing
and Wi-Fi profiles. Writing a merged factory image can erase those settings.

## INI (read on plugin launch)

```ini
enabled=1
vsh=1
pops=1
overlay=1
overlay_always=0
tvout=0
metadata=0
# allow_path=/ISO/MyGame.iso
# exclude_path=/PSP/GAME/UsbApp/
```

- `enabled`: global input enable.
- `vsh`, `pops`: permit those contexts; their ARK entries are also required.
- `overlay=0`: off; `1`: compatible framebuffer polling; `2`: presentation hook.
- `overlay_always=1`: permanently visible; default 0 shows state changes for 5 s.
- `tvout=0`: off; `1`: request TV when a controller is connected; `2`: on launch.
- `metadata=0`: original input-only EP0 traffic; `1`: advertise optional name/
  battery status support. Requires 0.3.12 or later: 0.3.11 sends metadata without
  this capability check. Basic controller input remains independent.
- Up to eight case-sensitive `allow_path` and `exclude_path` substrings filter
  the launch path. Use ARK per-title rules for disc-ID based exceptions.

The overlay is upper-right, StreamerOC remains upper-left. It shows the reported
device name when available, connection/input owner, USB/BT errors and battery
percentage **only if the HID report actually exposes Battery Strength**. No
voltage-based guesses or invented battery values. Unknown battery is `--` in
PSPStreamer's Bluetooth menu. Long names are shortened; the plugin uses its small
ASCII diagnostic font. Scan RSSI is not shown as a live signal-strength reading.

Mode 2 cooperates with the updated StreamerOC display hook rather than replacing
it with a competing patch. Without OC it installs its own hook. When OC has not
published its driver yet, registration retries once per second for at most 20 s.
An unavailable/incompatible hook disables the OSD, not controller input. It does
not silently substitute polling for mode 2. Kernel/direct presentation
paths can still require polling; neither mode guarantees flicker-free output in
every game. The next discussion is a more stable rendering alternative.

Regression investigation: the first 0.2 game test produced black screens/forced
shutdown with USB connected, even with `tvout=0` and VSH activation removed.
The log confirms early OC registration returned NODEV; this startup race is
fixed, but is **not proven to be the cause of the game hang**. The current
diagnostic configuration uses `overlay=0`, `tvout=0`; firmware and OC clock
settings originally stayed unchanged. That test still failed. The next isolation
build uses firmware 0.3.12 and `metadata=0`, restoring input-only EP0 traffic;
OC settings are untouched. It fixes input starvation by metadata, metadata errors
disabling input, and stale-time comparisons against a timestamp older than the
latest packet. None is yet proven to explain the PSP shutdown. A disabled Consolizer OSD never queries/writes display
buffers. Re-enable mode 2 only after establishing a stable no-OSD baseline.

### TV activation (experimental)

After an 8 s startup grace period and cable detection, the plugin requests
Sony's normal long-display-button action for at most 6 s. Only the kernel button
mask receives this action, not the game's buttons. It does not change game
framebuffer size, stride, GE lists or clocks. An already non-LCD display mode is
left alone. A request is made at most once per launch; no repeated toggling on
brief BT disconnects, and no automatic return to LCD. Check the logged mode and
cable on hardware: Sony's impose handling and mode reporting vary with context.

PSPStreamer keeps its existing output policy; this automatic action is suppressed
while the app is present. Selecting `tvout=0` restores manual output control.

## Recovery and test

Hold physical **NOTE + VOLUP for two seconds** to disable external input for
the current launch. Physical PSP controls remain available. Stale input expires
after 750 ms and Sony emulation has a finite sampling lifetime.

Test a known PSP game first, then XMB and a PS1 title separately. Check controller
off/on, unplug/replug, app/game exit, both overlays together and suspend/resume.
Then test PSPStreamer learning, playback and an HTTP download. For TV test one
policy at a time, with a known-working cable and manual output as fallback.

Logs: `ms0:/SEPLUGINS/StreamMasterPad/loader.log` and `last.log`, replaced at
each plugin launch. Copy before another game if that launch's log is needed.
Disable the ARK entries to return to the previous app-only controller path.

## Architecture and build

One USB bridge owns the bus. A 100 Hz worker reads cached EP0 input; no media
sockets, per-button allocations or RPC polling. PSPStreamer claims the same
bridge and retains direct input capture; the resident module is not unloaded by
the app. Optional metadata uses sixteen ordered four-byte, no-data-stage EP0
requests when changed (10 s refresh otherwise). Input has priority after 100 ms;
metadata cannot monopolize bulk media queues. Incomplete updates are discarded.

```sh
make -C psp-controller
make -C psp-controller/bridge
make -C psp-overclock
make -C psp-client/streammaster_usb
make -C psp-client
# After activating ESP-IDF and setting IDF_TOOLS_PATH:
bash streammaster/build-bluetooth.sh qio80-iram
```

Kernel modules use O2; the app retains O3/LTO. Shared OC helpers are MIT; the
plugin/common bridge are GPL-2.0-or-later. Sony API semantics were checked against
[uOFW ctrl](https://github.com/uofw/uofw/blob/master/src/kd/ctrl/ctrl.c),
[PSPSDK impose](https://pspdev.github.io/pspsdk/pspimpose__driver_8h.html), and
[ARK NID mappings](https://github.com/PSP-Archive/ARK-4/blob/main/core/systemctrl/src/nid_660_data.c).
The [controller emulation example](https://github.com/crozone/PSP-EmulatedControllerTest)
was reviewed, not copied. Runtime API lookup failures disable injection safely.
