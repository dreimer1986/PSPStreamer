#!/usr/bin/env python3
"""Reproducible technical PNG test pattern; no image libraries required."""
import binascii
import struct
import zlib
from pathlib import Path


def png_bytes(width=128, height=128):
    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data +
                struct.pack('>I', binascii.crc32(kind + data) & 0xffffffff))
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            color = (30, 200, 255) if (x // 16 + y // 16) % 2 else (255, 180, 30)
            alpha = 255 if (x-width/2)**2 + (y-height/2)**2 < (width*.44)**2 else 0
            rows.extend((*color, alpha))
    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    target = Path(__file__).resolve().parents[1] / 'psp-client/presets/textures/checker.png'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(png_bytes())
