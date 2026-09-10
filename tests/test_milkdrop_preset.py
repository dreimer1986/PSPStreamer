import ctypes
import math
import os
import random
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class Warp(ctypes.Structure):
    _fields_ = [(key, ctypes.c_float) for key in
                ("zoom", "rotation", "warp", "warp_speed", "warp_scale", "decay", "dx", "dy",
                 "cx","cy","sx","sy","zoomexp")]


class Shape(ctypes.Structure):
    _fields_=[(key,ctypes.c_float) for key in ("enabled","sides","additive","textured",
        "x","y","rad","ang","tex_ang","tex_zoom","r","g","b","a","r2","g2","b2","a2",
        "border_r","border_g","border_b","border_a")]


class Border(ctypes.Structure):
    _fields_=[(key,ctypes.c_float) for key in ("size","r","g","b","a")]


class Decor(ctypes.Structure):
    _fields_=[(key,ctypes.c_float) for key in ("wave_x","wave_y","wave_param","wave_dots",
        "wave_thick","wave_additive","wave_brighten","wave_mod_alpha","wave_mod_start","wave_mod_end",
        "echo_zoom","echo_alpha","echo_orient")]+[("outer",Border),("inner",Border),
        ("gamma",ctypes.c_float),("wave_alpha",ctypes.c_float),("shapes",Shape*4)]


class Op(ctypes.Structure):
    _fields_ = [("op", ctypes.c_int), ("arg", ctypes.c_int),
                ("line", ctypes.c_int), ("value", ctypes.c_float)]


class Program(ctypes.Structure):
    _fields_ = [("count", ctypes.c_int), ("lines", ctypes.c_int), ("code", Op * 128)]


class Preset(ctypes.Structure):
    _fields_ = [("warp", Warp), ("red", ctypes.c_float),
                ("green", ctypes.c_float), ("blue", ctypes.c_float), ("program", Program),
                ("legacy",ctypes.c_int),("wave_mode",ctypes.c_int),("wrap",ctypes.c_int),
                ("gamma",ctypes.c_float),("wave_scale",ctypes.c_float),
                ("wave_smoothing",ctypes.c_float),("wave_alpha",ctypes.c_float),("decor",Decor),
                ("init_program",Program)]


class PresetState(ctypes.Structure):
    _fields_=[("ready",ctypes.c_int),("q",ctypes.c_float*32)]


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
                        str(ROOT / "psp-client/milkdrop_wave.c"),
                        str(ROOT / "psp-client/milkdrop_decor.c"),
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
                    "unknown", "wavecode_0_enabled"):
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
                       "time=0;", "rot=unknown;", "rot=unknown_func(1);", "rot=1e99;",
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

    def test_legacy_fields_and_output_rules(self):
        code,preset,_=self.parse(b"presetName=Native test\nnWaveMode=0\nfGammaAdj=2\nbTexWrap=0\n"
            b"per_frame_1=wave_r=1.35; wave_g=-0.2; zoom=2; dx=.01; dy=-.02;\n")
        self.assertEqual(code,0)
        self.assertEqual((preset.wave_mode,preset.wrap,preset.gamma),(0,0,2))
        result,warp,color,_=self.evaluate(preset,0)
        self.assertEqual(result,0)
        self.assertEqual(color&0xffff,255)
        self.assertEqual(warp.zoom,2)
        self.assertAlmostEqual(warp.dx,.01)
        self.assertAlmostEqual(warp.dy,-.02)
        for field,value in (("fVideoEchoAlpha",2),("ob_alpha",2),("bInvert",1),
                            ("nWaveMode",2),("cx",1.6),("fWaveParam",2)):
            self.assertEqual(self.parse(f"presetName=test\n{field}={value}".encode())[0],3)
        for data in (b"presetName=x\npresetName=x",b"[preset00]\ndx=0\ndx=0"):
            self.assertEqual(self.parse(data)[0],2)

    def test_user_hyperdrive_when_supplied(self):
        path=os.environ.get("HYPERDRIVE_PRESET")
        if not path: self.skipTest("Set HYPERDRIVE_PRESET to test the unbundled user preset")
        code,preset,error=self.parse(Path(path).read_bytes())
        self.assertEqual(code,0,bytes(error))
        for time in range(1800):
            for bass in (0,1,4,10,1000):
                signal=Signal((ctypes.c_float*13)(*([0]*7+[bass]*6)))
                self.assertEqual(self.evaluate(preset,time,signal)[0],0)

    def test_extended_math(self):
        for source,expected in (("floor(-.2)",-1),("ceil(.2)",1),("atan(1)",math.pi/4),
            ("log(exp(1))",1),("log10(100)",2),("sqr(-1.5)",2.25),("sign(-4)",-1),
            ("pow(2,1.5)",2**1.5),("atan2(1,-1)",3*math.pi/4),
            ("above(2,1)",1),("below(2,1)",0),("equal(1,1.000001)",1),
            ("equal(1,1.001)",0)):
            code,preset,_=self.parse(f"[preset00]\nper_frame_1=warp={source};".encode())
            self.assertEqual(code,0,source)
            result,warp,_,_=self.evaluate(preset,0)
            self.assertEqual(result,0,source)
            self.assertAlmostEqual(warp.warp,expected,places=5)
        for source in ("log(0)","log10(-1)","pow(-1,.5)","exp(1000)"):
            code,preset,_=self.parse(f"[preset00]\nper_frame_1=warp={source};".encode())
            self.assertEqual(code,0)
            self.assertEqual(self.evaluate(preset,0)[0],2)

    def test_transforms_and_static_layers(self):
        data=b"[preset00]\nzoom=1\nfZoomExponent=1.5\ncx=.4\ncy=.6\nsx=2\nsy=.5\n"
        code,preset,_=self.parse(data+b"per_frame_1=cx=.5+.1*sin(time); sy=1;\n")
        self.assertEqual(code,0)
        self.assertAlmostEqual(self.evaluate(preset,0)[1].cx,.5)
        self.assertEqual(self.evaluate(preset,0)[1].sy,1)
        for key,value in (("sx",0),("sy",-1),("fZoomExponent",10),("bWaveDots",.5),
                          ("nVideoEchoOrientation",1.5)):
            self.assertEqual(self.parse(f"[preset00]\n{key}={value}".encode())[0],3)
        for key in ("cx","cy","sx","sy","zoomexp"):
            code,preset,_=self.parse(f"[preset00]\nper_frame_1={key}=10;".encode())
            self.assertEqual(code,0)
            self.assertEqual(self.evaluate(preset,0)[0],2)
        code,preset,_=self.parse(b"[preset00]\nshapecode_0_enabled=1\nshapecode_0_sides=32\n"
            b"shapecode_0_textured=1\nshapecode_0_border_a=.5\nfVideoEchoAlpha=.5\n"
            b"nVideoEchoOrientation=3\nob_size=.1\nob_alpha=.5\nbWaveThick=1\n")
        self.assertEqual(code,0)
        self.assertEqual(preset.decor.shapes[0].sides,32)
        self.assertEqual(preset.decor.echo_orient,3)
        self.assertEqual(preset.decor.wave_thick,1)
        for key,value in (("shapecode_4_enabled",1),("shapecode_0_sides",33),
            ("shapecode_0_textured",.5),("shapecode_0_tex_zoom",0),("shapecode_0_per_frame1",0)):
            self.assertEqual(self.parse(f"[preset00]\n{key}={value}".encode())[0],3)
        self.assertEqual(self.parse(b"[preset00]\nshapecode_0_x=.5\nshapecode_0_x=.5")[0],2)
        self.assertEqual(self.parse(b"[preset00]\nbModWaveAlphaByVolume=1\nfModWaveAlphaStart=1\nfModWaveAlphaEnd=1")[0],2)

    def test_conditional_lazy_nested_and_boolean_math(self):
        cases = (("if(1,2,1/0)",2), ("if(0,sqrt(-1),3)",3),
            ("if(-1,2,3)",2), ("if(.000001,2,3)",3),
            ("if(.00001,2,3)",2), ("bnot(.00001)",0),
            ("band(.00001,1)",0), ("bor(.00001,0)",0),
            ("bnot(-.000001)",1), ("band(-2,3)",1), ("bor(0,-2)",1),
            ("1+if(0,0,if(1,2,log(0)))*.5",2),
            ("if(if(0,1,0),2,3)",3), ("if(1,if(0,1,2),3)",2),
            ("tan(.2)",math.tan(.2)), ("asin(.5)",math.asin(.5)),
            ("acos(.5)",math.acos(.5)), ("sigmoid(0,2)",.5),
            ("sigmoid(2,3)",1/(1+math.exp(-6))),
            ("sigmoid(-2,3)",1/(1+math.exp(6))),
            ("sigmoid(3e38,3e38)",1), ("sigmoid(-3e38,3e38)",0))
        for expression, expected in cases:
            code,preset,_=self.parse(f"[preset00]\nper_frame_1=warp={expression};".encode())
            self.assertEqual(code,0,expression)
            result,warp,_,_=self.evaluate(preset,0)
            self.assertEqual(result,0,expression)
            self.assertAlmostEqual(warp.warp,expected,places=5)
        # Named band/bor are deliberately eager, unlike if().
        for expression in ("if(1,1/0,2)","if(0,2,log(0))",
                           "band(0,1/0)","bor(1,1/0)","asin(2)","acos(-2)"):
            code,preset,_=self.parse(f"[preset00]\nper_frame_1=warp={expression};".encode())
            self.assertEqual(code,0)
            self.assertEqual(self.evaluate(preset,0)[0],2,expression)

    def test_conditional_syntax_budgets_and_bytecode_safety(self):
        for expression in ("if()","if(1,2)","if(1,2,3,4)","if(,2,3)",
                           "if(1,,3)","if(1,2,)","if(1,2,unknown)",
                           "if(1,2,rand(1))","if(1,2,warp=3)"):
            self.assertNotEqual(self.parse(f"[preset00]\nper_frame_1=warp={expression};".encode())[0],0)
        expression="0"
        for _ in range(18): expression=f"if(0,0,{expression})"
        self.assertNotEqual(self.parse(f"[preset00]\nper_frame_1=warp={expression};".encode())[0],0)
        lines="\n".join(f"per_frame_{i}=warp=if(1,if(0,0,1),if(1,1,0));" for i in range(1,17))
        self.assertEqual(self.parse(f"[preset00]\n{lines}".encode())[0],2)
        # A failed append never changes the previously compiled instructions.
        program=Program()
        compile_fn=self.library.pm_compile
        compile_fn.argtypes=[ctypes.POINTER(Program),ctypes.c_char_p,ctypes.c_int]
        self.assertEqual(compile_fn(ctypes.byref(program),b"warp=1;",2),0)
        previous=b"".join(bytes(op) for op in program.code[:program.count])
        count=program.count
        self.assertNotEqual(compile_fn(ctypes.byref(program),b"warp=if(1,2,);",3),0)
        self.assertEqual(program.count,count)
        self.assertEqual(program.lines,1)
        self.assertEqual(b"".join(bytes(op) for op in program.code[:program.count]),previous)
        # Corrupt forward-jump destinations must fail atomically, never loop.
        self.assertEqual(compile_fn(ctypes.byref(program),b"warp=if(0,2,3);",3),0)
        branch_index=count+1  # PUSH condition, then JZ.
        execute=self.library.pm_execute
        execute.argtypes=[ctypes.POINTER(Program),ctypes.POINTER(ctypes.c_float),ctypes.POINTER(ctypes.c_int)]
        for index in (branch_index,branch_index+2):
            for destination in (-1,index,program.count+1):
                damaged=Program.from_buffer_copy(program)
                damaged.code[count].value=1  # also exercise unconditional jump
                damaged.code[index].arg=destination
                values=(ctypes.c_float*87)(*([.5]*87)); before=bytes(values)
                error=ctypes.c_int()
                self.assertEqual(execute(ctypes.byref(damaged),values,ctypes.byref(error)),0)
                self.assertEqual(bytes(values),before)

    def test_conditional_music_branches_long_run(self):
        code,preset,_=self.parse((ROOT / "psp-client/presets/branch-beat-demo.milk").read_bytes())
        self.assertEqual(code,0)
        for frame in range(18000):
            level=(frame%101)/100
            signal=Signal((ctypes.c_float*13)(*([level]*7+[level*4]*6)))
            self.assertEqual(self.evaluate(preset,frame*.1,signal)[0],0)
        code,preset,_=self.parse(b"[preset00]\nper_frame_1=warp=if(above(psp_low,0),1/psp_low,0);\n")
        self.assertEqual(code,0)
        for level,expected in ((0,0),(.5,2),(0,0),(1,1)):
            signal=Signal((ctypes.c_float*13)(level))
            result,warp,_,_=self.evaluate(preset,0,signal)
            self.assertEqual(result,0)
            self.assertEqual(warp.warp,expected)

    def evaluate_state(self,preset,state,time,signal=None):
        fn=self.library.md_eval_preset_state
        fn.argtypes=[ctypes.POINTER(Preset),ctypes.c_float,ctypes.POINTER(Signal),
                     ctypes.POINTER(PresetState),ctypes.POINTER(Warp),ctypes.POINTER(ctypes.c_uint),
                     ctypes.POINTER(Decor),ctypes.POINTER(Error)]
        warp,decor,error=Warp(),Decor(),Error()
        color=ctypes.c_uint(123)
        before=bytes(state)
        result=fn(ctypes.byref(preset),time,ctypes.byref(signal) if signal else None,
                  ctypes.byref(state),ctypes.byref(warp),ctypes.byref(color),
                  ctypes.byref(decor),ctypes.byref(error))
        if result:
            self.assertEqual(bytes(state),before)
            self.assertEqual(bytes(warp),bytes(Warp()))
            self.assertEqual(bytes(decor),bytes(Decor()))
            self.assertEqual(color.value,123)
        return result,warp,error

    def test_init_q_lifetime_and_output_reset(self):
        code,preset,_=self.parse(b"[preset00]\nper_frame_init_1=q1=time; q32=.25; zoom=9;\n"
            b"per_frame_1=q1=q1+1; warp=q1; wave_r=q32;\n")
        self.assertEqual(code,0)
        state=PresetState()
        for time in (1,2,10,300):
            result,warp,_=self.evaluate_state(preset,state,time)
            self.assertEqual(result,0)
            self.assertEqual(warp.warp,2) # init time=1, not a growing accumulator
            self.assertAlmostEqual(warp.zoom,preset.warp.zoom)
            self.assertEqual(state.q[0],1)
            self.assertEqual(state.q[31],.25)
        # New activation gets new seeds, independent of another instance.
        other=PresetState()
        self.assertEqual(self.evaluate_state(preset,other,2)[1].warp,3)
        self.assertEqual(state.q[0],1)
        state=PresetState()
        self.assertEqual(self.evaluate_state(preset,state,0)[1].warp,1)
        code,preset,_=self.parse(b"[preset00]\nper_frame_1=q1=q1+1; warp=q1;\n")
        self.assertEqual(code,0)
        state=PresetState()
        for time in range(10): self.assertEqual(self.evaluate_state(preset,state,time)[1].warp,1)

    def test_init_q_validation_atomicity_and_budgets(self):
        for name in ("q0","q33","q01","q999999999999999999999","q1x","Q1","counter"):
            self.assertEqual(self.parse(f"[preset00]\nper_frame_init_1={name}=1;".encode())[0],3)
        for lines in ("per_frame_init_2=q1=0;",
                      "per_frame_init_1=q1=0;\nper_frame_init_1=q1=1;",
                      "\n".join(f"per_frame_init_{i}=q1=0;" for i in range(1,18)),
                      "\n".join(f"per_frame_init_{i}=q1=0+0+0+0+0;" for i in range(1,15))):
            self.assertEqual(self.parse(f"[preset00]\n{lines}".encode())[0],2)
        code,preset,_=self.parse(b"[preset00]\nper_frame_init_1=q1=1/0;\n")
        self.assertEqual(code,0)
        result,_,error=self.evaluate_state(preset,PresetState(),0)
        self.assertEqual(result,2); self.assertEqual(error.line,2)
        self.assertEqual(error.key.decode(),"init formula")
        code,preset,_=self.parse(b"[preset00]\nper_frame_init_1=q1=2;\nper_frame_1=warp=1/(time-1);\n")
        self.assertEqual(code,0)
        state=PresetState()
        self.assertEqual(self.evaluate_state(preset,state,1)[0],2)
        self.assertEqual(state.ready,0)
        self.assertEqual(self.evaluate_state(preset,state,2)[0],0)
        self.assertEqual(state.ready,1)
        self.assertEqual(self.evaluate_state(preset,state,1)[0],2)
        # Init and frame lines can interleave, with independent numbering.
        code,_,_=self.parse(b"[preset00]\nper_frame_1=warp=q1;\nper_frame_init_1=q1=1;\n"
            b"per_frame_2=warp=warp+q2;\nper_frame_init_2=q2=1;\n")
        self.assertEqual(code,0)

    def test_init_q_demo_long_run(self):
        code,preset,_=self.parse((ROOT / "psp-client/presets/init-orbit-demo.milk").read_bytes())
        self.assertEqual(code,0)
        for activation in range(3):
            state=PresetState()
            for frame in range(6000):
                level=(frame%101)/100
                signal=Signal((ctypes.c_float*13)(*([level]*7+[level*4]*6)))
                self.assertEqual(self.evaluate_state(preset,state,frame*.1,signal)[0],0)
            self.assertAlmostEqual(state.q[0],.7)

    def test_feature_demos_and_dynamic_fields(self):
        for name in ("receiver-fx-demo.milk","echo-dots-demo.milk"):
            code,preset,error=self.parse((ROOT / "psp-client/presets" / name).read_bytes())
            self.assertEqual(code,0,(name,error.line,error.key))
            state=SignalState()
            rng=random.Random(45)
            for frame in range(12000):
                now=frame*100000
                self.update(state,[rng.randrange(101) for _ in range(12)],rng.randrange(101),now)
                self.assertEqual(self.evaluate(preset,now/1e6,state.signal)[0],0,(name,frame))
        code,preset,_=self.parse(b"[preset00]\nper_frame_1=echo_alpha=.6; ob_r=.3; gamma=2; wave_a=.4;")
        self.assertEqual(code,0)
        evaluate=self.library.md_eval_preset_visual
        evaluate.argtypes=[ctypes.POINTER(Preset),ctypes.c_float,ctypes.POINTER(Signal),
            ctypes.POINTER(Warp),ctypes.POINTER(ctypes.c_uint),ctypes.POINTER(Decor),ctypes.POINTER(Error)]
        out,warp,color,error=Decor(),Warp(),ctypes.c_uint(),Error()
        self.assertEqual(evaluate(ctypes.byref(preset),0,None,ctypes.byref(warp),ctypes.byref(color),
                                 ctypes.byref(out),ctypes.byref(error)),0)
        self.assertAlmostEqual(out.echo_alpha,.6)
        self.assertAlmostEqual(out.outer.r,.3)
        self.assertAlmostEqual(out.wave_alpha,.4)
        self.assertEqual(out.gamma,2)

    def test_wave_snapshot_and_reference_geometry(self):
        class Vertex(ctypes.Structure):
            _fields_=[("u",ctypes.c_float),("v",ctypes.c_float),("color",ctypes.c_uint),
                      ("x",ctypes.c_float),("y",ctypes.c_float),("z",ctypes.c_float)]
        publish=self.library.visualization_pcm_publish
        publish.argtypes=[ctypes.POINTER(ctypes.c_short),ctypes.c_int]
        snapshot=self.library.md_wave_snapshot
        snapshot.argtypes=[ctypes.POINTER(ctypes.c_short)]
        capture=ctypes.c_int.in_dll(self.library,"md_wave_capture")
        capture.value=0
        pcm=(ctypes.c_short*1152)(*(int(math.sin(i*.08)*30000) for i in range(1152)))
        right=(ctypes.c_short*576)()
        self.library.md_wave_forget()
        publish(pcm,576)
        self.assertEqual(snapshot(right),0)
        capture.value=1
        publish(pcm,576)
        self.assertEqual(snapshot(right),1)
        self.assertEqual(list(right),list(pcm)[1::2])
        self.assertEqual(snapshot(right),0)
        self.library.md_wave_forget()
        self.assertEqual(snapshot(right),0)
        vertices=(Vertex*241)()
        draw=self.library.md_wave_circle
        draw.argtypes=[ctypes.POINTER(Vertex),ctypes.POINTER(ctypes.c_short),
                       ctypes.c_float,ctypes.c_float,ctypes.c_float,ctypes.c_float,ctypes.c_uint]
        smoothed=[right[0]/32768]
        for raw in list(right)[1:]: smoothed.append(raw/32768*.25+smoothed[-1]*.75)
        for aspect in (.25,.5625,2/3):
            draw(vertices,right,1,.75,7,aspect,0xff123456)
            for i in range(240):
                radius=.5+.4*smoothed[i+120]
                if i<24:
                    mix=.5-.5*math.cos(i/24*3.1416)
                    radius=(.5+.4*smoothed[i+360])*(1-mix)+radius*mix
                angle=i/239*6.28+7*.2
                self.assertAlmostEqual(vertices[i].x,128+128*radius*math.cos(angle)*aspect,places=4)
                self.assertAlmostEqual(vertices[i].y,128-128*radius*math.sin(angle),places=4)
            self.assertEqual(bytes(vertices[0]),bytes(vertices[240]))
        publish(pcm,2)
        self.assertEqual(snapshot(right),1)
        self.assertEqual(list(right)[2:],[0]*574)
        capture.value=0


if __name__ == "__main__":
    unittest.main()
