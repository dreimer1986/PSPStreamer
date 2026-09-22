# Scalar VM dispatch optimization (2026-09-22)

The original scalar opcode range now bypasses the extended loop, register,
memory and branch dispatch. This does not fuse formulas, change floating-point
operations, reduce geometry, raise fuel limits or remove validation. Extended
operations retain their existing execution and rollback paths.

PSP build completed before testing. Eight focused VM tests passed: lazy
evaluation, exceptional values, loops, memory/register rollback, sparse variable
rollback, aggregate fuel exhaustion, malformed bytecode and dense-wave budgets.

Host comparison against commit `0333783`, using `tests/preset_copy_bench.c`,
`cc -O3`, five alternating before/after runs of 600 frames each (median):

| Preset | Before (ms) | After (ms) | Less CPU time |
| --- | ---: | ---: | ---: |
| wave-budget-demo | 493.990 | 422.026 | 14.6% |
| dense-wave-demo | 83.625 | 76.649 | 8.3% |
| eel-wave-loop-demo | 16.481 | 14.947 | 9.3% |
| fine-mesh-demo | 40.598 | 38.580 | 5.0% |
| phase-memory-demo | 39.798 | 32.889 | 17.4% |

Before/after hashes match for all five presets, including geometry and remaining
frame fuel. These are host evaluation timings, not rendered PSP FPS or GPU
measurements. Native hardware improvement remains to be measured. No changes to
audio, network, frame pacing, overclocking, or artwork were needed.

Heavy presets remain an optimization target. Simultaneous execution of two
presets for live transitions is deferred until sufficient headroom exists;
snapshot transitions remain available.

## Persistent mesh variables (follow-up)

The user confirmed a noticeable gain from scalar dispatch, without enough FPS
yet for expensive presets. A further change keeps q variables and named pixel
variables in the live VM array between mesh vertices. Only resettable inputs
and outputs are restored from the frame template. This removes three copies of
288 floats per vertex (3,456 bytes, almost 1 MiB at 289 vertices) and 1,152 bytes
of temporary stack storage. State is still committed only after the whole grid
succeeds; q propagation stays local to that grid.

Five alternating host comparisons against `e02e53f`, 600 frames, median:

| Preset | Before (ms) | After (ms) | Less CPU time |
| --- | ---: | ---: | ---: |
| fine-mesh-demo | 37.990 | 30.506 | 19.7% |
| eel-grid-logic-demo | 67.083 | 59.776 | 10.9% |
| wave-budget-demo | 418.874 | 420.239 | -0.3% (unchanged within noise) |

All output/fuel hashes match. Five targeted grid tests passed, including
persistent locals, q propagation, failure atomicity, both grid densities and
wave-budget reservation. PSP build precedes testing; hardware FPS remains a
user test. Audio and GPU code are unchanged.

Operand-pair dispatch, last-store caching, switch dispatch and local fuel
bookkeeping were also measured but produced no reliable improvement in the
host comparison; none are included in the release. Pure heavy wave programs
still need a different approach for substantial additional speed.

## Sparse runtime metadata (follow-up)

Runtime copies now clear the destination's old page-map entries and insert
the source's resident entries instead of copying 4,096 map bytes every time.
There are at most 32 resident pages per runtime, and usually none. Both operands
must be initialized runtimes, as all production call sites already require.
Keys, fill descriptors, random state and resident memory remain unchanged.

Only initialization can alter uniform fill descriptors (via the native clear
loop). Ordinary frame/point invocations therefore no longer save and restore
those eight descriptors unnecessarily. Init rollback keeps its full snapshot.

Five alternating host comparisons against `38309f4`, 600 frames, median:

| Preset | Before (ms) | After (ms) | Less CPU time |
| --- | ---: | ---: | ---: |
| wave-budget-demo | 413.410 | 414.299 | -0.2% (noise) |
| dense-wave-demo | 77.371 | 75.884 | 1.9% |
| fine-mesh-demo | 31.081 | 30.661 | 1.4% |
| phase-memory-demo | 32.654 | 32.157 | 1.5% |
| eel-memory-orbit-demo | 2.904 | 2.596 | 10.6% |

Output/fuel hashes match. Five targeted tests passed, including 128 transitions
between zero/one/32 resident pages compared byte-for-byte against the old copy,
self-copy, native-fill rollback and shape/wave/pixel failure atomicity.
Build completed before tests; these timings are not PSP FPS predictions.
