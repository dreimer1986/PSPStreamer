"""Check this linked build for references to nxdk's assertion-only C stubs.

This is a static guard, not a claim to prove all runtime paths. Covers direct
calls/tail calls, instruction address operands and stored function pointers.
Unused functions can survive linking inside a mixed libc object, so the mere
presence of a stub symbol is not a failure.
"""
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def audit(root=ROOT):
    sdk = root/'.toolchain/nxdk/lib'
    stubs = {}
    definition = re.compile(r'^\w[^;{}]*?\b(\w+)\s*\([^;{}]*\)\s*\{\s*assert\s*\(\s*(?:0|false)\s*\)', re.M)
    for folder in ('pdclib/platform/xbox/functions', 'xboxrt'):
        for path in (sdk/folder).rglob('*.c'):
            for match in definition.finditer(path.read_text()):
                stubs['_'+match[1]] = str(path.relative_to(sdk))
    symbols = {}
    for line in (root/'xbox-client/bin/player.map').read_text().splitlines():
        match = re.match(r'\s+\w+:\w+\s+(\S+)\s+([0-9a-fA-F]{16})\s+', line)
        if match and match[1] in stubs:
            symbols[int(match[2], 16)] = match[1]
    exe = root/'xbox-client/main.exe'
    assembly = subprocess.check_output(['objdump', '-d', str(exe)], text=True)
    references = []
    for line in assembly.splitlines():
        for match in re.finditer(r'0x([0-9a-fA-F]+)', line):
            target = int(match[1], 16)
            if target in symbols:
                references.append(f'{symbols[target]}: {line.strip()}')
    # PE data sections may retain pointers used by indirect calls.
    data = exe.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    sections, opt_size = struct.unpack_from('<H', data, pe+6)[0], struct.unpack_from('<H', data, pe+20)[0]
    for i in range(sections):
        header = pe+24+opt_size+40*i
        size, start = struct.unpack_from('<II', data, header+16)
        flags = struct.unpack_from('<I', data, header+36)[0]
        if flags & 0x20000000:  # executable section already disassembled
            continue
        for offset in range(start, start+size-3, 4):
            value = struct.unpack_from('<I', data, offset)[0]
            if value in symbols:
                references.append(f'{symbols[value]}: stored pointer at file offset {offset:#x}')
    return len(stubs), len(symbols), references


if __name__ == '__main__':
    found, linked, references = audit()
    print(f'SDK assertion-only C functions: {found}; linked unused candidates: {linked}; references: {len(references)}')
    for reference in references:
        print(reference)
    raise SystemExit(bool(references))
