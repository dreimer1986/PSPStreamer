# Desktop operand assignments (2026-09-24)

The original MilkDrop 2 `ReadCode` / `StripLinefeedCharsAndComments` joins
numbered formula records without inserting a semicolon. The original
`ns-eel2/nseel-compiler.c` `preprocessCode` was compiled and executed separately
to verify the binding (diagnostic line callbacks stubbed, preprocessing unchanged).

Hexcollie's `gamma=1 + bass*bass_att` followed by `chng=sin(time*.5);` becomes:

```
_set(gamma,1 + bass*_set(bass_attchng,sin(time*.5)));
```

Thus `bass_attchng` is one identifier, and assignment binds to the immediate
operand inside arithmetic. Adding a semicolon would change Desktop behavior.
The PSP parser now accepts this binding; preset files are never repaired or
rewritten. Assignment validation, bounded execution and safe numeric stores remain.

Verification: production build first; eight focused parser tests pass. Both
original Hexcollie nz+5/nz+6 presets pass import and 120-frame host execution
including expanded shapes, pixel and custom-wave evaluation. The expanded
collection initially imported 2266/2267 files; this was not a full-collection
execution test.

## Numbered lookup: Clouded Bottle

The original `state.cpp` `ReadCode` calls `GetFastString` for suffixes 1,2,...
and stops at the first absent key. `_GetLineByName` searches the file's indexed
keys when the next physical line does not match. Clouded Bottle's later duplicate
4/9/10 records are not appended; missing 12 means records 13 onward are not read.
The PSP importer now collects separate records, orders them by number and compiles
only the contiguous prefix. Preset files remain untouched. Physical source-line
diagnostics are retained, including when records occur out of order. Additional
import-only scratch space is bounded by source size and freed before playback;
the runtime memory layout and execution budgets are unchanged.

Build first, then six focused parser tests: eight formula contexts, duplicates,
gaps, missing first key, out-of-order records, diagnostics, comments, limits and
the previous Hexcollie fix. All pass. Clouded Bottle passes 120 host frames;
the complete expanded collection now imports **2267/2267**. This does not claim
full-collection runtime success, Desktop-identical rendering or PSP FPS.
