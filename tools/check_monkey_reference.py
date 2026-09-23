"""Optional isolated reference fixtures; requires Unicorn, never loads Winamp.

Usage: python3 tools/check_monkey_reference.py /path/to/vis_monkey.dll
The proprietary input is not distributed. Only the audited image is accepted.
The emulator has no OS/network hooks and executes a bounded instruction count.
"""
import hashlib
from pathlib import Path
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESI, UC_X86_REG_EBX, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_FPCW


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
    paths=machine(data)
    def tls(u,address,size,user):
        # Supply only the CRT thread-local RNG storage. No operating-system
        # code or imports are executed by the reference fixture.
        esp=u.reg_read(UC_X86_REG_ESP)
        ret=struct.unpack('<I',u.mem_read(esp,4))[0]
        u.reg_write(UC_X86_REG_EAX,0x20f00000)
        u.reg_write(UC_X86_REG_ESP,esp+4)
        u.reg_write(UC_X86_REG_EIP,ret)
    paths.hook_add(UC_HOOK_CODE,tls,begin=0x100137cd,end=0x100137cd)
    paths.mem_write(0x20f00014,struct.pack('<I',12345))
    paths.reg_write(UC_X86_REG_ESI,0x20000000)
    paths.reg_write(UC_X86_REG_EBP,0)
    paths.reg_write(UC_X86_REG_ESP,0x30008000)
    paths.emu_start(0x10003dd7,0x10004044,count=1000000)
    seed=struct.unpack('<f',paths.mem_read(0x20012a58,4))[0]
    print('path-init',seed,struct.unpack('<3f',paths.mem_read(0x20ed4930,12)))
    for t in range(128):
        paths.mem_write(0x20012a48,struct.pack('<f',t))
        paths.reg_write(UC_X86_REG_EBP,0x20000000)
        paths.reg_write(UC_X86_REG_ESP,0x30008000)
        paths.emu_start(0x10006c9d,0x10006e1c,count=100000)
        paths.reg_write(UC_X86_REG_ECX,0x20000000)
        paths.reg_write(UC_X86_REG_ESP,0x30008000)
        paths.mem_write(0x30008000,struct.pack('<If',0x40000000,t))
        paths.emu_start(0x10004050,0x40000000,count=1000000)
        if t in (0,1,2,32,64,127):
            print('path',t,*(struct.unpack('<4f',paths.mem_read(0x20000000+offset,16)) for offset in (0x71b94,0x71bd4,0x71c14)))
        if t==127:
            print('spline3',struct.unpack('<18f',paths.mem_read(0x20072588+3*72,72)))
            print('phase3',struct.unpack('<2f',paths.mem_read(0x20ed4630+3*48,8)))
    u = machine(data)
    for seed,t in [(0,0),(313.45,100),(10,2000),(996.99,10000)]:
        u.reg_write(UC_X86_REG_EBP,0x20000000)
        u.reg_write(UC_X86_REG_ESP,0x30008000)
        u.mem_write(0x20012a58,struct.pack('<f',seed))
        u.mem_write(0x20012a48,struct.pack('<f',t))
        u.mem_write(0x200129c4,struct.pack('<f',1))
        u.emu_start(0x10006c9d,0x10006e1c,count=100000)
        movement,roughness=struct.unpack('<2f',u.mem_read(0x200129e0,8))
        u.emu_start(0x1000759e,0x100075d2,count=10000)
        print('scene',seed,t,movement,roughness,struct.unpack('<f',u.mem_read(0x20014a60,4))[0])
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
    # RGB target smoothing, followed by the source brightness floor/clamps.
    for seed, phase, rgb in [(0, (0, 0, 0), (0, 0, 0)),
                             (313.45, (100, 200, 300), (120, 150, 180)),
                             (10, (20, 30, 40), (250, 40, 20))]:
        u.mem_write(0x20012a58, struct.pack('<f', seed))
        u.mem_write(0x20ed4930, struct.pack('<3f', *phase))
        u.mem_write(0x200129e8, struct.pack('<3f', *rgb))
        u.reg_write(UC_X86_REG_ECX, 0x20000000)
        u.reg_write(UC_X86_REG_ESP, 0x30008000)
        u.mem_write(0x30008000, struct.pack('<I', 0x40000000))
        u.emu_start(0x10003a10, 0x40000000, count=10000)
        u.reg_write(UC_X86_REG_ESI, 0x20000000)
        u.reg_write(UC_X86_REG_ESP, 0x30008000)
        u.emu_start(0x10005b62, 0x10005d31, count=10000)
        print('background', seed, phase, rgb, struct.unpack('<3f', u.mem_read(0x200129e8, 12)))


if __name__ == '__main__':
    main(sys.argv[1])
