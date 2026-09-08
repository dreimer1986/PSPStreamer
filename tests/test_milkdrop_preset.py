import ctypes
import random
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class Warp(ctypes.Structure):
    _fields_ = [(key, ctypes.c_float) for key in
                ("zoom", "rotation", "warp", "warp_speed", "warp_scale", "decay")]


class Preset(ctypes.Structure):
    _fields_ = [("warp", Warp), ("red", ctypes.c_float),
                ("green", ctypes.c_float), ("blue", ctypes.c_float)]


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
        for key in ("per_frame_1", "per_pixel_1", "warp_1", "comp_1",
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


if __name__ == "__main__":
    unittest.main()
