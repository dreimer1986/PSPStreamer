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
