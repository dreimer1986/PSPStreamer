# Thick custom-shape outlines

`shapecode_0_thickOutline=1` through `shapecode_3_thickOutline=1` now work.
Zero remains the default. Fractional, nonfinite and out-of-range flags are
rejected rather than silently changing behavior.

Reference: MilkDrop 2 `DrawCustomShapes` in `milkdropfs.cpp`, fixed-function
border rendering, and the equivalent MilkDrop 1.04b routine. Four line strips
are drawn at adjacent texel positions. D3D's upward-positive Y maps to negative
Y offsets in our downward-positive GU coordinates. Offsets are applied after
feedback-coordinate expansion, so horizontal and vertical steps both remain
one texel in the 512x256 feedback texture, on LCD and TV alike.

Each pass has immutable list-owned vertices: unlike desktop DrawPrimitiveUP,
PSP draw submission does not permit immediately rewriting a shared array.
The complete worst-case layer combination uses 55,952 of the fixed 65,536-byte
GU list for vertex allocations, leaving 9,584 bytes for commands/alignment.
No extra texture, framebuffer, PCM snapshot, audio work or display-mode switch
is introduced. Four thick 32-sided shapes add twelve draw calls and 9,472
allocated vertex bytes compared with their thin outlines. Disabled shapes and
transparent borders do not allocate outline geometry.

## Hardware check

Keep a backup of your current `presets/active.milk`. Copy `outline-demo.milk`
to `active.milk`, restart music and select the custom preset with Square.
The left cyan hexagon has a thin border; the right has a thick border. Check
receiver view/fullscreen on LCD and TV, music continuity, track changes and
switching back to video. Desktop tests validate geometry, closed polygons,
offset direction, parsing and worst-case list ownership; only the PSP can
validate rasterization and sustained playback performance.

Independent shape/wave formula contexts, more built-in wave modes, motion
vectors, external textures and transitions remain future work. This change
does not claim complete MilkDrop 2 support.
