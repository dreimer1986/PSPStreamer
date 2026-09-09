# Target preset: Geiss - Hyperdrive

User-selected compatibility target, inspected from
`/home/dreimer/Downloads/Geiss - Hyperdrive.milk`.
SHA-256: `3ae3786e91eb531ab252257a0ceebc13f86a7392d5fa1bd5c389f765c8e48507`.
63 lines; the original remains untouched and is not redistributed here.

This specific file has seven per-frame equations, no per-pixel equations,
no custom waves/shapes and no programmable warp/composite shader blocks.
It is a suitable fixed-function milestone, not a reason to abandon broader
MilkDrop 2 feature support. Its required subset is now implemented and host
tested with the exact file. Hardware and visual comparison remain outstanding.

## Original requirements and gaps (before implementation)

This original checklist is retained for comparison; see the implementation
status below for the new test build.

| Requirement | Current status / required work |
| --- | --- |
| File begins with `presetName`, no `[preset00]` | Add an explicit legacy/headerless dialect; do not silently ignore arbitrary fields |
| Time/sine/color equations, bass-driven zoom | Arithmetic and relative `bass` exist; measurement remains PSP-derived |
| `dx`/`dy` animate texture translation | Extend preset state, equation outputs and warp transform in original order/sign |
| `nWaveMode=0`, scale 1, smoothing 0.75, alpha 1 | Implement the real circular waveform from bounded PCM snapshots; our spectrum ring is not equivalent |
| RGB equations start at 1 and can exceed 1 | Original `DrawWave` clamps at rendering; current preset runtime rejects these values |
| `zoom=1.03+0.05*bass` | Exceeds current 1.2 cap when bass >3.4; compatibility needs appropriate safe transform handling, not rewriting the preset |
| Gamma adjustment 2 | Implement/validate fixed-function output brightness handling against reference |
| `bTexWrap=0` | Current feedback uses repeat; add the original clamp/wrap choice |
| Center .5/.5, stretch 1, zoom exponent 1 | Currently fixed to the needed values; recognize fields explicitly |
| Echo alpha 0, border alphas 0, effect flags 0 | Disabled in this file, but parser must distinguish accepted disabled features from unsupported enabled ones |

## Reference route

MilkDrop 1.04b `milkdropfs.cpp`, `DrawWave` at line 2009 and circular mode
near line 2119, provide the waveform behavior. The radius uses actual right
channel samples, with a smoothed closing region and time-dependent angle.
Our twelve frequency-band ring cannot reproduce that geometry.
RGB clamping is visible before the waveform-mode switch.
`WarpedBlitFromVS0ToVS1` supplies the fixed-function transform reference.
Cross-check the corresponding MilkDrop 2 no-shader behavior before porting.

Prioritize waveform snapshots/mode 0 and full transform handling for this
target, then file dialect and rendering-semantic compatibility. Custom shapes
remain a useful general feature but are not a prerequisite for Hyperdrive.

## Acceptance

- Load this exact user file, without deleting unsupported-looking lines or
  replacing its waveform with our native ring.
- Compare a captured reference using the same music: geometry, texture
  displacement, brightness, edge behavior and reaction to audio impulses.
- Test LCD/TV, fullscreen/receiver, pause, restart and music/video changes.
- Report remaining approximations (particularly spectrum analysis) explicitly.
- Preserve audio ownership and stable playback; no full compatibility claim
  based solely on successful parsing.

## Implemented test build

- Explicit headerless `presetName` dialect and the target's static fields.
  Unsupported enabled effects still fail; they are not silently discarded.
- Writable `dx`/`dy` subtracted from UV after rotation, as in the reference.
- Mode 0 uses 576 right-channel PCM samples, spatial smoothing, 240 circular
  points plus a closing vertex and a 24-point seam crossfade. Trigonometric
  tables are precomputed instead of evaluating sin/cos per point every frame.
- Sequence-checked snapshots never wait or transfer decoder-buffer ownership.
  Capture runs only when the circular-wave effect is selected, at the existing
  decoder analysis point, not at the DAC. Queue depth can affect visual latency.
- Alpha blending for the wave; additive final-display passes for gamma
  brightness (two for this preset). Brightness does not feed back.
- Clamp/repeat selection; legacy RGB clamping at rendering; positive runtime
  zoom from 0.1 to 64. Strict native PSP presets retain their previous checks.
- Circle aspect follows the active viewport. Feedback is still 256 square
  with an 8x8 mesh, not a pixel-identical desktop renderer.

Run the optional exact-file test with:

```sh
HYPERDRIVE_PRESET='/path/to/Geiss - Hyperdrive.milk' python3 -m unittest discover -s tests -q
```

No third-party preset is added to Git. Copy the original as `presets/active.milk`,
restart music and select slot four with Square. Downloads remains untouched.
