# PSP Consolizer 0.2 — StreamMaster controller plugin

FuSaFullscreen 0.25+ overlay compatibility: update PSPConsolizerUSB.prx
together with the fullscreen plugin. Both overlay methods (1/2) send text and
visibility to its compositor, at the top right; normal output is unchanged.
Always-on configuration remains effective. No loader or ESP firmware update
is needed for this change, and input/rumble behavior is unchanged.

### Current build

In VSH, both overlay modes now initialize five seconds later than the controller
service (also with `overlay_always=1`). Until then no Consolizer overlay buffers
are allocated and no overlay display hook/shared callback is attached. USB,
controller input and optical audio start normally. GAME/POPS timing is unchanged.
This avoids overlay work during the VSH startup sound; verify with a cold boot.
`overlay=0` remains fully off, not temporarily off. Existing settings are retained.

The PCM stall watchdog now starts a fresh deadline when new audio is accepted
after all previously submitted samples have played. Silence before that burst
does not count as a DMA stall. This applies to GAME/VSH/POPS; an existing stuck
backlog still triggers recovery. Test isolated menu sounds after at least three
seconds of silence as well as continuous audio.

With `report=1`, VSH also logs cumulative `VSH capture` counters: mixer calls,
largest inter-call gap, gaps above 5 ms, consecutive captures from the same DMA
half, and sample rate. Silence can legitimately increase these counters; they
are diagnostic evidence, not proof of corruption or duplicate samples. No PCM
content is logged. Six 32-bit counter fields are added, not another audio ring.
The VSH startup distortion is not yet confirmed fixed. No firmware change.

**Experimental system PCM → S/PDIF (2026-10-08):** Soul Calibur `ULES01298`
used normal mixer channels 3/4/6 in the 120-second hardware probe (2,027
observations); SRC/Output2 was never reserved in that trace. A first PCM mirror
is now available. The user confirmed repeated Soul Calibur restarts/gameplay
(remaining lobby stutter), Metal Slug XX, Star Ocean and Street Fighter Alpha 3.
VSH and POPS are now enabled for testing. It requires the verified 6.61
audio-driver layout and Onju optical firmware 0.3.23 or newer. No firmware or
PSPStreamer update is needed for the mixer itself. Use firmware 0.3.25 and the
matching PSPStreamer build to remove the temporary 0.3.24 ESP diagnostics.

Replace `PSPConsolizerUSB.prx`, retain your other settings and use:

```ini
report=1
audio_probe=0
audio_mirror=1
vsh=1
pops=1
```

Restart Soul Calibur. Select the optical input at the receiver (expect PCM
stereo), test menu/combat audio, then leave through the PS menu. Keep the PSP
volume up for this test: samples include the software mixer's gain. The ordinary
PSP output remains active; listening to it simultaneously can produce an echo
because the optical copy is buffered. Return `last.log` and `last.log.previous`
from the plugin directory. Logs include accepted/played frame counts, buffer
levels, underruns and capture faults. `audio_mirror=0` restores the old behavior.

Editable in PSPStreamer: **Settings -> Plugins -> PSPConsolizer: Global ->
Global S/PDIF PCM**. START saves; restart the game (or PSP for VSH) to apply.

The dedicated PCM transport worker uses PSP priority `0x18` (previously `0x28`)
to reduce scheduling delays observed during busy game menus. Its USB event
waits, 2 ms loop yield, packet limits, stack and capture ring are unchanged.
No Sony mixer/game-thread priorities are modified. This applies to GAME, VSH
and POPS mirroring, not PSPStreamer's own playback. Check menu responsiveness,
controller input and game exit as well as audio when testing this change;
hardware validation is still required. Existing opt-in timing logs record the
effective worker priority and callback-to-worker delays.

Default is off. This requires the enabled plugin and wired Onju optical output.
It mirrors system stereo PCM, independently of PSPStreamer's player output and
AC-3/DTS passthrough settings. The existing VSH/POPS context switches still apply.
The separate `audio_probe` diagnostic option remains INI-only.

The plugin must also be enabled for VSH and POPS in ARK's plugin configuration;
the INI flags alone cannot load it in those contexts. VSH startup may precede
USB readiness, so the earliest boot sound is not guaranteed to be mirrored.
POPS is a context test, not support for every Popsloader driver: an unknown
audio-driver signature still suppresses capture and is logged. SRC/Output2 has
its own verified capture hook; it never treats the normal-mixer buffer as SRC.
Firmware 0.3.25 removes the temporary ESP event ring and HTTP diagnostics server,
and both PSP clients stop requesting that diagnostic opcode. No port 8080 is
opened. Existing opt-in PSP reports and the original audio-open diagnostic remain.

The updated resident bridge no longer reserves the 144 KiB download pipeline
in games. PSPStreamer still obtains that pool when taking USB ownership,
before decoder startup; the standalone bridge retains its existing layout.
Release waits for outstanding USB callbacks. The first game trace had only
about 80 KiB free kernel memory (largest block about 50 KiB) after OSD startup.
This removes concrete memory pressure. The next test reached gameplay, but
showed repeated capture overflows followed by two-second optical restarts.
The PCM worker now queues diagnostics for the controller service instead of
performing Memory Stick I/O itself. At most eight messages are retained;
overflow drops diagnostics, not audio. Entries include the original event time.
Capture overflow drops the incoming block without restarting optical output.
When servicing resumes with more than 3,072 frames pending, the worker discards
old capture data and retains the newest 1,024 frames (about 23 ms at 44.1 kHz).
This bounds stale playback but cannot reconstruct audio lost during a stall.
Worker gaps, USB RPC time, discarded frames and ESP underruns remain logged.
The validated game audio path and its Sony audio clock/hook timing are unchanged.

The implementation validates the whole relevant mixer function using a
relocation-normalized signature, the module/segment layout and the flush stub.
It redirects one JAL, retaining its original delay slot. The hook copies each
64-frame, already mixed/clipped stereo block and still executes the original
DDR flush. It never sends USB, logs, waits, reserves audio channels, or changes
the DAC/mixer clock. No decoding or remixing of individual channels is needed.
The new worker reuses the existing USB bridge and its semaphore, not a second
USB driver. Optional memory is about 20 KiB plus a 4 KiB worker stack. No buffers
or worker are allocated when disabled or at PSPStreamer startup. In VSH/POPS
the same optional allocation now applies when those contexts are enabled.

Initial optical prefill is 2,304 frames (about 52 ms at 44.1 kHz), not a claim
of total end-to-end latency. This prototype does not yet resample to compensate
long-term PSP/ESP oscillator drift: buffer levels are logged first. A sample
rate change stops capture and reopens the optical session; transport failure
never blocks the game's original audio, and poisoned USB transport remains
disabled until reset/relaunch. Suspend, emergency disable and PSPStreamer
ownership suppress capture. Unknown driver signatures are left unpatched;
no universal firmware compatibility is claimed.

**SRC/Output2 capture (GTA audio confirmed, cutscene smoothness still open):** a separately fingerprinted
6.61 internal call mirrors only successfully submitted stereo PCM. Its original
return value, interrupt handling and blocking/pacing remain Sony's. A 32 KiB
raw ring is allocated only while SRC is in use, then released on returning to
the normal mixer. No game pointer survives the hook and it never waits for USB.
32/44.1/48 kHz are sent at native rate. The worker converts the lower Sony SRC
rates to 48 kHz with continuous-phase integer linear interpolation. Capture
honors SRC volume and clips to signed 16-bit. Output2 uses the same entry point.
While SRC is reserved, it replaces normal-mixer capture; simultaneous mixing
of both hardware paths is not implemented. Allocation/signature failures are
logged and leave original PSP audio untouched. Test GTA VCS beyond its intro,
then exit/restart and check normal-channel games and POPS for regressions.

With `report=1`, a GAME without a framebuffer after 20 seconds of controller
service and at least 10 seconds without new audio blocks gets two read-only module/thread
snapshots two seconds apart in `startup-TITLEID.log`. No additional thread,
audio buffer, thread suspension or forced recovery is used. At most one pair
is written per launch. A short startup sound no longer suppresses the report.
VSH and PSPStreamer are excluded. A successful later launch leaves the failed-start file intact;
another detected failed start of that same title replaces it. `report=0`
disables this diagnostic. Include it along with `last.log` when reporting a
black cold start. It is diagnostic, not a claim that the startup bug is fixed.

The loader retains its established two-second startup delay in all contexts.
The attempted early VSH audio path was withdrawn following a cold-boot shutdown
report: resident modules are not proof of completed initialization. Optical
output may miss the beginning of the boot sound while USB/receiver lock settles.

When optical audio has completely drained, DMA status is queried every 100 ms
instead of every 10 ms. Actual audio packets bypass this idle deadline. During
outstanding playback the original 10 ms status interval remains in use.

**Optional read-only audio probe:** this separate diagnostic feature does not
send audio or install the PCM hook by itself. Add `audio_probe=1` and set
`report=1` in `PSPConsolizer.ini`, then
restart a game. Do not save plugin settings in PSPStreamer during this test:
this experimental option is INI-only, not yet part of its settings editor.

**POPS PCM capture test (2026-10-09):** use `audio_mirror=1`, `audio_probe=0`,
`report=1`, `pops=1` and restart the PS1 title completely. The first ME producer
implementation reuses the existing USB PCM output without changing firmware.
Replace **both** PRX files: the small loader now prepares capture synchronously
before its USB delay, or in a chained pre-start module callback when POPS loads
later. The USB bridge acquires the ring via a versioned kernel-only interface;
the loader cannot unload while that consumer holds a reference. Check
`loader.log` for early installation time/status and `last.log` for handoff and
sample counts. The previous late-install failure was confirmed on hardware.
Only the verified manager layout is accepted, and only before its ME callback
has been configured. `POPS ME already configured` means the loader arrived too
late; this build deliberately does not reset a running emulator. Published
producer code/ring stay in user memory until process teardown, not kernel RAM.
This is a hardware-test build, not yet confirmed POPS playback support.

Probe v3 additionally captures the validated kernel code of `scePops_Manager`
when POPS has no normal audio driver. For this case use `audio_mirror=0` and
run a PS1 game for about 30 seconds. The title's `-text.bin` then contains the
manager (identified in the log), not `sceAudio_Driver`. No ME functions are
called or patched. See `docs/POPS_AUDIO_FINDINGS.md` for the separate ME path.

For the POPS discovery test, also set `audio_mirror=0`, launch a PS1 game and
play for four minutes. Probe v2 retries discovery for up to 120 seconds instead
of stopping at the first missing module. Missing modules and rejected text
ranges have separate messages, including module metadata for rejected ranges.
Bounded module/thread inventories are recorded every 30 seconds, including
non-audio names to expose alternate POPS routes. No extra worker or heap buffer
is allocated. The PCM mirror and ESP firmware are unchanged by this diagnostic.

Once a validated audio driver is found, the plugin queries queue occupancy every
50 ms or later and writes cumulative summaries every five seconds, stopping
after another 120 seconds. It uses the existing service thread, not an audio callback.
Current input, USB, optical-streaming firmware, TV and clock behavior is unchanged.

Return the `audio-probe-<title>.log` and `audio-probe-<title>-text.bin` files from
`ms0:/SEPLUGINS/PSPConsolizer/`. XMB uses its own `VSH` name. Each title retains
one previous pair. The binary is a bounded copy of the loaded audio driver's
validated executable text (at most 128 KiB), **not** a recording of game audio
or a dump of general RAM. Treat it as a local diagnostic firmware extract;
do not include it in releases. The log includes load addresses, exports and
audio thread entry points needed to validate a later hook against this exact
driver, rather than assuming the older uOFW offsets match.

Normal channel occupancy and SRC/Output2 occupancy are tracked independently.
`src_unreserved` is normal when that route is unused; no observations on a
channel do not prove it is never used. Missing exports or an invalid module
range are reported without patching anything. No audio channels are reserved,
no mixer/DMA settings are changed, and no additional USB transfers are made.
PSPStreamer ownership disables this probe. `report=0` disables it completely;
`audio_probe=0` (default) leaves ordinary Consolizer reports available.

**POPS rumble output test (2026-10-02):** real motor commands were captured
in Need for Speed High Stakes (`SLUS00826`). The updated `PSPConsolizerUSB.prx`
and StreamMaster **0.3.17-bt-qio80-iram** now forward them to a compatible
Bluetooth XInput controller. The user confirmed correct physical rumble with the
SF30 Pro in XInput mode in Need for Speed High Stakes and Wipeout 3 (2026-10-02).
The first test target is the **8BitDo SF30 Pro, X + START mode**.
See the POPS section below; PSPStreamer itself needs no update for this test.

Plugin directory, PRXs, INI and thread/module names now use PSPConsolizer.
The new companion StreamMaster firmware is **0.3.13-bt-qio80-iram**. It adds
per-controller PS/Home learning without changing existing profile storage sizes
or erasing bonds/Wi-Fi settings. Update the application, both plugin PRXs and
the firmware together. Application-owned `StreamMasterUSB.prx` keeps its name:
that is the separate StreamMaster transport, not the Consolizer plugin.

### Per-title and exact-path settings

The current USB bridge also accepts expiring native-app rumble from PSPStreamer
Monkey. App and POPS motor caches are separate, and ownership selects one source;
the POPS worker cannot erase an app effect. Capability is advertised independently
of `pops_rumble` so the app does not require USB re-enumeration. Outside app
ownership, `pops_rumble=0` still disables POPS output. No extra worker or bulk
channel is allocated. Update both `PSPConsolizerUSB.prx` and the app's standalone
`StreamMasterUSB.prx` when updating PSPStreamer; the ESP firmware is unchanged.

PSPStreamer now exposes main options, title/path rules and allow/exclude filters
under **Settings → Plugins → PSPConsolizer**. Changes are startup-only; Start
saves the current draft with a `.bak` backup. ARK registration is unchanged.

Copy `PSPConsolizer-rules.ini.example` to `PSPConsolizer-rules.ini` in the plugin
directory. This separate file survives changes to the general configuration.

```ini
[title:ULUS12345]
enabled=1
tvout=1
overlay=2
home_combo=0

[path:ms0:/PSP/GAME/Example/EBOOT.PBP]
enabled=0
```

Replace the illustrative title ID with the game's DISC_ID, not its displayed
name. Paths match the full `sceKernelInitFileName()` launch path. Both are
case-insensitive; IDs may contain a hyphen. Title IDs beat paths; the first
matching section wins equal-priority ties. Omitted settings inherit the global
INI. Supported rule keys: `enabled`, `home_combo`, `overlay`, `overlay_always`,
`tvout`, `metadata`, `pops_rumble`, with the same values as the main INI. No file means no change.
Rules are evaluated once per launch; invalid files disable controller injection
and automatic TV switching for that launch and log a negative section/error.
The file limit is 64 KiB; individual lines may contain up to 383 bytes.
Existing global VSH/POPS and allow/exclude-path restrictions still apply, even
when a matching rule enables the controller. PSPStreamer's ownership remains
untouched. These settings do not prevent the loader/USB module from loading;
use ARK plugin restrictions if a title must not load the module at all.

### Previous memory fixes

The disabled overlay previously still reserved three framebuffer backups in
kernel BSS (152,688 bytes). The resident bridge's fixed footprint had grown from
228,756 bytes in 0.1 to 339,192 bytes in 0.2. The initial fix used 187,388 bytes: OSD storage
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
An active USB function is never forcibly disconnected. The user has since
confirmed VSH cold boot with Bluetooth, TV activation and game changes. The
separate sporadic ESP cold-start issue is not declared fixed.

VSH now rechecks local USB state every two seconds even after the first start
succeeded. If the bridge was stopped or USB deactivated during XMB startup,
it can restart/rearm on an inactive bus. Live transfers prevent rebuilding;
an already-active function is not reset. State changes are logged, without
new network or controller polling traffic. This addresses a missing recovery
path; the subsequent VSH cold-start hardware test passed.

Both plugins now store their own two-color overlay pixels as a bit mask rather
than 32-bit values. Original application pixels remain lossless. StreamerOC
saves 109,728 bytes of BSS; Consolizer's three enabled backups need 78,756 bytes
instead of 152,688. Including code changes, both modules plus the enabled
Consolizer backups use about 30 KiB less memory than the successful overlay-off
test. Allocator overhead and each game's other allocations still matter; this
is not a guarantee of compatibility. Install BOTH updated plugins. Clock policy
is unchanged. Allocation failure still
disables only the Consolizer overlay. The test configuration keeps temporary
status display (`overlay_always=0`), not a permanently visible overlay.

Use a controller paired with StreamMaster in PSP games, homebrew, and optionally
VSH or POPS. **0.2 game input, packed overlays, TV activation and USB/controller
reconnection and VSH cold startup passed hardware testing.**
No Rumble. Wi-Fi/media transport remains available to PSPStreamer.

Remove the previous plugin directory and replace its ARK entries when upgrading;
do not load old and new copies together. Preserve your INI as `PSPConsolizer.ini`.
Pairing and button/analog learning remain in PSPStreamer.

## Installation

Copy both files into `ms0:/SEPLUGINS/PSPConsolizer/`:

- `PSPConsolizer.prx` (loader)
- `PSPConsolizerUSB.prx` (resident USB bridge/controller service)

Copy `PSPConsolizer.ini.example` as `PSPConsolizer.ini` and adjust it. Use
the companion PSPStreamer EBOOT.PBP, PSPStreamer.prx and **app** StreamMasterUSB.prx.
Do not interchange the app and resident bridge binaries. Install the companion
StreamerOC.prx too if using both plugins with presentation-hook overlays.

Add the desired contexts to ARK's active `SEPLUGINS/PLUGINS.TXT`:

```text
game, ms0:/SEPLUGINS/PSPConsolizer/PSPConsolizer.prx, on
vsh, ms0:/SEPLUGINS/PSPConsolizer/PSPConsolizer.prx, on
pops, ms0:/SEPLUGINS/PSPConsolizer/PSPConsolizer.prx, on
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
- `report=1`: also record a compact input diagnostic every five seconds:
  worker loop count, longest loop gap and input age (microseconds), packet
  sequence, received buttons, injection state and Sony button-emulation result.
  This distinguishes missing packets from a starved worker or API rejection.
  No additional USB requests, task or heap buffer is used. Timing includes
  scheduling and log I/O; it is not pure CPU execution time.
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

For a cold VSH start, the request additionally waits for boot status 0x20000,
the shell and PAF modules, a valid LCD framebuffer and two seconds of stable
layout. Normal double-buffer address changes do not restart that timer. Missing
cable/readiness does not consume the single activation attempt. This addresses
the startup race separately from FuSa game scaling and needs hardware validation;
the latest VSH log did not capture the reported cold-boot failure itself.

PSPStreamer keeps its existing output policy; this automatic action is suppressed
while the app is present. Selecting `tvout=0` restores manual output control.

## Recovery and test

### USB storage hand-off (VSH)

Hold the PSP's physical **NOTE + Volume Down for two seconds** in XMB. This
pauses Consolizer and stops its USB function; the overlay shows `USB RELEASED`.
Now connect the PC and enter the normal USB Connection screen. The plugin will
not reclaim USB while paused. Leave USB Connection and safely finish/eject PC
transfers before reconnecting StreamMaster; use the same chord to resume.
The pause lasts until toggled back or the next VSH launch. This is an explicit
hand-off, not automatic detection of which USB host is attached. It is disabled
while PSPStreamer owns the bridge, to avoid interrupting media transfers.

If the previous plugin build prevents copying this update, temporarily disable
its VSH entry in ARK and restart VSH/PSP, or use a memory-stick reader. Install
the updated plugin before re-enabling the VSH entry.

### PS / HOME from the controller

In PSPStreamer choose Settings > StreamMaster USB > Bluetooth controller >
Buttons / reconnect > **PS / Home**, then press X and the desired controller
button. Save with Start. The existing wizard includes this additional target.
Firmware 0.3.13 stores it per device in previously reserved bytes; previous
profiles remain usable, with Home unassigned. Assigning a source to Home
removes its previous regular mapping, and vice versa. Any HID button exposed
by the supported controller parser can be used; a vendor-specific Guide
button not reported as such is not automatically supported.

Consolizer sends Home only to Sony's kernel input path, not as a game button.
The firmware encodes it in a previously unused EP0 bit only when the updated
bridge advertises support. The learned Home button also works inside
PSPStreamer: ordinary buttons and the analog stick remain application-owned,
but Home goes independently to the system menu through the resident worker.
This can provide an escape from a stuck application UI while USB input, the
worker and the system menu are still responsive; it cannot recover a kernel
lockup. Disabled/excluded titles, emergency disable, suspend, USB release and
disconnected/stale controller input still suppress it. No new polling is added.

Hold the mapped **Start + Select for one second** in a game to send one 150 ms
HOME pulse to Sony's kernel input handler. Release both before using the chord
again. The combined buttons are suppressed from game input while held together;
a button pressed earlier on its own can still reach the game. Set `home_combo=0`
in the Consolizer INI to disable this optional fallback (default `1`). The
individually learned Home button remains enabled independently.

Hold physical **NOTE + VOLUP for two seconds** to disable external input for
the current launch. Physical PSP controls remain available. Stale input expires
after 750 ms and Sony emulation has a finite sampling lifetime.

Test a known PSP game first, then XMB and a PS1 title separately. Check controller
off/on, unplug/replug, app/game exit, both overlays together and suspend/resume.
Then test PSPStreamer learning, playback and an HTTP download. For TV test one
policy at a time, with a known-working cable and manual output as fallback.

Logs: `ms0:/SEPLUGINS/PSPConsolizer/loader.log` and `last.log`. With reports
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

With `report=1`, `loader-pops.log` and `last-pops.log` additionally retain the
latest PS1 launch, independently of the general log rotation. Returning to VSH
or starting a file manager does not replace these files; the next POPS run does.

## Experimental POPS DualShock / rumble output

Default: `pops_rumble=0`. To test, update `PSPConsolizerUSB.prx`, flash the
matching StreamMaster 0.3.17 firmware using its component `flash_args` (keeps
NVS Wi-Fi/bonds), and add
these settings to the existing `PSPConsolizer.ini`, preserving other settings:

```ini
pops=1
pops_rumble=1
report=1
```

The Consolizer must also be enabled for POPS in ARK. The option can alternatively
be enabled per PS1 title/path in `PSPConsolizer-rules.ini`. It does nothing in
PSP games, VSH or PSPStreamer. Return to `pops_rumble=0` and restart the PS1 title
to restore the original path.

The first supported layout is the supplied **6.60 03g** serial dispatcher.
6.61 is not presumed identical: it must pass the same instruction and relocated
target checks. Unknown layouts are logged and left unmodified. No Sony PRX is
packaged or replaced; do not install the Go PRX on another PSP model.

Our independently implemented user-mode routine covers commands `42` through
`4D` following the inspected Go handlers, retaining Sony's ordinary button
mapping and current digital/analog selection. In particular, Go's `44` resets
motor values rather than forcing a new analog mode. This is a first compatibility
test, not a claim of complete DualShock hardware emulation. The second analog
stick is not added by this change. Reset/save-state compatibility is not yet
hardware-verified.

Installation waits until at least ten seconds of PSP uptime and a loaded POPS
module. If the title negotiated its controller before installation, use the PS
menu's game reset once after waiting. Then enable vibration in the game's options
and reproduce a known rumble event. Physical PSP controls suffice to test capture
while an external controller is unavailable. Verify ordinary controls and memory
card loading too. Copy `last.log` **and** `last.log.previous` before launching
additional apps: returning to VSH can rotate the PS1 run into the previous log.

Look for `POPS DualShock capture installed; EP0 motor output`, followed by
`POPS p0` lines. `seen` is a bitmask for commands 42..4D; `map` contains enabled
positions / large-motor selection; `motor` gives the current small/large bytes;
`peak` retains short pulses; the hexadecimal value at line end counts changes.
`unsupported signature; unchanged` means no patch was applied and the module
layout needs analysis. Zero motor values alone do not establish that the game
lacks rumble. Include the title/region and scene with the logs.

The copied payload has no relocations, imports, GP setup, I/O, USB operations or
allocation. A single atomic call-site replacement routes only the normal pad
handler through it; memory cards and other serial targets keep their original
function pointer. The original pad function remains intact. Roughly **1.7 KiB
of user RAM** holds code, entry stub and bounded state. The worker logs snapshots
every two seconds; `report=0` disables these writes. No extra thread is created.
On plugin stop the call site is restored if still owned. The small user block
is retained until the POPS process exits because a suspended user thread could
still be executing it; it references no unloadable kernel code.

### SF30 Pro hardware test

1. Power the SF30 Pro off, then hold **X + START** for XInput mode. Hold its
   pairing button for three seconds and pair it through PSPStreamer's existing
   StreamMaster Bluetooth menu. Relearn buttons if this mode has another layout.
2. Start the PS1 title with `pops=1`, `pops_rumble=1`, `report=1` and `metadata=1`.
   Enable vibration in the game's settings. If necessary, wait ten seconds and
   reset the game from the PS menu to repeat controller negotiation.
3. Trigger a collision in Need for Speed High Stakes. Check both motor response
   and ordinary input. Pause/leave the game and disconnect/reconnect to check
   that vibration stops and control returns.
4. Preserve `last.log` and `last.log.previous`. `Rumble transport` records
   controller VID/PID, backend (1 = Xbox Bluetooth), blocked output (1 = an
   output error/timeout), metadata freshness and the count of EP0 replies.
   Firmware UART logs also report the selected backend and write failures.

   Version 0.3.16 also logs `Rumble progress`: `rx` means a valid motor frame
   reached the ESP32, `rx_nz` a nonzero frame, `tx_nz` a nonzero Bluetooth write,
   and `ack_nz` successful host-stack completion of such a write (not proof of
   physical movement). These flags stay set for that controller connection.
   `PSP_nonzero` counts nonzero EP0 replies submitted by the PSP. This separates
   capture, publication, USB reception and Bluetooth delivery in one test.

The implemented report is Xbox Bluetooth output report 3, with the known
`045e:02e0` / `045e:02fd` identities and an eight-byte report payload verified
against the actual HID descriptor. A different identity or descriptor remains
input-only and must be inspected, not blindly treated as compatible. Android,
macOS and Switch modes of the SF30 Pro are **not** rumble-supported by this build.
The SHANWAN `2563:0526` USB adapter also remains input-only: its descriptor has
no Output/Feature reports and Linux exposes no force-feedback capability.
This does not rule out an undocumented vendor command.

Motor state returns in the existing input exchange over EP0; no extra USB pipe,
PSP thread or dynamically growing effect queue is added. Only opted-in POPS
sessions advertise this capability. Exchanges are capped at 50 Hz; unchanged
active effects refresh at 80 ms. Packets request 200 ms effects with no repeats;
some controller clones ignore timing fields, so this is not a universal hardware
stop guarantee. A 250 ms stale-input/game-poll watchdog explicitly requests zero output.
Transfer errors downgrade to input-only; Bluetooth output errors disable only
motor writes until reconnect. Existing WLAN/media transport is unchanged.

Protocol references: [Linux hid-microsoft.c](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-microsoft.c),
[SDL Xbox One HID driver](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_xboxone.c),
[official SF30 Pro manual](https://download.8bitdo.com/Manual/Controller/SN30pro%2BSF30pro/SN30pro%2BSF30pro_Manual.pdf).
Packet serialization is independently implemented, not copied driver code.

0.3.16 uses SDL's `0x0f` actuator mask instead of `0x03`, retaining zero trigger
magnitudes. This handles the reversed/swapped motor mask behavior documented
by [xpadneo](https://github.com/atar-axis/xpadneo/blob/master/hid-xpadneo/src/xpadneo/rumble.c).
The prior Wipeout 3 log (`SCES02845`) confirmed actual motor commands (peak
`1/190`), recognized `045e:02e0`, and continuous EP0 replies without an output
fault; it did not record nonzero values at each output stage. Therefore the
mask change is a compatibility measure, not the confirmed cause of silence.
The 0.3.16 follow-up showed `rx_nz=1`, `tx_nz=0`, `ack_nz=0`: the ESP32 received
motor values but never submitted a motor report. 0.3.17 fixes the impossible
queue guard (`free > 4` on a four-slot queue) to allow output with at least two
free slots. No new PSP plugin is needed over 0.3.16's companion plugin.

Focused host checks (after building the PRX):

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/streammaster_rumble_test.c -o /tmp/streammaster-rumble-test
/tmp/streammaster-rumble-test
cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/pops_serial_test.c -o /tmp/pops-serial-test
/tmp/pops-serial-test
cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/pops_signature_test.c -o /tmp/pops-signature-test
/tmp/pops-signature-test /path/to/your/660/pops_03g.prx
```

The second test uses your own decrypted binary; no proprietary fixture is
included. Build-time checks reject payload relocations/imports and separate
data sections. See [binary findings](../docs/POPS_RUMBLE_BINARY_FINDINGS.md).

## Optional health-triggered PSP game rumble

The resident bridge can monitor a verified health address without modifying
game memory. Configure **SELECT -> Plugins -> PSPConsolizer: Game rumble** in
PSPStreamer. This is separate from native POPS rumble and Monkey effects.
Read [RUMBLE.md](RUMBLE.md) for every field, address discovery, limitations and
testing. The disabled example contains the user-confirmed Soul Calibur
ULES01298 health address. Guided CWCheat-line and RetroArch-result imports
convert addresses without enabling cheats; imported profiles default to off.
