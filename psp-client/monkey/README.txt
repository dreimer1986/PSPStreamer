Optional user-supplied Monkey textures
====================================
Copy this directory beside EBOOT.PBP (PSP/GAME/PSPStreamer/monkey/).

Supported names (same banks as the original Winamp plugin):
  supertex_a1.jpg
  supertex_a2.jpg
  supertex_a3.jpg
  supertex_a4.jpg
  supertex_a5.jpg
  supertex_b1.jpg
  supertex_b2.jpg

PNG and .jpeg are accepted too; search order: .jpg, .png, .jpeg.
JPEG: up to 1024x1024, automatically resized to powers of two, max 256x256.
PNG: power-of-two dimensions between 16 and 256. Each file: max 1 MiB.
Original 512x512 JPEG files need no manual conversion.

Use only images you are entitled to use. Original Monkey images are not bundled.
Missing, invalid, oversized or unallocatable images use generated replacements
per slot. Partial sets work. In diagnostics, an external texture mask of 7F
means all seven loaded. Re-enter Cave (or its options and back) to reload files.

Images decode one per visual update during startup, before GU list creation;
there is no recurring filesystem access after all seven slots were attempted.
The cache is released on leaving the renderer. At most 1.75 MiB is retained
for seven 256x256 RGBA images; audio and video buffers are unchanged.
