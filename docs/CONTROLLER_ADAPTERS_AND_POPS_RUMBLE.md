# USB controller adapters and POPS rumble: initial inspection

Inspected 2026-10-02. No device firmware, pairing, kernel driver, PSP plugin
configuration or runtime hook was changed. Descriptor inspection is not a
functional controller/rumble test.

## Attached devices

### PSX adapter: 2563:0526, SHANWAN Android Gamepad

- USB Full Speed (12 Mbit/s), two HID interfaces, descriptor power 350 mA.
- Interface 0: 97-byte report descriptor, gamepad report ID 7; four 8-bit axes,
  hat, button bits and two additional simulation-control bytes. Interrupt IN
  0x81 and OUT 0x02, 32-byte packets, 10 ms interval.
- Interface 1: 101-byte descriptor, system/consumer controls (report IDs 1/2),
  interrupt IN 0x83. This is not evidence of a second PSX controller port.
- Both report descriptors read successfully via Linux sysfs. They declare no
  Output/Feature report. Linux advertises force-feedback capability zero.
- OUT endpoint alone does not establish rumble support. It may need an
  undocumented command or a different operating mode; do not send guessed
  motor commands based only on the vendor ID.
- Promising moderate-size addition: implement a USB HID host input path and
  feed the existing normalized controller/mapping layer. Current StreamMaster
  external USB controller path selects Bluetooth HCI, not generic USB HID.
  Descriptor layout and enumeration are checked; live input is not yet tested.

### Xbox Wireless Adapter: 045e:02e6, XBOX ACC

- Vendor-specific interface, currently High Speed (480 Mbit/s), eight bulk
  endpoints advertising 512-byte packets; descriptor power 500 mA.
- xow supports this adapter family and controller model 1697. Reference:
  https://github.com/medusalix/xow (GPL-2.0-or-later, separate firmware download).
- Requires radio initialization/firmware and GIP transport, not Bluetooth HCI
  or an ordinary USB HID parser. Existing Bluetooth pairing support cannot
  simply be reused as the device driver.
- Before porting, test enumeration/operation at Full Speed: the S3 USB host
  supports FS/LS, not HS. Current HS enumeration does NOT prove FS is impossible.
  https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html
- Driver, endpoint/channel allocation alongside PSP/HCI, memory, and USB-speed
  compatibility make this substantially more complex than the PSX adapter.

## POPS rumble: what is and is not established

- Existing PSPConsolizer has no rumble output protocol/implementation.
- POPSAnalog patches controller input for the PSP Go DS3 path. Its main.c is
  an input remapper, not a rumble hook:
  https://github.com/rereprep/POPSAnalog
- Searched local PSPSDK, uOFW controller sources and psplibdoc commit
  5e15a3cff6c3642f3217b93aeb2f6b759cee1b02. No named rumble/actuator export was
  located. Many exports are unidentified, so this does not establish absence.
  https://github.com/pspdev/psplibdoc
- PPSSPP's sceCtrlGetLeftVibration/RightVibration are emulator helper functions,
  not evidence of callable PSP firmware exports. Vita sceCtrlSetActuator is
  also not proof of a PSP counterpart.
- No usable local POPS PRX dump was located; PSP was not mounted during this
  inspection. There is no verified 6.61 PSP-3000 rumble hook or instruction
  signature yet. Do not patch guessed addresses.

Next evidence needed: read-only module/import inspection of the user's running
POPS (model/firmware recorded), or a decrypted dump of their matching POPS and
controller modules. Compare controller-mode setup and PS1 actuator command
processing with the Go path. First log motor values during a known rumble scene;
only after that add a bounded/coalescing output path to StreamMaster, with
mandatory motor-off on disconnect, title exit, suspend and communication timeout.
Controller-specific output support is a separate requirement from finding the
POPS commands. Do not promise the PSX adapter supports it yet.

## Fullscreen plugin coexistence

FuSaFullscreenTest 0.13 removes module, model, firmware and GAME-only allowlists.
Consolizer and both overlays may run alongside it. Memory/source validation and
the 30 Hz capture scheduler remain. The PRX was copied and byte-verified on the
mounted PSP. Coexistence is now open for hardware testing, not yet confirmed.

## Wired HID implementation (0.3.14)

The Onju Bluetooth build now also accepts a non-boot USB HID gamepad in place
of the HCI dongle. It asynchronously fetches the report descriptor on EP0,
claims only the selected interface and polls interrupt IN. Existing transfers,
task, input wire protocol and per-device learning are reused; no extra streaming
buffers or PSP kernel allocations. A VID/PID/interface profile key keeps the
mapping across reconnects. Only one controller transport is active at a time.

Built before running targeted tests. The actual 97-byte SHANWAN descriptor,
synthetic input, malformed interface bounds and existing mapping/report tests
pass under ASan/UBSan. Live input and media coexistence are not yet tested.

Rumble references found for SHANWAN target **2563:0575**, not this device's
**2563:0526** (different descriptors/report IDs). Do not reuse their output
packets without establishing the adapter's mode/protocol:
https://github.com/hbiyik/hid-shanwan/blob/master/hid-shanwan.c
https://github.com/nefarius/DsHidMini/pull/567

## Review of `Dokumentation PSP Rumble Mod.pdf`

The two-page document describes a concept, not a verified PSP rumble driver:

- Addresses `0x09BD0000/1` lack firmware/module signatures, source references or
  measurement evidence. Reading two arbitrary bytes cannot identify motors.
- `sceKernelStdoutSend` is not declared in the local PSPSDK/uOFW examined here;
  the sample also supplies no thread-start/module lifecycle or UART setup.
- `sendRumbleToBluetoothController` is a placeholder, not a provided driver.
- A 100 Hz poll cannot promise zero latency. A three-byte frame with 0xFF also
  allowed as intensity has no checksum/escaping or reliable resynchronization.
- DualShock actuator negotiation/mapping (not just the 0x42 poll command) must
  be understood before interpreting motor bytes. Audio-derived vibration is
  not a replacement for authentic PS1 effects.

An actual source lead is remotejoy-minus at commit
`1e151fcc1f5924607e008a2c817f7dc681af549c`: `pops_aim.c` describes 0x30-byte
per-port state at scratchpad `0x13c00 + port*0x30`, guarded by ID/length/header
checks. `main.c` verifies instruction signatures for a separate port-B gate.
This is input/GunCon evidence, **not** a discovered rumble buffer. No code from
that project was copied and no guessed POPS memory writes were added.
https://github.com/SourceK78/remotejoy-minus

Next step remains inspection of matching decrypted POPS code around serial-pad
command handling (or a read-only diagnostic capture during a known PS1 rumble
scene). The mounted stick contains no usable POPS module dump. The PDF alone
does not close this evidence gap; real motor output is not implemented.
