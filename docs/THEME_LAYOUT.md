# Measured theme screens

The authoritative coordinates live in `psp-client/theme_layout.h`.
The user's measurements include the lower-right pixel; C drawing rectangles
use exclusive right/bottom edges.

| Screen | Inclusive corners | Size |
| --- | --- | --- |
| LCD left | 31,27 → 350,154 | 320 × 128 |
| LCD right | 372,27 → 449,156 | 78 × 130 |
| TV left | 25,59 → 534,292 | 510 × 234 |
| TV right | 557,59 → 692,293 | 136 × 235 |

Windowed MilkDrop and Monkey and the menu artwork use these left-screen
boundaries. Spectrum bars are distributed within the same screen, below the
music metadata. Full and incremental spectrum draws share the geometry.
Framebuffer pitch, internal visualization textures, refresh intervals, audio,
networking, fullscreen modes and receiver controls have not changed.

Themed text keeps its internal margins. LCD glyphs advance seven pixels;
the usual x=38 left origin permits 44 cells, and x=376 right origin permits
10 cells. Width is measured after UTF-8 decoding. Long LCD rows end in `...`;
the stored filenames/settings remain unchanged. TV text is proportional and
word-wrapped, with both available width and row count limited by screen edges.
It therefore has no universal character-count limit.

English/German fixed LCD sidebar messages are checked against ten cells.
Examples: `AUDIO/SUB`, `TON & SUB`, `WAIT...`, `WARTEN...`; saved preferences use
three short lines. Counts fit up to four decimal digits. Dynamic content is
bounded independently of translation, so server strings cannot paint on the
frame. Cover heights and the final TV options selection highlight are bounded
too. LCD settings and preset-browser rows fit inside the left glass.

Fullscreen help disables themed text bounds. The separate heading, status/help
strip, VU meters, volume knob and indicator lights retain their layouts.

## Focused checks

- Actual LCD/TV full versus incremental music renderers agree.
- Actual glyph renderers keep long UTF-8 text within both screens and reject
  rows that cannot fit vertically. Fullscreen help still renders normally.
- Measured coordinates and compact English/German sidebar strings are checked.

Hardware acceptance: browse files/favorites/local storage and settings on LCD
and TV; check long names, loading/options/info sidebars and covers; switch
Spectrum → MilkDrop → Monkey and fullscreen/windowed modes.
