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
collection imports 2266/2267 files; this is not a full-collection execution test.
The remaining Clouded Bottle uses duplicate/gapped record numbers (including
missing per_frame_12). Desktop stops reading a code block at a missing key;
matching that import behavior remains separate work. No PSP FPS claim is made.
