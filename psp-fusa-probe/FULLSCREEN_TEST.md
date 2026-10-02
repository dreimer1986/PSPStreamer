# FuSa-style fullscreen experiment 0.9 — bundled CPU-path optimizations

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

## Previous test history

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

## Deliberately narrow first test

- PSP-3000 (`model=2`), reported firmware 6.60 or 6.61, GAME only.
- Component cable, Sony game TV mode `0x2D2`, 480x272.
- 16-bit source formats 5650/5551/4444, stride 512 or 1024, wholly in lower 2 MiB.
  The user's probe captured 5551/512. 32-bit games are refused for now.
- Manual activation; automatically attempts restoration after 60 seconds.
- At most 20 preview images per second, with capture/scaling included in the
  period. Actual rate depends on accepted captures and CPU/memory contention.
- No clock changes, no old inline patch trampolines, no firmware offsets,
  VBlank replacement, global thread suspension or SpeedBooster. Selected
  presentation calls now yield for the bounded worker handoff described above.

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
   enables the scaler. After 60 seconds it attempts to return to normal TV output.
   Press the same chord again to return earlier. HOME or SCREEN also requests
   restoration; their normal system function is not swallowed.
   After successful restoration, release and press NOTE+R again to retry.
6. Note: correct/full picture? Colours? Sound? Successful return to normal TV?
   Then exit the game and return `SEPLUGINS/FuSaFullscreenTest/test.log`.
7. Disable the test and restore your previous GAME plugin entries afterwards.

If activation fails, return the log rather than repeatedly forcing activation.
If the screen goes black, release the buttons and allow the 60-second rollback.
If the system itself hangs, the timeout cannot guarantee recovery; restart and
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
