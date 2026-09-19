# Formula storage expansion

September 19, 2026; baseline `e84d740`.

This batch chooses formula storage before shape batching: it permits additional
presets to load/run without changing the GU instance pipeline. Shape instances
remain capped at eight; shaders remain intentionally ignored.

| Resource | Before | Now |
| --- | ---: | ---: |
| Local megabuf float slots per context | 1024 | 2048 |
| Shared gmegabuf float slots | 1024 | 2048 |
| Compiled pixel/custom-wave-point instructions | 1024 | 2048 |
| Compiled frame/init instructions | 2048 | unchanged |
| Execution steps per invocation/frame | 4096 / 262144 | unchanged |

The point-program change uses already allocated `PmProgram` capacity. Programs
are still rejected transactionally when they exceed the ceiling; they are not
truncated. Memory accesses remain checked, with rollback of local/shared memory,
registers, variables and random state on failure. Large memory operations still
consume execution fuel. Higher addresses do not grant extra runtime work.

## Collection comparison

`tools/check_milkdrop_collection.py`, same 1,715 files and 120 deterministic
frames, including frame/shape, pixel and custom-wave evaluation:

| Result | Before | After |
| --- | ---: | ---: |
| Imports successfully | 1607 | 1651 |
| Passes all exercised formula stages | 1188 | 1217 |

No previously passing case regressed. All 29 newly passing cases previously
failed import: the observed gain in this collection comes from compiled point
capacity, not a demonstrated gain from megabuf expansion. The larger megabuf
is verified separately by boundary/rollback tests and the new memory demo.
The remaining failures are not all memory failures and are not claims about
hardware limits. Host acceptance does not prove performance or desktop visual
equivalence; all newly passing collection files contain skipped shader code.

## Memory and stack

Each runtime grows by 4 KiB. The two render-owned state structures together grow
by 112 KiB, and the shared/fallback VM arrays by 8 KiB. Preset program storage
does not grow. Temporary state copies and rollback journals also grow.

PSP GCC with the normal `-O2 -G0` plus `-fstack-usage` reports 175,320 bytes for
custom-wave evaluation, 36,768 for VM execution, 3,392 for `md_frame`, 1,784 for
`play_audio_once`, 96 for `play_audio`, 2,616 for `main`, 8,816 for `offline_play`
and 9,352 for `offline_browser`. Together these named frames use 238,144 bytes
on the deeper local-music path, leaving about 23 KiB within the existing 256 KiB
main stack for leaf helpers. This is a static call-path check, not a hardware
stack-watermark measurement. Host convenience wrappers are not the PSP playback
path. No stack, thread priority, audio buffer or scheduler changes are included.

## Test selection

- `extended-memory-demo.milk`: eight colored orbiting hexagons; local and shared
  addresses 1536–1551 and 2047, including bulk copy and separate shape memory.
- `extended-point-program-demo.milk`: cyan animated wave with a point program
  above the previous 1024-instruction ceiling, below the unchanged fuel limit.
- Optional original `Flexi - crush ice 37.milk`: newly imports and completes the
  formula test. Its shader sections are ignored, so expect a reduced appearance.

The two owned demos run through the LCD/TV, window/fullscreen GU harness.
On PSP, test them with streamed and local music, switch tracks/presets, then
return to video. Listen for crackling and check responsiveness. Profiling can
also reveal whether larger state copies affect expensive existing presets.
