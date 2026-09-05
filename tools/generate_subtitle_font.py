#!/usr/bin/env python3
"""Generate the compact Latin-1 subtitle atlas used by the PSP client."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

CELL_WIDTH = 16
CELL_HEIGHT = 20
ATLAS_WIDTH = CELL_WIDTH * 16
ATLAS_HEIGHT = CELL_HEIGHT * 16
FONT = "/usr/share/fonts/TTF/DejaVuSans.ttf"
OUTPUT = Path(__file__).parents[1] / "psp-client" / "assets" / "subtitle_font.raw"


def main() -> None:
    atlas = Image.new("L", (ATLAS_WIDTH, ATLAS_HEIGHT), 0)
    draw = ImageDraw.Draw(atlas)
    font = ImageFont.truetype(FONT, 17, layout_engine=ImageFont.Layout.BASIC)
    for code in range(32, 256):
        character = bytes([code]).decode("latin-1")
        if not character.isprintable():
            continue
        left = (code & 15) * CELL_WIDTH
        top = (code >> 4) * CELL_HEIGHT
        bounds = draw.textbbox((0, 0), character, font=font)
        width = bounds[2] - bounds[0]
        draw.text(((left + (CELL_WIDTH - width) // 2), top - 2), character, fill=255, font=font)
    OUTPUT.write_bytes(atlas.tobytes())
    print(f"Wrote {OUTPUT} ({OUTPUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
