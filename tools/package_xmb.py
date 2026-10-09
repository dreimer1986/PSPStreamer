#!/usr/bin/env python3
"""Repackage XMB assets without altering any executable or PARAM.SFO bytes.

Requires an explicit output different from the input; no in-place writes.
Release mode requires SND0.AT3. --silent-preview is for visual checks only.
"""
import argparse
import hashlib
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'psp-client/assets'


def read_pbp(path):
    data = path.read_bytes()
    if len(data) < 40 or data[:4] != b'\0PBP':
        raise ValueError('Not a PBP')
    offsets = list(struct.unpack_from('<8I', data, 8)) + [len(data)]
    if offsets[0] != 40 or offsets != sorted(offsets) or offsets[-2] > len(data):
        raise ValueError('Invalid PBP section bounds')
    return data[4:8], [data[a:b] for a, b in zip(offsets, offsets[1:])]


def package(source, dest, assets=ASSETS, silent=False):
    if source.resolve() == dest.resolve():
        raise ValueError('Use a separate output; preserve the original build')
    version, entries = read_pbp(source)
    unchanged = {i: entries[i] for i in (0, 3, 6, 7)}
    for slot, name, size in ((1, 'icon0.png', (144, 80)), (4, 'pic1.png', (480, 272))):
        data = (assets/name).read_bytes()
        if data[:8] != b'\x89PNG\r\n\x1a\n' or len(data) < 24 or struct.unpack_from('>II', data, 16) != size:
            raise ValueError('Wrong PNG dimensions: '+name)
        entries[slot] = data
    pmf = (assets/'icon1.pmf').read_bytes()
    if len(pmf) < 2048 or pmf[:8] != b'PSMF0014' or struct.unpack_from('>II', pmf, 8) != (2048, len(pmf)-2048):
        raise ValueError('Invalid PSMF header or length')
    entries[2] = pmf
    if silent:
        entries[5] = b''
    else:
        audio = (assets/'snd0.at3').read_bytes()
        if len(audio) < 12 or audio[:4] != b'RIFF' or audio[8:12] != b'WAVE' or struct.unpack_from('<I', audio, 4)[0]+8 != len(audio):
            raise ValueError('Invalid ATRAC RIFF container')
        entries[5] = audio
    if len(entries[2])+len(entries[5]) > 500*1024:
        raise ValueError('Combined XMB video/audio exceeds 500 KiB budget')
    offsets = []; pos = 40
    for entry in entries:
        offsets.append(pos); pos += len(entry)
    blob = b'\0PBP'+version+struct.pack('<8I', *offsets)+b''.join(entries)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open('xb') as out:
        out.write(blob)
    out_version, check = read_pbp(dest)
    assert version == out_version and all(check[i] == content for i, content in unchanged.items())
    print('Executable/SFO unchanged; SHA256:', hashlib.sha256(blob).hexdigest())
    print('XMB media bytes:', len(entries[2])+len(entries[5]))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--silent-preview', action='store_true')
    args = ap.parse_args()
    package(args.source, args.output, silent=args.silent_preview)


if __name__ == '__main__':
    main()
