# Bounded per-grid-point formulas

`per_pixel_1` through `per_pixel_16` now compile into a separate program,
limited to 64 instructions total. It executes at the 81 points of the existing
8×8 mesh: at most 5,184 bytecode instructions per rendered frame, no loops.
The GPU interpolates the resulting texture coordinates. This is not shader
execution on every output pixel. Feedback resolution remains 512×256.

Writable: `zoom`, `zoomexp`, `rot`, `warp`, `cx/cy`, `dx/dy`, `sx/sy`.
Read-only: frame transform values, decay, time/music inputs, final frame q1–q32,
and `x`, `y`, `rad`, `ang`. Each point starts from the same evaluated frame
transform; one point's writes cannot accumulate into the next point.
Named per-frame user variables are not shared into this context: export values
through q variables. Pixel-local persistent variables and q writes are not
supported in this bounded first implementation.

Coordinates: x/y run 0–1, with origin top-left. Radius uses logical square
coordinates (center zero, corners sqrt(2)); angle uses y-up atan2 in radians
and is zero at the exact center. This follows our existing square warp math,
not the desktop renderer's viewport-aspect-adjusted radius convention.

```ini
per_frame_1=q1=.04+.04*psp_low;
per_pixel_1=rot=rot+q1*sin(ang+time*.5)*min(1,rad);
```

All point formulas/ranges are validated before opening the GU list. Failure
preserves the previous grid and persistent frame state and uses the existing
effect-error UI. Ranges: zoom .1–64, rotation ±.2, warp ±4, translation ±1,
center 0–1, stretch .25–4, zoom exponent .5–2. No audio-worker changes.
The adapter adds 4,212 bytes for 81 prepared transforms; init/frame q export
adds 128 bytes of state, and the parsed preset holds another 2,056-byte program.
No textures, GU vertex count, presentation passes or display modes are added.

Reference: MilkDrop 2 `milkdropfs.cpp`, per-vertex variable restoration and
execution in the no-shader warp path, revision
`b5e4136c2f050eafa10aa199bb72c8e5c12c9320`. The implementation reuses our
existing adapted warp equations after evaluating each local transform.

`grid-twist-demo.milk` combines angular rotation and position-dependent drift.
Copy it as `presets/active.milk`, restart music and select the custom effect.
CPU tests check coordinates, frame-q export, reset rules, invalid writes,
instruction limits and atomic grid failure; the GU harness runs the demo on
LCD/TV in receiver and fullscreen. Hardware performance still needs a test.

Remaining: independent shape/wave programs and more wave modes, pixel-local
state, external textures, preset browsing/transitions, shader alternatives.
