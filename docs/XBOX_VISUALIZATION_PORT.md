# Xbox visualization port — 0.6.0

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
