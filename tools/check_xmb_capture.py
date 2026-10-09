#!/usr/bin/env python3
"""Validate a private Consolizer XMB capture before firmware analysis (no writes)."""
import argparse
from pathlib import Path
import re


def validate(directory):
    text = (directory / 'capture.txt').read_text()
    if not text.startswith('PSPConsolizer XMB probe v1 READ ONLY\n'):
        raise ValueError('Unknown capture format')
    if 'finish=complete completed_mask=7\n' not in text:
        raise ValueError('Incomplete capture; keep capture.txt to diagnose missing modules/errors')
    expected = {}
    for line in text.splitlines():
        if line.startswith('module='):
            fields = dict(item.split('=', 1) for item in line.split())
            name = fields['module']
            if name not in ('scePaf_Module', 'game_plugin_module', 'vsh_module') or name in expected:
                raise ValueError('Unexpected or duplicate module')
            count = int(fields['segments'])
            if not 1 <= count <= 4:
                raise ValueError('Invalid segment count')
            expected[name] = count
    segments = {}
    for match in re.finditer(r'^segment=(\w+)-(\d)\.bin address=([0-9A-F]{8}) size=(\d+) fnv1a=([0-9A-F]{8}) complete=1$', text, re.M):
        name, index, address, size, checksum = match.groups()
        key = name, int(index)
        if name not in expected or not 0 <= key[1] < expected[name] or key in segments:
            raise ValueError('Unexpected or duplicate segment')
        size = int(size)
        if not 0 < size <= 8 * 1024 * 1024:
            raise ValueError('Invalid segment size')
        path = directory / f'{name}-{index}.bin'
        if path.stat().st_size != size:
            raise ValueError(f'Wrong size: {path.name}')
        data = path.read_bytes()
        value = 2166136261
        for byte in data:
            value = ((value ^ byte) * 16777619) & 0xffffffff
        if value != int(checksum, 16):
            raise ValueError(f'Checksum mismatch: {path.name}')
        segments[key] = (int(address, 16), size)
    if len(expected) != 3 or len(segments) != sum(expected.values()):
        raise ValueError('Missing module/segment records')
    return segments


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        for (name, index), (address, size) in validate(args.directory).items():
            print(f'{name}-{index}.bin: {address:08X}, {size} bytes, checksum OK')
    except (OSError, ValueError, KeyError) as exc:
        parser.exit(1, f'Capture not ready: {exc}\n')
