# Focused renderer and texture-loader cleanup

This pass deliberately leaves playback, timestamp synchronization, subtitle
paging, overclocking, render resolution and preset appearance unchanged.

* Unit zoom skips the nested zoom-exponent powers. On the current 17×17 grid
  this avoids 578 `powf` calls per frame when every vertex has unit zoom and a
  non-unit exponent. Non-unit zoom and per-pixel EEL execution are unchanged.
  This is a conditional saving, not a general framerate improvement claim.
* JPEG column sampling is computed once per output column instead of once per
  pixel. A 256×256 texture needs 256 column divisions instead of 65,536. The
  mapping remains the same integer nearest-neighbour mapping; the additional
  temporary table uses 1 KiB on the texture loader's stack, not persistent RAM.
* Compressed image data and PNG decoder resources are released before the
  swizzled texture allocation. The loader no longer retains the compressed file
  (up to 1 MiB) alongside both RGBA buffers. The earlier decode peak can still
  dominate overall peak memory; this is not a promise of 1 MiB less total usage.
* Corrected an obsolete comment claiming that no texture resizing takes place:
  JPEG resizing is supported, whereas PNG still requires native dimensions.

Focused host tests count the power calls and compare unit-zoom vertices exactly,
check PNG/JPEG bounds, alpha, corruption and cleanup, and verify a patterned
non-power-of-two JPEG's sampling after GU swizzling. The PSP release is rebuilt.
No full preset collection or unrelated server/OC tests are needed for this pass.

Higher feedback resolution and visual approximation improvements remain separate
tasks. In particular, this pass does not lift formula-memory or CPU-fuel limits.
