# Init, frame/shape and custom-wave resource handling (2026-09-21)

This batch addresses all three runtime failure groups together. It does not
implement shader support, increase feedback resolution or modify audio, OC,
decoder, networking or display switching.

## 1. Initialization

The baseline has 73 init failures. Most initially fail inside large loops
clearing `megabuf`/`gmegabuf`, often 10,000–1,048,576 iterations. Those loops
spent the entire old per-call allowance just interpreting repeated assignments.

The init VM now recognizes precisely this bytecode pattern:

```
loop(count, megabuf(index)=0; gmegabuf(index)=0; index+=1;)
```

Either memory store can be omitted; `index=index+1` also works. The index must
be an ordinary named variable, initially an exactly representable integer in
the bounded recognition range. Nonzero fills, extra side effects, branches,
duplicate stores and other loop bodies use the ordinary interpreter.

Recognized loops clear only their intersection with resident PSP memory and
preserve the final index and expression result. Skipping zero stores outside
resident memory is an explicit PSP approximation, **not full Desktop memory
emulation**. Subsequent ordinary out-of-range reads/writes still report errors.
Native work is charged per visited resident cell, journaled for rollback and
bounded by both invocation and frame fuel. There is no playback allocation.

The inspected Desktop `ns-eel2/ns-eel.h` defines a counted-loop ceiling of
1,048,576. Ordinary loops now use that ceiling rather than silently truncating
at the PSP invocation-fuel constant. They still stop with an error on exhausted
fuel. Complex raymarching init loops are not converted into no-ops or successes.

## 2. Frame and shape execution

One global 262,144-step budget remains shared across the whole visual frame.
The per-call caps now distinguish execution frequency:

* Point/pixel/ordinary calls: unchanged 4,096 steps.
* Init calls: at most 65,536 steps, from the same frame budget.
* The one main preset-frame program: at most 131,072 steps, also from that budget.

This is an explicit redistribution of existing frame work, **not** an unchanged
per-call limit or a promise of faster execution. Previously rejected nontrivial
frame programs may now take longer. The remaining mesh, shape and wave work must
fit in the remaining pool. Infinite/huge loops still fail transactionally.

The original failing shape reads global slots around 6144. Shared global memory
now has 8,192 slots; local contexts remain at 4,096. Increasing just the shared
buffer avoids duplicating an expansion across every context. Together with
the enlarged rollback journal this adds about 48.5 KiB on the 32-bit PSP, not
hundreds of KiB per preset. Slots beyond these limits remain unsupported.

## 3. Custom-wave density

The original 192 wave-point cases exhausted aggregate frame fuel, not arithmetic
or import validation. Remaining wave work is estimated from bytecode; requested
point counts share available fuel proportionally, after reserving later wave
setup. Loops/bulk operations use conservative hints. Expensive waves evaluate
fewer points instead of raising aggregate fuel. All hard VM checks stay active.

PCM/spectrum preparation still covers the original requested window. Reduced
geometry samples that smoothed window across its full range, keeping both
endpoints and `sample=0..1`. Non-reduced waves retain the prior path; point
`samples` reflects the reduced count only when reduction occurs. Stateful
per-point equations therefore run fewer times and may look different. This is
a deliberate bounded-quality fallback, not Desktop visual equivalence.

The minimum is two points; if a formula cannot fit even that, it still fails.
Conservative estimates are not a proof that every dynamic loop will fit.
Invalid addresses, malformed bytecode and non-finite rendering outputs are
not hidden by density reduction.

## Validation and hardware test

Collection comparison (1,715 files, 120 frames each, identical default audit
settings): 1,688 still import; full passes increase from 1,418 to **1,624**.
No previously passing preset regressed. All 192 original wave-point failures,
all four original main-frame failures and the original shape-frame failure pass.
Of the original 73 init failures, nine now pass completely. The remaining 64
fail at init (23), main frame (34) or shape frame (7); the later failures were
previously masked by init failure, not regressions. The 27 import failures are
unchanged. Further memory/complex-formula work remains necessary; these changes
do not claim complete Desktop compatibility.

The directly affected suites pass 103 tests (two platform-dependent skips).
Both new demo presets also pass 120 frames with expanded shape evaluation.
The PSP build succeeds; real-hardware performance remains to be confirmed.

Tests cover optimized-versus-interpreted clear results, rollback of local/global
memory and registers, rejected optimization patterns, expanded global bounds,
shared fuel exhaustion, huge-loop rejection, four-wave fairness, full-window
endpoints, and genuine formula errors. Existing preset and renderer tests are
also exercised. Host tests cannot establish real-time PSP audio or visual parity.

Two original diagnostic presets are provided:

* `phase-memory-demo.milk`: large init clear, a 1,024-iteration frame calculation,
  and a moving cyan shape reading shared slot 7000.
* `wave-budget-demo.milk`: four dense colored waves sharing fixed frame fuel.

Also compare a familiar demanding preset and the formerly failing green-machine
preset. Try normal/fullscreen rendering, preset changes, music Stop and then a
video. Check controls and clean audio. Only EBOOT/PSPStreamer.prx plus the two
new preset files change; no server or OC plugin update is needed.

Hardware follow-up: the wave-budget demo runs, but slowly. The phase-memory
demo was reported black despite valid formula output. Its transparent edge
exposed missing explicit `GU_SMOOTH` state: flat shading uses the last vertex,
making the whole triangle transparent. The renderer now selects smooth shading
whenever it restores a render target, including list restarts. The GU harness
starts each list without that state, requires it before drawing, and exercises
the actual demo in LCD/TV and windowed/fullscreen layouts. This checks submitted
geometry/state, not hardware rasterization; visual confirmation remains needed.
