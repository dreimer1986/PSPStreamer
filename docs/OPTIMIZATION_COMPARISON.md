# PSP optimization comparison — 2026-09-20

Update: the user tested O3 successfully and reported subjectively smoother
operation. O3 is now the default; `OPT_LEVEL=-O2` remains available for comparison.
The figures/hashes below describe the original comparison builds, **not** the
subsequent build with runtime LCD/TV menu switching.

The client source is unchanged from `fbb51c4` (illustrated help). Only the
Makefile now allows `OPT_LEVEL=-O3`; its default remains `-O2`. Both variants
were rebuilt in separate clean directories with PSP GCC 15.2.0. Firmware,
decoder, playback clocks, audio buffering and MilkDrop budgets are unchanged.
No fast-math or architecture changes are included.

| | Default O2 | Test O3 |
|---|---:|---:|
| EBOOT.PBP bytes | 4,631,804 | 4,702,140 |
| PRX bytes | 4,440,302 | 4,510,638 |
| ELF text bytes | 3,356,460 | 3,413,788 |
| ELF data bytes | 794,808 | 794,808 |
| ELF BSS bytes | 2,225,992 | 2,225,992 |

Both builds completed without compiler warnings. PBP offsets, the matching
embedded PRX, application icon and XMB background were checked. The clean O2
EBOOT is byte-for-byte identical to the existing release. This is **not** a
real-hardware performance or stability result for O3.

## Test installation

`psp-client/release/PSPStreamer/` remains the normal O2 release.
`psp-client/release/PSPStreamer-O3/` contains the two **replacement files** for
the comparison, not a complete standalone installation.

1. Close the PSP application and retain a copy of the normal EBOOT/PRX pair.
2. Copy **both** `EBOOT.PBP` and `PSPStreamer.prx` from the O3 directory into
   the existing `PSP/GAME/PSPStreamer/` directory. Leave the bridge, TV module,
   font, presets and PSP/SYSTEM configuration untouched. Safely eject.
3. Compare the same demanding MilkDrop presets with identical music, display
   mode and CPU-clock settings. Include Cauldron painterly 3, Explosion,
   fullscreen transitions and switching between music and video.
4. Check audio crackling, controls, stop/start, seeking and one longer video
   on LCD and TV. Revert the pair to O2 if any regression appears.

The separate server update does not require the O3 client. Do not assume
larger code is faster: O3's inlining/unrolling can also increase cache pressure.

## SHA-256

```text
O2 EBOOT.PBP:       ee2c9f59a78cb67088b59e208cdbe40f91dbbbd3fd48a73f37ab9de7a7c7ad17
O2 PSPStreamer.prx: 2ece20417a783dd4d502e7880cd8828da2fbf67073e7757c5ebf3cf1f18628ea
O3 EBOOT.PBP:       7f3ae8b17e3300e4546dc5d9cae246dbbcb1cf3458fb83d1e95affe700ea7ace
O3 PSPStreamer.prx: cec8f8ddfb7bed47aa7ab01427bfe87f7aef7fb6d755831cb21ee76988f158ab
```
