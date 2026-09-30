# USB Bluetooth experiment: build and verification record

## Follow-up: 0.3.2 hub regression fix

User A/B test: 0.3.1 connects directly but fails behind the same hub even with
no Bluetooth dongle; the previous firmware connects with that hub and dongle.
The mounted PSP log contains `result=-4 stage=wait USB attach` (109795 and
113560 ms), so failure precedes WLAN and Bluetooth pairing. Mounted EBOOT and
StreamMasterUSB.prx match the supplied test files by SHA256.

Code review found unconditional polling GET_STATUS actions entered the port
state machine without a change event and while existing port work could still
be pending. IDF's unchanged ENABLED path can emit RESET_COMPLETED again; its
GET_STATUS action also has priority over a queued RESET. The corrected poller
arms where IDF would re-enable hub interrupts, sends a raw EP0 probe and only
notifies the original driver for nonzero change bits. This preserves the
driver's status_lock and reset sequence; unchanged polls do not touch them.

DIO/QIO builds were produced before the focused generated-C regression test.
That check covers 100 unchanged polls, changed-status handoff, normal driver
responses, reset/busy exclusion and removal guards under ASan/UBSan. Hardware
success remains unconfirmed until the user retests hub-only, then hub+dongle.

## Initial implementation (0.3.1)

The first hardware test uses Onju Voice V3 + one powered hub + PSP + CSR8510
(`0a12:0001`), with a Snakebyte iDroid:con Classic HID controller. The alternative
detected Barrot dongle (`33fa:0010`) is allowlisted but secondary. Xbox 1697 is
not a Bluetooth controller. No hardware pairing or successful input delivery
has been claimed yet.

## Delivered

- PSP app + StreamMasterUSB kernel bridge: controller input snapshot, no HTTP
  input polling, explicit StreamMaster settings submenu, stale-input release.
- Experimental 0.3.1 host-only Bluedroid HCI-over-USB firmware in DIO and QIO
  variants. Both CPU240/flash40/Octal-PSRAM80; no overclocking or new media
  buffer tuning. Configuration comparison differs only in flash-mode choices.
- Version-checked, build-local USB hub polling adaptation to fit eight channels;
  installed IDF source remains untouched. Stable firmware remains separately built.
- Pair/disconnect/forget controls, four bonds, eight discovery results, bounded
  HID report parsing. Controller-family compatibility still needs hardware.
- GPL3 combined-firmware notice and licenses, non-erasing component flash files,
  optional fresh-start factory image, rebuild/package scripts and SHA256 lists.

See [the setup and limitations](../streammaster/BLUETOOTH.md).

## Focused checks

Build first: PSP application, PSP kernel bridge, experimental DIO and QIO.
The shared code also compiles in the non-Bluetooth firmware configuration.

Host-side checks:

- HID buttons/axes/hat, report IDs, signed axis limits, truncated reports,
  malformed descriptor rejection, 10,000 deterministic malformed descriptors,
  and protocol structure sizes under ASan/UBSan.
- Existing protocol/configuration test.
- Actual PSP transport / ESP socket ownership harness (10,000 PSP cycles and
  4,000 ESP owner iterations); no controller data routed through media queues.
- Existing asynchronous driver buffer ownership test.
- Whitespace/diff checks. No whole-project regression suite.

Physical checks still required: HCI startup/pairing, PSP EP0 control-request
delivery, button mapping, hub hotplug, concurrent media stability and DIO/QIO
speed. Do not infer these from a successful compile or parser test.

## HACS follow-up

The separate PSPStreamerHA repository is updated to integration 0.1.4.
Its status/artwork/provider browser API remains compatible with server 0.1.66;
the enabled Plex Watchlist follows the provider browser. Offline reservation
management remains in the server UI, not a new HA entity.

Two concrete queue gaps fixed: explicit Next now sends `manual=1` to bypass
repeat-one, and Next/Previous passes the returned item's audio/subtitle/quality
settings into Play. A focused test executes the actual methods with mocked HA
dependencies; the corresponding full-HA integration test is included but was
not run here because this environment has no installed Home Assistant runtime.
No running HA installation or user configuration was changed.
