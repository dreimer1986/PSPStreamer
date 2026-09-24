# Numbered formula blocks: collection audit

Historical audit below. As of 2026-09-24, the expanded collection imports
2267/2267 presets. Numbered lookup now handles physical reordering and stops
at missing keys like Desktop. See [current results](MILKDROP_ASSIGNMENT_COMPATIBILITY.md).

Date: 2026-09-18. Baseline: `cb0cb24`. Collection:
`favorite_presets_2021_01_03`, including subdirectories (1,715 `.milk` files).

## Change

Compile each context's numbered records as one program, following MilkDrop 2's
`ReadCode` and `StripLinefeedCharsAndComments` in `vis_milk2/state.cpp`.
Remove record delimiters, optional leading backticks and both Desktop line-comment
forms; preserve explicit whitespace. This allows split expressions, identifiers,
loops and block comments. Physical line mappings remain available for errors.
Init programs are compiled before the corresponding frame programs.

Source buffers are allocated only during import and released on every exit.
Playback structures and execution budgets are unchanged.

## Like-for-like results

| Check | Baseline | New | Gain |
| --- | ---: | ---: | ---: |
| Successfully imported | 960 | 1,003 | 43 |
| All tested formula stages passed for 120 frames | 817 | 838 | 21 |

No previously passing preset regressed in either check. Of the 43 newly imported
presets, 22 encounter another execution problem under this test signal.

The execution check covers init/frame/shape programs, every vertex of the pixel
grid, and enabled custom waves, with deterministic synthetic audio inputs and
normal per-frame budgets. It is **not** a PSP rendering, texture-loading,
performance or long-duration playback test. Passing does not establish identical
Desktop appearance or safety for every possible audio input.

Reproduce with:

```sh
python3 tools/check_milkdrop_collection.py /path/to/presets --frames 120 > audit.json
# Compare against a source snapshot using the same audit harness:
python3 tools/check_milkdrop_collection.py /path/to/presets --frames 120 \
  --source-root /path/to/baseline-checkout > baseline.json
```

## Why presets still fail

There are **712 import failures**: 660 reported as invalid and 52 as unsupported.
The invalid category currently combines syntax/numbering errors and compiler
resource limits; the audit does not distinguish their exact individual totals.
It must not be interpreted as 660 broken Desktop presets or unavoidable PSP limits.

- Compilation is still bounded to 128 numbered records per context, 512
  instructions per init/frame program, 256 per pixel/point program, 64 named
  locals and parser depth 16. These are implementation budgets, not measured
  hardware maxima. Truncating a program is not equivalent to clamping a drawing
  parameter and can remove calculations other formulas depend on.
- Compatibility gaps remain. Examples in this collection include consecutive
  empty statements (`;;`), currently rejected assignments to `sample` and
  `num_inst`, and unrecognized fields such as `nEchoWrap_x`,
  `wavecode_0_bDrawBack` and `shapecode_0_tex_capture`. These need individual
  reference comparisons, not a blanket claim of hardware incompatibility.
- 165 imported presets fail during execution of this synthetic test: 94 at the
  frame/shape stage, 49 in custom waves and 22 in pixel programs. Execution
  diagnostics still combine invalid arithmetic, memory addressing and exhausted
  execution budgets. The stage counts do not establish the underlying cause.

Existing drawing-value clamps and shader-source skipping remain in effect.
The next compatibility work should distinguish rejection causes and implement
safe reference-based fallbacks; it should not silently truncate formula code or
remove execution safeguards protecting music playback.

## Suggested PSP checks

- `multiline-formula-demo.milk` (included): split formulas in all eight contexts,
  animated shape and custom wave. Verify both LCD and TV output and clean audio.
- `Aderrasi - Curse of the Mirror Emu.milk` (user collection).
- `Unchained - Unified Drag 2.milk` (user collection).

Both collection presets newly pass the full host formula-stage test. Third-party
collection files are not bundled with the application.
