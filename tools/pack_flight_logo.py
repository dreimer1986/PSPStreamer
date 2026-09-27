"""Pack the generated flight logo into a 256x128 little-endian RGB565 atlas."""
from pathlib import Path
import struct
from PIL import Image

root = Path(__file__).resolve().parents[1]
source = root / "psp-client/assets/monkey-flight-logo.png"
with Image.open(source) as original:
    picture = original.convert("RGB")
    picture.thumbnail((256, 85), Image.Resampling.LANCZOS)
    atlas = Image.new("RGB", (256, 128))
    atlas.paste(picture, ((256-picture.width)//2, 0))
    data = b"".join(struct.pack("<H", (r>>3) | ((g>>2)<<5) | ((b>>3)<<11))
                    for r, g, b in atlas.getdata())
(source.parent / "monkey-flight-logo.raw").write_bytes(data)
