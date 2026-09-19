# Batched custom-shape rendering

The playback renderer now imports up to **512 instances per shape**, rather
than reducing every request to eight. There are still four shape slots and
3–100 drawable polygon sides. These are PSP implementation budgets, not
claims about absolute hardware capacity or unrestricted Desktop equivalence.

## Evaluation and state

`md_eval_preset_shapes` evaluates instances in slot/instance order, retaining
the existing shared per-shape locals, init seeds, q inputs and random/memory
state. `instance` and `num_inst` reflect the effective native loop. Assigning
them does not resize that loop. The ordinary legacy evaluator still returns
its bounded eight-instance `MdDecor`; playback uses the expanded API.

Extra instances share at most half the remaining frame execution allowance,
after conservative reservations for pixel and custom-wave formulas. Wave
programs that can change sample count reserve the full supported point count;
loop/bulk-memory hints remain conservative. Requests are proportionally reduced
when necessary, keeping the existing up-to-eight baseline per active slot.
Fuel is not increased and formulas are neither truncated nor retried after
partially changing shared VM state. Genuine runtime errors remain errors.

All shape parameters are evaluated before opening a GU list. On an evaluation
failure, the existing public state/decor/warp output is not committed and no
shape geometry is submitted. The private parameter buffer is staging scratch,
not an independently transactional public output. Its counts publish only on
successful shape evaluation. Existing cross-context global VM semantics remain
unchanged; there is no new whole-frame replay/rollback claim.

## GPU ownership and framebuffer safety

Subsequent update: [offscreen shape clipping](MILKDROP_OFFSCREEN_SHAPES.md)
uses weighted packet slots for expanded clipped geometry. The ordinary
32-shape packet remains unchanged for fully interior shapes.

At most **32 actually drawable shapes** are expanded into vertices per packet.
Before reusing the fixed list/vertex buffer, the renderer finishes and waits
for the previous GU list. It starts a new DIRECT list and explicitly restores
the feedback target, pixel format, viewport and scissor. Every textured shape
rebinds its feedback/external texture; blend and texture enable state are set
for each shape. Instance drawing order is unchanged, including translucent
fills and outlines. No VBlank wait, LCD/TV mode switch or extra presentation is
inserted between packets. Only the completed frame is presented as before.

This explicit restore is required because PSPSDK's
[`sceGuStart`](https://github.com/pspdev/pspsdk/blob/master/src/gu/sceGuStart.c)
can reissue its stored framebuffer pointer, while
[`sceGuDrawBufferList`](https://github.com/pspdev/pspsdk/blob/master/src/gu/sceGuDrawBufferList.c)
only writes list commands. Merely restarting the list is not sufficient.

If a restart fails, the already-finished list is not reused as an active one;
normal visualization shutdown releases its resources. The host harness tests
this path and simulates the SDK restoring a different framebuffer.

## Resources and verification

The parameter staging allocation is 188,432 bytes, owned only during active
MilkDrop rendering and freed on stop/start failure. It stores positions/colors
and other shape parameters, not expanded vertices. The existing 1.5 MiB GU
buffer, feedback textures, 256 KiB main stack, audio priorities and idle factor
remain unchanged. No additional full-frame/state copy is added to the stack.

The maximum-layer host test renders 4 × 512 simple shapes with maximum sides,
thick borders, waves and effects on LCD/TV in both layouts. Peak vertex storage
remains 1,206,368 bytes; preceding packets are synchronized before reuse.
Tests also cover instance order across packet boundaries, persistent locals,
late-instance failure without partial state publication, budget reservation,
and the original `Geiss - Explosion nz+.milk`.

The completed host suite passes all 175 tests. A comparison of the same 1,715
collection presets retains 1,651 successful imports and 1,217 successful
120-frame formula runs, with no previously passing preset regressing.
`Explosion nz+` completes all 120 frames with effective instance counts
`[1, 311, 281, 0]`. These checks establish bounded execution and host-renderer
invariants, not PSP frame rate or audio-underrun performance.

Profiling still reports the final outstanding GPU wait separately. Time spent
waiting between shape packets is included in `geometry_submit`; that phase is
therefore not a pure CPU measurement.

## PSP tests

1. `shape-batches-demo.milk`: a rotating ring of 128 colored diamonds, four
   geometry packets. Check LCD/TV and fullscreen/window transitions.
2. `Geiss - Explosion nz+.milk`: the formula test reaches all requested 311 and
   281 instances of shapes 1 and 2 (plus shape 0), rather than eight each.
   This increases work substantially and does not promise high FPS.
3. Switch presets/tracks, stop playback and start a video. Listen for audio
   underruns and watch for framebuffer corruption or stale partial images.

For collection checks, pass `--expanded-shapes` to
`tools/check_milkdrop_collection.py`; omitting it retains the legacy evaluator
for like-for-like comparisons. Shader execution remains excluded.
