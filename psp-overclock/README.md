# StreamerOC — optional experimental kernel plugin

This is separate from PSP Streamer and does not change its configuration file.
**Not hardware-validated. Leave disabled until you are ready for a controlled
test. Overclocking can freeze/reboot the PSP and corrupt files being written.**
Back up the Memory Stick; do not test while downloading/saving important data.
Never enable this together with expover, OverClockPlugin, the stress tester,
the picker plugin or ARK's own overclock override. No automatic maximum search.

## Origin and review

PLL arithmetic, denominator 20, 37 MHz base, ratio index 5, pipeline settle and
the gradual CPU/bus domain transition are adapted from m-c/d's
[Experimental Overclock Stress Tester](https://github.com/mcidclan/psp-beyond-444mhz/tree/ce746451b724599332ba1b315952f003a20ada75/experimental/tester).
This inspected upstream revision identifies itself as v2.6 (the user previously
tested v2.5); the exact revision is pinned here rather than claiming binary identity.
The MIT notice is retained in `LICENSE`. This is not a claim that every later
PSP revision supports that register recipe. Only SDK models 0, 1, 2 and 4
(01g, 02g, early 03g, Go/05g) are allowed to write clock registers.
Other models stay in monitor-only mode. No Vita/ePSP support.

The old experimental plugin tracks a requested `lastFreq`, not the actual
frequency; its PLL-ready wait is unbounded, and its downward transition starts
at the configured maximum rather than the actual register value. Its bitwise
PLL-index test is also not an equality test. Those assumptions are not carried
over here. The new implementation has bounded waits, exact ratio checks and
reads the actual numerator before reducing it. No global memory-protection
unlock is used. The default overlay writes a small rectangle into validated
VRAM without hooks; optional `overlay=2` hooks framebuffer submission syscalls.
If an application changes the PLL/domain setup during a yielded ramp, the
ramp aborts and enforcement is disabled rather than continuing blindly.

Startup now makes one reference-style `scePowerSetClockFrequency(333,333,166)`
call before the direct PLL sequence. It runs in the worker, with interrupts
and dispatch enabled, after the existing startup wait and safety checks.
The observed inherited OC multiplier at PLL ratio 3/4 is allowed into this
Sony preparation step; the direct register writer's stricter checks are not
relaxed. Sony failure aborts startup; success is followed by actual register
validation, never assumed to mean 333 MHz. CFW can replace the call with a
successful no-op. A safe unchanged state retains the existing direct fallback;
the narrowly recognized inherited state described below is first reduced.
Other unsafe mixed states are still rejected. Profile changes, enforcement ramps
and exit restoration remain direct and do not repeat the Sony call.

### Inherited OC startup recovery (`build=oc-inherited-start-3`)

PSP Streamer's repeated failure was phase 12 / -4 before app control became
active: Sony returned success but left control 3 and multiplier `0124E114`.
The player contains no Sony clock setter. Its app-control requests are not
accepted until plugin startup completes; this was not intentional app ownership.
The installed ARK settings also contained no explicit CPU override. The logs
prove the ineffective call, not which firmware/plugin component caused it.

After a successful Sony call, startup now recognizes only control 3 or 4,
upper multiplier bits `0124`, denominator 20, numerator 181..254, and both
domains exactly `01FF01FF`. It decreases the numerator one unit at a time to
180 **before** any ratio increase. Each write retains the denominator, upper
bits and domains, followed by the inline settling sequence. The worker yields
between steps and checks all four registers before and after each write, plus
after the last yield. Cancellation or interference stops the operation.

This preparation does not claim normal ratio-5 ownership. On cancellation it
leaves the reduced state in place: no blind restore is attempted while still
at ratio 3/4. The existing strict `apply()` path then accepts the normalized
180/20 state and performs its established adjacent ratio transition and ramp.
An effective Sony call, stock 9/1, and already-valid ratio-5 state do not enter
this fallback. No Sony hooks are removed, no calls are repeatedly forced, and
the player's code/configuration is unchanged.

`clock_inherited_reduce_begin` and `clock_inherited_reduce_ready` bracket the
fallback; numeric phase 21 identifies its failures. No per-step file writes.
Eleven OC host checks pass, including the exact recorded tuple, control 3/4,
downward-only writes, baseline multipliers at ratio changes, cancellation,
foreign writes, unknown PLL recipe rejection and the inline machine-code check.
Hardware confirmation of this new fallback is pending. Test with ARK Auto,
the existing INI and PSP Streamer both directly and after leaving a game.

Events `clock_sony_baseline_begin` and `clock_sony_baseline_result` bracket the
call. `sony_baseline_result` is its hexadecimal return value (`80000000` means
not called yet); the same snapshots contain the before/after clock registers.
Host checks cover the call order, inherited-state preparation, patched no-op,
failure, cancellation and existing ramps/restoration. Hardware safety of the
reintroduced Sony call is **not yet confirmed**. Keep the configured tested
target, start with ARK Auto, and compare startup plus subsequent app starts.

Sony's cached getters do not describe a direct PLL overclock. Register writes
use the reference's CP0 Status.IE masking and
dispatch suspension; neither file I/O nor kernel calls occur inside that
critical section. For the observed 222 MHz / ratio-3 state,
normalization uses ARK-5's adjacent ratio sequence (3, 4, 5), checking each
completed step instead of jumping to 5. Unknown initial ratios fail closed.
The original multiplier is preserved until ratio 5 is confirmed. Only then
is the stock 9/1 representation converted to 180/20 and the OC ramp started.
This explicitly establishes the baseline requested by the reference tester
instead of changing the PLL denominator while still at ratio 3.
See [startup investigation and regression checks](../docs/OC_STARTUP_REGRESSION.md).

This is a bounded adaptation, not a verbatim copy of the two-minute stress
test. The ramp uses numerator steps with 10 ms yields. It does NOT validate
silicon stability; a previously tested target can still fail in a different
game, on battery or during hardware activity. Register settling can briefly
stall other work. Neither uninterrupted audio nor universal app support is
promised during reapplication.

## Install and test

Build with the PSPDEV toolchain: `make -C psp-overclock`.
Copy `StreamerOC.prx` and `StreamerOC.ini.example` (renamed to `StreamerOC.ini`)
into `ms0:/SEPLUGINS/StreamerOC/`. In ARK's plugin manager enable this PRX for
**GAME/homebrew only**, not XMB or POPS. The worker additionally refuses
non-GAME application contexts and stops enforcement if it observes a context
change. This is not a load-exec hook. Do not overwrite your existing plugin
list. Defaults are `enabled=0`, `target_mhz=333`, `enforce=0`,
`enforce_unlimited=0`, `app_control=1`, `report=1`, `overlay=1`.
Simply loading it with that config must not change CPU clocks.

For ARK-5, use one line in your existing plugin list:

```text
homebrew, ms0:/SEPLUGINS/StreamerOC/StreamerOC.prx, on
```

`homebrew` restricts loading to homebrew, including PSP Streamer. Use `game`
instead if you also want PSP games; ARK's `game` runlevel includes homebrew.
Do not add both lines for the same plugin.

1. With other clock overrides disabled, start a homebrew and wait six seconds.
   Check `StreamerOC-status.txt` beside the plugin. Hold L+R+SELECT to refresh
   this file on demand. It never draws into the application's framebuffer.
   With the default disabled config, expect `power_callback_ready=1`, a
   `power_callback_slot` from 0 to 15, and `enabled=0`. Verify this before
   enabling clock changes. A callback registration failure still blocks them.
2. First test `enabled=1`, `target_mhz=333`, `enforce=0`, restart the homebrew,
   and check status, playback, Stop and exit. Only then try a frequency known
   to be stable on this particular PSP. Accepted requested range: 66–471 MHz.
   Targets below 333 MHz keep the PLL at 333 MHz and use direct CPU/bus
   dividers (bus approximately half CPU). These low-frequency targets are
   our adaptation, not a hardware-validated feature of the reference tester.
   The 9-bit divider quantizes requests by less than 1 MHz; the overlay shows
   register-derived estimates rather than pretending the request is exact.
   333 MHz itself keeps the established reference full-domain initialization;
   higher targets use its PLL ramp, without calling a Sony setter beforehand.
   Before switching profiles, a custom PLL is ramped down and restoration
   errors stop the transition. Sony getters alone cannot confirm a restored
   clock; register estimates are checked too. Low frequencies can starve
   decoding, networking or Memory Stick I/O; 66 MHz is a limit, not a promise
   that a particular workload runs there. Start underclock tests at 222 MHz.
3. With `enforce=1`, the worker checks every 500 ms and reapplies the target if
   an app changes the clocks. This is eventual correction, not a syscall hook
   or an absolute lock. More than three reapplications within one minute
   disables enforcement to avoid an endless fight with the app/another plugin.
   After an application sends a valid clock SET through the plugin API,
   ordinary enforcement is suppressed for the remainder of that app session.
   Explicit application requests still work; clock observation and reporting
   remain active. Merely querying the clock or an invalid request does not
   activate this exemption.
   Optional `enforce_unlimited=1` removes this conflict-count limit **only when
   `enforce=1`**. It keeps trying at the bounded 500 ms checks rather than
   giving up after three corrections. It also overrides the cooperating-app
   exemption and enforces the latest requested target (not a forced return to
   the INI target). The default is off. Suspend, leaving
   GAME, failed clock writes, unsupported models and startup bypass still stop
   it. This opt-in can fight another clock plugin and grow the event log;
   disable competing overrides rather than using it to hide hardware errors.
4. Suspend/resume disables enforcement until app restart. Holding R through
   the initial six-second delay bypasses all overclock writes. Config is read
   at plugin startup, not hot-reloaded. For recovery, disable/remove the plugin
   in ARK and cold-reboot; do not rely on unloading after a hardware freeze.

`report=0` disables status file writes. It is independent of PSP Streamer's
`debug` switch because this plugin also runs in other applications. Reporting
only happens at startup/state changes or on demand, not every polling tick.

### Optional application control

The optional device driver is published only after initial clock setup,
not from `module_start`. Client discovery retries until it is available.
The `control_driver_ready` event records registration afterwards; early
startup checkpoints intentionally show `control_driver=0`.

Add `app_control=1` to the plugin INI to allow temporary application clock
profiles. `enabled=1` and successful startup/power callback registration are
also required. The default is `app_control=1`; old INI files remain valid.
An explicit `app_control=0` still disables requests. Defaults apply only to
missing keys; existing explicit values are never silently overwritten.
Requests above 333 MHz cannot exceed your configured `target_mhz`, so an app
cannot silently raise your tested overclock ceiling. A zero request returns
to the INI target without ending the session's enforcement exemption. Requests
never rewrite the INI. Reports include `app_control_active=1` after the first
accepted SET. The latch resets when the application/plugin starts again.

The optional `streameroc:` device exposes the buffer-free `sceIoDevctl`
commands in `control_api.h`: STATUS (ready/pending/error), CPU_KHZ (register
estimate, Sony fallback), TARGET, and SET ORed with MHz. No mandatory client
imports or user pointers are required. The plugin worker performs all changes;
callers must wait for STATUS to finish before assuming a target was applied.
Suspend, unsupported models, startup bypass and conflict protection still
disable clock control. The device is removed when the plugin unloads.

PSP Streamer also sends PREPARE_EXIT before `sceKernelExitGame`, both from
the browser and the HOME/PS exit callback. The worker rejects further clock
requests, stops its overlay and restores only its own recognized clock state
before acknowledging EXIT_STATUS. The player waits at most 2.5 seconds;
missing/older plugins are tolerated. This is not a global exit hook for other
apps. It avoids depending solely on plugin unload during loadexec, but cannot
recover an already stalled CPU or guarantee exit from a hung firmware module.

With `overlay=1`, startup, application requests and observed clock changes
also show the existing overlay for five seconds. Changes made by other apps
are sampled every 500 ms, including monitor mode; very short changes between
samples may be missed. `clock_observed_change` and `app_clock_result` events
are appended when `report=1`. Manual overlay and snapshot chords still work.

### Persistent event history

With `report=1`, `StreamerOC-events.log` beside the plugin is an **append-only
history across application starts**. `StreamerOC-status.txt` remains the latest
snapshot. Copy the events log after returning to XMB; starting another homebrew
does not erase earlier sessions. No INI changes are needed.

Each event includes the application pathname (when available), session start
tick in microseconds, worker ID, elapsed milliseconds, configuration, callback
diagnostics and clock registers/estimates. Times are PSP system-clock ticks,
not calendar dates; they can restart after reboot. File order and session-start
records separate runs. Logged events include:

- Session start and startup result (including bypass/initialization failure).
- With reporting enabled, startup wait completion, clock request start,
  `clock_raw_baseline_begin`, completed domain initialization and bounded
  ramp checkpoints. These distinguish early driver/startup failures from an
  actual clock transition; no file writes happen with interrupts disabled.
  Additional diagnostic checkpoints mark the completed interrupt-guard round
  trip (`clock_raw_guard_ready`), multiplier conversion, and each settled PLL
  ratio (3/4/5). Reporting temporarily releases the guard at those boundaries;
  all four clock registers are checked again before continuing. A foreign
  change aborts rather than resuming from stale assumptions.
- Detected target mismatch **before** correction, reapplication result and
  enforcement/conflict-limit shutdown.
- Suspend/resume, recorded after resume with the observed suspend tick/flags.
- L+R+SELECT snapshots, once per chord press rather than every held poll.
- Leaving GAME context and orderly worker shutdown, before/after restoration.

Unchanged polling iterations do not write. This is not a CPU trace: clock
changes between polls are not automatically captured. Observed changes are
also recorded in monitor mode. Each event is written and closed immediately; partial
writes are handled. `previous_journal_result` reports the preceding append's
I/O result (zero on success), so a later snapshot can expose a failed write.
After both files close, `sceIoSync` flushes their device; a sync failure is
also carried forward in `previous_journal_result`. This matters for diagnosing
hard shutdowns with Memory Stick caching enabled.
`report=0` disables **both** files. The log does not auto-rotate or delete old
sessions; archive/remove it on a PC when no application is using the plugin.

Power callbacks perform no file I/O. A suspend record is held in RAM until
resume; shutdown while suspended loses that pending event. Forced load-exec,
crashes or power loss may omit the final shutdown record (or the latest write).
Already-written events remain useful; an exit record is not required to retain
earlier sessions. No crash-proof or synchronous hardware-flush guarantee is made.

### Power callback diagnostics

Automatic power callback slot assignment can fail in kernel plugins. Like the
explicit-slot approach documented in
[PSP-KillSwitch](https://github.com/crozone/PSP-KillSwitch/blob/main/killswitch.c),
StreamerOC now tries slots 15 through 0 if automatic registration fails.
Occupied slots are left alone. Cleanup unregisters only our successfully
registered slot, not the zero returned by explicit registration.

The report includes `power_callback_id` (callback creation result),
`power_auto_result` and `power_register_result` as hexadecimal SDK results.
`FFFFFFFF` means not attempted for the registration fields when callback
creation itself failed. A negative `power_auto_result` is harmless if the
fallback succeeds: `power_callback_ready=1` and `power_register_result=00000000`.
`configured_enabled` records the parsed configuration before runtime safety
checks; `enabled` is the effective state. Registration is checked even when
clock changes are disabled. The correction does not change the PLL recipe,
requested frequency, enforcement policy or suspend/resume protection.

### Configuration loading diagnostics

L+R+SELECT refreshes the report using the running plugin's state. It does **not**
reload the INI or apply a new clock target. Fully exit and restart the homebrew
after editing the INI; the report's modification time is not its config-load time.

`config_path` identifies the startup INI. `config_state=loaded` confirms that
the complete file was read and validated. `config_bytes` and `config_keys`
record bytes read and recognized settings; the sample has seven settings
(older files remain valid; omitted overlay and app control now default to on).
`config_io_result` gives the hexadecimal I/O result (zero after successful EOF).
`config_error_line` identifies a parse error, or is zero on success. Failed
open/read/close operations and invalid files keep the safe defaults and report
the failure instead of silently appearing to be a deliberately disabled config.

The parser has no shared tokenization state. It accepts LF/CRLF, a UTF-8 BOM,
spaces around `=`, and `#`/`;` comments. Unknown keys, invalid booleans (anything
except 0 or 1), out-of-range targets and empty/comment-only files are rejected.
Settings are committed only after the whole file passes validation. No runtime
reload or automatic overclock activation has been added.

### Optional diagnostic overlay (experimental)

### Clock diagnostics (inline-settle build and later)

The latest reported Soul Calibur shutdown ended after `clock_raw_guard_ready`
at ratio 5, multiplier 9/1 and full CPU/bus domains. The Sony call had returned;
the OC ramp and overlay installation had not begun. That brackets an interval,
not the exact failing instruction. The earlier empty-event report likewise
does not prove memory corruption or a competing clock writer.

Machine-code inspection found a concrete difference from the pinned tester:
its settling delay is inline assembly at the write site, whereas our ordinary
static C helper became an out-of-line function under `-O2`. The PLL store was
followed by `sync; jal settle`. The delay is now forced inline, including the
reference's leading `sync`. No call/return is needed during that settling
interval. The loop count, register recipe, target, Sony preparation and all
ownership/abort guards remain unchanged. This does not lock the instruction
cache and **is not proof that the shutdown is fixed**; hardware confirmation
is required. No cache manipulation, voltage change or memory unlocking was added.

Hardware follow-up (2026-09-20): after a restart, the user ran Soul Calibur,
Tomb Raider, GTA Vice City Stories, then the same three games again, followed
by PSP Streamer. All seven starts survived. The six game sessions recorded
`target applied`, `diag_failed=0` and a register estimate of 416250 kHz for the
418 MHz request. Game names are assigned from the user's sequence; the log
itself reports only the shared `disc0:/PSP_GAME/SYSDIR/EBOOT.BIN` path.
This supports the inline-settle correction, but is not a general stability
guarantee. The final PSP Streamer session did **not** apply OC: Sony returned
success without changing ratio 3 / multiplier `0124E114`, and the raw-path
input guard rejected that inherited state with phase 12 / result -4. The
plugin disabled clock writes as intended. That initialization case remains
open; a running player is not evidence of successful overclocking.

With `report=1`, every existing event now also includes a record sequence,
event-string address/length/FNV-1a hash and numeric clock diagnostics. Empty
event strings are reported as `EMPTY_EVENT`. The clock algorithm, Sony call,
limits, waits and enforcement policy are unchanged. Instrumentation adds some
CPU work and longer reports, so timing-sensitive symptoms can still change.
There are no additional file writes inside clock locks or per multiplier step.

`diag_failed=1` selects the **first** failure of the current operation.
`diag_phase`, `diag_line`, `diag_result` and `diag_argument` identify the stage,
source line in this build, exact signed return code and current ramp numerator.
`diag_actual` is captured at that decision, before releasing an active clock
guard. `diag_owned` is the register tuple the plugin last recorded as its own;
`diag_changed` indicates whether that ownership is active. Both tuples use
`PLL control / multiplier / CPU domain / bus domain` ordering.
`diag_checkpoint_expected` contains the last pre-I/O checkpoint tuple.
`diag_last_*` shows the most recent stage even if the first failure was retained.
The guard markers should remain `4F434442/4244434F`. Snapshot text uses static
worker-only storage rather than increasing the worker's stack requirement.

Phase IDs: 1 startup, 2 Sony input guard, 3 Sony call, 4 Sony return;
10 apply, 11 PLL-ready wait, 12 input validation, 13 post-report checkpoint
(argument identifies its parent phase), 14 ratio normalization, 15 multiplier
normalization, 16 domain normalization, 17 low-clock profile, 18 OC ramp step,
19 ramp yield, 20 final comparison, 21 inherited startup reduction;
30 restore, 31 restore multiplier,
32 restore domains. Common returns: -1 ready timeout, -2 cancellation/suspend,
-3 changed/unexpected registers or ownership mismatch, -4 unsupported input;
Sony API errors retain their original value. Stage and raw tuples distinguish
these cases without assuming that every -3 was caused by another app.

The eleven OC maintenance checks cover clock/diagnostic mock registers,
initialization order, configuration, enforcement, callback registration,
control permissions, logging and overlay pixel bounds. They do not establish
hardware safety. Two stale test expectations were updated: overlay mode 2 is
valid, and cancellation now records its return through `OC_RESULT`.
The machine-code regression check additionally compiles the real delay helper
with the PSP compiler at O0/O2/O3 and verifies the in-place instruction sequence.
The next comparison is Soul Calibur after a full restart versus after another
game; preserve the logs after a failure rather than repeating hard shutdowns.

### Overlay controls

Add `overlay=1` to `StreamerOC.ini` and restart the application. Hold
**L+R+Triangle** briefly to show CPU/bus register estimates, requested target,
Sony's clock report, effective enabled state, enforcement setting and callback
availability for five seconds. Release and press again to hide it early.
L+R+SELECT still appends a snapshot to the log. `overlay=1` is the default;
the overlay is independent of `report`. Key combinations are not consumed,
so the application can also react to them.

With `overlay=1`, this is a small ASCII overlay at the upper left, refreshed at up to 30 Hz only
while visible. Drawing starts in a detected VBlank; polling yields and is
bounded to 20 ms. A missing display or suspend cancels the wait. This reduces
the gap between redraws but cannot prevent applications overwriting the OSD,
nor guarantee that all pixel writes fit inside the blanking interval.
The clock enforcement check remains at 500 ms. No GU state,
display mode, frame-buffer selection or syscall/display hooks are changed in mode 1.
The rasterizer accepts validated VRAM-backed 565/5551/4444/8888 surfaces up to
720x480 and uses their actual stride. Unsupported/main-RAM framebuffers are
skipped. TV-Out visibility depends on whether the active TV surface is exposed
by the standard display query; support is not promised for every TV mode/app.

Applications continue rendering independently: flicker or partial overwrite is
possible, particularly with double buffering or hardware video. This is **not**
a universally composited overlay. When hiding, only pixels still matching our
paint are restored; application redraws are left alone. A coincident identical
pixel cannot be distinguished from our own. Mode changes or suspend discard
stale backup pixels instead of restoring them into a different layout; a static
screen may retain traces until its application redraws. Set `overlay=0` if an
app misbehaves. No display writes occur in the power callback.

Log timestamps now use 32-bit formatting components instead of `%llu`, which
produced zero fields on the tested kernel. A real-device test must confirm the
corrected output. Elapsed milliseconds wrap after roughly 49 days in one run.
Missing final events on forced application replacement remain a limitation;
no global exit callback is claimed or installed, and clock restoration across
such replacement is not guaranteed. Earlier event records are retained.

## What the reported speed means

The report separates Sony's API value from the register-derived estimates:
`PLL = 37 MHz × numerator / denominator`, then
`CPU = PLL × CPU-domain numerator / denominator` (likewise bus).
This calculation is valid only for the supported recipe/PLL ratio. Unknown
ratios or invalid divisors return 0 meaning **unknown**, not a stopped CPU.
For example requested 443 MHz selects numerator 239 and estimates 442.150 MHz;
requested 471 selects 254 and estimates 469.900 MHz. The target is not echoed
as if it were a measurement.

The stress tester itself shows target/saved values and rendering throughput;
it does not provide an independently calibrated CPU cycle measurement. A
synthetic timed-loop benchmark is therefore deliberately not labelled “MHz”.
No register readout proves performance or stability; both need real hardware
tests. Host tests cover formula/model bounds, not physical PLL behavior.

### Framebuffer hook: overlay=2

Mode 2 hooks the standard user-mode `sceDisplaySetFrameBuf` syscall through
ARK's SystemControl API. After a successful submission, it draws into that
submitted VRAM surface, using its pixel format and stride. The original call,
its parameters and return value are preserved. Kernel/direct display calls
are not patched. No GE command lists, framebuffer addresses or video modes
are replaced. The original syscall mapping is restored on worker shutdown;
module unload is refused if a tracked presentation call is still executing.

Clock reads and text formatting stay in the worker. The hook only draws cached
text and maintains three per-buffer backups, covering usual double/triple
buffering. It does not wait for VBlank, allocate, log or set clocks. When hidden,
each buffer's unchanged overlay pixels are restored on its next submission.
The worker also restores the current buffer when hiding, including static
screens. Suspend/mode changes discard stale backups. Other overlays hooking the
same syscall are not a supported combination.

This aims to reduce flicker, **not guarantee universal compatibility**: late GE
writes can still overwrite the text and immediate submissions can tear. If no
hook-driven draw occurred for 100 ms while visible, the worker refreshes the
current framebuffer near VBlank. This covers static menus, in-place rendering
and apps bypassing the syscall. Both paths share one raster lock and per-buffer
backups. Normal continuously hooked presentation does not use this fallback;
fallback rendering can still flicker like mode 1. If the display export cannot be found,
the worker falls back to mode-1 polling and logs
`overlay_hook_unavailable_polling`. Otherwise logs include
`overlay_hook_installed`, `overlay_hook_present_calls`, `overlay_hook_draws`,
`overlay_hook_fallback_draws` and `overlay_hook_rejected`. These distinguish a
hook actually drawing from mere installation, unsupported surfaces and fallback
refreshes. A zero call count during rendering suggests this app bypasses the
hooked path or does not submit new buffers. Use `overlay=1` to revert
or `0` to disable, then restart the app. Default remains 1. This version was
compiled without running tests, as requested; hardware validation is pending.

## Editing from PSPStreamer

In PSPStreamer, open **Select → StreamerOC plugin → X**. The submenu edits all
seven INI options without a PC. **Start** saves the plugin INI (with a `.bak`
copy); **Circle** cancels. Changes apply at the **next application start** and
do not touch the running clock-control path. This is separate from saving the
player's CPU profiles. Only use target clocks already tested on your PSP.
