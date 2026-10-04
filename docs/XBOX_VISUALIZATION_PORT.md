# Xbox visualization port assessment (after 0.4.0)

The resolution/codec test XBE is deliberately independent of this work.
Current Xbox rendering uses SDL's software backend. Scaling a rendered image
to 720p or 1080i does not provide hardware-accelerated 3D or feedback textures.

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
