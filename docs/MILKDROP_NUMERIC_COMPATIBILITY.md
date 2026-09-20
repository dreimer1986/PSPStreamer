# Numerical compatibility: square roots and exceptional intermediates

Reference: supplied MilkDrop 2 tree at commit
`b5e4136c2f050eafa10aa199bb72c8e5c12c9320`,
[ns-eel2/asm-nseel-x86-msvc.c](https://github.com/eef2697d62fbe08e2fd927278/milkdrop2/blob/b5e4136c2f050eafa10aa199bb72c8e5c12c9320/ns-eel2/asm-nseel-x86-msvc.c).
Both its MSVC and GCC implementations perform `fabs` before `fsqrt`.

The PSP interpreter incorrectly rejected negative arguments. It now evaluates
`sqrt(fabs(x))`, matching that defined EEL behavior. The correction applies to
all formula contexts through the shared VM. It does not increase storage,
instruction fuel, shape density or rendering resolution, and does not alter
audio, playback clocks, or exit handling.

Regression tests cover negative/positive/zero arguments and negative dynamic
expressions. `eel-sqrt-demo.milk` exercises preset-frame, pixel and custom-wave
formulas with negative arguments. Its cyan wave should animate without errors
on LCD/TV, window/fullscreen; check that music stays clean. Host evaluation
alone does not establish PSP performance or desktop visual equivalence.

## Division by zero and assignment

The reference's `nseel_asm_div` uses floating-point division, permitting Inf
or NaN as intermediate results. With the reference's default 64-bit EEL
values, ordinary `nseel_asm_assign` converts non-finite and denormal values
to zero at assignment. It does not abort the preset at the division itself.
The inspected older compound-assignment paths do not have the same cleanup
sequence. They retain exceptional results. Ordinary assignment returns the
original right-hand expression, not the sanitized destination: for example,
`above(a=1/0,2)` is true while `a` contains zero afterwards.

Replacing every invalid operation with immediate zero would not reproduce
these semantics. Comparisons, expressions and chained assignments can observe
intermediate values. The PSP VM now retains them until ordinary variable,
register or memory assignment, including `assign()`. Compound stores bypass
the filter. Division by zero constructs IEEE Inf/NaN without issuing a PSP
hardware divide-by-zero. Domain/overflow results from log, pow, asin, acos and
exp can reach assignment too. Comparisons, min/max and branching reproduce
the inspected x87 unordered comparison behavior, rather than assuming NaN
compares like an ordinary number.

Safety boundaries remain explicit: non-finite addresses, loop counts, memory
lengths, bitwise and remainder integer operands are rejected before conversion.
Work budgets and rollback stay intact; renderer inputs still undergo finite
checks and bounds. A compound assignment that leaves a render field infinite
can therefore still fail safely. Syntax and unsupported-field errors are not
silenced. `invsqrt` still has its existing positive-input restriction.

The PSP VM remains single precision. Its overflow/denormal thresholds differ
from the default double Desktop VM; the assignment filter uses the equivalent
float exponent classes. This is not a claim of bit-for-bit Desktop parity.
Regression tests cover ordinary/chained/function/compound assignments,
registers, both memory spaces, unordered comparisons and integer boundaries.
Rollback tests now use genuinely invalid addresses instead of treating `1/0`
itself as a VM failure. The diagnostic preset also checks a sanitized division
and a bounded infinite intermediate.

Other observed remaining causes include initialization loops over 50,000
memory cells (beyond the PSP memory/work budget), malformed static values,
extension fields absent from this Desktop reference, and runtime arithmetic
or execution-budget failures. Failure-stage counts alone do not identify
their root causes.

## Collection result (2026-09-20)

Same supplied collection and deterministic 120-frame audit, including frame,
pixel and custom-wave formulas: 1,715 files, 1,651 imports, **1,414 full passes**
versus 1,217 before this batch. The isolated sqrt fix yielded 1,221; exceptional
intermediate/assignment handling accounts for the rest. No previously passing
case regressed. Import acceptance is unchanged. Remaining: 64 import failures
and 237 runtime failures (193 wave point, 36 init, four frame, three pixel,
one shape-frame). A stage is not a root-cause diagnosis. These are host formula
tests, not 1,414 verified PSP visual/performance matches.
