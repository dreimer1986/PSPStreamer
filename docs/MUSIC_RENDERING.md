# Music rendering: ownership and performance baseline

The user confirmed crackle-free music on both LCD and native TV output with
commit `1852d20`. Keep that revision as the hardware-validated reference.
The subsequent cleanup preserves audio decoding, PCM ownership, DAC timing,
FLV timestamps, video presentation and the existing diagnostic facilities.
Host tests alone do not establish real-device audio or display timing.

## Responsibilities

| Component | Responsibility |
| --- | --- |
| `main.c: play_audio` | Start/stop workers, input, initial scene before audio, scope GUI priority and restore it before returning |
| `music_ui.h` | Shared visual envelope, 50 ms minimum redraw interval, 10 ms input poll and GUI priority 0x40 |
| `lcd_music.h` | LCD music scene and incremental writes at 480×272, stride 512; no extra framebuffer |
| `tv_gui.h` | Native TV scene and dirty rectangles at 720×480, stride 768, using the existing RAM canvas |
| `display_output.h` | Output mode ownership and transitions, not playback scheduling |
| Existing audio worker / output worker | Decode and deliver PCM; renderer must not change or retain their buffers |
| Existing FLV/PTS video path | Audio/video synchronization; visual refresh intervals are never playback clocks |

The priority numbers follow PSP scheduling: smaller numbers have higher
priority. GUI work remains below the existing DAC worker. The caller's
original priority is restored on both the create-thread failure and normal
exit paths. This cleanup does not change any worker's priority.

## Rendering invariants

- Draw the first scene before starting music decoding. Reset scene state for
  every new track. Fullscreen changes redraw once, not on every loop.
- Keep the active framebuffer address, format and stride fixed during music.
  Incremental ticks must not reset the output mode, especially with an OSSC.
- Never let a music renderer write while video or the other output owns
  scanout. Stop/join playback workers before returning to menu rendering.
- Advance the visual spectrum envelope once per rendered tick. Its existing
  attack/decay formula is deliberately unchanged; it is not an audio filter.
- Preserve compositing order. LCD spectrum peaks overlap four rows of the
  volume label; the fullscreen knob overlaps the last six spectrum rows.
  Restore/repaint those intersections only when needed.
- Keep counters and sync diagnostics. Copy-byte counters describe background
  restoration or rectangle transfers, not total CPU time or all pixel writes.

The cleanup removes duplicated spectrum formulas and scattered music timing
constants. It also avoids redundant LCD volume text and overlap redraws when
neither volume nor the affected background changed. Full-render paths remain
as startup/layout rendering and test references; they are not dead code.

## Verification and remaining opportunities

The host suite checks 720 LCD and 180 TV incremental images against full
rendering, with actual artwork, both layouts, volume changes, pause/decay,
padding and ownership guards. LCD tests use the shipped font. An independent
exhaustive comparison of 10,201 level/target pairs protects the shared
envelope against accidental behavior changes.

Further changes should be measured separately:

- LCD needle envelopes are still restored each visual tick. Tracking
  quantized needle changes could save additional small copies, but requires
  preserving analogue state advancement separately from drawing.
- TV indicator colors currently sample input/time multiple times. A per-tick
  snapshot could reduce calls and make rollover behavior deterministic.
- Do not expand this cleanup into network lifecycle, decoder initialization,
  audio queue sizing, compiler optimization levels or thread scheduling.
  These need separate failure-injection tests and hardware validation.
- Review finding for a separate lifecycle patch: music checks thread creation
  but does not yet check the return from starting that thread. This predates
  the GUI changes. A failed start needs tested cleanup, not a silent retry
  or a change to the proven audio loop.

## Boundary for a MilkDrop prototype

Keep an optional visualization behind a separate renderer interface. It may
consume bounded snapshots of analysis data; it must not keep pointers into
PCM buffers, block the DAC, change sample clocks or take over video-owned
scanout. Bound frame work and memory before integrating presets. The current
20 Hz receiver budget is a safe existing policy, not a promise that MilkDrop
will reach that rate. Source/license and preset-feature investigation comes
next; see [visualization references](VISUALIZATIONS.md).
