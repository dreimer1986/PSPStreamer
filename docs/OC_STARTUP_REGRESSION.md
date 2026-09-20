# StreamerOC startup regression investigation

The September 20 hardware report is a hard power-off shortly after starting
PSPStreamer with plugin build `b6d0a6b`. The user also reproduced it with all
application CPU profiles disabled. Do not attribute it to the later profiles.

The three recorded sessions on the Memory Stick contain `session_start` but
no `startup_result`. All request 433 MHz, model 2, callback slot 15, successful
driver registration. Initial PLL control is 5, multiplier `01240901`, and both
CPU/bus domains are `01FF01FF`: the register estimate is 333 MHz. These are
the actual initial values used in the extended host regression harness.

The newly added below/at-333 Sony shortcut cannot by itself explain this
433 MHz startup. The source and generated Allegrex instructions for the
433 MHz ramp were compared with the preceding implementation. The clock
sequence itself did not change on that initial path. Driver registration and
the report changed before it; automatic overlay notification follows the
`startup_result` write. Because the old writes were not explicitly synced,
ARK's enabled Memory Stick cache can also lose the last records during a hard
power-off: their absence does **not** conclusively exclude the overlay or a
later instruction. The exact failing instruction is not known.

ARK's existing clock setting is not evidence that the user's configuration
caused a newly introduced regression. It has not been changed. ARK-5's
[clock service](https://github.com/PSP-Arkfive/ARK-5/blob/main/SystemControl/src/cpuclock.c)
can replace Sony clock setters with successful no-ops. Host tests now cover
that behavior too; success returns must not be treated as proof of a low clock.

Separate transition defects corrected during review:

- 333 MHz uses the complete reference PLL/domain initialization, as before
  profiles were introduced, instead of the Sony-only shortcut.
- A previously owned custom clock is restored before entering another mode;
  failed/refused restoration is propagated, not silently ignored.
- Underclock preparation shares the normalized baseline; register changes
  made by another owner prevent restoration based only on stale Sony getters.

Reporting now brackets the individual startup/clock stages and records ramp
progress every 16 numerator steps and at completion, outside interrupt locks.
Reports explicitly sync their storage device after closing both files.
This is a diagnostic candidate, **not a hardware-confirmed fix of the reported
433 MHz shutdown**. No feature, profile, default, ARK setting or personal INI
has been removed or changed to mask the problem. Further confirmation requires
the next hardware report; do not repeatedly reproduce a hard power-off.

## Follow-up: synchronized hardware trace

Session `2935776` from the next test reaches `clock_sony_baseline_done` at
6294 ms. During the startup delay, the state changes from ratio 5 / 333 MHz
to ratio 3 / Sony 222 MHz. Both before and after the successful Sony baseline
call, the ratio remains **3**, multiplier `01240901`, domains `01FF01FF`.
No `clock_domains_ready` follows. This localizes the observed failure window
to the baseline PLL/domain normalization, not the later 433 MHz ramp or an
application-requested underclock. It does not identify the exact failing
hardware instruction.

The normalizer incorrectly jumped directly to ratio 5 when Sony had not
actually established its requested baseline. It now follows the adjacent
index sequence in ARK-5's
[adjustPLLRatio](https://github.com/PSP-Arkfive/ARK-5/blob/main/Compat/PSP/src/overclock.c):
re-latch 3, then 4, then 5, settling and verifying each completed transition.
Interrupts and thread dispatch remain blocked during the bounded normalization;
logging stays outside that section. Supported initial ratios are 3, 4 and 5;
unexpected ratios fail closed without a guessed transition.

The regression test now starts with the **post-delay** recorded registers and
simulates a Sony success/no-op. It verifies the exact `83,84,85` control writes,
then the existing 54 numerator steps to requested 433 MHz (432.9 MHz estimate).
Separate tests cover unchanged ratio 5, ratio 4, failed completion at each step,
unexpected readback and unsupported initial ratios. Builds remain `-O2 -G0`.
The correction still needs PSP confirmation; host mocks do not model PLL physics.

## Follow-up: direct-register path and reference interrupt protection

The next hardware test still powers off. Session `2933780` ends at
`clock_sony_baseline_done` (6289 ms), again with ratio 3, multiplier
`01240901`, full CPU/bus domains and Sony reporting 222 MHz. Adjacent ratio
steps alone did **not** fix the failure. The failing instruction is still
unknown; neither the later 433 MHz ramp nor the 133 MHz app profile is reached.

The implementation now removes **all Sony clock setters**, including the
restore and underclock branches. Initial normalization accepts only known
stock/custom multiplier recipes and ratio indices 3–5. Below 333 MHz the PLL
remains at 333 MHz; CPU/bus numerators are stepped toward bounded 9-bit
divider targets. Above 333 MHz the established numerator/20 PLL ramp remains.
333 MHz preserves the reference full-domain recipe. No configuration setting,
profile, driver API, overlay or reporting feature is removed.

The OC tester's
[CP0 Status interrupt guard](https://github.com/mcidclan/psp-beyond-444mhz/blob/ce746451b724599332ba1b315952f003a20ada75/experimental/tester/main.h)
is now used for register writes, alongside dispatch suspension. Previously
the plugin used `sceKernelCpuSuspendIntr` instead. The generated Allegrex code
was inspected for save/mask/restore of CP0 register 12, including the reference
sync/nop sequence. This is a concrete reference-alignment change, **not proof
that the interrupt mechanism caused the shutdown**. All SDK calls, waits that
yield threads and report writes stay outside the raw critical section.

Host tests exercise the actual `clock_transition.h`, with mocked hardware:
every 66–333 MHz divider target, 433→133→433, the recorded ratio-3 startup,
stop/suspend during the ramp, changed-owner rejection, invalid input and ready
timeout. Ownership is rechecked after report I/O as well as ramp yields.
Explicit enforcement may adopt a recognized external clock again; restoration
never silently overwrites another owner's changed tuple. The build is still
`-O2 -G0`. Hardware stability and underclock operation remain unverified until
the user tests this build. No player EBOOT or personal INI was changed.

## Direct-path hardware failure and narrower diagnostic boundaries

Session `2933581` again hangs. The mounted PRX SHA-256 matches build
`4697053` (`6c1abf4193393781501b0bfa4e70cf3b46fc7fefccea9f3310c95ef252b55b6c`).
The last persisted event is `clock_raw_baseline_begin` at 6238 ms, with
CTL `3`, MUL `01240901`, and CPU/BUS `01FF01FF`. Removing Sony setters and
using the reference CP0 guard did **not** resolve the observed failure.

No further clock formula or target change is introduced in this diagnostic
build. The existing settled normalization boundaries are now separated by
synced checkpoints: a guard-only round trip before the first register write,
multiplier conversion, and completed ratio 3, 4 and 5 transitions. The next
missing checkpoint narrows the failing region; absence alone still cannot
identify one instruction. File I/O is always outside CP0/dispatch protection.
After reacquiring the guard, changes to any clock register, suspend or stop
abort the transition. Ownership is recorded before yielding, never adopted
from a foreign change during logging. Host tests inject external writes at
each new checkpoint and verify that no further ratio steps occur.

This changes timing by adding diagnostic I/O between settled stages. It is
explicitly a **diagnostic build, not a claimed shutdown fix**. A single next
hardware run is needed to locate the remaining failure. The INI and ARK
configuration remain untouched; repeated hard-power-off testing is not advised.

### Stable-to-first-regression comparison

Compared plugin sources at `93a354f` (before the feature addition) and
`b6d0a6b` (first feature build). The initial >333 MHz register sequence,
six-second delay, worker priority `0x30`, stack `0x4000`, power callback,
settle loop, register arithmetic and `-O2 -G0` flags were unchanged. The
new <=333 branch was not selected for a cold start at 433 MHz. Nothing was
removed from that high-clock path in the first regression commit.

Added behavior comprised the optional I/O driver and its command handler,
application clock requests/low profiles, automatic overlay notification,
observed-clock notifications, unlimited enforcement and larger report fields.
The latter runtime features start after initial clock application; unlimited
enforcement is disabled in the user's INI. Driver registration, however, was
new work in `module_start`, before the old OC worker even began. Queries of
clock registers were available immediately, although SETs were gated.

This build defers registration until after `startup_result`, preserving the
application-control feature and allowing existing client discovery retries.
Worker shutdown is joined before removing the driver to close a registration/
unload race. Thus the newly introduced device/callback interface is absent
throughout initial clock work. The next trace will establish whether failure
still occurs without it. This isolates an actual first-regression difference;
it does **not** establish driver registration as the cause. Diagnostic
checkpoints remain to narrow the failing stage if it still occurs. Personal
INI/ARK settings, app profiles and the player EBOOT are not modified.

## Guard completed; failure before multiplier checkpoint

Session `2934845` with verified build `a340ef7` ends at
`clock_raw_guard_ready` (6291 ms): CTL `3`, MUL `01240901`, full domains,
Sony 222 MHz and `control_driver=0`. The device interface is not registered,
and the guard-only round trip completed. This rules out the new driver as a
necessary cause of this reproduced hang. The next missing checkpoint is
`clock_raw_multiplier_ready`, before ratio transitions or the 433 MHz ramp.
The precise failing instruction remains unproven: this interval also includes
guard reacquisition, the multiplier write, settling and checkpoint handling.

The reference tester requests 333 MHz before initial multiplier adjustment.
Our old Sony baseline call returned success without establishing that state;
removing it still left denominator conversion ahead of baseline preparation.
The correction now preserves the original multiplier through the bounded
3→4→5 ratio transition, verifies ratio 5, and only then converts 9/1 to
180/20. No new frequency formula, target, profile, or client API is introduced.
Existing ratio checkpoints now precede the multiplier checkpoint.

The host harness rejects every multiplier write outside ratio 5, checks the
original multiplier at each startup ratio step for initial indices 3 and 4,
and injects completion failures at indices 3/4/5 to prove that no multiplier
write follows a failed baseline transition. All twelve maintenance tests pass.
This is a correction to the baseline precondition, not a hardware-confirmed
fix or a complete explanation of why earlier builds appeared stable.
