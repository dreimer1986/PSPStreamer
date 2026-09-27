"""Focused differential checks against the user's audited DLL, never loaded by OS.
MONKEY_REFERENCE=/path/vis_monkey.dll python -m unittest ...
Requires the optional Unicorn package. No proprietary bytes are distributed.
"""
import ctypes as c
import hashlib
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
class State(c.Structure):
    _fields_=[('average',c.c_float*3),('history',c.c_float*3),('threshold',c.c_float),
              ('target',c.c_float),('last_peak',c.c_float),('ready',c.c_int),('armed',c.c_int)]

@unittest.skipUnless(os.environ.get('MONKEY_REFERENCE'),'local audited DLL required')
class MonkeyReferenceTests(unittest.TestCase):
    def test_sound_and_detector(self):
        from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
        from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_ESI,UC_X86_REG_ESP,UC_X86_REG_FPCW
        data=Path(os.environ['MONKEY_REFERENCE']).read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(),'c7dcfc838d0979870947d8e5b3f1fd5ad5a9e9c8e92fa77ba8de23c842fc614d')
        with tempfile.TemporaryDirectory() as directory:
            lib=Path(directory)/'audio.so'
            subprocess.run(['cc','-shared','-fPIC','-O2','-I',str(ROOT/'psp-client'),
                str(ROOT/'tests/monkey_audio_harness.c'),'-lm','-o',str(lib)],check=True)
            dll=c.CDLL(str(lib));dll.reset.argtypes=[c.POINTER(State)]
            dll.detect.argtypes=[c.POINTER(State),c.c_int,c.c_float,c.c_float,c.c_float]
            dll.step.argtypes=[c.POINTER(State),c.POINTER(c.c_float),c.c_int,c.c_float]
            dll.bands.argtypes=[c.POINTER(c.c_float),c.POINTER(c.c_float)]
            u=Uc(UC_ARCH_X86,UC_MODE_32)
            u.mem_map(0x10000000,0x1000000);u.mem_write(0x10000000,data)
            u.mem_map(0x20000000,0x1000000);u.mem_map(0x30000000,0x10000);u.mem_map(0x40000000,0x1000)
            u.reg_write(UC_X86_REG_FPCW,0x37f)
            def put(offset,value,fmt='f'):u.mem_write(0x20000000+offset,struct.pack('<'+fmt,value))
            def get(offset,fmt='f'):return struct.unpack('<'+fmt,u.mem_read(0x20000000+offset,4))[0]
            def run(start,end):
                u.reg_write(UC_X86_REG_ECX,0x20000000);u.reg_write(UC_X86_REG_ESI,0x20000000)
                u.reg_write(UC_X86_REG_ESP,0x30004000);u.mem_write(0x30004000,struct.pack('<I',0x40000000))
                u.emu_start(start,end,count=100000)
            for sensitivity in (0,4,8,12,16):
                state=State();dll.reset(c.byref(state))
                put(0x12a3c,sensitivity,'i');put(0x24a8,20)
                put(0xed49dc,1,'i');put(0xed49e0,5);put(0xed49e4,1.3);put(0xed49e8,1.3)
                events=0
                for frame in range(240):
                    immediate=8 if frame%20==0 else .1 if frame%20<10 else 1
                    average=.6*immediate+.4
                    for g in range(3):put(0x10+g*4,immediate);put(0x1c+g*4,average);put(0x34+g*4,1)
                    put(0xed49d8,0,'i');run(0x10009d80,0x40000000)
                    event=dll.detect(c.byref(state),sensitivity,immediate*.9999,average*.9999,.05)
                    self.assertEqual(event,get(0xed49d8,'i'));events+=event
                    self.assertEqual(state.armed,get(0xed49dc,'i'))
                    for name,offset in [('threshold',0xed49e0),('target',0xed49e4),('last_peak',0xed49e8)]:
                        # x87 extended intermediates vs PSP/C float rounding.
                        self.assertAlmostEqual(getattr(state,name),get(offset),delta=2e-5*(1+abs(get(offset))))
                self.assertGreater(events,0)
            state=State();dll.reset(c.byref(state))
            for g in range(3):put(0x1c+4*g,1);put(0x28+4*g,1);put(0x34+4*g,1)
            put(0x24a8,20)
            for frame in range(60):
                values=[abs(math.sin(i*.1+frame*.4))*(1+frame%9) for i in range(512)]
                raw=(c.c_float*3)();dll.bands((c.c_float*512)(*values),raw)
                u.mem_write(0x2000124c,struct.pack('<512f',*values))
                run(0x1001058c,0x100106ff)
                dll.step(c.byref(state),raw,8,.05)
                for g in range(3):
                    self.assertAlmostEqual(raw[g],get(0x10+4*g),delta=2e-5)
                    self.assertAlmostEqual(state.average[g],get(0x1c+4*g),delta=2e-5)
                    self.assertAlmostEqual(state.history[g],get(0x34+4*g),delta=2e-5)
