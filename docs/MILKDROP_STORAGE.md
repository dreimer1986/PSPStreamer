# Bounded, owned formula storage

Compiled programs no longer embed 2,048 instructions in every one of the 23
contexts, including unused contexts. A zero-initialized `PmProgram` owns a
checked, geometrically grown import-time allocation. After import it is shrunk
to its actual instruction count when possible. Empty programs allocate nothing.
The preset-wide limit charges allocated capacity, not just used instructions.

| Resource | Previous | Current |
| --- | ---: | ---: |
| Instructions per program, including point/pixel | 2,048 | 4,096 |
| Numbered source records per block | 512 | 1,024 |
| Named variables per context | 64 | 128 |
| Local/shared memory slots | 2,048 | 4,096 |
| Allocated bytecode per loaded preset | fixed 736 KiB | at most 256 KiB |
| Execution fuel per invocation / frame | 4,096 / 262,144 | unchanged |

The new aggregate cap is 16,384 instructions (16 bytes each). A candidate
program can temporarily add at most 4,096 instructions before the aggregate
check; the old preset remains alive until its replacement succeeds. Source
buffers and the candidate are freed on all error paths. A failed shrink may
retain capacity, but that capacity is still charged to the hard cap.

`md_load_preset` now requires a zero-initialized owning destination. Successful
replacement frees old programs; failed replacement leaves it untouched.
`md_free_preset` and `pm_program_free` release their respective owners and are
idempotent. Do not shallow-copy owners or clear an owning program with `memset`.
Playback keeps the last loaded preset cached; replacements release its code.
Allocation failure is an I/O/resource error, not an unsupported formula.

Larger activation candidates and VM rollback scratch moved off the PSP stack
into bounded storage owned by the existing single visual-renderer thread.
The VM already has shared frame fuel/registers and remains non-reentrant.
There are no added allocations in formula execution. At `-O3`, PSP GCC reports
107,824 bytes for the largest wave evaluator and 1,296 bytes for its VM callee,
below the 256-KiB main stack. This is compiler stack accounting, not a measured
whole-program high-water mark. The host owning preset is 63,464 bytes, with a
237,228-byte activation state; host pointer size differs from PSP.

Tests cover growth, maximum variables/memory, aggregate rejection, atomic
failed appends, bytecode safety, repeated preset replacement, 116 injected
allocation failures and idempotent release. The allocation tracker observes no
leak and no allocations during the exercised playback evaluation. Existing
LCD/TV/window/fullscreen renderer tests still exercise the actual import/VM path.

## Collection result, 2026-09-20

Same 1,715 files and deterministic 120-frame audit: **1,688 imports** (previously
1,651), **1,418 full passes** (unchanged), no import or runtime regressions.
The 37 newly accepted files still fail during initialization. Remaining import
failures include malformed input, extension fields and some storage/namespace
limits. Execution budgets deliberately did not grow along with storage.
192 wave-point cases still exhaust the frame budget. More import capacity does
not imply those presets can render in real time on a PSP.

Test `formula-storage-demo.milk` (72 named variables and address 3000), existing
dense/Geiss presets, repeated preset/music changes, then transition to a video.
Check both outputs and clean audio. No server or OC-plugin update is required.
