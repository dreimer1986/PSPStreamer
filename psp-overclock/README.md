# StreamerOC — optional experimental kernel plugin

This is separate from PSP Streamer and does not change its CPU settings.
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
reads the actual numerator before reducing it. No framebuffer hooks or global
memory-protection unlock are used.
If an application changes the PLL/domain setup during a yielded ramp, the
ramp aborts and enforcement is disabled rather than continuing blindly.

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
list. The sample config has `enabled=0`, `target_mhz=333`, `enforce=0`, `report=1`.
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
   to be stable on this particular PSP. Accepted requested range: 333–471 MHz.
3. With `enforce=1`, the worker checks every 500 ms and reapplies the target if
   an app changes the clocks. This is eventual correction, not a syscall hook
   or an absolute lock. More than three reapplications within one minute
   disables enforcement to avoid an endless fight with the app/another plugin.
4. Suspend/resume disables enforcement until app restart. Holding R through
   the initial six-second delay bypasses all overclock writes. Config is read
   at plugin startup, not hot-reloaded. For recovery, disable/remove the plugin
   in ARK and cold-reboot; do not rely on unloading after a hardware freeze.

`report=0` disables status file writes. It is independent of PSP Streamer's
`debug` switch because this plugin also runs in other applications. Reporting
only happens at startup/state changes or on demand, not every polling tick.

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
