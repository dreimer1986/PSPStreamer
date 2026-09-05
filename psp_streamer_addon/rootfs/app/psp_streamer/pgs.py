"""Small, dependency-free PGS decoder for PSP subtitle sprite preparation."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass
class PgsCue:
    start: float
    end: float
    x: int
    y: int
    width: int
    height: int
    pixels: bytes
    palette: bytes
    canvas_width: int
    canvas_height: int


def _u16(data: bytes, offset: int) -> int:
    return (data[offset] << 8) | data[offset + 1]


def _ycbcr(y: int, cb: int, cr: int, alpha: int) -> bytes:
    red = max(0, min(255, round(y + 1.402 * (cr - 128))))
    green = max(0, min(255, round(y - .344136 * (cb - 128) - .714136 * (cr - 128))))
    blue = max(0, min(255, round(y + 1.772 * (cb - 128))))
    return bytes((red, green, blue, alpha))


def _decode_rle(data: bytes, width: int, height: int) -> bytes:
    output = bytearray(width * height)
    x = y = position = 0
    while position < len(data) and y < height:
        color = data[position]
        position += 1
        if color:
            run = 1
        else:
            if position >= len(data):
                break
            code = data[position]
            position += 1
            if code == 0:
                x = 0
                y += 1
                continue
            run = code & 0x3F
            if code & 0x40:
                if position >= len(data):
                    break
                run = (run << 8) | data[position]
                position += 1
            if code & 0x80:
                if position >= len(data):
                    break
                color = data[position]
                position += 1
            else:
                color = 0
        run = min(run, width - x)
        output[y * width + x:y * width + x + run] = bytes((color,)) * run
        x += run
    return bytes(output)


def parse_pgs(data: bytes) -> list[PgsCue]:
    palettes: dict[int, bytearray] = {}
    objects: dict[int, tuple[int, int, bytes]] = {}
    fragments: dict[int, bytearray] = {}
    active: tuple[float, list[tuple[int, int, int]], int, int, int] | None = None
    cues: list[PgsCue] = []
    offset = 0
    while offset + 13 <= len(data):
        if data[offset:offset + 2] != b"PG":
            offset += 1
            continue
        pts = int.from_bytes(data[offset + 2:offset + 6], "big") / 90000.0
        kind = data[offset + 10]
        length = _u16(data, offset + 11)
        payload = data[offset + 13:offset + 13 + length]
        offset += 13 + length
        if len(payload) != length:
            break
        if kind == 0x14 and len(payload) >= 2:
            palette = palettes.setdefault(payload[0], bytearray(256 * 4))
            for index in range(2, len(payload) - 4, 5):
                entry, y, cr, cb, alpha = payload[index:index + 5]
                palette[entry * 4:entry * 4 + 4] = _ycbcr(y, cb, cr, alpha)
        elif kind == 0x15 and len(payload) >= 4:
            object_id = _u16(payload, 0)
            flags = payload[3]
            cursor = 4
            if flags & 0x80 and len(payload) >= 11:
                cursor = 11
                width, height = _u16(payload, 7), _u16(payload, 9)
                fragments[object_id] = bytearray(payload[cursor:])
                objects[object_id] = (width, height, b"")
            elif object_id in fragments:
                fragments[object_id].extend(payload[cursor:])
            if flags & 0x40 and object_id in objects:
                width, height, _ = objects[object_id]
                objects[object_id] = (width, height, bytes(fragments.pop(object_id, b"")))
        elif kind == 0x16 and len(payload) >= 11:
            if active:
                start, refs, palette_id, canvas_width, canvas_height = active
                for object_id, x, y in refs:
                    if object_id in objects and palette_id in palettes:
                        width, height, rle = objects[object_id]
                        if rle:
                            cues.append(PgsCue(start, pts, x, y, width, height, _decode_rle(rle, width, height), bytes(palettes[palette_id]), canvas_width, canvas_height))
            canvas_width, canvas_height = _u16(payload, 0), _u16(payload, 2)
            palette_id = payload[9]
            count = payload[10]
            refs = []
            cursor = 11
            for _ in range(count):
                if cursor + 8 > len(payload):
                    break
                object_id, flags = _u16(payload, cursor), payload[cursor + 3]
                x, y = _u16(payload, cursor + 4), _u16(payload, cursor + 6)
                refs.append((object_id, x, y))
                cursor += 8 + (8 if flags & 0x80 else 0)
            active = (pts, refs, palette_id, canvas_width, canvas_height)
    if active:
        start, refs, palette_id, canvas_width, canvas_height = active
        for object_id, x, y in refs:
            if object_id in objects and palette_id in palettes:
                width, height, rle = objects[object_id]
                if rle:
                    cues.append(PgsCue(start, start + 8, x, y, width, height, _decode_rle(rle, width, height), bytes(palettes[palette_id]), canvas_width, canvas_height))
    return cues
