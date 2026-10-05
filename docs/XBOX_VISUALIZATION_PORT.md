# Xbox visualization port — 0.6.9

User confirmed 0.6.8: all effects now run cleanly and substantially faster.
0.6.9 adds static music-GUI retention/partial presentation, active-row-only
spectrum clearing and time-based PSP envelope steps. Real-console comparison
is pending; remaining larger options are listed in `XBOX_NEXT_STEPS.md`.

0.6.8 also handles a secondary error exposed in the saved 0.6.6 log: after the
debugger-induced network interruption, `net_done=1 clean=0 net_bytes=0` persisted
with `audio_queue=1` and frozen `pos_ms=63815`. Once both encoded queues are
empty, failed network EOF now immediately enters the existing stop/reconnect
path instead of waiting indefinitely for that DMA slot. Clean EOF is unchanged.
0.6.7 live telemetry on 1920x1080 output showed about 7–8 UI updates/s, no audio
underruns in the observed windows, and roughly 28–82 ms per present (including
SDL's deferred software commands). MilkDrop's first GPU-submission phases fell
from roughly 105–114 to 35–41 ms, though first-frame tracing itself adds overhead.
0.6.8 additionally removes additive end-of-frame waits: both GUI and effect use
a 50 ms start-to-start ceiling, without sleeping another frame's processing time.
Remaining CPU/HD composition cost is real; these samples are not a full benchmark.

## Shared performance bottleneck (0.6.7)

Two serial samples of 0.6.6 stopped in `_memcpy+0x24` at `0x00015fe4`:
the pinned SDK implements a byte load/store loop. One caller was texture
staging in sceGuDrawArray, the other the final effect scaler. Requesting a
whole row via memcpy was therefore still reading WC GPU memory byte by byte.
0.6.7 uses explicit SSE1 unaligned 64/16-byte blocks with an exact byte tail
for texture readback/upload, effect scaling and SDL framebuffer damage copies.
It does not replace libc globally or change PSP code. Scope includes Spectrum,
Monkey, MilkDrop, and the shared GUI output copy. Video decode/PTS is unchanged.

Seven focused visual checks plus the existing damage-copy/cache harness pass.
The new assembly-copy test includes all pointer alignments, tail sizes, output
canaries and inaccessible guard pages (ASan alone cannot inspect inline asm).
Serial `visual timing` summaries every five seconds report mode, output size,
frame count, elapsed time, composition/present totals, worst draw and underruns.
No real-console speedup is claimed before a comparable console run.

The debugger halts during sampling themselves caused temporary repeating audio;
they must not be counted as spontaneous crashes. Normal measurements should use
the non-stopping timing summaries.

Remaining candidates, conditional on those measurements: caching the static
receiver composition instead of rebuilding it for every music frame; avoiding
unnecessary HD copies when only a panel changes; GPU presentation/scaling instead
of GPU-to-CPU readback; batch projection of Monkey vertices. These are not proven
causes of the present regression, and GPU presentation changes ownership of the
scanout, so it needs a separate compatibility/lifecycle check.

## Console fixes and current verification

- 0.6.1–0.6.5: SDK exp2/expm1 and numeric-conversion placeholders replaced;
  NV2A DMA contexts/depth pitch and scanout teardown corrected. Fullscreen
  skips the hidden menu and uses an opaque row-cached presentation scaler.
- 0.6.5 serial evidence: flight-start access violation at `0x001b53c2`,
  `MOVAPS [ESI+0x3ccc10]` with `ESI=0x7ec00008`. The Xbox malloc substitution
  violated CaveScene's alignment. 0.6.6 retains 64-byte aligned allocation.
- 0.6.5 first MilkDrop frames spent about 265–274 ms in GPU submission, versus
  about 1 ms in the final GPU wait. This includes CPU adapter work, not just
  GPU execution. 0.6.6 stages texture swizzling in cached RAM, reads GPU rows
  sequentially and batches command publication instead of flushing per register
  or ten vertices. Preset resolution/semantics are not reduced.
- Main-thread audio-only refill is serviced at drawing phase/batch boundaries
  and texture-row work, throttled to 20 ms. No new decoder thread, renderer
  reentry or guessed audio clock. It cannot prevent underruns during an
  arbitrarily long indivisible formula evaluation or stalled network.
- Static audit of the pinned SDK finds 106 assertion-only C functions. The
  linked build has no instruction/data references to those stubs. This is a
  bounded guard, not proof that every SDK function or indirect runtime path is
  fully implemented. Host frame tests now use the actual alignment adapter.

0.6.6 builds with O3/LTO; ten focused visual/math/SDK-audit checks pass (about
12 seconds, not a full preset-collection run). The shared flight test now asserts
the CaveScene alignment and the texture test verifies all 512x256 Morton texels.

Hardware acceptance remains: start the Monkey game, play/stop, run the same
MilkDrop preset at the same settings, and listen for interruptions. Measure
performance on-console; host command capture does not emulate NV2A.

## Implemented test build

The existing PSP engines now compile through an Xbox NV2A adapter, with no
changes to PSP sources. Included: Monkey materials/audio response/textures,
flight/combat/ships/scores/rumble; MilkDrop parser, formulas, waves/shapes,
textures, preset browser, automatic changes and live transitions. Options persist.

GPU effects render to bounded offscreen targets: Monkey 512x256, MilkDrop
512x256 or 512x512. SDL scales/composes the final 720x480 surface to the display.
This is not native 1080i effects rendering or zero-copy presentation. Projection
and shared geometry/formulas remain CPU work. Video PVIDEO/PTS is independent.
Desktop HLSL is not added; the shared engine's compatibility limits remain.

The O3/LTO XBE builds. Focused ASan/UBSan host checks cover GPU command recording,
resource lifecycle, shared Monkey frames/flight, presets and live transitions;
combat and scores also pass. These do not emulate NV2A: rendering, performance
and actual 64 MB headroom still require console tests. Try 480p/720p first,
then 1080i and music → video → music. PSP hardware paths remain unchanged.

Music X opens visualizations/options, Y toggles fullscreen. Monkey LT+RT opens
the flight intro; hold both five seconds to leave. Left stick steers, A fires,
D-pad up/down changes speed, LT/RT roll (double tap: barrel roll). Intro
left/right selects the ship. Rumble strengths default to zero.

Install `visual-font.raw`, `presets/` and `monkey/` beside the XBE/theme/font.
Preserve personal config, scores and textures. FATX filename limits apply.

## Historical assessment (before this implementation)

The resolution/codec test XBE is deliberately independent of this work.
GUI/visualization rendering uses SDL's software backend. Version 0.4.2 adds an
independent NV2A PVIDEO overlay for video conversion/scaling. That saves memory
compared with initializing a multi-backbuffer 3D renderer, but does not provide
hardware-accelerated 3D or feedback textures for Monkey/MilkDrop.

0.4.0 verification: native -O3/LTO build, eight focused host tests (both codecs,
all six sizes, exact PTS/final-picture drain, truncated transport, real 5.1
matrix downmix, bitmap-filter construction and legacy Xbox HTTP handling).
Hardware throughput/output switching and receiver decoding remain user tests.

## Monkey first: medium-to-large, not a small follow-up

Reuse `cave_visual.c`, `cave_paths.c`, scene/material/flight/combat mathematics,
model data, game state, collision, score and the existing audio analysis.
Do not replace the reconstructed behavior with a different tunnel.

`cave_gu.h`, `cave_game_gu.h`, `cave_combat_gu.h` and associated texture/HUD
code submit PSP GU commands and use PSP allocation/cache/state conventions.
An Xbox GPU adapter must replace those calls: triangles, depth, projection,
fog, texture addressing, blend modes, model/particle/HUD rendering. Add native
controller mapping and rumble, persistent settings/scores and texture loading.
The Easter Egg is reusable logic, not a free renderer port.

Recommended slices: GPU-backed drawing foundation and output lifecycle;
reference-faithful Monkey visualization; complete Easter Egg/UI/input/rumble.
Validate transitions between music/video and every output mode, not just a
standalone tunnel. Keep output size separate from optional internal render size.

## MilkDrop: larger

Reuse the existing preset parser, expression engine, audio analysis, geometry,
wave/shape rules and transition timing. Replace `milkdrop_gu.c` plus GU adapters
with Xbox render targets, feedback/warp passes, blending, texture filtering and
resource ownership. Dual-live-preset transitions need two complete state/render
paths. A 64 MB Xbox must also retain audio/network/UI headroom at HD output.

Port existing non-shader behavior first. This does not grant new HLSL shader
support or change the project's reference-behavior policy. No software 3D
shortcut is bundled into the stable audio player just to claim feature parity.

## Separate next item

Read dashboard language as the initial locale; expose an explicit override and
reuse PSP translation strings where appropriate, with Xbox controller labels.
