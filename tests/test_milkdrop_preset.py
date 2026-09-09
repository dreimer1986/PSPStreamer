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


class Signal(ctypes.Structure):
    _fields_ = [("values", ctypes.c_float * 13)]


class SignalState(ctypes.Structure):
    _fields_ = [("signal", Signal), ("tick", ctypes.c_ulonglong), ("ready", ctypes.c_int),
                ("average", ctypes.c_float * 3), ("long_average", ctypes.c_float * 3),
                ("origin", ctypes.c_ulonglong)]


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
                        str(ROOT / "psp-client/milkdrop_signal.c"),
                        str(ROOT / "psp-client/milkdrop_warp.c"), "-lm", "-o", str(library)],
                       check=True)
        cls.library = ctypes.CDLL(str(library))
        cls.load = cls.library.md_load_preset
        cls.load.argtypes = [ctypes.c_char_p, ctypes.POINTER(Preset), ctypes.POINTER(Error)]
        cls.load.restype = ctypes.c_int
        cls.update_signal = cls.library.md_signal_update
        cls.update_signal.argtypes = [ctypes.POINTER(SignalState),
                                      ctypes.POINTER(ctypes.c_ubyte), ctypes.c_int,
                                      ctypes.c_ulonglong]
        cls.reset_signal = cls.library.md_signal_reset
        cls.reset_signal.argtypes = [ctypes.POINTER(SignalState)]

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

    def evaluate(self, preset, seconds, signal=None):
        fn = self.library.md_eval_preset_signal
        fn.argtypes = [ctypes.POINTER(Preset), ctypes.c_float, ctypes.POINTER(Signal), ctypes.POINTER(Warp),
                       ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(Error)]
        warp, color, error = Warp(), ctypes.c_uint(123), Error()
        result = fn(ctypes.byref(preset), seconds, ctypes.byref(signal) if signal else None, ctypes.byref(warp),
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
                       "time=0;", "rot=unknown;", "rot=tan(1);", "rot=1e99;",
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

    def update(self, state, bands, level, now):
        self.update_signal(ctypes.byref(state), (ctypes.c_ubyte * 12)(*bands), level, now)
        for i, value in enumerate(state.signal.values):
            self.assertTrue(math.isfinite(value) and 0 <= value and (i >= 7 or value <= 1))

    def test_native_signal_mapping_clamping_and_reset(self):
        state = SignalState()
        self.update(state, [100]*4 + [50]*4 + [0]*4, 75, 0)
        self.assertEqual(list(state.signal.values)[:7], [1, .5, 0, .75, 1, .5, 0])
        self.update(state, [255]*12, 999, 6_000_000)
        self.assertEqual(list(state.signal.values)[:7], [1]*7)
        self.update(state, [0]*12, -5, 12_000_000)
        self.assertEqual(list(state.signal.values)[:7], [0]*7)
        self.reset_signal(ctypes.byref(state))
        self.assertEqual(bytes(state), bytes(SignalState()))

    def test_smoothing_uses_elapsed_time_not_frame_count(self):
        states = []
        for interval in (25_000, 50_000, 100_000, 250_000):
            state = SignalState()
            self.update(state, [100]*12, 100, 0)
            for now in range(interval, 1_000_001, interval):
                self.update(state, [0]*12, 0, now)
            self.assertEqual(list(state.signal.values)[:4], [0]*4)
            self.assertAlmostEqual(state.signal.values[4], math.exp(-4), places=6)
            states.append(state)
        # A duplicate tick cannot advance the smoother; a clock reset rebases it.
        state = states[0]
        smooth = state.signal.values[4]
        self.update(state, [100]*12, 100, state.tick)
        self.assertEqual(state.signal.values[4], smooth)
        self.update(state, [50]*12, 50, 1)
        self.assertEqual(list(state.signal.values)[:7], [.5]*7)

    def test_native_formula_inputs_are_read_only_and_not_eel_aliases(self):
        names = ("psp_low", "psp_mid", "psp_high", "psp_level",
                 "psp_low_smooth", "psp_mid_smooth", "psp_high_smooth")
        signal = Signal((ctypes.c_float * 13)(.1, .2, .3, .4, .5, .6, .7))
        for i, name in enumerate(names):
            code, preset, _ = self.parse(f"[preset00]\nper_frame_1=warp={name};".encode())
            self.assertEqual(code, 0)
            self.assertAlmostEqual(self.evaluate(preset, 0, signal)[1].warp, (i+1)/10, places=6)
            self.assertEqual(self.parse(f"[preset00]\nper_frame_1={name}=0;".encode())[0], 3)
        for name in ("bass", "mid", "treb", "bass_att", "mid_att", "treb_att"):
            self.assertEqual(self.parse(f"[preset00]\nper_frame_1={name}=0;".encode())[0], 3)
        for bad in (float("nan"), float("inf"), -1, 1.1):
            signal.values[0] = bad
            self.assertEqual(self.evaluate(preset, 0, signal)[0], 2)

    def test_music_demo_with_varying_signals(self):
        code, preset, _ = self.parse((ROOT / "psp-client/presets/music-demo.milk").read_bytes())
        self.assertEqual(code, 0)
        state = SignalState()
        rng = random.Random(55)
        now = 0
        for frame in range(12000):
            now += rng.choice((50_000, 75_000, 100_000))
            bands = [rng.randrange(101) for _ in range(12)] if frame % 100 < 70 else [0]*12
            self.update(state, bands, max(bands), now)
            self.assertEqual(self.evaluate(preset, now/1e6, state.signal)[0], 0)

    def test_relative_analysis_against_reference_equations(self):
        state = SignalState()
        self.update(state, [20]*12, 20, 0)
        self.assertEqual(list(state.signal.values)[7:], [1]*6)
        avg = long_avg = .2
        rng = random.Random(125)
        now = 0
        for _ in range(2000):
            dt = rng.choice((33333, 50000, 100000))
            now += dt
            raw = rng.choice((0, .2, .5, 1))
            rate = (.2 if raw > avg else .5)**(dt/1e6*30)
            avg = avg*rate + raw*(1-rate)
            rate = (.9 if now < 1666667 else .992)**(dt/1e6*30)
            long_avg = long_avg*rate + raw*(1-rate)
            self.update(state, [round(raw*100)]*12, 0, now)
            self.assertAlmostEqual(state.signal.values[7], 1 if long_avg < .001 else raw/long_avg, places=4)
            self.assertAlmostEqual(state.signal.values[10], 1 if long_avg < .001 else avg/long_avg, places=4)
        self.reset_signal(ctypes.byref(state))
        self.update(state, [20]*12, 20, 0)
        self.update(state, [100]*12, 100, 33333)
        self.assertGreater(state.signal.values[7], 1)
        self.assertLess(state.signal.values[10], state.signal.values[7])

    def test_min_max_sqrt_and_relative_demo(self):
        code, preset, _ = self.parse(b"[preset00]\nper_frame_1=warp=min(3,max(-1,sqrt(4)));\n")
        self.assertEqual(code, 0)
        self.assertEqual(self.evaluate(preset, 0)[1].warp, 2)
        for expression in ("min(1)", "max(1,2,3)", "sqrt(1,2)", "min(,1)"):
            self.assertNotEqual(self.parse(f"[preset00]\nper_frame_1=warp={expression};".encode())[0], 0)
        code, preset, _ = self.parse(b"[preset00]\nper_frame_1=warp=sqrt(-1);")
        self.assertEqual(code, 0)
        self.assertEqual(self.evaluate(preset, 0)[0], 2)
        code, preset, _ = self.parse((ROOT / "psp-client/presets/relative-demo.milk").read_bytes())
        self.assertEqual(code, 0)
        for value in (0, .5, 1, 2, 10, 1000):
            signal = Signal((ctypes.c_float * 13)(*([0]*7+[value]*6)))
            self.assertEqual(self.evaluate(preset, 123, signal)[0], 0)


if __name__ == "__main__":
    unittest.main()
