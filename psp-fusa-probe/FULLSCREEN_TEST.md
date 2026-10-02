# FuSa-style fullscreen experiment 0.2 — hardware validation required

This is a new, separate test implementation, **not a repaired release of FuSa**
and not yet part of Consolizer. It enlarges the completed 480x272 game image to
720x480 component output. It does not increase a game's internal resolution.
Use the TV's 16:9 interpretation of 480p; sample pixels are not square.

## Deliberately narrow first test

- PSP-3000 (`model=2`), reported firmware 6.60 or 6.61, GAME only.
- Component cable, Sony game TV mode `0x2D2`, 480x272.
- 16-bit source formats 5650/5551/4444, stride 512 or 1024, wholly in lower 2 MiB.
  The user's probe captured 5551/512. 32-bit games are refused for now.
- Manual activation; automatically attempts restoration after 30 seconds.
- Target 30 scaled pictures per second, including scaling time. Two packed
  source snapshots in upper VRAM separate capture from scaling: the game caller
  briefly copies a submitted frame before returning; the worker never scales a
  live game buffer except the initial activation image. No allocation or waiting
  in the capture hook; interrupt masking covers only small state changes.
  Games submitting before their GPU finishes may still need additional handling.
- A consumed snapshot cannot be overwritten. Stale queued snapshots can be
  replaced without blocking the game. Destination reuse waits for a VBlank after
  presentation. Copy/scale count and mean/max microseconds are logged at rollback.
- No clock changes, no old inline patch trampolines, no firmware offsets,
  VBlank replacement, game-thread suspension or SpeedBooster.

## Install and test

1. Copy the `SEPLUGINS/FuSaFullscreenTest` folder, including `dvemgr.prx`.
2. Temporarily disable **GAME** entries for StreamerOC, PSP Consolizer, old FuSa,
   FuSaProbe, save-state plugins and any other display hook. No need to change
   their INIs; keep a copy of your ARK plugin list to restore afterwards.
   This test refuses startup if OC/Consolizer/PSPStreamer modules are detected.
3. Add/enable this ARK entry (supplied example is off):

   `game, ms0:/SEPLUGINS/FuSaFullscreenTest/FuSaFullscreenTest.prx, on`

4. Start the **same game as the probe**, wait at least 10 seconds, enter actual
   gameplay after intro movies, and hold Sony's screen button to enable normal
   component TV output. Do not save progress during the experiment.
5. Press physical **NOTE (music-note button) + R** together, then release. This
   toggles the scaler. After 30 seconds it attempts to return to normal TV output.
   Press the same chord again to return earlier. HOME or SCREEN also requests
   restoration; their normal system function is not swallowed.
6. Note: correct/full picture? Colours? Sound? Successful return to normal TV?
   Then exit the game and return `SEPLUGINS/FuSaFullscreenTest/test.log`.
7. Disable the test and restore your previous GAME plugin entries afterwards.

If nothing changes, return the log rather than repeatedly forcing activation.
If the screen goes black, release the buttons and allow the 30-second rollback.
If the system itself hangs, the timeout cannot guarantee recovery; restart and
disable the test. The first hardware run is experimental, not guaranteed safe
for unsaved gameplay. No other plugin configuration is edited by this package.

## Memory and lifecycle

The SDK `sceGeEdramSetSize(4 MiB)` enables the upper VRAM area supported by newer
hardware; two 768x480x16-bit buffers plus two packed 480x272x16-bit snapshots fit
there (1,996,800 bytes). No equivalent kernel-RAM allocation is made. Restoration
stops new snapshots and waits a bounded time for an in-flight copy before
shrinking VRAM. User GE-size queries continue reporting 2 MiB
while testing so a conventional game does not claim the new space.

Only the game's user `sceDisplaySetFrameBuf` syscall is intercepted to capture
its source; the worker uses original driver calls to display scaled frames.
Unsupported submissions request cancellation. No observed submissions for two
seconds also triggers rollback (e.g. a competing hook or incompatible title).
Kernel/direct submissions are not intercepted: this is not universal support.

Original output mode, primary layer and latest game source are restored before
shrinking EDRAM. Failed restoration retains expanded EDRAM and resident hooks
instead of unloading live code. Suspend stops copying and defers restoration
until resume. Hard crashes, unusual video engines, direct VRAM users and other
kernel plugins remain risks; this first test is deliberately isolated.

The DVE manager is the same binary already shipped with PSPStreamer. Its export
path avoids FuSa's missing Hibari call. It is not rewritten or relabelled here.

## References and provenance

- FuSa source investigation: see `README.md`, pinned upstream commit.
- SDK documents 2/4 MiB EDRAM and rejection on FAT:
  https://pspdev.github.io/pspsdk/pspge_8h.html
- uOFW `src/kd/ge/ge.c`, `sceGeEdramSetSize`, commit
  `5e192e75a83d043d5a65db21128d62758e5741f5`: verifies register/size transition.
- The user-supplied https://www.gamebrew.org/wiki/FuSa_SD_mod_PSP describes
  neur0n's 6.3x mod (tested on 6.39 ME-9), not confirmed ARK/6.60/6.61 support.
  It documents game compatibility/exit/stuttering limitations. No code from that
  binary is included in this implementation.

New implementation and scaler: MIT. Existing third-party DVE component retains
its existing provenance; this is not a relicensed FuSa derivative.

Build: `make -C psp-fusa-probe -f Makefile.fullscreen` with the project's PSP SDK.
