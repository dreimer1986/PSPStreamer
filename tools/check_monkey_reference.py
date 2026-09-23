"""Optional isolated reference fixtures; requires Unicorn, never loads Winamp.

Usage: python3 tools/check_monkey_reference.py /path/to/vis_monkey.dll
The proprietary input is not distributed. Only the audited image is accepted.
The emulator has no OS/network hooks and executes a bounded instruction count.
"""
import hashlib
from pathlib import Path
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESI, UC_X86_REG_EBX, UC_X86_REG_ESP, UC_X86_REG_FPCW


def machine(data):
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    # This audited PE uses matching raw/RVA layout. Include its CRT BSS.
    u.mem_map(0x10000000, 0x1000000)
    u.mem_write(0x10000000, data)
    u.mem_map(0x20000000, 0x1000000)
    u.mem_map(0x30000000, 0x10000)
    u.mem_map(0x40000000, 0x1000)
    u.reg_write(UC_X86_REG_FPCW, 0x37f)
    return u


def main(path):
    data = Path(path).read_bytes()
    expected = 'c7dcfc838d0979870947d8e5b3f1fd5ad5a9e9c8e92fa77ba8de23c842fc614d'
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError('Only the audited Monkey DLL is supported')
    u = machine(data)
    # Avoid triggering the original's initial-frame reset on EVERY fixture.
    u.mem_write(0x20014a7c, struct.pack('<I', 0xffffffff))
    for seed, t in [(0, 0), (313.45, 1), (313.45, 2), (313.45, 3), (313.45, 100)]:
        u.reg_write(UC_X86_REG_ESI, 0x20000000)
        u.reg_write(UC_X86_REG_EBX, 0x20000000)
        u.reg_write(UC_X86_REG_ESP, 0x30008000)
        u.mem_write(0x20012a58, struct.pack('<f', seed))
        for offset in (0x14, 0x2c):
            u.mem_write(0x30008000+offset, struct.pack('<f', t))
        u.emu_start(0x10005d87, 0x1000617f, count=100000)
        print('bend', seed, t, struct.unpack('<6f', u.mem_read(0x200df734, 24)))


if __name__ == '__main__':
    main(sys.argv[1])
