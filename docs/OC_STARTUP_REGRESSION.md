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
