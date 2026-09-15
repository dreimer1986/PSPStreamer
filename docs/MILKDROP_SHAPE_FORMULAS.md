# Custom-shape formula contexts

Each of the four existing shapes supports `shape_0_init1=...` and
`shape_0_per_frame1=...` (replace 0 with 0–3). Lines in each program must be
numbered consecutively from 1, using the original MilkDrop naming convention.
Static fields still use `shapecode_0_*`. A disabled static shape does not execute.

Each shape owns independent init/frame bytecode, 16 named variables and eight
`t` seeds. Init executes once on first activation. Shape init reads preset-init
`q` seeds; frame formulas receive the current preset frame's `q1`–`q32`.
Shape writes to `q` stay local and cannot change other shapes or the warp grid.
Before every frame, `t1`–`t8` restore the shape's init values. Named variables
persist between frames, while visual fields reset to their static defaults.
Changing/restarting the active preset clears its runtime state.

The lifetime model follows MilkDrop 2's `LoadCustomShapePerFrameEvallibVars`
and `DrawCustomShapes`, inspected at revision
`b5e4136c2f050eafa10aa199bb72c8e5c12c9320`. This is our bounded interpreter,
not a port of the complete desktop VM or its multiple-instance shape system.

## Inputs and outputs

Inputs: `time`, `frame`, `fps`, `bass/mid/treb`, their `*_att` forms and existing
`psp_*` music inputs. Time/music/frame inputs are read-only.

Outputs: `enabled`, `sides`, `additive`, `textured`, `x`, `y`, `rad`, `ang`,
`tex_ang`, `tex_zoom`, `r/g/b/a`, `r2/g2/b2/a2`, `border_r/g/b/a` and `thick`.
The border names are individually `border_r`, `border_g`, `border_b`, `border_a`.
`thick` is the formula equivalent of static `thickOutline`.

Existing strict bounds remain: sides 3–32 (integer), positions/radius/colors
0–1, angles -100–100, texture zoom 0.1–10, flags exactly 0 or 1. This differs
from desktop clamping/casting. Invalid values or arithmetic reject the entire
frame before committing outputs or runtime state. No half-updated shape is drawn.

Each init and frame program has at most 128 bytecode instructions and 16 lines;
the whole preset remains limited to 16 KiB. There are no loops, runtime heap
allocations or additional audio-worker operations. Four compiled contexts add
18,512 bytes to preset storage (and its temporary parsing copy); persistent
shape state adds 400 bytes. Texture size, GU-list size and video/audio clocks
are unchanged. Existing `textured` shapes reuse feedback; external images are
still unsupported.

## Hardware test

Copy EBOOT.PBP, PSPStreamer.prx and `presets/shape-orbits-demo.milk` from the
release folder. During music open the Circle preset browser and choose it.
An orange pentagon and blue triangle orbit opposite one another, rotate
independently and change size with low/high music activity. Check LCD and TV,
embedded and fullscreen; listen for crackling and try track/preset changes and
a return to video. No server update is required.

Host tests cover independent states, seed restoration, preset-q isolation,
disabled shapes, invalid output rollback and the real parser/interpreter/GU
path for 600 frames per display/layout combination. PSP performance still
requires the hardware test above.

Custom PCM wave programs have since been added; see
[custom waves](MILKDROP_CUSTOM_WAVES.md). Stereo spectrum and image effects are
also available in the [later batch](MILKDROP_SPECTRUM_EFFECTS.md).
Remaining major gaps include shape instances,
complete NS-EEL semantics, extra engine inputs and
automatic preset transitions/blending. Shaders and external textures remain
deferred.
