# Offscreen custom shapes

MilkDrop 2's `DrawCustomShapes` in `vis_milk2/milkdropfs.cpp` submits original
shape centers, radii and triangle fans to D3D. It does not pin centers or radii
to 0–1 before drawing. The PSP importer/evaluator previously did so, changing
both the shape and the inputs seen by its formulas.

## Implementation

Shape x/y/radius now accept −4..4 at import and after formula evaluation.
Negative radius is retained. These are explicit PSP numerical budgets, not
Desktop bounds or measured hardware maxima. Non-finite values remain errors;
other finite values outside this range use the existing silent limiting policy.
Angles, texture zoom, color handling and formula execution budgets are unchanged.

The renderer creates the original fan in expanded 512×256 feedback coordinates.
Fully interior shapes use the existing fan and line-strip primitives. Geometry
wholly outside the viewport bounding box (including outline offsets) is skipped.
Crossing shapes take a bounded clipping path:

- Each original fan triangle is clipped against four viewport planes. Resulting
  convex polygons are triangulated, preserving texture/color interpolation and
  drawing order. This avoids clamping individual vertices and distorting shapes.
- Only original perimeter segments are clipped for outlines. No outline is
  drawn along newly introduced viewport edges. Thick-outline offsets are applied
  before clipping, so each of the four passes remains inside the viewport.
- UV coordinates and RGBA channels are interpolated at intersections. Packed
  channels round to nearest byte per clipping intersection; this can differ by
  a few channel levels from Desktop floating-point rasterizer interpolation.
- All submitted arrays live in the synchronized GU buffer, never temporary
  stack storage. Local fan/clipping scratch is reused only after copying.

One triangle needs at most 15 output vertices; one shape needs at most 1,500
fill and 800 thick-outline vertices. A clipped shape consumes six ordinary
packet slots, so at most five worst-case clipped shapes fit in one packet.
The existing list finish/sync/restart/feedback-target restore protocol remains
unchanged. Clipped shapes can cost more CPU/GPU work and packet synchronizations.

## Verification and resources

Tests cover exact unchanged interior triangles, known clipped areas, interpolated
texture coordinates/colors, segment rejection, 1,000 randomized triangles, static
and formula offscreen coordinates, and maximum clipped shape density together
with all other layers. Original Geiss Explosion and both display/layout variants
are exercised through the host GU harness.

All 178 automated tests pass. The same 1,715-file collection retains 1,651
imports and 1,217 successful 120-frame formula runs, without regressions or new
passes. This step improves rendered geometry rather than parse acceptance.

The stress-test peak remains 1,206,368 vertex bytes in the existing 1.5 MiB list.
There is no extra persistent framebuffer or heap allocation. PSP GCC stack-use
reports `md_frame` at 5,880 bytes (previously about 3.4 KiB), triangle clipping
at 504 bytes and line clipping at 120 bytes. The deepest wave-evaluation path
still fits the existing 256 KiB thread stack under the earlier static accounting;
this is not a hardware stack-watermark measurement.

## PSP test

`offscreen-shapes-demo.milk` moves colored, outlined polygons across all four
edges and draws a larger textured feedback shape centered outside the left edge.
Look for clean cuts, smooth entry/exit, no stuck centers, no new straight border
along the viewport and no wrapping fragments. Test LCD/TV, window/fullscreen,
then an existing preset (including Explosion), music switch and video start.
Audio remains the priority: report new crackling or unresponsive controls.

Custom-wave clipping and zoom/exponent finite-coordinate handling remain separate
next steps. Feedback resolution, scheduler, audio and A/V timing are unchanged.
