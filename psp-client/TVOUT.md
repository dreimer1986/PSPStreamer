# TV-out calibration stage

The first TV-out build intentionally does not alter streaming resolution.
At the library screen, press `SELECT + L + R` to enter a component-480p
calibration image. Press the same combination again to return to the PSP LCD.

`dvemgr.prx` provides the historical `pspDveManager` interface used by
PMPlayer Advance. It is included for this compatibility test from the
PMPlayer Advance source archive and is covered by that project's GPL-2.0-or-
later licensing. The PSP Streamer source that invokes it is GPL-2.0-or-later.

Only component cables can enter this first progressive test. A missing or
composite cable is reported as an on-screen signed hexadecimal result after
returning to the library.
