# Import and bounded-memory expansion

The import/memory batch and Martin FPU return-path fix are now confirmed on the
PSP. The new performance changes below still need hardware feedback. Successful
host evaluation is not a frame-rate or visual-equivalence claim.

## 2026-09-22: less copying, unchanged formula semantics

- Main/shape transactions no longer copy independent wave/pixel state. The main
  scratch block shrinks from 533,468 to 190,784 bytes: **342,684 bytes (334.7 KiB)
  less static RAM**. New allocations during playback: none.
- Runtime transactions copy only resident local-memory pages. Newly created
  pages still initialize from the sparse fill defaults; rollback, random state
  and page maps remain intact.
- ADD/SUB/MUL have an early interpreter path, with exactly the same arithmetic,
  instruction charges and assignment filtering. No fast-math, reduced point
  counts, increased budgets or clock changes.
- A focused `-O3` host comparison of 120 deterministic frames against `d8f5c85`
  produced identical geometry/fuel hashes for Wave Budget (`49c5bc69`), Local
  Sparse Fill (`86cfd585`) and Phase Memory (`d9727a25`). One paired run took
  111.603 → 95.034 ms, 3.077 → 0.307 ms and 11.493 → 8.411 ms respectively.
  These are host CPU measurements, **not PSP FPS**; verify the gain on hardware.
  Reproducer: `tests/preset_copy_bench.c` (preset path, frame count).
- The 99 directly affected preset tests passed (two opt-in skips); the explicit
  Martin/FPU crash smoke test still passes. No full collection rerun was made.

Sync CSV saving is also buffered now: the focused 4096-row fixture uses 27
Memory Stick writes instead of 4097. Short writes and I/O errors are handled;
writing still happens only after playback workers stop.

## Import corrections

MilkDrop 2's `utility.cpp:GetPrivateProfileFloat` preserves its initialized
default when numeric conversion fails. The nine base float fields now follow
that behavior instead of rejecting entries such as `rot=-`. Existing PSP
defaults are retained; nonfinite numbers, overflow and trailing garbage still
report an error.

Named INI lookup does not treat orphan lines or empty keys as formula entries.
Those records are ignored inside the preset section. Repeated numbered formula
and shape scalar keys keep the first value, matching Desktop's INI lookup.
Static shape flags use integer conversion before truth testing: `.0525` is
false, not a texture requirement. Missing operators/statement separators are
not invented and third-party preset files are not rewritten.

Capacities are now 256 KiB per file, 256 named variables per context, 2048
numbered records per block, 8192 instructions per init/frame program and
32768 instructions per preset. Pixel/point programs retain their 4096-instruction
cap. Bytecode/source allocation remains transactional; empty programs allocate
nothing. Allocation failures leave the old preset usable.

## Sparse local memory and uniform initialization

Local `megabuf` now addresses 0–1048575 with 32 resident pages of 256 floats
(32 KiB) per context. The shared `gmegabuf` pool has 64 pages (64 KiB), also
covering 1048576 logical cells. Shared indices use the reference's integer
conversion/address mask, including negative/wrapped indices. Local invalid
indices still report an error. Desktop local memory can be larger (8 Mi cells);
the PSP logical ceiling is an explicit implementation limit.

An exact recognized init loop such as
`n=0;loop(300000,megabuf(n)=.1;n+=1;);` stores a uniform range default rather
than writing/allocating 300000 floats. Up to eight defaults coexist. Subsequent
sparse changes allocate pages initialized from those defaults. Loops with
additional side effects are not replaced. Native global zero clears handle
wrapped ranges without clearing unrelated cells.

The runtimes own their arrays by value, with no playback-time heap allocation
or shared ownership pointers. Failure rollback restores registers, random
state, memory writes, page reservations and range defaults. Sparse allocation
avoids reserving a full megabyte-valued array per formula context, but costs
about 4 KiB of page bookkeeping per context and increases state-copy traffic.

### Deliberate quiet PSP limit

When all resident pages are occupied, a store needing a new page is discarded.
Existing pages are never evicted or aliased. Reads from absent shared pages
return zero; absent local pages return their uniform default (otherwise zero).
Stores to existing cells continue normally. This allows memory-heavy presets
to continue, **but is not Desktop-equivalent memory** and may visibly alter
their output. Fragmentation matters; even isolated cells can fill the page pool.
The host audit's `resource_limits` mask records local saturation as bit 1 and
shared saturation as bit 2. These are not formula parse failures.

## Execution budgets and remaining hard case

Setup and the main per-frame program each have a 4194304-instruction hard cap.
They no longer consume the separate 262144-step point/geometry pool, so a
large initialization does not leave the first image without geometry. Ordinary
point calls retain their 4096-step cap and adaptive mesh/wave/shape density.
The larger programs and bulk memory operations cooperatively yield every
4096 steps on PSP. This lets audio/network workers run; it does not make an
expensive visualization fast or guarantee responsive controls on hardware.

Arbitrary infinite/huge loops still fail transactionally. In particular,
`martin - city of shadows` exhausts main-frame fuel at frame 116 of the
120-frame synthetic test. Its formulas perform nested 3D search/ray-marching
work in addition to its unsupported shaders. **Umbau nötig:** faster formula
execution or interruptible/resumable evaluation, not another unchecked budget
increase. It is not included as an expected-success hardware test.

## Validation and PSP test

Hardware follow-up: the user reported reproducible forced shutdowns with
`martin - lock and release` and `martin - sphery tales`; the earlier test presets
worked. Consequently host formula success must not be treated as hardware
acceptance. The custom-wave transaction scratch has been moved off the main
thread stack (PSP compiler: 108352 -> 10032 bytes for that function). This
removes a large stack burden, but does not by itself prove the crash cause.
With `debug=1`, the first two rendered frames per activation/preset switch
append formula, wave and GPU phase breadcrumbs plus free stack to
`PSP/SYSTEM/PSPStreamer-watch-music.txt`. Later frames update only the in-memory
watchdog stage, without ongoing per-frame file writes. This follow-up build
needs a new hardware test; the shutdown issue remains open until confirmed.

The next hardware log still stops at `MilkDrop frame/shape formulas`, with
226848 bytes of free stack, before custom waves or GPU submission. Thus the
wave-stack reduction did not resolve or establish the cause. Additional
bounded startup breadcrumbs now separate VM initialization, native local fill,
shared clear, main-frame evaluation and entry/return of the PSP scheduling
yield. At most 64 extra records per initial frame are written, for two frames;
yield messages have their own eight-record cap so they cannot hide phase ends.
no execution limits or clock settings are changed by this diagnostic build.

The following log confirms native local fill, shared clear and init completion,
then enters the main frame program. An earlier preset runs in the same session
at 333 MHz. Investigation found that the renderer relied on host-default
non-trapping floating-point evaluation while EEL deliberately permits NaN/Inf
intermediates. [PSPSDK documents FPU exception controls](https://pspdev.github.io/pspsdk/pspfpu_8h.html),
and [PPSSPP records PSP-default FPU traps as a hardware/emulator difference](https://github.com/hrydgard/ppsspp/issues/22098).

The renderer now saves FCR31, masks IEEE trap enables/clears pending status for
the visualization call, and restores the saved control policy on every return.
Rounding and flush mode stay unchanged. The change is renderer-thread-local;
it does not change audio worker state, clocks, formula budgets or memory limits.
The host counterpart uses `feholdexcept`/`fesetenv`. A focused test explicitly
enables invalid/divide-by-zero/overflow traps: the unguarded renderer raises
SIGFPE on the Martin fixture, while the guarded renderer completes both Martin
fixtures in LCD/TV paths and restores the caller's enabled traps. This is a
reproduced defect, but successful playback on the real PSP remains to be verified.

The next PSP run completed init, frame/shape formulas, pixel formulas, custom
waves and GPU synchronization, and displayed one frame before hanging. The
remaining failure is therefore after the first render's completion breadcrumb,
not the previously blocked initial formula evaluation. The PSP return path now
clears exception status while traps are still masked, then restores caller
controls/sticky flags without replaying transient cause bits. Explicit instruction
ordering separates these writes. Bounded startup diagnostics log saved/current
FCR31 before and after restoration. The user has now confirmed successful
playback on the real PSP with this two-stage return-path correction (2026-09-22).
The import/sparse-memory demonstration presets have also passed their hardware
tests. This resolves the reported Martin startup/return crash; it does not remove
the separate computation limit on `martin - city of shadows`.

The expanded 2026-09-24 collection imports **2266/2267** files. Both previously
rejected Hexcollie nz+5/nz+6 files now pass import and 120 host-evaluated frames.
The earlier diagnosis of a source error was incorrect: Desktop accepts the
joined operand assignment. The parser now follows that binding without changing
the files. See [assignment compatibility](MILKDROP_ASSIGNMENT_COMPATIBILITY.md).
The earlier focused 79-file problem set had 77 imports and **76 successful 120-frame
runs**; the one runtime failure is the case described above. The additional
full-collection execution run was stopped by the assistant after incorrectly
interpreting the user's build-first instruction as a cancellation request.
That run wrote no partial results. No new full-collection 120-frame pass count
is claimed; the completed import and focused execution results remain available.

Direct checks cover import boundaries, INI defaults/duplicates, sparse and
uniform memory, scalar/native clear equivalence, wrapped clears, rollback,
pool saturation without aliasing, infinite-loop protection, formula allocation
failures and LCD/TV renderer ownership. They do not exercise unrelated server
features or re-run media/network tests.

Install **both EBOOT.PBP and PSPStreamer.prx** from the same release and copy
the supplied `presets/` additions. No config change is needed.

1. `local-sparse-fill-demo.milk`: moving cyan circle around a stationary orange
   diamond in the center. Select another preset and return to check reset.
2. `amandio c - new life.milk`: expanded variable capacity.
3. `martin - lock and release.milk`: larger local memory/initialization.
4. `martin - sphery tales.milk`: heavy local uniform initialization and formulas;
   a low visualization frame rate is possible.
5. `amandio c - epicenter the end of the world we never knew.milk`: large-file
   import and shared-memory saturation. Appearance may differ because of the
   documented memory approximation and ignored shaders.

The four third-party files are supplied only in local release folders from the
user's collection, not added to Git. Test LCD/TV as convenient, clean audio,
preset switching and return to video. Report preset name and whether an error,
audio interruption or unresponsive controls occurs. Hardware success can close
the implementation/test items; malformed inputs and the separate heavy 3D
formula case must not be counted as solved.
