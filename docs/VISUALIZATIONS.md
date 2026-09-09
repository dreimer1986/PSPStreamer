# Future music visualizations — reference backlog

Saved at the user's request on 2026-09-08. The first opt-in
[MilkDrop warp prototype](MILKDROP_PROTOTYPE.md) now adapts a small part of
MilkDrop 2's no-shader equations. No complete engine or third-party presets
are bundled.

Update: the user confirmed crackle-free LCD and TV music with `1852d20`.
The preparation/cleanup and ownership boundaries are documented in
[MUSIC_RENDERING.md](MUSIC_RENDERING.md). No MilkDrop code has been integrated
by that cleanup.

## MilkDrop 2

MilkDrop 1.04b is now an additional inspected reference for fixed-function
rendering: [source audit and implementation candidates](MILKDROP1_REFERENCE.md).
It still uses Direct3D 8, and its x86 evaluator is not directly portable to PSP.

- Source mirror: https://github.com/eef2697d62fbe08e2fd927278/milkdrop2
- Ryan Geiss's 2.25c source archive: https://www.geisswerks.com/milkdrop/milkdrop_225c_src.zip

These are intentionally retained separately from MilkDrop 3: investigate
older, shader-free rendering paths, audio analysis, waveform/shape evaluation,
feedback meshes, and a possible deliberately limited preset subset.

Do not assume that being a Winamp plug-in means DirectX was optional. The
mirror's `vis_milk2/plugin.h` uses Direct3D 9 texture and pixel/vertex shader
types. Its `MD2_PS_NONE` enum is a lead for investigation, not proof that an
arbitrary MilkDrop 2 preset can run on PSP hardware. The archive link responds
as a ZIP; it has not yet been unpacked or audited.

## MilkDrop 3

- https://github.com/milkdrop2077/MilkDrop3

Its Windows/Direct3D renderer and shader-dependent presets are not a drop-in
PSP implementation. The repository's Linux instructions currently use Wine.

## Constraints for any future implementation

- Keep audio playback/PCM ownership independent of the visualization renderer.
- Use bounded copies of the existing PCM analysis data; never block the DAC.
- Document supported preset features explicitly rather than claiming full
  MilkDrop compatibility. Check licenses of each imported source/preset first.
- Prioritize native PSP-friendly waveforms, shapes and texture feedback.
- Server-rendered visualizations would be a separate, optional video-stream
  feature with an explicit server/GPU cost, not a hidden HA add-on dependency.
