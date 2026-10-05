"""Targeted regression for SDK math placeholders reachable after Xbox LTO."""
import ctypes
import math
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class XboxMathTests(unittest.TestCase):
    def test_string_conversion(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary=Path(tmp)/'strto'
            subprocess.run(['cc','-O2','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                            str(ROOT/'tests/xbox_strto.c'),str(ROOT/'xbox-client/visual_strto.c'),
                            str(ROOT/'xbox-client/vendor/musl/floatscan.c'),'-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)

    def test_exp2_replacement(self):
        with tempfile.TemporaryDirectory() as tmp:
            lib = Path(tmp) / 'math.so'
            subprocess.run(['cc', '-shared', '-fPIC', '-O3', '-fno-builtin',
                            '-Wl,-Bsymbolic', str(ROOT/'xbox-client/visual_math.c'),
                            '-lm', '-o', str(lib)], check=True)
            dll = ctypes.CDLL(str(lib))
            double = dll.exp2
            double.argtypes = [ctypes.c_double]
            double.restype = ctypes.c_double
            single = dll.exp2f
            single.argtypes = [ctypes.c_float]
            single.restype = ctypes.c_float
            for x in [-1074, -1022, -149, -126, -12.3, -1, 0, .1, .5, 1, 12.3, 127, 1023]:
                expected = math.pow(2, x)
                self.assertAlmostEqual(double(x)/expected, 1, places=14)
            for i in range(-1490, 1280):
                x = ctypes.c_float(i / 10).value
                expected = ctypes.c_float(math.pow(2, x)).value
                self.assertEqual(single(x), expected)
            self.assertTrue(math.isnan(double(math.nan)))
            self.assertEqual(double(math.inf), math.inf)
            self.assertEqual(double(-math.inf), 0)
            self.assertEqual(double(-1075), 0)
            self.assertEqual(double(1024), math.inf)
            minus = dll.expm1
            minus.argtypes = [ctypes.c_double]
            minus.restype = ctypes.c_double
            for x in [-100, -1, -.49, -1e-15, 1e-15, .49, 1, 10]:
                self.assertAlmostEqual(minus(x)/math.expm1(x), 1, places=14)
            self.assertEqual(minus(-math.inf), -1)
            self.assertEqual(minus(math.inf), math.inf)
            self.assertTrue(math.isnan(minus(math.nan)))

    def test_no_sdk_math_assertion_objects_in_xbox_link(self):
        sdk = ROOT/'.toolchain/nxdk/lib/pdclib/platform/xbox/functions/math'
        link = ROOT/'xbox-client/bin/player.map'
        if not sdk.exists() or not link.exists():
            self.skipTest('Build Xbox first; SDK/link map required')
        linked = link.read_text()
        for source in sdk.glob('*.c'):
            if 'assert(0)' in source.read_text():
                self.assertFalse('libpdclib:'+source.stem+'.obj' in linked,
                                 'Unimplemented SDK math function linked: '+source.name)


if __name__ == '__main__':
    unittest.main()
