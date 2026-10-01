# PSP Consolizer 0.2 — StreamMaster controller plugin

### Current regression diagnostic build

The disabled overlay previously still reserved three framebuffer backups in
kernel BSS (152,688 bytes). The resident bridge's fixed footprint had grown from
228,756 bytes in 0.1 to 339,192 bytes in 0.2. It is now 187,388 bytes: OSD storage
is allocated only when enabled (one backup for polling, three for presentation).
Allocation failure disables the OSD rather than controller input. Storage is
released only after the worker and registered callbacks have stopped.

The latest PSPStreamer log failed at Sony `Network Common` module loading
(`800208D9`), while StreamMaster settings exchanges, a transfer benchmark and
HTTP succeeded. That is not evidence of a failed USB connection. The plugin now
logs free kernel memory and the largest block before OSD initialization, plus
free memory afterwards. These values are hexadecimal byte counts.

The user confirmed games, TV activation and controller reconnection after USB
and power removal work with the packed overlays enabled. VSH still returned
`80243001`: the USB bus was already started. The bridge now borrows an idle
existing bus instead of failing, and stops only a bus it started itself.
An active USB function is never forcibly disconnected. VSH and POPS remain
separate hardware tests; the ESP cold-start issue is not declared fixed.

VSH now rechecks local USB state every two seconds even after the first start
succeeded. If the bridge was stopped or USB deactivated during XMB startup,
it can restart/rearm on an inactive bus. Live transfers prevent rebuilding;
an already-active function is not reset. State changes are logged, without
new network or controller polling traffic. This addresses a missing recovery
path, not yet a hardware-confirmed diagnosis of the cold-start failure.

Both plugins now store their own two-color overlay pixels as a bit mask rather
than 32-bit values. Original application pixels remain lossless. StreamerOC
saves 109,728 bytes of BSS; Consolizer's three enabled backups need 78,756 bytes
instead of 152,688. Including code changes, both modules plus the enabled
Consolizer backups use about 30 KiB less memory than the successful overlay-off
test. Allocator overhead and each game's other allocations still matter; this
is not a guarantee of compatibility. Install BOTH updated plugins. Firmware,
clock policy and controller emulation are unchanged. Allocation failure still
disables only the Consolizer overlay. The test configuration keeps temporary
status display (`overlay_always=0`), not a permanently visible overlay.

Use a controller paired with StreamMaster in PSP games, homebrew, and optionally
VSH or POPS. **0.2 game input, packed overlays, TV activation and USB/controller
reconnection passed hardware testing. The new VSH bus fix awaits testing.**
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
- `report=0`: disable loader/runtime diagnostic writes, including log truncation;
  existing logs are left untouched. Default `1`. StreamerOC independently has
  the same `report=0` setting in its own INI. Overlay display is independent.
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

Historical regression investigation: the first 0.2 game test produced black screens/forced
shutdown with USB connected, even with `tvout=0` and VSH activation removed.
The log confirms early OC registration returned NODEV; this startup race is
fixed, but was **not proven to be the cause of the game hang**. The earlier
diagnostic configuration uses `overlay=0`, `tvout=0`; firmware and OC clock
settings originally stayed unchanged. That test still failed. The next isolation
build uses firmware 0.3.12 and `metadata=0`, restoring input-only EP0 traffic;
OC settings are untouched. It fixes input starvation by metadata, metadata errors
disabling input, and stale-time comparisons against a timestamp older than the
latest packet. None is yet proven to explain the PSP shutdown. A disabled Consolizer OSD never queries/writes display
buffers. The subsequent kernel-memory fix and packed buffers resolved the
reported game regression; see the current status above.

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

Logs: `ms0:/SEPLUGINS/StreamMasterPad/loader.log` and `last.log`. With reports
enabled, the preceding launch is retained as `loader.log.previous` and
`last.log.previous`; older history is rotated out. With `report=0`, neither
current logs nor history are changed. Copy logs before multiple further launches.

For XMB controller testing, leave the USB storage screen and turn off Sony's
USB Auto Connect setting if it opens that screen automatically. The plugin
borrows a running, idle bus; it does not eject a mounted memory stick or seize
another active USB function. A PC storage connection and the StreamMaster
USB device function cannot be active on the PSP port simultaneously.

More invasive overlays (private output buffers / GE command interception) are
not enabled. The former needs substantial framebuffer storage and copies;
the latter adds game-specific GPU synchronization hazards. Neither existing
mode promises universally flicker-free output. Preserve the stable modes
until a separate opt-in design can be tested safely.
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
