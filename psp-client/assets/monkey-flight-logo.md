# Monkey flight intro logo

`monkey-flight-logo.png` was generated with the built-in image-generation tool
on 2026-09-27, using the [original Monkey logo](https://www.geisswerks.com/monkey/monkey_logo.jpg)
as the user-requested visual reference. It is a homage, not an official Monkey release.
The original wordmark/design is credited to the Monkey project / Ryan Geiss;
this document does not claim ownership of the reference or relicense it.

The runtime RGB565 atlas `monkey-flight-logo.raw` is embedded in the EBOOT.
Regenerate it with `python3 tools/pack_flight_logo.py` (Pillow required).
It uses 256x128 pixels, with the visible 3:1 logo in the first 85 rows.

## Generation prompt

Use case: logo-brand. Asset type: wide title logo for the Monkey flight Easter
egg inside PSPStreamer, rendered small on a PSP. Image 1 is the original Monkey
logo and is the visual reference. Create a clean faithful homage to its exact
lime-green quirky organic lowercase wordmark "monkey", black offset shadow,
and green/orange tunnel atmosphere. Keep the word "monkey" large and fully
legible; no extra words, no buttons, no UI, no added characters. Wide horizontal
logo lockup, approximately 3:1, tight usable framing with all letters contained.
The tunnel should be a restrained dark backdrop with edges fading fully to
black, so this can sit above a Hall of Fame in a black menu. Preserve the playful
irregular letter shapes and lime green identity of the reference. Designed to
remain readable at 256x85 pixels.

The shield HUD is drawn in code, inspired by the user-provided
[Star Fox SNES screenshot](https://retrocookie.com/wp-content/uploads/2014/09/retroarch-0614-011850.png).
No HUD image from that game is included.
