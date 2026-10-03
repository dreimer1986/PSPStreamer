# FuSa-style fullscreen experiment 0.23 — verified physical mode reuse

0.22 Star Ocean evidence: `queries=0`, but the intercepted mode-0 480x272
request returned `80000107` (display argument/invalid mode) after we replaced
its arguments with TV output geometry. The movie then never supplies a new
buffer. Logical GetMode was not used and did not fix this run.

0.23 checks actual output geometry in kernel context. If it is already the
required 0x1d2/720x480, a logical game reset reuses it and succeeds without a
redundant Sony mode call. Otherwise only the fixed, validated replacement
arguments are applied with K1 temporarily cleared, restored on all branches.
User passthrough requests are unchanged. Query/set failures still propagate;
this does not blindly hide error 80000107. The K1 cause of Sony's rejection is
plausible, not independently proven; idempotent reuse avoids that call entirely.

Mode diagnostics: route=0 passthrough/query failure, 1 verified existing mode,
2 actual kernel-context change. Test Star Ocean name entry -> fade -> movie.
Copy, VBlank pacing, OC and INI remain unchanged.

## 0.22 — logical game geometry

0.21: Metal Slug XX and Soul Calibur hardware tests pass. Star Ocean's movie
after name entry proceeds without the plugin but blanks with it. Latest log:
frame 649, null game source/zero stride after two intercepted mode calls; zero
copy rejects and zero added VBlank waits. This implicates plugin interaction,
not a confirmed root cause. It is not evidence of a slow-copy bottleneck.

0.22 pairs mode redirection with a user-syscall GetMode view: successful game
requests are remembered as logical mode/width/height, while actual scanout
remains 720x480. Game queries no longer unexpectedly expose the scaler geometry.
The worker and kernel display paths still query actual hardware mode. Sony's
getter validates pointers and supplies its normal result before substitution;
errors are not hidden. The hook is removed with the other display hooks.

Log mode requests/results and query counts without I/O in the hook. If the
game never queries this API, this change will not explain/fix the movie stall;
the new diagnostics distinguish that case. Pixel copy, scheduling and INI are
unchanged. Test the name-entry -> fade -> movie transition first, then a short
regression check in the two working games. No OC/firmware changes.

## 0.21 — remove game/output lockstep

0.20 hardware result: clean images across tested games, but slower gameplay
and stuttering audio. Soul Calibur copy averaged 1.856 ms (previously 15.4 ms),
while output settled near 20 Hz. The coordination waited for every submitted
frame to be captured at the slower output cadence, then performed another Sony
VBlank wait. There were 32 coordination timeouts in the last logged run.

0.21 runs the original Sony wait FIRST and guards only a copy already in
progress, for at most 4 ms rather than 100 ms. Skipped output frames, scaling,
idle output intervals and upcoming captures never hold the game. Guard calls,
timeouts and average additional delay are logged. It remains optional via the
same `experimental_speedboost` setting (enabled on the user's test PSP).

Output pacing now uses actual VBlank counts (one capture slot per two blanks),
not 33.334 ms delay followed by another VBlank wait. Worker priority 24 is set
before waiting so it can promptly capture at the boundary, then restored to 56
before scaling. VFPU copying, strict snapshot validation, HOME and reboot
cleanup remain. Residual CPU/bus load and possible timing effects still require
hardware validation; this is not a promise of native game speed.

Test Metal Slug XX and Soul Calibur: clean moving image AND game/audio speed,
then HOME/exit. Build before targeted policy/config/scalar-reference tests.

## 0.20 — VFPU and coordinated capture

0.19 restored menu output but hardware feedback confirmed tearing in both
Metal Slug XX and Soul Calibur. Soul Calibur's scalar 16-bit copy averaged
15.4 ms, with large scheduling outliers. Reboot cleanup is now seen in logs.

0.20 uses an explicitly VFPU-enabled worker, vector 16-bit copy and RGB565
conversion, read-only cached VRAM with fresh invalidation, and the existing
uncached RAM-system source path. Capture follows vblank, at priority 24 only
for the copy, returning to priority 56 for scaling/other work. A submission
change during capture rejects the copy; changes during later scaling do not.
No extra full-frame allocation and no pause inside the display driver.

`experimental_speedboost=1` additionally coordinates the four standard user
VBlank wait syscalls with snapshot completion. It waits at most 100 ms, then
executes the original Sony wait, preserving its result and CB dispatch. It is
only active in fullscreen and outside system-layer display; hooks are removed
on restore/reboot. Calls/timeouts are logged. This can reduce game speed to the
capture cadence; it is NOT a promise of higher FPS. Kernel/direct wait calls
and the multi-VBlank APIs are not intercepted. The worker uses real VBlank.

Important correction: the available FuSa `spb.c` has its real speedboost
patches/timers commented out. This implementation adapts wait/capture
coordination, not a known-working transplant of its released Speedbooster.
No synthetic interrupts, fake Vcount, controller hooks or firmware offsets.
The repository default is 0; the user's test PSP is explicitly set to 1.

Test after reboot: Metal Slug menu/game, Soul Calibur motion, HOME and exit to
VSH. Report tearing, black output, game/audio speed and provide test.log.
Hardware timing, VFPU pixels and concurrent firmware behavior remain untested
by host tests; those cover scalar reference conversion, policy and config.

## 0.19 — continuous snapshot output

0.18 hardware feedback: Metal Slug XX no longer hangs at its first transition,
and gameplay works. Its 60 Hz RGB32 menu remains black: the log shows zero
accepted game snapshots, 1385 rejected copies, and about 21.9 ms per copy.
Requiring an unchanged submission counter during the copy cannot succeed at
16.7 ms frame intervals. HOME snapshots did succeed independently.

0.19 rejects changes to mode, stride, format, blank state or source layer,
but not ordinary double-buffer swaps. Global GE activity is no longer a veto:
the engine may be drawing a different buffer. This follows FuSa's live-copy
semantics, retaining our private copy for scaling. It may permit tearing during
buffer reuse; no thread suspension, game wait or additional frame allocation
is introduced. System snapshots follow the same layout-generation policy.

An explicit exported `module_reboot_before` now requests worker shutdown and
display restoration before exit-to-VSH. Previously only `module_stop` existed;
the reference also handles reboot. The observed black VSH return is not yet
proven to have this cause. A dedicated log entry confirms whether cleanup ran.

Test: restart PSP, Metal Slug menu -> gameplay -> HOME -> VSH, without Screen
workarounds. Then Star Ocean transitions if that passes. INI is unchanged.
Build first; only display-policy, pixel-copy and auto-activation host tests.

## 0.18 — separated game source and output

See [the full reference audit](REFERENCE_COMPARISON.md) for corrected and
deliberately retained differences from the original FuSa source.

0.18 preserves Sony's real game buffer on layer 2 and renders scaled output only
on layer 0. Public and direct kernel game submissions share the internal hook.
System-layer calls now reach Sony with the substituted output rather than being
silently acknowledged. Mode requests are intercepted before execution, replacing
the failed post-hoc DVE repair loop. Deliberate Screen-key changes and our own
start/restore calls bypass this remapping.

Producer waits are removed: an internal display caller may own driver locks.
The worker snapshots notified sources without blocking the caller, rejects
changes/busy GE, and can refresh unchanged sources after 100 ms. It retains the
private RAM snapshot, progressive double buffers, 30 Hz pacing and current CPU
clock. A changed source is dropped, not scaled from live memory.

PS-menu handling from 0.17 remains; its successful user test is recorded. This
build still needs hardware confirmation for Metal Slug's first scene transition,
HOME/resume/exit, and then Star Ocean intro transitions/video smoothness.

## 0.17 RAM HOME buffer and live mode repair

0.16 logs identify HOME layer 0 at `0xABBBC000`, stride 512, RGB5551. It is
main RAM, not VRAM. System sources now accept complete buffers inside queried
partitions 2/5/8/11, including extended system partition 8 containing the observed
buffer on Slim. The source uses uncached KSEG1, not the VRAM alias. No fixed
64 MiB assumption, extra RAM allocation or partition resizing is introduced.
Game source validation remains lower-2MiB VRAM only.

Metal Slug XX failed even on its first scene transition: recovery speed alone
was not sufficient. A live 480x272 external progressive layout now reconfigures
the output in place and reattaches the last completed 720x480 frame. Hooks,
VRAM mapping, source tracking and buffers survive; no five-second teardown/
rearm cycle and no manual LCD round trip should be needed. The next cycle
checks the VBlank-pending mode. LCD selection/cable removal are not overridden.
Each repair is logged, with a session count on exit.

RGB8888-to-RGB565 copying now processes four pixels per iteration, with two
packed aligned stores. Pixel results are unchanged. Host checks cover full-frame
reference conversion, guard words, RAM boundaries/aliases and repair policy.
This is an optimization candidate, not a measured PSP speedup; clocks remain
unchanged. The 0.16 sample spent about 17.8 ms copying and 5.0 ms scaling RGB32.

Build first, then focused tests. Pending hardware checks: visible HOME and
resume/exit, Metal Slug's first gameplay transition without Screen, repeated
Star Ocean First Departure intro transitions, and video smoothness at the same
clock. Consolizer, firmware and rumble are unchanged.

## 0.16 HOME/system layer capture

0.15 hardware feedback: cold VSH TV activation works; Metal Slug XX can now
allocate its snapshot and scale gameplay after a manual LCD/TV cycle. Save/load
dialogs stay fullscreen. HOME opens but is invisible in all three tested games.

0.16 handles the missing internal primary-layer path, following the original
FuSa source's priority: use Sony layer 0 when nonblank, otherwise the game.
The internal setter entry is intercepted because kernel impose calls bypass
the public game syscall. Only two position-independent prologue instructions
are relocated; branches/jumps/unknown instructions are refused and logged.
The hook only remembers source state. No copying, blocking, logging or extra
allocation takes place there. Our own output bypasses the hook through its
trampoline; the latest original system source is preserved for restoration.

The existing worker refreshes system snapshots even while HOME pauses game
submissions. It checks source bounds, GE idle and layer sequence across copying;
this is not a guarantee against all direct-rendering tearing. System snapshot
accept/reject counters appear when leaving fullscreen. The RGB16 game handoff,
RGB32 conversion, 30 Hz pacing and single 255 KiB RAM snapshot are unchanged.
Internal game-layer queries now use selector 2, as in FuSa, rather than 1.

Both known progressive external 480x272 layouts (0x2d2 and 0x1d2) can re-enter
scaling. This repairs the automatic recovery gap after Metal Slug's scene
transition without forcing a deliberately selected LCD or interlaced output.
The configured recovery delay remains in effect.

Only the FuSa PRX changes. Consolizer, ESP firmware, INIs, OC and rumble stay
unchanged. Build first, then focused host checks of source copying, mode/layer
policy, prologue validation and automatic activation. Hardware validation is
still required: visible HOME, resume/exit in all three games, and Metal Slug's
menu-to-game transition without manually cycling LCD/TV.

## 0.15 system display handoff

The 0.14 hardware logs identified two separate failures: Metal Slug XX had only
192 KiB of contiguous user RAM for a 255 KiB snapshot; Soul Calibur submitted a
zero-stride layer during HOME, and Gran Turismo submitted RGB8888 at 0x04154000.
That 512-stride, 272-row RGB32 buffer ends at 0x041dc000, below the scaler outputs;
the proven rejection was its pixel format, not a demonstrated buffer overlap.

- Allocate the snapshot through managed partition 11 when available, with the
  original user-partition fallback. No fixed extra-RAM addresses, repartitioning,
  enlarged static kernel buffers or changes to clocks.
- Accept RGB8888 within the existing lower-2MiB bounds and convert to RGB565 in
  the snapshot. RGB16 copying/scaling and upper-2MiB output placement are unchanged.
- Accept transient blank-layer submissions instead of returning a display error.
- Respect externally changed display geometry during restoration; do not replay
  the old game TV mode over a system transition. Retain expanded VRAM while a
  scanout could still reference it, then retire it once both layers allow this.
- Write the stop-cause diagnostics after handing display ownership back, rather
  than delaying the transition with slow memory-stick writes.

Companion Consolizer change: cold VSH TV activation waits for completed boot,
the shell/PAF modules and a stable valid LCD layout. The existing single-request
policy remains. FuSa is installed for GAME only on the test stick: the reported
VSH cold-boot failure is a separate Sony TV-button transition, not FuSa scaling.
The current logs do not establish its precise cause; this readiness change needs
a real cold-boot test. No rumble/USB transport or ESP firmware changes.

Hardware tests pending: cold boot to TV, Metal Slug XX fullscreen, Soul Calibur
and Gran Turismo HOME menu (resume and exit). No claim of hardware confirmation
or uninterrupted fullscreen HOME composition yet. Existing INIs are preserved.

## 0.14 persistent fullscreen

Add `keep_fullscreen=1` to `FuSaFullscreenTest.ini` (included in the new example).
Missing/zero retains the old behaviour. With this option, HOME/SCREEN no longer
requests restoration and the initial two-second no-submission timeout is off.
An explicit NOTE+R activation or automatic activation remembers the fullscreen
intent, even if an intro initially presents an unsupported frame. After an
actual error is restored, zoom is retried after `auto_zoom_delay_seconds` in
normal supported component TV mode. Repeated failures are spaced by that delay.
NOTE+R explicitly disables zoom and cancels pending recovery.

This is persistent intent, not a promise of uninterrupted fullscreen under any
hardware condition: suspend, invalid buffers, failed API calls and lost VRAM
ownership still require recovery. It does not force TV when the cable is absent
or LCD mode is selected. At that version there was no 32-bit capture. A system HOME menu that
uses a different rendering path may still require recovery after closing.
New logs identify the rejected initial source address, stride, pixel format and
display mode. HOME visibility, persistence and recovery need hardware testing.

## 0.13 coexistence

Consolizer, PSPStreamer and StreamerOC (including their overlays) are no longer
blocked. There is no model, firmware-version or GAME-only allowlist. Actual
driver exports, source format, memory bounds and VRAM ownership are still
validated. Removing an allowlist does not implement 32-bit capture or kernel
presentation interception. The capture scheduler remains 30 Hz; there is no
session duration limit. Existing INI settings are unchanged.

Place `FuSaFullscreenTest.ini` beside the PRX:

```ini
auto_zoom=1
auto_zoom_delay_seconds=5
keep_fullscreen=1
```

`auto_zoom`: 0 (default, manual only) or 1. Delay: 1–60 seconds, default 5.
Settings are read at plugin/game startup. The timer starts after the existing
10-second plugin initialization grace, once normal component game TV mode
480x272 is observed. This does **not** switch LCD to TV automatically. Normal
source/format/VRAM checks still apply; an unsupported game is not forced.
The automatic attempt runs once per observed TV session. Manual NOTE+R choice,
failed activation and automatic error restoration do not immediately retrigger
zoom. Return to LCD and then TV to rearm, or use NOTE+R manually. Checks run only
while inactive, every 250 ms; no new polling in the active scaling path.
Release HOME/SCREEN/NOTE/R before the delay starts. No playback duration limit.
Release defaults leave automatic activation off; the user's test-stick INI
enables it with a five-second delay.

## Previous test history

0.11 removes the automatic one-minute stop. NOTE+R toggles off/on after clean
restoration; HOME/SCREEN and actual safety failures still request restoration.
The per-capture 50 ms deadline remains: it releases the producer and drops only
that capture, not the session. There is no duration limit on fullscreen output.

StreamerOC no longer causes startup refusal, allowing user-controlled clock
comparisons. Since 0.13, overlays and Consolizer are allowed as well. No clock,
OC INI or ARK plugin entry is changed automatically. The joint configuration
still needs hardware testing. The 30 Hz scheduler is unchanged.
For complete statistics, toggle fullscreen off before exiting/mounting the PSP.

0.10 completed 60 s cleanly: 1,664 frames (~27.73 Hz), 8.343 ms mean copy,
8.935 ms mean scale, three handled producer timeouts and zero wait errors.
User reported good gameplay. Game submissions were ~38.2/s, so output FPS alone
is not proof of unaffected game speed; long-term/other-game/OC tests remain.

0.10 raises only the request-to-request ceiling from 20 to 30 Hz (33,334 us).
The 0.9 event handoff/copy/scaler stays unchanged, as do the 50 ms producer
timeout, 60-second test, repeat activation, CPU clock and compiler flags.
Actual output and game speed must be checked on hardware; more captures can
increase producer stalls even when the displayed animation is smoother.

0.9 hardware baseline: 1,135 outputs in ~60 s (18.91 Hz, ~28% above 0.8),
8.102 ms mean copy, 9.382 ms mean scale, 8.388 ms mean producer hold. Three
producer timeouts and two rejected copies were tolerated with zero wait errors
and clean timed restoration. Maximum scale elapsed time was 388.018 ms; this
includes scheduler delays and does not establish their cause. User reported
visibly smoother output. This is not a claim that all stutters are eliminated.

0.9 bundles these changes for one hardware comparison against 0.8:

- Producer completion uses an event wait rather than waking every 500 us. A
  second event wakes the worker on an offered frame. Events persist across
  activations, clear before reuse, wake on stop/restore, and are deleted only
  after the worker and all hook users have exited. Single producer ownership,
  monotonic tickets, GE/sequence checks and the 50 ms timeout are retained.
- The 20 Hz period starts when requesting the frame, not when the worker later
  receives it. Capture-wait time no longer adds to the intended period.
- Eight-word copy batches reduce loop/control overhead without cached VRAM
  reads, DMA, VFPU ownership or additional memory allocations.
- The scaler computes a source row once and writes its one/two destination rows
  together. No intermediate 1440-byte row buffer or second row-copy loop is
  needed; nearest-neighbour mapping, bit patterns and padding remain identical.
- Idle pacing sleeps up to 10 ms instead of polling every 2 ms; power callbacks
  and controls remain serviced. Mean producer hold time is added to the logs.

No CPU clock, GE-idle transition or framebuffer-layout changes. Two small event
objects are new; the large snapshot remains in user RAM. Host tests check exact
pixel mapping, copy guards, padding, snapshot independence and handoff/timing
predicates. Event scheduling, speed and stability require the PSP test. No speed
gain is claimed before measuring. Copy/scaler changes stay portable scalar C;
compiler optimization remains -O2. DMA/cached game-VRAM/GE injection are deferred
because they introduce separate ownership/cache/hardware hazards.

0.8 hardware baseline: 884 outputs in 60 s (~14.73 Hz), 9.128 ms mean copy,
11.030 ms mean scale, one producer timeout handled without abort, clean timed
restoration. User reported visibly smoother output. Game submissions were ~45.6/s
versus ~50.8/s in the previous scene; more capture work may slow the game.

0.8 changes only the preview period from 83,333 to 50,000 us (20 Hz maximum)
and stop diagnostics. The bounded worker handoff/copy/scaler is unchanged.
Actual output may be slower; increasing capture frequency can also reduce game
speed because the submitting thread yields during each selected RAM copy.
No clock changes are made. The 60-second duration and reactivation remain.

The first cancellation cause and its error code are retained without file I/O
in hooks/callbacks. Worker logs distinguish unsupported source (including its
address, stride, format and sync), wait failure, suspend/resume, mode/VRAM
change, presentation/restore failure, missing submissions, HOME/SCREEN,
NOTE+R, timeout and module shutdown. A slow capture alone is still only dropped.

0.7's second hardware activation completed 60 s with 579 accepted/scaled frames,
zero rejected copies/wait timeouts/errors, 9.327 ms mean copy and 14.973 ms mean
scale. User confirmed good picture. Its earlier short activation cancelled for
an unknown reason; the new diagnostics target that gap. One scale elapsed
568.427 ms, including scheduling delays, so smoothness is not yet guaranteed.

0.7 corrects the overly aggressive 0.6 test policy: a single 50 ms producer
deadline drops that capture and releases the game, **not** the entire test.
There is no five-second no-frame rollback. The previous good output remains
visible while capture retries continue, up to the normal 60-second limit.
NOTE+R can activate again after complete restoration (no live hook/expanded
VRAM or outstanding hook user). Statistics reset per activation; handshake
tickets remain monotonic. Genuine wait/API/mode errors still request rollback.
Reactivation is hardware-unverified; unlike withdrawn 0.2, GE-idle transitions
remain in place. The 0.6 log showed nine good frames followed by one timed-out
copy, which incorrectly ended the entire test. This build fixes that policy,
not a demonstrated cure for all display artefacts.

0.6 requests a capture at the next valid game presentation. That selected
calling thread yields until the worker finishes copying the submitted buffer
to RAM; it is released **before scaling**. Other calls are not deliberately
held, and observed concurrent submissions still invalidate the capture. There
is no pixel copy in the hook and no global scheduler/interrupt lock during it.
The caller checks a 50 ms deadline every 500 us (not a hard real-time guarantee
if it is starved by another thread); timeout or a forbidden wait context cancels
the test. Interrupt-context/interrupt-disabled calls are not selected to wait.
Stop, restoration and suspend release the handshake. Tickets distinguish late
returns from a later handoff. The worker waits for GE idle before copying and
checks GE/ownership/sequence afterward. Direct writers and independent render
threads remain a compatibility limitation, not magically fenced by this hook.

This may slow the selected game frames. It is a bounded producer/consumer test,
not FuSa's SpeedBooster or a claim of universal tear-free capture. The published
FuSa hook signals a screen event through a polled semaphore but does not wait
for this worker acknowledgement. Firmware-offset/VBlank patches remain unused.

0.5 returned automatically after ~13 s: only 15 accepted versus 539 rejected
copies, 16.388 ms mean copy, 13.896 ms mean RAM-source scale. Five seconds without
an accepted frame caused the rollback. This demonstrated optimistic-copy
starvation, despite faster RAM-source scaling. 0.6 logs handoff count, wait
timeouts/errors and maximum producer hold time in addition to copy timings.

0.5 copies into a single 261,120-byte **user-partition RAM** buffer in the
worker, never in the game's presentation hook. It accepts a copy only when GE
is idle before and after copying and the observed presentation sequence has not
changed. Rejected copies are not displayed; the previous output remains visible.
Scaling reads only this private snapshot, including at startup (black until the
first accepted capture). This is optimistic validation, not a universal GPU
fence: direct writers or rendering not reflected by these observations can still
escape detection. Hardware validation, especially Soul Calibur, is required.

The 12 Hz limit now measures start-to-start time, including capture/scaling,
instead of adding 83 ms after each scale. Late frames do not trigger catch-up
bursts. Five seconds without an accepted/displayed snapshot requests rollback.
Logs include accepted/rejected captures, GE-busy polls and copy timings. Source
submissions during scaling are now expected and harmless to the private copy.
The 60-second limit, one activation per launch and GE-idle VRAM transitions stay.

The 0.4 hardware run restored cleanly: 441 scales in 60 s (7.35 Hz), 39.757 ms
mean scale, 823.231 ms maximum elapsed scale (including scheduling delays).
All 441 scales overlapped source submissions; this supported but did not prove
the live-source tearing hypothesis. Visual stair artefacts remained.

0.4 retains the 0.3 transition/capture path confirmed stable by the user, but
packs four input pixels into three 32-bit output writes instead of six 16-bit
writes. Repeated destination rows share a 1440-byte local worker row, so each
source row is fetched once. Exact nearest-neighbour pixel equivalence and
destination padding/bounds are host-tested. No snapshot copy in the game thread.
Scale duration and overlapping source submissions are logged after restoration;
these counters indicate possible tearing, not proof of its exact cause.
The automatic rollback is now **60 seconds after activation**, at user request.

Version 0.2 is withdrawn after reported hard shutdowns in Soul Calibur and
Street Fighter Alpha 3. Its log measured ~24 ms copying in the game's calling
thread and ~52–57 ms scaling: the 30 Hz target was not achieved. Some sessions
restored successfully; one ended between expansion and first presentation and
the final one before the expansion result. The precise shutdown cause remains
unproven. Do not use the 0.2 build for further testing.

0.3 returns to worker-only live-source scaling, removes the snapshot writes and
blocking copies from the game's presentation call, and adds a bounded GE-idle
check around each EDRAM size switch. Scheduling and interrupts are excluded only
for the immediate idle check/register change, not while waiting. Extra stage
logs bracket that transition. **Only one activation attempt per game launch**
is allowed during this regression investigation. It is not a proven crash fix.

This is a new, separate test implementation, **not a repaired release of FuSa**
and not yet part of Consolizer. It enlarges the completed 480x272 game image to
720x480 component output. It does not increase a game's internal resolution.
Use the TV's 16:9 interpretation of 480p; sample pixels are not square.

## Supported rendering path

- Originally tested on PSP-3000, firmware 6.60/6.61. No hard allowlist now.
- Component cable, Sony game TV mode `0x2D2`, 480x272.
- 16-bit source formats 5650/5551/4444, stride 512 or 1024, wholly in lower 2 MiB.
  The user's probe captured 5551/512. 32-bit games are refused for now.
- Manual or optional delayed automatic activation; no session time limit.
- At most 30 preview images per second, with capture/scaling included in the
  period. Actual rate depends on accepted captures and CPU/memory contention.
- No clock changes, no old inline patch trampolines, no firmware offsets,
  VBlank replacement, global thread suspension or SpeedBooster. Selected
  presentation calls now yield for the bounded worker handoff described above.

## Install and test

1. Copy the `SEPLUGINS/FuSaFullscreenTest` folder, including `dvemgr.prx`.
2. Consolizer and StreamerOC, including overlays, may remain enabled.
   Coexistence is permitted, not a guarantee of compatibility with every hook.
3. Add/enable this ARK entry (supplied example is off):

   `game, ms0:/SEPLUGINS/FuSaFullscreenTest/FuSaFullscreenTest.prx, on`

4. Start the **same game as the probe**, wait at least 10 seconds, enter actual
   gameplay after intro movies, and hold Sony's screen button to enable normal
   component TV output. Do not save progress during the experiment.
5. Press physical **NOTE (music-note button) + R** together, then release. This
   enables the scaler without a time limit.
   Press the same chord again to return. With `keep_fullscreen=0`, HOME or SCREEN
   also requests restoration. Their normal system function is not swallowed.
   After successful restoration, release and press NOTE+R again to retry.
6. Note: correct/full picture? Colours? Sound? Successful return to normal TV?
   Then exit the game and return `SEPLUGINS/FuSaFullscreenTest/test.log`.
7. Disable the test and restore your previous GAME plugin entries afterwards.

If activation fails, return the log rather than repeatedly forcing activation.
If the screen goes black, release the buttons, then press NOTE+R to restore.
There is no timed rollback. If the system itself hangs, restart and
disable the test. The first hardware run is experimental, not guaranteed safe
for unsaved gameplay. No other plugin configuration is edited by this package.

## Memory and lifecycle

The SDK `sceGeEdramSetSize(4 MiB)` enables the upper VRAM area supported by newer
hardware; two 768x480x16-bit buffers fit there (1,474,560 bytes). VRAM snapshots
from 0.2 are no longer accessed. The 0.5 snapshot uses 261,120 bytes of the user
partition, allocated once, released only after the worker has ended and all
hooks/expanded VRAM are safely released. Allocation failure refuses activation.
It does not consume an equivalent block of scarce kernel RAM.
User GE-size queries continue reporting 2 MiB
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
