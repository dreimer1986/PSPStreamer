# MilkDrop 1.04b source reference

Archive supplied by the user:
[milkdrop_104b_src.zip](https://www.geisswerks.com/milkdrop/milkdrop_104b_src.zip).

Downloaded successfully with a browser user agent, the MilkDrop landing page
as Referer and `?download=1`; the unmodified curl request returned HTTP 406.
SHA-256: `fb7e7294eb7c487efbe49be48dc5a4e32dae8afc1beacbd8484ee25c1a49e695`.
Local study copy: `/home/dreimer/psp-streamer/milkdrop1-study.Y0HTSx/milkdrop_104b_src/`.
The archive was inspected, not built or executed. No bundled executable,
library, preset collection or source file was imported into PSPStreamer.

## Findings

The examined renderer is a useful fixed-function reference, but is still
Windows/Direct3D 8 code, not a platform-independent PSP engine.

- `support.h:26,79–81`: Direct3D 8 include and FVF vertex-format definitions.
  Calls such as `SetVertexShader(SPRITEVERTEX_FORMAT)` select these formats;
  they are not evidence of programmable vertex programs. No
  `CreateVertexShader`, `CreatePixelShader` or `SetPixelShader` calls were
  found in the archive's C++/header sources.
- `milkdropfs.cpp:1306`, `WarpedBlitFromVS0ToVS1`: CPU transform calculations
  and texture feedback. This fits our existing GU feedback architecture.
- `milkdropfs.cpp:1435`: so-called per-pixel equations are evaluated in the
  warp mesh path. A PSP implementation should have a separate vertex context
  and a total mesh execution budget, not run code for every screen pixel.
- `milkdropfs.cpp:1554`, `DrawCustomShapes`: polygon geometry and blending.
  A strong next candidate because simple shapes need no new PCM analysis.
- `milkdropfs.cpp:1820`, `DrawCustomWaves`, and `:2009`, `DrawWave`: custom
  and built-in waveform paths, including per-point evaluation. Actual waveform
  support needs a bounded snapshot of audio samples; our twelve spectrum bins
  are not a substitute for a stereo waveform.
- `milkdropfs.cpp:992`, `DrawMotionVectors`: an additional mesh-derived effect
  to consider after transform support, without needing shader hardware.
- `state.cpp:1484–1654`: distinct initialization, frame and point/mesh program
  lifetimes. Do not merge their state or reset persistent variables every frame.
- `evallib/cfunc.c`: naked functions and x86 inline assembly used as copied
  machine-code templates. This older evaluator is not a portable MIPS fallback;
  keep extending our bounded interpreter rather than linking it unchanged.

## Implementation priorities

1. Add bounded custom shapes, alpha/additive blending and their static fields.
2. Extend transform parameters and a separately budgeted mesh program context.
3. Add real waveform snapshots and selected built-in waveform modes without
   handing audio-buffer ownership to rendering.
4. Expand preset/state compatibility and test it against documented original
   behavior. Old presets are not automatically supported by the current subset.

These are implementation candidates, not features enabled by this audit.
Keep the validated audio/PTS/display-mode paths unchanged.

## Attribution boundary

The archive contains Nullsoft permissive notices (for example the full header
of `utility.cpp` and `DOCUMENTATION.TXT`'s License section). Do not assume the
MilkDrop 2 BSD notice automatically covers every file here: inspect and retain
the applicable notice for each future imported component. The evaluator and
bundled presets require their own provenance review before redistribution.
