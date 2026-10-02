# FuSa reference audit — 0.18 (2026-10-02)

Scope: the display path behind the observed HOME, scene-change and video
regressions, including relevant timing/teardown behavior. This is not a claim
that all options of every FuSa binary/mod are implemented or that the supplied
legacy source ran on ARK-5. Reference inspected locally: `fusa-reference/src/`
(`hook.c`, `main.c`, `dve.c`, `spb.c`, `fs/fs3.s`, `fs/fs3_e.s`). No legacy
firmware offsets or copied assembly are included in this implementation.

## Incorrect deviations corrected together

| Area | Reference | Previous implementation | 0.18 |
| --- | --- | --- | --- |
| Game buffer | Public presentation forwards the original address/stride/format to internal layer 2. | Swallowed submissions; published the scaled buffer through the public presenter as well as layer 0. | Real layer-2 submissions reach Sony. Only layer 0 receives scaled output. Buffer queries therefore remain meaningful to the game. |
| Entry coverage | Patches the public function entry and internal setter, including direct calls. | Game capture was syscall-only; the internal hook only tracked system layer 0. | Common internal layer-2 interception covers public and direct paths, without two captures of one submission. |
| System source | Saves original layer-0 source and substitutes the scaler output in the actual driver call. | Remembered the source but often returned success without calling the original setter. | Saves the source, forwards a real layer-0 call with the output buffer, preserves the driver result. |
| Mode changes | `SetMode` intercepts requests before Sony applies them. Explicit output switching has a bypass. | Polled for changes, tore down/restarted, then 0.17 repeatedly called DVE to repair output afterward. | Checked mode-entry hook maps game-sized requests to existing 720x480 progressive output. Own start/restore calls and deliberate Screen-key switching bypass remapping. No repeated DVE repair loop. |
| Producer scheduling | Nonblocking event notification; periodic timeout refresh when no event arrives. | Waited up to 50 ms for a snapshot. No game submission meant indefinite waiting for that handoff. | Setter never waits for the scaler. Worker uses notifications and a 100 ms unchanged-source refresh. |
| Source stability | Copies the selected source directly, with a primary-layer preference. | Handshake snapshot plus scaling; public game state could be stale/incorrect. | Keeps a private snapshot but rejects source-sequence changes, in-flight registration or a busy GE. Blank game sources remain blank rather than being replaced with an old pointer. |
| Restore | Returns original primary source; original game layer stays registered. | Replayed stored game frames, sometimes after a newer source or system transition. | Stops primary/mode virtualization; restores primary output and leaves the live Sony game layer alone. |
| Selector | Uses internal selector 2 for the game. | Used selector 1 in some cleanup queries. | Corrected in 0.16 and retained. |
| RAM sources | Uses supplied system source pointers, not only VRAM. | Rejected HOME at `0xABBBC000`; generic VRAM alias was unsuitable for it. | Queried RAM partition bounds and correct uncached RAM alias, corrected in 0.17 and retained; also usable for valid game sources. |

The original does not validate all source addresses/return values; our checks
remain. They must accept observed legitimate formats and actual memory ranges,
not turn into title allowlists.

## Deliberate remaining differences

- **480p vs legacy interlace:** reference mode `0x1d1`, height 503 and field
  offsets differ from our tested component 720x480 `0x1d2` output. Copying its
  field offsets would be wrong for this layout. Composite/interlaced modes and
  its alternative crop/offset modes are not added by this fix.
- **Private snapshot and double buffering:** reference writes from the live
  source directly to its fixed output region; we snapshot then scale to a back
  buffer. This avoids scaling a changing source and preserves completed output.
  It costs copying time. The producer is no longer held while this happens.
  Sequence/GE checks cannot guarantee that CPU in-place rendering never tears;
  hardware testing remains necessary.
- **Scalar vs VFPU copy:** reference screen thread owns VFPU context and uses
  vector loads/conversion/cache operations. Our worker is scalar and retains
  the verified progressive mapper and packed conversion. A VFPU port needs its
  own context and cache validation; the old interlaced assembler is not a
  drop-in replacement. No speedup is claimed without PSP timing measurements.
- **Timing/priority:** reference event-driven thread uses priority 24 (12 for
  POPS) and a timeout refresh, not necessarily a hard 10 Hz cap. Our worker keeps
  priority 0x38 and at most 30 output frames/s. Raising priority or uncapping it
  can steal game CPU. The prior artificial producer wait is removed instead.
- **Optional Speedboost:** `spb.c` changes VBlank waits, counts and related
  functions and uses legacy firmware-dependent machinery. That changes the
  game's timing contract, not just pixel-copy speed. Not transplanted to ARK-5.
- **VRAM reporting/lifetime:** both expand physical VRAM and hide extra space
  from game queries. Reference patches the GE-size entry globally and retains
  its preparation beyond a toggle. Our size override is syscall-scoped; kernel
  drivers still see physical size. Our plugin restores size when scanout allows
  it. Kernel homebrew that deliberately allocates upper VRAM is not proven
  compatible; no blanket global size-entry patch is introduced here.
- **Hook safety:** reference copies two instructions without relocation checks
  and relies on old imports/offsets. We resolve exports, reject branch/jump
  prologues, flush data/instruction caches, roll back partial hook installation
  and refuse unloading active hook users. This is compatibility validation, not
  a time limit or title blacklist.
- **Menus/configuration:** reference has its own blocking menu, language/fonts,
  hotkeys, per-layout offsets and thread suspension around setup/menu. Our INI,
  NOTE+R toggle, automatic activation and lifecycle remain. No global suspension
  of game threads is added to display capture.

## Evidence and validation

0.17 Metal Slug log stopped at exactly 980 observed frames across two DVE repair
attempts about 16 seconds apart. This rules out a rapid repair loop in that log;
it does not prove which query the game made. Source inspection proved that we
overwrote Sony's registered game buffer and missed direct layer-2 capture.
HOME was hardware-confirmed working in 0.17; retain it as a regression check.

Build precedes targeted host tests. Tests cover routing, mode interception and
explicit bypasses, prologue validation, concurrent-source rejection, pixel
mapping/conversion, memory boundaries and activation policy. They do not execute
Sony firmware. New hardware tests: first Metal Slug menu-to-game change without
Screen, visible HOME/resume/exit, then repeated Star Ocean intro transitions.
New 10-second diagnostics include observed source, actual Sony game buffer,
output/reject counts and remapped mode count; counters also appear on exit.
