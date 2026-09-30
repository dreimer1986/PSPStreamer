# USB Bluetooth experiment: build and verification record

## 2026-10-01: QIO accepted as Onju default

User confirms stable controller learning, player navigation and Monkey control
with both variants. Latest recovery log is QIO; history-00E31CBA2B2A1CCC is DIO.
Both downloaded the same 170143443-byte media file completely with HTTP 200,
rc=0, last_errno=0 and no resume. Body times: DIO 302138 ms (549.9 KiB/s),
QIO 251792 ms (659.9 KiB/s), approximately 20% higher complete-body throughput.
This includes transfer/storage pauses; the displayed moving average differs.
QIO HTTP setup took 3092 ms versus DIO 161 ms, not a sustained throughput issue.
Keep DIO fallback and generic firmware UNTESTED status. Bluetooth build script
defaults to QIO; pass dio for fallback or all for both variants.

## Follow-up: learning works, normal controls absent

Mounted PSP held the pre-Bluetooth 15482-byte StreamMasterUSB.prx (SHA-256
183ededf8841d37fb7866741c8ad52a1bbdf3541b0e255efcd9215993aa9280b),
also present in the normal working release. Bluetooth test packages 0.3.1–0.3.8
contained the 16882-byte EP0-capable driver. The 0.3.9 package omitted it,
incorrectly relying on the installed version. Raw learning RPC still worked,
while the old driver could not receive normal controller events. Rebuilt the
unchanged Bluetooth driver, corrected the mounted copy and release packages.
Packaging now always includes both app files AND StreamMasterUSB.prx.

## Follow-up: 0.3.9 raw-input transport repair

User reports working normal controls but immediate exit from all/individual
learning. Code confirms EP0 sends only mapped buttons and X/Y; the kernel
snapshot has zero session/raw fields. The learning session check therefore
returned immediately. Add full snapshot RPC 48 and one capture-only bounded
worker, retaining the proven EP0 playback path. Show mapping page numbers and
allow controller navigation outside capture. All 12 PSP targets plus stick
selection and inversion are present across three pages.

PSP and both firmware variants built first. Four affected tests passed, including
an actual command-handler assertion that raw buttons, all axes, hat and session
survive the new command byte-for-byte. Hardware wizard/reconnect remains to test.
No PSP storage was mounted during this fix, so no new device logs were read.

## Follow-up: 0.3.8 guided per-controller setup

User confirms improved controller behavior and Monkey steering through the
default X/Y analog stick. Retain this path as default. Replace the raw HID-row
editor with a PSP-target wizard, individual recapture and per-address storage.
The stick learner requires two exercised axes (range and multiple movements),
then a stable return to centre; jitter or one trigger is insufficient. More
than two exercised axes is deliberately ambiguous, requiring a fresh capture.

Build PSP and DIO/QIO before tests. Focused ASan/UBSan checks compile the actual
HID parser, profile/learning helpers, setup command handler and HID callback:
button/hat identification, duplicate reassignment, six axes and inversion,
circle/centre learning, unchanged 32-byte ABI, four separate saved profiles,
reconnection reload, stale-session/wrong-device rejection, failed persistence,
and the earlier OPEN/CONNECTING regression. No complete-project test suite.
NVS is simulated in the command test; physical persistence and stick choice are
the user's next hardware test. Flash components with flash_args to preserve NVS.

## Follow-up: 0.3.7 real versus intermediate HID events

New log repeatedly reports state=5/reports=0 (17850, 101288, 161286, 189033,
250748 ms). Internal free heap is now 38175–42311 bytes in the sampled rows,
so the memory fix is retained. No claim that playback stability is proven yet.

Exact SDK path: btc_hh_connect emits ESP_HIDH_OPEN_EVT with OK, CONNECTING and
BTA_HH_INVALID_HANDLE (0xff) immediately after queuing BTA_HhOpen. The later
BTA_HH_OPEN_EVT handler emits CONNECTED with the actual handle. Our callback
had accepted the first event, cleared pair_until and adopted 255; 0.3.6's
new active-handle guard then rejected/disconnected the actual handle. Fix the
state interpretation rather than removing stale-handle protection. Likewise,
btc_hh_disconnect first emits DISCONNECTING, not final disconnection.

Both firmware variants are built before the focused ASan/UBSan callback test.
It compiles the production hid() function and exercises acknowledgements,
valid open, descriptor and input, duplicate acknowledgements, stale close,
disconnecting/final close, repeated reconnect and a genuine SDP error.
PSP app/kernel bridge unchanged. Hardware test: component flash with flash_args
to preserve bonds, reconnect without deleting them first, then test input and
browsing. Newly discovered device pairing should also deliver input again.

## Follow-up: 0.3.6 internal memory and saved-controller reconnect

New mounted logs: latest session at 158809 ms reports wifi=2, reason=0, RSSI=-46
and internal_free=1019. Socket owners are alive (heartbeat 0–1 ms), requests
were written, reads repeatedly return EAGAIN. Previous session also had 1411
bytes free during playback. This supports internal memory pressure, not a
blocked socket task; it does not prove every server timeout has this cause.
Latest controller failure is state=6/error=7 at 69656 ms: ESP_HIDH_ERR_SDP.
IDF btc_hh.c saves HID descriptors but comments out the startup restoration call.

Build 0.3.6 restores that existing call, guarded by exact source SHA256. Reserve
channel staging (channels 2–5, RX/TX 4096 each) moves to PSRAM only in Bluetooth
builds. Six owners, payload sizes, primary channels, task priorities, timeout
and USB DMA placement remain unchanged. Known-device reconnect, explicit
row-targeted forgetting and persistent raw-HID button mapping are added.

Hardware checks: flash components (not factory image) to preserve bonds. Power
cycle controller/Onju and test a saved connection without Forget/Find first.
Check manual Disconnect does not immediately reconnect. In Buttons/reconnect,
assign a mismatched button using PSP controls, save and verify after restart.
Browse several library levels while connected, then play media/Monkey. Compare
`ESP network` internal_free and `ESP socket` progress with the previous run.

PSP and DIO/QIO builds precede focused host checks. The production HID parser
test covers raw button numbers, reassignment, duplicate targets, releases and
invalid settings. The socket harness checks the 16 KiB primary / 32 KiB reserve
staging split as well as existing transport ownership. The cache generator test
verifies the exact one-line change, rejects other source revisions and confirms
the SDK is untouched. These do not emulate controller reconnects or RF traffic.

## Follow-up: 0.3.5 controller works, server stalls

User confirms discovery, pairing and input. Mounted log reaches two discovered
devices at 91.550 s and connecting at 96.007 s. Received bytes plateau at 32625
from approximately 159 s; status RPCs continue at up to roughly 870/s, with no
reported USB transport errors. Socket pool free space stays 124856, peak live
sockets two. Later connect attempts fail with -1005. This does not identify
whether ESP scheduling, RF interference or another network issue causes it.

Limit unready POLLOUT status RPCs per socket to 50/s, across poll(...,0) calls.
Do not cache readiness, throttle actual payload transfers or extend timeouts.
Optional capability 64 / command 43 supplies six bounded per-socket snapshots.
Logs distinguish increasing would-block counts (owner running, no input) from
stale task heartbeats and report Wi-Fi state/reason/RSSI and internal free heap.
A connecting task can naturally have an aging heartbeat during its blocking
connect call. UINT32_MAX rx_age means no received bytes. Free slots retain the
last connection's counters until reuse; their heartbeat age is not starvation.
The sampled bitmask identifies acquired locks; unsampled slots are not zeroed
measurements. Diagnostic collection never waits on a socket owner lock.

Hardware test: update EBOOT and DIO or QIO firmware, keep HTTP and debug logging
unchanged, pair the controller and browse/play while using it. If traffic stops,
leave it running for 30 seconds before saving PSP/SYSTEM logs. Compare with the
controller disconnected, without simultaneously changing AP/protocol/clock.

Verification: PSP app and both firmware variants built before focused tests.
Production transport/socket harness passes (including 50/s status throttling,
nonblocking polls, prompt ready/error handling, independent sockets, diagnostic
counters and nonblocking snapshot locks), as does the protocol/configuration
harness under ASan/UBSan. No full-project suite. No compiler warnings in these
builds. Hardware outcome remains pending; firmware was packaged, not flashed.

## Follow-up: 0.3.4 PSP-first startup

Mounted log: five `wait USB attach` timeouts up to 153599 ms, attach success at
159692 ms after user removed the dongle. Following hotplug, CSR8510 `0a12:0001`
reaches phase 7, state 2 (host ready) at 183315 ms; inquiry is state 3 at
186584 and 195575 ms with zero error and zero discoveries so far. The last
snapshot is about nine seconds into the nominal 10.24-second inquiry, not
proof of a failed search. At the end an RPC times out; its cause is not yet
established by the log.

0.3.4 gates HCI interface claiming on the PSP's successful streaming-interface
claim, reproducing the successful plug order without requiring manual unplug.
This targets channel contention during initial PSP enumeration; it is not
claimed as hardware-proven. The existing discovery harness covers deferred
claim, released client handle, retry after PSP readiness and claim failure.
Hub polling and HCI discovery commands are unchanged. PSP application receives
one additional diagnostic line; no kernel-bridge ABI change.

## Follow-up: 0.3.3 discovery diagnostics

User confirmed the 0.3.2 hub connection. The new mounted PSP log measures
447–448 KiB/s on the StreamMaster echo test; Bluetooth snapshots remain
`adapter=0000:0000 state=0`, and Scan returns SM_OFFLINE (-2). This only proves
the USB Bluetooth adapter was never claimed, not why. The old discovery code
discarded failures before setting VID/PID/state.

The user's connected PC dongle is CSR8510 `0a12:0001`, full-speed, class
e0/01/01; HCI interface 0 alt 0 has event IN81/MPS16, bulk OUT02/MPS64 and
bulk IN82/MPS64. SCO interface 1 is unused. These match the implementation.
No controller pairing or host setting was changed on the PC.

0.3.3 exposes eight bounded last-probe records (including rejected hub/PSP
devices) via optional command 42 and PSP diagnostic log. Known adapters now
retain identity/error on claim failure. DIO, QIO and PSP application compiled
before the two focused tests: actual discovery/diagnostic C functions with CSR
layout and simulated claim errors, plus the existing HID descriptor/report
harness under ASan/UBSan. No change to hub polling, Wi-Fi tuning, HCI behavior
or allowlist was made without evidence. Exact hardware failure remains open.

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
