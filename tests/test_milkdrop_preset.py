import ctypes
import math
import random
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class Warp(ctypes.Structure):
    _fields_ = [(key, ctypes.c_float) for key in
                ("zoom", "rotation", "warp", "warp_speed", "warp_scale", "decay")]


class Op(ctypes.Structure):
    _fields_ = [("op", ctypes.c_int), ("arg", ctypes.c_int),
                ("line", ctypes.c_int), ("value", ctypes.c_float)]


class Program(ctypes.Structure):
    _fields_ = [("count", ctypes.c_int), ("lines", ctypes.c_int), ("code", Op * 128)]


class Preset(ctypes.Structure):
    _fields_ = [("warp", Warp), ("red", ctypes.c_float),
                ("green", ctypes.c_float), ("blue", ctypes.c_float), ("program", Program)]


class Error(ctypes.Structure):
    _fields_ = [("code", ctypes.c_int), ("line", ctypes.c_int), ("key", ctypes.c_char * 40)]


class PresetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.root = Path(cls.directory.name)
        library = cls.root / "preset.so"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-shared", "-fPIC", "-fsanitize=undefined",
                        str(ROOT / "psp-client/milkdrop_preset.c"),
                        str(ROOT / "psp-client/preset_math.c"),
                        str(ROOT / "psp-client/milkdrop_warp.c"), "-lm", "-o", str(library)],
                       check=True)
        cls.library = ctypes.CDLL(str(library))
        cls.load = cls.library.md_load_preset
        cls.load.argtypes = [ctypes.c_char_p, ctypes.POINTER(Preset), ctypes.POINTER(Error)]
        cls.load.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def parse(self, data):
        path = self.root / "test.milk"
        path.write_bytes(data)
        preset, error = Preset(), Error()
        ctypes.memset(ctypes.byref(preset), 0x42, ctypes.sizeof(preset))
        before = bytes(preset)
        result = self.load(str(path).encode(), ctypes.byref(preset), ctypes.byref(error))
        self.assertEqual(result, error.code)
        if result:
            self.assertEqual(bytes(preset), before, "Failed loads must be transactional")
        return result, preset, error

    def test_shipped_preset(self):
        result, preset, _ = self.parse((ROOT / "psp-client/presets/active.milk").read_bytes())
        self.assertEqual(result, 0)
        self.assertAlmostEqual(preset.warp.zoom, 1.018, places=5)
        self.assertAlmostEqual(preset.blue, 1)

    def test_whitespace_bom_crlf_and_defaults(self):
        result, preset, _ = self.parse(b"\xef\xbb\xbf; note\r\n [preset00] \r\n zoom = 1.02 \r\n// comment\n")
        self.assertEqual(result, 0)
        self.assertAlmostEqual(preset.warp.zoom, 1.02, places=5)
        self.assertAlmostEqual(preset.warp.decay, .97, places=5)

    def test_unsupported_fields_are_not_ignored(self):
        for key in ("per_pixel_1", "warp_1", "comp_1",
                    "nWaveMode", "fZoomExponent", "unknown", "wavecode_0_enabled"):
            result, _, error = self.parse(("[preset00]\nzoom=1\n" + key + "=0\n").encode())
            self.assertEqual(result, 3)
            self.assertEqual(error.line, 3)
            self.assertEqual(error.key.decode(), key)

    def test_invalid_numbers_duplicates_and_sections(self):
        for value in ("nan", "inf", "-inf", "1e99", "1e-999", "", "1 + bass",
                      "1; trailing", "0", "2", ".799", "1.201"):
            result, _, error = self.parse(("[preset00]\nzoom=" + value).encode())
            self.assertEqual(result, 2, value)
            self.assertEqual(error.line, 2)
        for data in (b"", b"[preset00]", b"zoom=1", b"[preset00]\nzoom=1\nzoom=1",
                     b"[preset00]\nzoom=1\n[preset00]", b"[preset01]\nzoom=1"):
            self.assertEqual(self.parse(data)[0], 2)

    def test_bounds_for_every_supported_field(self):
        for key, low, high in (("zoom", .8, 1.2), ("rot", -.2, .2), ("warp", -4, 4),
                               ("fWarpAnimSpeed", 0, 4), ("fWarpScale", .1, 8),
                               ("fDecay", .8, 1), ("wave_r", 0, 1),
                               ("wave_g", 0, 1), ("wave_b", 0, 1)):
            for value in (low, high):
                self.assertEqual(self.parse(f"[preset00]\n{key}={value}".encode())[0], 0)
            for value in (low-.01, high+.01):
                self.assertEqual(self.parse(f"[preset00]\n{key}={value}".encode())[0], 2)

    def test_limits_binary_and_missing(self):
        for data in (b"[preset00]\nzoom=1\x00", b" " * 256,
                     b";\n" * 8193, b" " * 255):
            self.assertEqual(self.parse(data)[0], 2)
        preset, error = Preset(), Error()
        self.assertEqual(self.load(str(self.root / "missing").encode(),
                                  ctypes.byref(preset), ctypes.byref(error)), 1)

    def test_deterministic_malformed_inputs(self):
        rng = random.Random(17)
        for _ in range(300):
            data = bytes(rng.randrange(256) for _ in range(rng.randrange(400)))
            result, _, _ = self.parse(data)
            self.assertIn(result, (2, 3))

    def evaluate(self, preset, seconds):
        fn = self.library.md_eval_preset
        fn.argtypes = [ctypes.POINTER(Preset), ctypes.c_float, ctypes.POINTER(Warp),
                       ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(Error)]
        warp, color, error = Warp(), ctypes.c_uint(123), Error()
        result = fn(ctypes.byref(preset), seconds, ctypes.byref(warp),
                    ctypes.byref(color), ctypes.byref(error))
        if result:
            self.assertEqual(bytes(warp), bytes(Warp()))
            self.assertEqual(color.value, 123)
        return result, warp, color.value, error

    def test_time_formulas_reset_and_precedence(self):
        result, preset, _ = self.parse(b"[preset00]\nwarp=1\n"
            b"per_frame_1=warp=warp+0.2*sin(time); rot=-(1+2)*0.01;\n"
            b"per_frame_2=wave_r=abs(-0.5); wave_g=wave_r/2; wave_b=cos(0);\n")
        self.assertEqual(result, 0)
        for t in (0, 1, 3, 40, 1, 0):
            code, warp, color, _ = self.evaluate(preset, t)
            self.assertEqual(code, 0)
            self.assertAlmostEqual(warp.warp, 1+.2*math.sin(t), places=6)
            self.assertAlmostEqual(warp.rotation, -.03, places=6)
            self.assertEqual(color, 0xffff3f7f)

    def test_formula_rejections_and_budgets(self):
        for source in ("rot=1", "rot=;", "rot=(1;", "0", "rot=nan;",
                       "time=0;", "rot=bass;", "rot=sqrt(1);", "rot=1e99;",
                       "rot=" + "("*18 + "0" + ")"*18 + ";",
                       "rot=" + "+".join(["0"]*70) + ";"):
            self.assertNotEqual(self.parse(f"[preset00]\nper_frame_1={source}".encode())[0], 0)
        for lines in ("per_frame_2=rot=0;", "per_frame_1=rot=0;\nper_frame_1=rot=0;",
                      "\n".join(f"per_frame_{i}=rot=0;" for i in range(1,18))):
            self.assertEqual(self.parse(f"[preset00]\n{lines}".encode())[0], 2)

    def test_runtime_errors_are_atomic(self):
        for source in ("rot=1/(time-1);", "warp=3e38*3e38;", "zoom=0;", "wave_r=2;"):
            result, preset, _ = self.parse(f"[preset00]\nper_frame_1={source}".encode())
            self.assertEqual(result, 0)
            code, _, _, error = self.evaluate(preset, 1)
            self.assertEqual(code, 2)
            self.assertEqual(error.line, 2)

    def test_formula_fuzz_and_total_instruction_limit(self):
        # Short valid lines individually fit; their combined bytecode must not.
        lines = "\n".join(f"per_frame_{i}=rot=0+0+0+0+0;" for i in range(1,15))
        self.assertEqual(self.parse(f"[preset00]\n{lines}".encode())[0], 2)
        rng = random.Random(121)
        alphabet = "rot=0123.+-*/(); time_sincosabs\t"
        for _ in range(500):
            source = "".join(rng.choice(alphabet) for _ in range(rng.randrange(180)))
            code, preset, _ = self.parse(f"[preset00]\nper_frame_1={source}".encode())
            self.assertIn(code, (0, 2, 3))
            if code == 0:
                self.assertIn(self.evaluate(preset, 1)[0], (0, 2))

    def test_time_demo_entire_episode(self):
        result, preset, _ = self.parse((ROOT / "psp-client/presets/time-demo.milk").read_bytes())
        self.assertEqual(result, 0)
        for frame in range(36000):
            self.assertEqual(self.evaluate(preset, frame/20)[0], 0)


if __name__ == "__main__":
    unittest.main()
