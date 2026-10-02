# USB Bluetooth controllers — Onju Voice V3

Current test build: **0.3.16-bt-qio80-iram**, adding guarded POPS rumble output
for a Bluetooth XInput controller; wired USB HID input remains available.
Controller learning, player/Monkey
control and four-deep downloads are hardware-tested; extended Bluetooth soak
testing remains open. Run `bash build-bluetooth.sh` and
`bash package-bluetooth.sh` without arguments for this standard. Earlier version
sections below document the development history, not installation choices.

## Hardware and scope

### Wired USB HID (0.3.14)

Instead of the Bluetooth dongle, attach a USB HID gamepad or PSX-to-USB adapter
to the powered hub. It connects automatically; no pairing or scan is needed.
Use the existing controller-learning menu for buttons, D-pad, Home and analog
axes. Profiles use the adapter VID/PID/interface and persist in NVS. Identical
adapters share a mapping. PSPStreamer and Consolizer use the same normalized
input as Bluetooth; no PSP binary update is required for this firmware change.

The initial implementation handles one controller transport at a time: unplug
the Bluetooth dongle when testing wired HID. It supports non-boot HID joystick/
gamepad report descriptors up to 1024 bytes and interrupt-IN packets up to 64
bytes. Consumer/keyboard interfaces are not used. No guessed button offsets,
rumble packets or extra streaming buffers are introduced. Device disconnect
releases controls and drains transfers before releasing the interface.

The connected SHANWAN 2563:0526 descriptor and synthetic reports pass host
tests. Live input, unplug/replug and concurrent media streaming still require
the hardware test. Rumble is not implemented: this device currently advertises
no HID output report; drivers found for 2563:0575 use a different layout.

- Use the Onju V3, **one powered USB hub**, the PSP and a USB Bluetooth adapter.
  Do not add another hub, keyboard, storage device or USB audio device.
- First test adapter: CSR8510 A10, USB `0a12:0001`. The detected Barrot
  `33fa:0010` is also allowlisted, but is a secondary, untested alternative.
- First controller: Snakebyte iDroid:con in its standard Android/HID pairing
  mode. Its actual report descriptor/button ordering still needs a live test.
- Xbox One **1697 has no Bluetooth** and cannot pair through either dongle.
- The S3's internal Bluetooth radio is **not** used. This is Bluetooth Classic
  HID over an external HCI adapter, not a BLE
  controller implementation. Pairing uses Just Works or legacy PIN `0000`.
- This experiment is not built for generic S2/S3 boards yet.

The S3 has eight USB host channels. Normally a hub, PSP and Bluetooth dongle
would require nine. Only this experimental build polls hub-port status over
EP0 (one port per 50 ms), replacing the hub's separate interrupt channel.
That leaves exactly eight channels. The build generates a patched copy of one
ESP-IDF v5.5.1 USB source file and rejects other source revisions; the installed
SDK itself is never modified. Hotplug/debounce remains handled by IDF.

Version 0.3.2 fixes the 0.3.1 hub polling regression: raw probes run only after
all port work has completed. Unchanged status is discarded; actual change bits
hand control back to the original port state machine. Probes never enqueue
unconditional port GET_STATUS actions during reset/enumeration. The PSP app and
StreamMasterUSB.prx from the 0.3.1 test package do not need replacing for this fix.

Version 0.3.3 adds USB discovery diagnostics after the hub fix was confirmed on
hardware. It requires the updated EBOOT for PSP-side diagnostic logging; the
kernel bridge is unchanged. This is a diagnostic build, not a claimed pairing
fix. A supported adapter rejected during descriptor parsing/interface claiming
now reports its VID/PID and actual error instead of appearing absent.

Version 0.3.4 prioritizes PSP startup: the adapter is recognized but its HCI
interface is not claimed until the PSP has claimed its streaming interface.
The existing one-second rescan retries without holding the adapter client handle
or waiting inside the USB callback. This targets the observed startup-order
failure with the dongle already attached; hotplug after PSP attachment had
already reached host-ready and discovery state. Full startup recovery and
controller pairing still require hardware verification.

## Installation and first test

1. Keep the previous working firmware and PSP files for rollback. Copy **both**
   `PSPStreamer/EBOOT.PBP` and `PSPStreamer/StreamMasterUSB.prx` from this test
   package into the existing application directory. Keep the other files and
   your configuration; this package is an update, not a fresh installation.
2. Connect the Onju to the PC. Use the **QIO** folder (tested default on Onju V3).
   DIO remains a compatibility fallback. From that folder:

   ```sh
   esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash @flash_args
   ```

   Adjust the port; older esptool versions may use equivalent command spelling.
   These three image writes **preserve NVS/Wi-Fi profiles and Bluetooth bonds**.
   Do not erase flash. A merged factory image is also supplied, but writing it
   overwrites the gaps, including NVS: use it only for a deliberate fresh start.
3. Connect the powered hub to the Onju host port, with PSP and CSR adapter on
   that hub. Select the StreamMaster network route in PSPStreamer.
4. Open **Settings → StreamMaster → Bluetooth controller**. Put the controller
   into pairing mode, select **Find controllers**, wait roughly ten seconds,
   and select the controller with X. The list refreshes automatically every two
   seconds; Square is a manual refresh. `*` marks saved controllers.
5. Test the D-pad/stick, X/Circle/Square/Triangle, L/R, Select and Start in menus,
   then while playing music/video. Physical PSP controls remain available.
   Disconnect/reconnect the controller and confirm that no direction remains
   held. Use **Disconnect** before finding/pairing another controller.
6. To forget a device, highlight its row, press **Triangle**, then **X** to
   confirm. This removes that exact device, not a hidden last-selected address.
   Known controllers automatically reconnect by default, without a new inquiry
   or deleting keys. Startup waits five seconds; further outgoing attempts are
   spaced at least 60 seconds apart and rotate through saved addresses. Incoming
   reconnects are accepted only for known devices or the explicitly selected
   pairing target. Disconnect/Find/Forget suspends automatic reconnect until a
   manual connection, saving options, or firmware restart.
7. If comparing the DIO fallback, flash that folder with its own `flash_args` and
   repeat the same test/transfer. Both variants use CPU **240 MHz**, flash
   **40 MHz**, Octal PSRAM **80 MHz**, and the same optimization settings.

### QIO 80 MHz default (validated on Onju V3)

`bash build-bluetooth.sh qio80` and `bash package-bluetooth.sh qio80` produce a
separate `StreamMaster-Bluetooth-QIO80` package, version `0.3.9-bt-qio80`.
Only flash clock changes from 40 to 80 MHz; CPU stays 240 MHz and Octal PSRAM
80 MHz. This does not enable experimental 120 MHz PSRAM. Use the package's
`flash_args` (including 80m, with the intentional DIO boot header); flash all
three components, not just the app. Existing NVS profiles/bonds are preserved.
Compare the same HTTP download, PSP CPU clock, AP and controller workload.
The 80 MHz hardware test succeeded: 700.5 KiB/s versus 659.9 KiB/s at 40 MHz
for the same complete download. QIO80 is now the default build/package choice;
QIO40 and DIO40 remain explicit fallbacks. Bluetooth long-duration testing is
still pending. Firmware retains the identifying version `0.3.9-bt-qio80`.

### Guided controller setup (0.3.9)

Connect the controller, then open **Bluetooth controller → Buttons / reconnect**.
An unsaved controller starts a wizard: press the desired controller input for
each PSP function (X, Circle, Square, Triangle, L, R, Select, Start and the four
D-pad directions). Release between steps. The final step asks for a circle with
the desired analog stick, followed by letting it return to the centre. Only move
one stick; if several axes were exercised, cancel that capture and retry it.

Use the physical PSP's **Square to skip** a step (keep its previous assignment)
or **Circle to cancel** the capture/wizard. Controller input is captured but does
not navigate during capture. The overview accepts controller navigation too.
The working X/Y stick remains the default
when skipped. Generic HID X/Y/Z/Rx/Ry/Rz axes are available for selection; moving
in a circle identifies the pair, not arbitrary vendor-specific axis semantics.
The overview also offers horizontal/vertical inversion.

After the wizard, the overview lists **PSP targets**, for example `X = HID02`
and `Analog stick = Rx / Ry`. Select any target with the PSP D-pad and press X
to capture it again, or choose **Set up all controls** to repeat the wizard.
The overview has three numbered pages: continue down for L/R, Select/Start,
all four D-pad directions, analog selection and axis inversion.
Assigning an input moves it from any previous target, avoiding accidental double
actions. **Start saves; Circle discards all unsaved edits.** The controller's
Bluetooth address is shown below the list. A disconnected/replaced controller
cannot receive the stale draft intended for the previous connection.

Profiles are saved by Bluetooth address in ESP NVS (four bounded slots, matching
the bond limit), loaded on connection and survive component firmware updates.
One controller's mapping no longer replaces another's. Automatic reconnect is
a global option in the same menu. PSP EBOOT and firmware 0.3.9 are both required;
the USB kernel driver remains unchanged. Normal control uses compact EP0 events.
Capture uses a dedicated full-snapshot RPC at most 20 times/second, only while
learning. One bounded worker stops on completion/cancel; no permanent polling
task or additional playback traffic is introduced.

0.3.6 restores the existing ESP-IDF bonded HID descriptor loader (previously
commented out), via a hash-checked build-local source replacement. The installed
SDK is unchanged. It also moves 32 KiB of reserve socket staging to PSRAM in
Bluetooth builds only: the first two channels and USB DMA buffers are unchanged.
This addresses observed internal free heap as low as 1019 bytes; it needs a new
hardware run to establish whether server stability and saved reconnects improve.

The QIO bootloader starts with a **DIO image header**, then enables QIO according
to its compiled configuration. `--flash_mode dio` in the supplied QIO command
is intentional; do not override it. QIO is a flash-bus access mode, not CPU or
Wi-Fi overclocking. Most code remains cached flash code; IDF's selected Wi-Fi
IRAM optimizations remain enabled. Moving all code into IRAM is neither done
nor implied. No performance improvement is claimed before measurement.

## Input path and limitations

The generic bounded HID descriptor parser maps the common ten-button gamepad
layout and X/Y/hat axes. Unsupported layouts produce an error rather than
guessing offsets. Mapping may need adjustment once an actual iDroid report is
available. Other HID devices and controller families are not certified.

Inputs use no web polling: Bluedroid callbacks update a fixed-size snapshot;
USB sends changes at most every 20 ms through the PSP's existing EP0, plus a
200 ms keepalive. The PSP reads a local driver snapshot without acquiring the
media RPC lock. Reports expire after 750 ms without USB updates; Bluetooth close
and USB unplug release buttons. Physical PSP stick input takes priority.
This bounds overhead but is **not a measured end-to-end latency guarantee**.

Queues, descriptor fields and discovered devices are bounded (eight discovered
devices, four saved bonds, one active controller). Bluetooth host allocations
prefer PSRAM; the existing hot media buffers retain their allocation strategy.
Bluetooth transfer errors do not deliberately reset the media connection. If
Bluetooth reports an adapter/host error, power-cycle the Onju before retesting;
automatic host recovery after every adapter fault is not yet guaranteed.

Refreshing the Bluetooth menu writes adapter VID/PID, state and report count to
the PSP diagnostic log when logging is enabled. Version 0.3.3 additionally logs
up to eight `StreamMaster BT USB` records: address, VID/PID, device/interface
class, endpoint addresses, last phase, error and attempt count. These are last
observations, not a guaranteed live inventory. Phase 1=open, 2=device descriptor,
3=allowlist filter, 4=configuration, 5=HCI interface, 6=interface/channel claim,
7=host start. Rejected PSP/hub rows at phase 3 are normal; inspect the dongle row.
Phase 8 means waiting for the PSP interface, not an unsupported adapter.
The additional `StreamMaster PSP USB` log records its last interface-claim result
or offline state. `error=00000000` means no error. Bluetooth state 2 is ready,
state 3 is scanning: wait at least 12 seconds. Starting with 0.3.5 the menu
refreshes asynchronously every two seconds; Square also refreshes manually.
The controller must be in discoverable Classic HID pairing
mode; merely powering it on is not enough.
The optional query is only made in the Bluetooth menu, not during playback.
ESP UART0 logs at 115200 baud
add descriptor size/field count and the first decoded report; these require UART
access, not the native USB port currently occupied by host mode. For a failure,
provide the PSP logs and state whether it was DIO or QIO. Pairing, external-HCI compatibility, PSP EP0
delivery have been confirmed by the user; concurrent controller/server operation
and hub hotplug still require verification. Version 0.3.5 additionally records
`ESP network` and `ESP socket` snapshots every 30 seconds when PSP debug logging
is enabled. These separate socket-worker inactivity from repeated would-block
reads and Wi-Fi disconnects. No new media timeout or task-priority tuning is used.

## Rebuild and licensing

Activate ESP-IDF **v5.5.1**, then run `bash build-bluetooth.sh` followed by
`bash package-bluetooth.sh`. Separate build directories/configs leave the normal
firmware untouched. Build PSPStreamer and its `streammaster_usb` driver with
the project's PSPSDK toolchain. Source corresponds to the Git revision in each
package's `SOURCE_REVISION.txt`; `SOURCE_STATUS.txt` records uncommitted changes.

The experimental firmware combines this project's **GPL-2.0-or-later** code
with ESP-IDF/Bluedroid under Apache-2.0 and other included notices. The combined
experimental firmware is distributed under **GPL-3.0**, using the project's
"or later" option. This does not change the license of unrelated PSP components
or third-party files. See `GPL-3.0.txt`, `ESP-IDF-license.txt` and `licenses/`.
No BTstack or USB Host Shield implementation is bundled. Complete corresponding
project source/build scripts are in the repository; ESP-IDF v5.5.1 source is at
<https://github.com/espressif/esp-idf/tree/v5.5.1>.

## POPS rumble test (firmware 0.3.16)

Use the matching PSP Consolizer build with `pops_rumble=1`. The initial motor
output target is an **8BitDo SF30 Pro in XInput mode (X + START)**, paired through
PSPStreamer. Android/Switch/macOS modes are not supported for rumble in this
build. Unknown controllers and the SHANWAN 2563:0526 USB adapter stay input-only.

Flash the component images with the supplied `flash_args`, not an erase-flash
operation, to keep saved WLAN profiles and bonds. The physical vibration test
is still pending. See `psp-controller/README.md` in the repository for the PS1
test sequence, diagnostics, supported report identities and watchdog behavior.
