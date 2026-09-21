import ctypes
import math
import os
import random
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(os.environ.get('PSP_MILKDROP_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
def source_constant(header, name):
    return int(re.search(r'\b'+name+r'\s*=\s*(\d+)', (ROOT/'psp-client'/header).read_text()).group(1))
MAX_OPS = source_constant('preset_math.h', 'PM_MAX_OPS')
MEMORY = source_constant('preset_math.h', 'PM_MEMORY')
USERS = source_constant('preset_math.h', 'PM_USER_COUNT')
VALUES = 154+USERS
RECORDS = source_constant('preset_math.h', 'PM_MAX_RECORDS')
DYNAMIC_CODE = 'PmOp *code' in (ROOT/'psp-client/preset_math.h').read_text()
GRID = source_constant('milkdrop_warp.h', 'MD_GRID')
GRID_POINTS = (GRID+1)**2
CUSTOM_POINTS = source_constant('milkdrop_preset.h', 'MD_CUSTOM_POINTS')
SHAPE_INSTANCES = (source_constant('milkdrop_decor.h', 'MD_SHAPE_MAX_INSTANCES')
                   if 'MD_SHAPE_MAX_INSTANCES=' in (ROOT/'psp-client/milkdrop_decor.h').read_text()
                   else source_constant('milkdrop_decor.h', 'MD_SHAPE_INSTANCES'))


class Warp(ctypes.Structure):
    _fields_ = [(key, ctypes.c_float) for key in
                ("zoom", "rotation", "warp", "warp_speed", "warp_scale", "decay", "dx", "dy",
                 "cx","cy","sx","sy","zoomexp")]


class Shape(ctypes.Structure):
    _fields_=[(key,ctypes.c_float) for key in ("enabled","sides","additive","textured",
        "x","y","rad","ang","tex_ang","tex_zoom","r","g","b","a","r2","g2","b2","a2",
        "border_r","border_g","border_b","border_a","thick_outline")]


class Border(ctypes.Structure):
    _fields_=[(key,ctypes.c_float) for key in ("size","r","g","b","a")]

class ShapeFrame(ctypes.Structure):
    _fields_=[('shapes',(Shape*SHAPE_INSTANCES)*4),('count',ctypes.c_int*4)]


class Decor(ctypes.Structure):
    _fields_=[(key,ctypes.c_float) for key in ("wave_x","wave_y","wave_param","wave_dots",
        "wave_thick","wave_additive","wave_brighten","wave_mod_alpha","wave_mod_start","wave_mod_end",
        "echo_zoom","echo_alpha","echo_orient")]+[("outer",Border),("inner",Border),
        ("gamma",ctypes.c_float),("wave_alpha",ctypes.c_float),("shapes",Shape*32)]


class Op(ctypes.Structure):
    _fields_ = [("op", ctypes.c_int), ("arg", ctypes.c_int),
                ("line", ctypes.c_int), ("value", ctypes.c_float)]


class Program(ctypes.Structure):
    _fields_ = [("count", ctypes.c_int), ("lines", ctypes.c_int),
                *(([("capacity",ctypes.c_int),("code",ctypes.POINTER(Op))])
                  if DYNAMIC_CODE else [("code",Op*MAX_OPS)])]
    _release=None
    def __del__(self):
        if self._b_base_ is None and type(self)._release is not None:
            type(self)._release(ctypes.byref(self))


class Symbols(ctypes.Structure):
    _fields_=[("count",ctypes.c_int),("names",(ctypes.c_char*32)*USERS)]


class Runtime(ctypes.Structure):
    _fields_=[("memory",ctypes.c_float*MEMORY),("random",ctypes.c_uint)]


class ShapeProgram(ctypes.Structure):
    _fields_=[("init",Program),("frame",Program),("symbols",Symbols)]

class ShapeState(ctypes.Structure):
    _fields_=[("ready",ctypes.c_int),("t",ctypes.c_float*8),("user",ctypes.c_float*USERS),("runtime",Runtime)]

class CustomWave(ctypes.Structure):
    _fields_=[(n,ctypes.c_float) for n in ("enabled","samples","sep","spectrum","dots","thick","additive","scaling","smoothing","r","g","b","a")]+[("init",Program),("frame",Program),("point",Program),("symbols",Symbols),("point_symbols",Symbols)]

class WaveState(ctypes.Structure):
    _fields_=[("frame",ShapeState),("point_user",ctypes.c_float*USERS),("point_runtime",Runtime)]

class Preset(ctypes.Structure):
    _release=None
    def __del__(self):
        if self._b_base_ is None and type(self)._release is not None:
            type(self)._release(ctypes.byref(self))
    _fields_ = [("warp", Warp), ("red", ctypes.c_float),
                ("green", ctypes.c_float), ("blue", ctypes.c_float), ("program", Program),
                ("legacy",ctypes.c_int),("wave_mode",ctypes.c_int),("wrap",ctypes.c_int),
                ("gamma",ctypes.c_float),("wave_scale",ctypes.c_float),
                ("wave_smoothing",ctypes.c_float),("wave_alpha",ctypes.c_float),("decor",Decor),
                ("init_program",Program),("symbols",Symbols),("pixel_program",Program),("motion",ctypes.c_float*9),
                ("shape_program",ShapeProgram*4),("waves",CustomWave*4),("effects",ctypes.c_float*5),("shape_instances",ctypes.c_int*4),("pixel_symbols",Symbols),("texture_path",(ctypes.c_char*512)*4),("shader_amount",ctypes.c_float)]


class PresetState(ctypes.Structure):
    _fields_=[("ready",ctypes.c_int),("q",ctypes.c_float*32),("user",ctypes.c_float*USERS),
              ("frame_q",ctypes.c_float*32),("frames",ctypes.c_uint),
              ("last_seconds",ctypes.c_float),("fps",ctypes.c_float),
              ("wave_mode",ctypes.c_int),("motion",ctypes.c_float*9),("shape",ShapeState*4),("waves",WaveState*4),("effects",ctypes.c_float*5),
              ("runtime",Runtime),("pixel_runtime",Runtime),("pixel_user",ctypes.c_float*USERS),("monitor",ctypes.c_float),("wrap",ctypes.c_int)]


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
                        "-shared", "-fPIC", "-fsanitize=undefined,float-cast-overflow",
                        str(ROOT / "psp-client/milkdrop_preset.c"),
                        str(ROOT / "psp-client/preset_math.c"),
                        str(ROOT / "psp-client/milkdrop_signal.c"),
                        str(ROOT / "psp-client/milkdrop_wave.c"),
                        str(ROOT / "psp-client/milkdrop_wave_extra.c"),
                        str(ROOT / "psp-client/milkdrop_decor.c"),
                        str(ROOT / "psp-client/milkdrop_warp.c"), "-lm", "-o", str(library)],
                       check=True)
        cls.library = ctypes.CDLL(str(library))
        Program._release=getattr(cls.library,'pm_program_free',None)
        if Program._release:Program._release.argtypes=[ctypes.POINTER(Program)]
        Preset._release=getattr(cls.library,'md_free_preset',None)
        if Preset._release:Preset._release.argtypes=[ctypes.POINTER(Preset)]
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
        # Owning program descriptors must start empty; retain scalar sentinels
        # to verify failed imports leave their destination untouched.
        preset.red=42;preset.wrap=42
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

    def test_multirecord_formulas_in_every_context(self):
        cases=[('per_frame_',lambda p:p.program),('per_frame_init_',lambda p:p.init_program),
               ('per_pixel_',lambda p:p.pixel_program),
               ('shape_0_init',lambda p:p.shape_program[0].init),
               ('shape_0_per_frame',lambda p:p.shape_program[0].frame),
               ('wave_0_init',lambda p:p.waves[0].init),
               ('wave_0_per_frame',lambda p:p.waves[0].frame),
               ('wave_0_per_point',lambda p:p.waves[0].point)]
        for prefix,get in cases:
            with self.subTest(prefix=prefix):
                lines=['q1=if(1,','(2+','3),4);']
                data='[preset00]\n'+'\n'.join(f'{prefix}{i+1}={line}' for i,line in enumerate(lines))
                code,preset,error=self.parse(data.encode())
                self.assertEqual(code,0,(error.line,error.key))
                code,flat,error=self.parse(f'[preset00]\n{prefix}1=q1=if(1,(2+3),4);'.encode())
                self.assertEqual(code,0,(error.line,error.key))
                block,reference=get(preset),get(flat)
                self.assertEqual(block.lines,3)
                self.assertEqual([(o.op,o.arg,o.value) for o in block.code[:block.count]],
                                 [(o.op,o.arg,o.value) for o in reference.code[:reference.count]])

    def test_multirecord_comments_tokens_and_interleaved_blocks(self):
        code,preset,error=self.parse(b'[preset00]\n'
            b'per_frame_init_1=is_beat=2;\n'
            b'per_frame_1=`q1=is_\n'
            b'per_frame_init_2=q2=0;\n'
            b'per_frame_2=beat; /* opening\n'
            b'per_frame_3=comment */ loop(3, // line comment\n'
            b'per_frame_4=q2+=1;\\\\ Desktop comment\n'
            b'per_frame_5=); q3=1e\n'
            b'per_frame_6=-2;\n')
        self.assertEqual(code,0,(error.line,error.key))
        state=PresetState()
        self.assertEqual(self.evaluate_state(preset,state,0)[0],0)
        self.assertEqual(list(state.frame_q)[:2],[2,3])
        self.assertAlmostEqual(state.frame_q[2],.01)

    def test_empty_statements_and_extended_compiled_programs(self):
        code,preset,error=self.parse(b'[preset00]\nper_frame_1=;;;q1=1;; /* gap */ ;q2=(q1+=2;;;q1*2;);;;;\n')
        self.assertEqual(code,0,(error.line,error.key))
        state=PresetState()
        self.assertEqual(self.evaluate_state(preset,state,0)[0],0)
        self.assertEqual(list(state.frame_q)[:2],[3,6])
        records='\n'.join(f'per_frame_{i}=q1+=1;' for i in range(1,301))
        code,preset,error=self.parse(('[preset00]\n'+records).encode())
        self.assertEqual(code,0,(error.line,error.key))
        self.assertGreater(preset.program.count,512)
        state=PresetState()
        self.assertEqual(self.evaluate_state(preset,state,0)[0],0)
        self.assertEqual(state.frame_q[0],300)
        self.assertEqual(len(state.runtime.memory),MEMORY)
        # More compiled space does not grant unbounded execution time.
        code,preset,_=self.parse(b'[preset00]\nper_frame_1=loop(1000000,q1+=1;);')
        self.assertEqual(code,0)
        self.assertNotEqual(self.evaluate_state(preset,PresetState(),0)[0],0)

    def test_mutable_shape_inputs_do_not_resize_native_loop(self):
        code,preset,error=self.parse(b'[preset00]\nshapecode_0_enabled=1\nshapecode_0_num_inst=2\n'
            b'shape_0_init1=num_inst=99;instance=99;\n'
            b'shape_0_per_frame1=x=instance/num_inst;num_inst=10000;instance=7;y=instance*.1;\n')
        self.assertEqual(code,0,(error.line,error.key))
        for frame in range(2):
            self.assertEqual(self.evaluate_state(preset,PresetState(),frame)[0],0)
            self.assertEqual(self.last_decor.shapes[0].x,0)
            self.assertEqual(self.last_decor.shapes[4].x,.5)
            self.assertAlmostEqual(self.last_decor.shapes[0].y,.7,places=6)
            self.assertFalse(self.last_decor.shapes[5].enabled)

    def test_multirecord_error_locations_and_existing_limits(self):
        code,_,error=self.parse(b'[preset00]\nper_frame_1=q1=(1+\n; unrelated physical line\nper_frame_2=);\n')
        self.assertEqual(code,2)
        self.assertEqual((error.line,error.key),(4,b'per_frame_2'))
        code,preset,error=self.parse(b'[preset00]\nper_frame_1=rot=megabuf(\n; unrelated physical line\nper_frame_2=-1);\n')
        self.assertEqual(code,0)
        state=PresetState();before=bytes(state)
        code,_,error=self.evaluate_state(preset,state,0)
        self.assertEqual(code,2)
        self.assertEqual(error.line,4)
        self.assertEqual(bytes(state),before)
        for body in ('per_frame_1=q1=(\nper_frame_3=2);',
                     'per_frame_1=q1=(\nper_frame_1=2);',
                     'per_frame_1=/* unclosed\nper_frame_2=comment',
                     '\n'.join(f'per_frame_{i}=// comment' for i in range(1,RECORDS+2))):
            self.assertNotEqual(self.parse(('[preset00]\nzoom=1\n'+body).encode())[0],0)
        # Explicit boundary whitespace must NOT be erased into the number 12.
        self.assertNotEqual(self.parse(b'[preset00]\r\nper_frame_1=q1=1 \r\nper_frame_2=2;\r\n')[0],0)
        code,preset,error=self.parse(b'[preset00]\nper_frame_1=q1=1<\nper_frame_2==2;\n')
        self.assertEqual(code,0,(error.line,error.key))
        state=PresetState()
        self.assertEqual(self.evaluate_state(preset,state,0)[0],0)
        self.assertEqual(state.frame_q[0],1)

    def test_multiline_demo_runs(self):
        code,preset,error=self.parse((ROOT/'psp-client/presets/multiline-formula-demo.milk').read_bytes())
        self.assertEqual(code,0,(error.line,error.key))
        state=PresetState()
        for frame in range(600):
            self.assertEqual(self.evaluate_state(preset,state,frame/30)[0],0)

    def test_dense_grid_reserves_wave_budget_and_interpolates(self):
        fn=self.library.md_eval_pixel_grid
        fn.argtypes=[ctypes.POINTER(Preset),ctypes.POINTER(Warp),ctypes.c_float,
                     ctypes.POINTER(Signal),ctypes.POINTER(PresetState),ctypes.POINTER(Warp),ctypes.POINTER(Error)]
        code,preset,error=self.parse(b'[preset00]\nper_pixel_1=counter+=1;dx=x*.1;dy=y*.1;\n'
            b'wavecode_0_samples=1024\nwave_0_per_point1=loop(10000,x=.5;);\n')
        self.assertEqual(code,0,(error.line,error.key))
        for enabled,expected in ((0,GRID_POINTS),(1,81)):
            preset.waves[0].enabled=enabled
            state=PresetState();code,warp,error=self.evaluate_state(preset,state,0)
            self.assertEqual(code,0)
            points=(Warp*GRID_POINTS)()
            before=self.library.pm_frame_remaining()
            self.assertEqual(fn(ctypes.byref(preset),ctypes.byref(warp),0,None,
                ctypes.byref(state),points,ctypes.byref(error)),0)
            self.assertEqual(state.pixel_user[0],expected)
            self.assertLess(self.library.pm_frame_remaining(),before)
            for y in range(GRID+1):
                for x in range(GRID+1):
                    self.assertAlmostEqual(points[y*(GRID+1)+x].dx,.1*x/GRID,places=6)
                    self.assertAlmostEqual(points[y*(GRID+1)+x].dy,.1*y/GRID,places=6)

    def test_desktop_boolean_switches_and_old_motion_default(self):
        code,preset,error=self.parse(b'[preset00]\nbTexWrap=-2\nbWaveDots=3\nbInvert=-1\n')
        self.assertEqual(code,0,(error.line,error.key))
        self.assertEqual(preset.wrap,1)
        self.assertEqual(preset.decor.wave_dots,1)
        self.assertEqual(preset.effects[4],1)
        for fields,alpha in ((b'bMotionVectorsOn=-2\n',1),
                             (b'bMotionVectorsOn=0\n',0),
                             (b'bMotionVectorsOn=1\nmv_a=.25\n',.25),
                             (b'mv_a=.25\nbMotionVectorsOn=1\n',.25)):
            code,preset,error=self.parse(b'[preset00]\n'+fields)
            self.assertEqual(code,0,(error.line,error.key))
            self.assertEqual(preset.motion[0],alpha)
        for fields in (b'bMotionVectorsOn=1\nbMotionVectorsOn=0',b'bMotionVectorsOn=.5',
                       b'bTexWrap=.5',b'bRedBlueStereo=.5',b'bMotionVectorsOn=nan'):
            self.assertNotEqual(self.parse(b'[preset00]\n'+fields)[0],0)

    def test_geiss_export_names_and_ranges(self):
        data=(b'[preset00]\nnWaveMode=5\nfZoomExponent=3.6\nob_a=.2\nib_a=.3\n'
              b'nMotionVectorsX=23.52\nnMotionVectorsY=17.832\nb1n=0\nb1x=1\nb1ed=.25\n'
              b'shapecode_0_enabled=1\nshapecode_0_sides=100\n'
              b'per_frame_1=\nper_frame_2=// comment\nper_frame_3=t2=time*6;\n'
              b'per_frame_4=wave_x=.5+.1*sin(t2);\nper_pixel_1=zoomexp=3.6;\n')
        code,preset,error=self.parse(data)
        self.assertEqual(code,0,(error.line,error.key))
        self.assertAlmostEqual(preset.motion[4],23.52,places=4)
        self.assertEqual(preset.decor.shapes[0].sides,100)
        self.assertEqual(self.evaluate(preset,1)[0],0)
        self.assertNotEqual(self.parse(data+b'ob_alpha=.4\n')[0],0)
        self.assertNotEqual(self.parse(b'[preset00]\nzoom=1\nb1n=nan')[0],0)

    @unittest.skipUnless(os.environ.get('GEISS_PRESET_DIR'), 'External Geiss presets not supplied')
    def test_external_geiss_presets(self):
        base=Path(os.environ['GEISS_PRESET_DIR'])
        fn=self.library.md_eval_pixel_grid
        fn.argtypes=[ctypes.POINTER(Preset),ctypes.POINTER(Warp),ctypes.c_float,
                     ctypes.POINTER(Signal),ctypes.POINTER(PresetState),ctypes.POINTER(Warp),ctypes.POINTER(Error)]
        for name in ('Geiss - Artifact 6d (junky warp distortion).milk','Geiss - Trampoline.milk',
                     'Geiss - Explosion nz+.milk',
                     'Geiss - Cauldron - painterly 3 (saturation remix).milk',
                     'Geiss - Cauldron - painterly 4 (saturation remix).milk',
                     'Geiss - Cauldron - painterly 5 (saturation remix).milk'):
            code,preset,error=self.parse((base/name).read_bytes())
            self.assertEqual(code,0,(name,error.line,error.key))
            state=PresetState();points=(Warp*GRID_POINTS)()
            for frame_no in range(600):
                now=frame_no*.05
                code,warp,error=self.evaluate_state(preset,state,now)
                self.assertEqual(code,0,(name,frame_no,error.line,error.key))
                self.assertEqual(fn(ctypes.byref(preset),ctypes.byref(warp),now,None,
                    ctypes.byref(state),points,ctypes.byref(error)),0,(name,error.line,error.key))
        code,preset,error=self.parse((base/'Geiss - Explosion nz+.milk').read_bytes())
        self.assertEqual(code,0,(error.line,error.key))
        self.assertEqual(preset.shape_instances[1],311)

    def test_reference_wave_gain_and_alpha(self):
        evaluate=self.library.md_eval_preset_visual
        evaluate.argtypes=[ctypes.POINTER(Preset),ctypes.c_float,ctypes.POINTER(Signal),
            ctypes.POINTER(Warp),ctypes.POINTER(ctypes.c_uint),ctypes.POINTER(Decor),ctypes.POINTER(Error)]
        opacity=self.library.md_wave_opacity
        opacity.argtypes=[ctypes.POINTER(Decor),ctypes.c_int,ctypes.c_float,ctypes.c_float,ctypes.c_float]
        opacity.restype=ctypes.c_float
        for value in (-2,0,1,1.255,4.1,46.46,100):
            data=f'[preset00]\nnWaveMode=2\nfWaveAlpha={value}\nfWaveScale=5.552\n'.encode()
            code,preset,error=self.parse(data)
            self.assertEqual(code,0,(error.line,error.key))
            self.assertAlmostEqual(preset.wave_scale,5.552,places=5)
            out,warp,color,error=Decor(),Warp(),ctypes.c_uint(),Error()
            self.assertEqual(evaluate(ctypes.byref(preset),0,None,ctypes.byref(warp),ctypes.byref(color),
                ctypes.byref(out),ctypes.byref(error)),0)
            self.assertAlmostEqual(out.wave_alpha,value,places=5)
            for mode,gain in ((0,1),(1,1.25),(2,.09),(5,.09),(8,1)):
                self.assertAlmostEqual(opacity(ctypes.byref(out),mode,1,1,1),min(1,max(0,value*gain)),places=5)
            self.assertAlmostEqual(opacity(ctypes.byref(out),3,1,1,1),.195,places=5)
        code,preset,error=self.parse(b'[preset00]\nfWaveScale=-2\nper_frame_1=wave_a=4.1;\n')
        self.assertEqual(code,0,(error.line,error.key))
        self.assertEqual(preset.wave_scale,-2)
        self.assertEqual(evaluate(ctypes.byref(preset),0,None,ctypes.byref(warp),ctypes.byref(color),
            ctypes.byref(out),ctypes.byref(error)),0)
        self.assertAlmostEqual(out.wave_alpha,4.1,places=5)
        out.wave_alpha=.2;out.wave_mod_alpha=1;out.wave_mod_start=0;out.wave_mod_end=1
        # Reference multiplies by >1 volume factors BEFORE the final clamp.
        self.assertAlmostEqual(opacity(ctypes.byref(out),0,3,3,3),.2*9*.333,places=5)
        out.wave_mod_start=1;out.wave_mod_end=0
        self.assertAlmostEqual(opacity(ctypes.byref(out),0,.5,.5,.5),.2*(1-1.5*.333),places=5)
        out.wave_mod_start=out.wave_mod_end=.5
        self.assertEqual(opacity(ctypes.byref(out),0,0,0,0),0)
        self.assertAlmostEqual(opacity(ctypes.byref(out),0,1,1,1),.2,places=5)
        for key in ('fWaveScale','fWaveAlpha'):
            for bad in ('nan','inf','-inf','1e99','abc'):
                self.assertNotEqual(self.parse(f'[preset00]\n{key}={bad}\n'.encode())[0],0)

    def test_legacy_effect_fallbacks_are_explicit_not_unknown_field_skipping(self):
        code,preset,error=self.parse(b'[preset00]\nfShader=.75\nbRedBlueStereo=1\n')
        self.assertEqual(code,0,(error.line,error.key))
        self.assertEqual(self.evaluate(preset,0)[0],0)
        self.assertEqual(preset.shader_amount,.75)
        for data in (b'fShader=nan',b'fShader=.5\nfShader=.7',b'fUnknownShader=1',
                     b'bRedBlueStereo=.5'):
            self.assertNotEqual(self.parse(b'[preset00]\n'+data)[0],0)
        code,preset,_=self.parse(b'[preset00]\nbModWaveAlphaByVolume=1\n'
                                b'per_frame_1=wave_mod_start=1; wave_mod_end=0;\n')
        self.assertEqual(code,0)
        self.assertEqual(self.evaluate(preset,0)[0],0)

    def test_reference_legacy_shading(self):
        row=ctypes.c_float*3
        colors=(row*4)()
        phase=(ctypes.c_float*4)(1,2,3,4)
        shade=self.library.md_shader_colors
        shade.argtypes=[ctypes.POINTER(row),ctypes.c_float,ctypes.c_float,
                        ctypes.POINTER(ctypes.c_float)]
        for seconds in (0,12,120):
            for amount in (-1,0,.0005,.5,1,2):
                shade(colors,seconds,amount,phase)
                strength=min(1,max(0,amount))
                for i in range(4):
                    raw=[.6+.3*math.sin(seconds*30*rate+offset+i*step+phase[p])
                         for rate,offset,step,p in ((.0143,3,21,3),(.0107,1,13,1),(.0129,6,9,2))]
                    for k in range(3):
                        expected=1 if strength<=.001 else (.5+.5*raw[k]/max(raw))*strength+1-strength
                        self.assertAlmostEqual(colors[i][k],expected,delta=.00001)
        for amount,expected in ((-2,0),(.75,.75),(4,1)):
            code,preset,_=self.parse(f'[preset00]\nfShader={amount}\n'.encode())
            self.assertEqual(code,0)
            self.assertEqual(preset.shader_amount,expected)
        for name,amount in (('legacy-shading-demo.milk',1),('legacy-shading-off-demo.milk',0)):
            code,preset,error=self.parse((ROOT/'psp-client/presets'/name).read_bytes())
            self.assertEqual(code,0,(error.line,error.key))
            self.assertEqual(preset.shader_amount,amount)
            self.assertAlmostEqual(preset.decor.echo_zoom,.65)
            self.assertEqual(self.evaluate(preset,2)[0],0)

    def test_echo_zoom_below_one(self):
        for value in (.001,.01,.65,1):
            code,preset,error=self.parse(f'[preset00]\nfVideoEchoZoom={value}\n'.encode())
            self.assertEqual(code,0,(error.line,error.key))
            self.assertAlmostEqual(preset.decor.echo_zoom,max(.01,value))
            code,preset,error=self.parse(f'[preset00]\nper_frame_1=echo_zoom={value};\n'.encode())
            self.assertEqual(code,0,(error.line,error.key))
            self.assertEqual(self.evaluate_state(preset,PresetState(),0)[0],0)
            self.assertAlmostEqual(self.last_decor.echo_zoom,max(.01,value))

    def test_reference_wave_scale_geometry(self):
        class Vertex(ctypes.Structure):
            _fields_=[('u',ctypes.c_float),('v',ctypes.c_float),('color',ctypes.c_uint),
                      ('x',ctypes.c_float),('y',ctypes.c_float),('z',ctypes.c_float)]
        draw=self.library.md_wave_circle
        draw.argtypes=[ctypes.POINTER(Vertex),ctypes.POINTER(ctypes.c_short),ctypes.c_float,
                       ctypes.c_float,ctypes.c_float,ctypes.c_float,ctypes.c_uint]
        samples=(ctypes.c_short*576)(*[int(math.sin(i*.1)*15000) for i in range(576)])
        baseline,unit,large=(Vertex*241)(),(Vertex*241)(),(Vertex*241)()
        draw(baseline,samples,0,.75,7,.5625,0xffffffff)
        draw(unit,samples,1,.75,7,.5625,0xffffffff)
        for scale in (5.552,-2):
            draw(large,samples,scale,.75,7,.5625,0xffffffff)
            for i in range(240):
                self.assertAlmostEqual(large[i].x-baseline[i].x,
                    (unit[i].x-baseline[i].x)*scale,delta=.0002)
                self.assertAlmostEqual(large[i].y-baseline[i].y,
                    (unit[i].y-baseline[i].y)*scale,delta=.0002)

    def test_external_texture_paths(self):
        result, preset, _ = self.parse(b'[preset00]\nzoom=1\npsp_texture_0=checker.png\n')
        self.assertEqual(result, 0)
        self.assertEqual(bytes(preset.texture_path[0]).split(b'\0')[0],
                         str(self.root / 'textures/checker.png').encode())
        for value in (b'../secret.png', b'/secret.png', b'ms0:secret.png', b'a\\b.png',
                      b'foo.gif', b'x'*128+b'.png', b'a\x01.png'):
            self.assertNotEqual(self.parse(b'[preset00]\nzoom=1\npsp_texture_0='+value+b'\n')[0],0)
        self.assertNotEqual(self.parse(b'[preset00]\nzoom=1\npsp_texture_0=a.png\npsp_texture_0=b.png\n')[0],0)
        self.assertNotEqual(self.parse(b'[preset00]\nzoom=1\npsp_texture_4=a.png\n')[0],0)
        for name in (b'foo.jpg',b'foo.JPEG',b'foo.PNG'):
            self.assertEqual(self.parse(b'[preset00]\nzoom=1\npsp_texture_0='+name+b'\n')[0],0)
    def execute_eel(self, source, runtime=None, success=True):
        program,symbols=Program(),Symbols()
        compile_fn=self.library.pm_compile_symbols
        compile_fn.argtypes=[ctypes.POINTER(Program),ctypes.c_char_p,ctypes.c_int,ctypes.POINTER(Symbols)]
        self.assertEqual(compile_fn(ctypes.byref(program),source.encode(),7,ctypes.byref(symbols)),0,source)
        values=(ctypes.c_float*VALUES)();error=ctypes.c_int()
        runtime=runtime if runtime is not None else Runtime()
        fn=self.library.pm_execute_runtime
        fn.argtypes=[ctypes.POINTER(Program),ctypes.POINTER(ctypes.c_float),ctypes.POINTER(ctypes.c_int),ctypes.POINTER(Runtime)]
        self.library.pm_begin_frame()
        result=fn(ctypes.byref(program),values,ctypes.byref(error),ctypes.byref(runtime))
        self.assertEqual(result,int(success),source)
        if not success: self.assertEqual(bytes(values),bytes((ctypes.c_float*VALUES)()),source)
        return {bytes(symbols.names[i]).split(b'\0')[0].decode():values[87+i] for i in range(symbols.count)},runtime

    def test_eel_operators_constants_and_lazy_sequences(self):
        cases={
            '2^3^2':512, '17%5':2, '-17%5':2, '4%0':0,
            '(6&3)|8':10, '2<=2':1, '2>=3':0, '2!=3':1, '2==2':1,
            '!3':0, '!0':1, '0 && 1/0':0, '1 || 1/0':1,
            '0?1/0:3':3, '1?2:1/0':2, '0?1:0?2:3':3,
            'exec2(a=1;a+=2,a*=2;a)':6, 'exec3(a=1,a+=1,a^=3)':8,
            'if(1,a=2;a+=1,1/0)':3, 'invsqrt(4)':.5,
            'assign(a,3);a':3,'assign(megabuf(3),7);megabuf(3)':7,'int(-1.2)':-2,
            '$PI':math.pi,'$e':math.e,'$phi':(1+math.sqrt(5))/2,
            '$xFF':255,"$'A'":65,
        }
        for expression,expected in cases.items():
            values,_=self.execute_eel('result='+expression+';')
            self.assertAlmostEqual(values['result'],expected,places=5,msg=expression)
        values,_=self.execute_eel('CoUnTeR=8;counter/=2;counter%=3;counter|=8;counter&=9; result=COUNTER; // note')
        self.assertEqual(values['result'],9)
        values,_=self.execute_eel('a=1; /* inline */ b=a=3;result=a+b;')
        self.assertEqual(values['result'],6)

    def test_eel_exceptional_intermediates_and_assignment(self):
        cases={
            '1/0':0,'-1/0':0,'0/0':0,'1/(1/0)':0,
            'min(1/0,3)':3,'max(-1/0,4)':4,
            'above(1/0,2)':1,'below(-1/0,0)':1,
            'if(0/0,9,3)':3,'bnot(0/0)':1,
            'equal(0/0,2)':1,'below(0/0,2)':1,'above(0/0,2)':1,
            '(0/0)<=2':0,'(0/0)>=2':0,'(0/0)!=2':0,
            'min(0/0,3)':0,'min(3,0/0)':3,
            'max(0/0,3)':3,'max(3,0/0)':0,
            'above(a=1/0,2)':1,'above(assign(a,1/0),2)':1,
            '1e-30*1e-10':0,
        }
        for expression,expected in cases.items():
            values,_=self.execute_eel('result='+expression+';')
            self.assertEqual(values['result'],expected,expression)
            if 'a' in values:self.assertEqual(values['a'],0)
        values,_=self.execute_eel('a=1;a/=0;b=above(a,2);c=a;d=0;d/=0;e=if(d,9,3);')
        self.assertTrue(math.isinf(values['a']))
        self.assertTrue(math.isnan(values['d']))
        self.assertEqual((values['b'],values['c'],values['e']),(1,0,3))
        self.library.pm_reset_globals()
        values,runtime=self.execute_eel('reg00=1/0;megabuf(0)=0/0;gmegabuf(0)=1/0;'
            'a=reg00+megabuf(0)+gmegabuf(0);megabuf(1)=1;megabuf(1)/=0;'
            'b=above(megabuf(1),2);c=megabuf(1);')
        self.assertEqual((values['a'],values['b'],values['c']),(0,1,0))
        self.assertTrue(math.isinf(runtime.memory[1]))
        for expression in ('megabuf(1/0)','gmegabuf(0/0)','loop(0/0,1)',
                           'loop(1/0,1)','memcpy(0,0,0/0)','memset(0,1,1/0)'):
            self.execute_eel('a=7;result='+expression+';',success=False)

    def test_eel_x87_integer_indefinite(self):
        cases={
            '(0/0)&1':0,'(1/0)&7':0,'(-1/0)&7':0,'(1e30)&2':0,
            '(1/0)|0':-2**63,'(-1/0)|0':-2**63,'(0/0)|0':-2**63,
            '(2^63)|0':-2**63,'(-2^63)|0':-2**63,
            '(0/0)%2':0,'(1/0)%3':2,'(2^31)%3':2,'(2^32)%3':2,
            '7%(0/0)':7,'7%(1/0)':7,'7%0':0,
            '-7.9%3.2':1,'(-7.9)&3.2':1,
        }
        for expression,expected in cases.items():
            values,_=self.execute_eel('result='+expression+';')
            self.assertEqual(values['result'],expected,expression)

    def test_eel_loops_and_runtime_budget(self):
        values,_=self.execute_eel('a=0;loop(5,a+=1);result=a;')
        self.assertEqual(values['result'],5)
        values,_=self.execute_eel('a=0;loop(3,loop(4,a+=1));result=a;')
        self.assertEqual(values['result'],12)
        values,_=self.execute_eel('a=0;result=loop(0,a=9);while(a+=1;a<5);result+=a;')
        self.assertEqual(values['result'],5)
        values,_=self.execute_eel('a=0;loop(-2,a=7);result=a;')
        self.assertEqual(values['result'],0)
        self.execute_eel('a=0;while(a+=1;1);',success=False)
        self.execute_eel('loop(1e30,1);',success=False)
        self.execute_eel('a=1;loop(2,1/0);')

    def test_eel_memory_registers_scope_overlap_and_rollback(self):
        self.library.pm_reset_globals()
        first=Runtime()
        values,_=self.execute_eel('reg00=3;reg99=7;gmegabuf(2)=9;megabuf(0)=4;0[1]=5;'
                                'megabuf(2)=6;memcpy(1,0,3);result=megabuf(1)+0[2]+0[3];',first)
        self.assertEqual(values['result'],15)
        values,_=self.execute_eel('result=reg00+reg99+gmegabuf(2)+megabuf(0);',Runtime())
        self.assertEqual(values['result'],19)  # local memory is not shared
        snapshot=bytes(first)
        self.execute_eel('reg00=99;gmegabuf(2)=99;megabuf(0)=99;rand(2);a=megabuf(-1);',first,False)
        self.assertEqual(bytes(first),snapshot)
        values,_=self.execute_eel('result=reg00+gmegabuf(2)+megabuf(0);',first)
        self.assertEqual(values['result'],16)
        values,_=self.execute_eel('memset(4,2,5);megabuf(4)+=3;freembuf(0);result=4[0]+megabuf(8);',first)
        self.assertEqual(values['result'],7)
        for source in (f'a=megabuf({MEMORY});','megabuf(-1)=2;','gmegabuf(1e30)=0;',
                       f'memcpy({MEMORY-1},0,2);',f'memset(0,1,{MEMORY+1});'):
            before=bytes(first)
            self.execute_eel(source,first,False)
            self.assertEqual(bytes(first),before)

    def test_sparse_variable_journal_first_write_and_failure(self):
        compile_fn=self.library.pm_compile_symbols
        compile_fn.argtypes=[ctypes.POINTER(Program),ctypes.c_char_p,ctypes.c_int,ctypes.POINTER(Symbols)]
        fn=self.library.pm_execute_runtime
        fn.argtypes=[ctypes.POINTER(Program),ctypes.POINTER(ctypes.c_float),ctypes.POINTER(ctypes.c_int),ctypes.POINTER(Runtime)]
        prefix=''.join(f'q{i}=q{i}+1;' for i in range(1,33))
        prefix+=''.join(f'user{i}={i};' for i in range(64))
        prefix+='loop(10,q1=q1+1);reg00=42;megabuf(0)=77;rand(2);'
        for ending in ('','q2=megabuf(-1);','loop(4096,q1=q1+1);'):
            program,symbols=Program(),Symbols()
            self.assertEqual(compile_fn(ctypes.byref(program),(prefix+ending).encode(),9,ctypes.byref(symbols)),0)
            values=(ctypes.c_float*VALUES)(*([1.25]*VALUES));before=bytes(values)
            runtime=Runtime();runtime.random=123;runtime.memory[0]=3
            old_runtime=bytes(runtime);error=ctypes.c_int()
            self.library.pm_reset_globals();self.library.pm_begin_frame()
            self.assertEqual(fn(ctypes.byref(program),values,ctypes.byref(error),ctypes.byref(runtime)),int(not ending))
            if ending:
                self.assertEqual(bytes(values),before)
                self.assertEqual(bytes(runtime),old_runtime)
                self.assertEqual(self.execute_eel('result=reg00;')[0]['result'],0)
            else:
                self.assertEqual(values[55],12.25)
                self.assertEqual(values[86],2.25)
                self.assertEqual(values[87+63],63)
                self.assertEqual(bytes(values)[151*4:],before[151*4:])

    def test_eel_random_engine_inputs_and_monitor_persistence(self):
        runtime=Runtime()
        values=[]
        for _ in range(16):
            result,_=self.execute_eel('result=rand(3.9);',runtime)
            self.assertTrue(0<=result['result']<=3)
            values.append(result['result'])
        self.assertGreater(len(set(values)),12)
        code,preset,_=self.parse(b'[preset00]\nper_frame_init_1=monitor=2;\n'
            b'per_frame_1=monitor+=1;wave_usedots=1;wrap=0;q1=pixelsx;q2=pixelsy;q3=aspecty;q4=meshx;\n')
        self.assertEqual(code,0)
        state=PresetState()
        for frame in range(3):
            self.assertEqual(self.evaluate_state(preset,state,frame)[0],0)
            self.assertEqual(state.monitor,frame+3)
            self.assertEqual(state.wrap,0)
            self.assertEqual(self.last_decor.wave_dots,1)
            self.assertEqual(state.frame_q[0],480)
            self.assertEqual(state.frame_q[1],272)
            self.assertAlmostEqual(state.frame_q[2],480/272,places=5)
            self.assertEqual(state.frame_q[3],GRID)

    def test_extended_memory_boundaries_and_rollback(self):
        self.assertGreaterEqual(MEMORY,2048)
        self.library.pm_reset_globals()
        runtime=Runtime()
        result,_=self.execute_eel(f'megabuf(1024)=3;megabuf({MEMORY-1})=7;'
            f'gmegabuf({MEMORY-1})=11;result=megabuf(1024)+megabuf({MEMORY-1});',runtime)
        self.assertEqual(result['result'],10)
        self.assertEqual(self.execute_eel(f'result=gmegabuf({MEMORY-1});')[0]['result'],11)
        before=bytes(runtime)
        self.execute_eel(f'megabuf({MEMORY-1})=99;gmegabuf({MEMORY-1})=99;result=megabuf(-1);',runtime,False)
        self.assertEqual(bytes(runtime),before)
        self.assertEqual(self.execute_eel(f'result=gmegabuf({MEMORY-1});')[0]['result'],11)
        result,_=self.execute_eel('memset(1024,2,8);memcpy(1025,1024,7);result=megabuf(1031);',runtime)
        self.assertEqual(result['result'],2)

    def test_extended_point_program_space(self):
        program, symbols=Program(),Symbols()
        compile_fn=self.library.pm_compile_wave
        compile_fn.argtypes=[ctypes.POINTER(Program),ctypes.c_char_p,ctypes.c_int,ctypes.POINTER(Symbols),ctypes.c_int]
        code=b';'.join([b'x=x+.001']*220)+b';'
        self.assertEqual(compile_fn(ctypes.byref(program),code,1,ctypes.byref(symbols),1),0)
        self.assertGreater(program.count,1024)
        values=(ctypes.c_float*VALUES)();runtime=Runtime();error=ctypes.c_int()
        fn=self.library.pm_execute_runtime
        fn.argtypes=[ctypes.POINTER(Program),ctypes.POINTER(ctypes.c_float),ctypes.POINTER(ctypes.c_int),ctypes.POINTER(Runtime)]
        self.library.pm_begin_frame()
        self.assertEqual(fn(ctypes.byref(program),values,ctypes.byref(error),ctypes.byref(runtime)),1)
        before=b''.join(bytes(op) for op in program.code[:program.count])
        self.assertNotEqual(compile_fn(ctypes.byref(program),code*4,2,ctypes.byref(symbols),1),0)
        self.assertEqual(b''.join(bytes(op) for op in program.code[:program.count]),before)

    def test_formula_storage_is_bounded_and_only_nonempty_blocks_allocate(self):
        code,preset,_=self.parse(b'[preset00]\nzoom=1\n')
        self.assertEqual(code,0)
        self.assertFalse(preset.program.code)
        self.assertFalse(preset.waves[3].point.code)
        lines=['[preset00]','zoom=1']
        # Large single program now fits without raising per-execution fuel.
        for i in range(1,31):lines.append(f'per_frame_{i}='+('q1=q1+.001;'*20))
        code,preset,error=self.parse('\n'.join(lines).encode())
        self.assertEqual(code,0,(error.key,error.line))
        self.assertGreater(preset.program.count,2048)
        self.assertEqual(preset.program.capacity,preset.program.count)
        self.assertEqual(self.evaluate(preset,0)[0],0)
        # Nine individually legal contexts exceed the preset-wide storage cap.
        lines=['[preset00]','zoom=1']
        prefixes=['per_frame_','per_frame_init_','per_pixel_']
        prefixes += [f'shape_{i}_per_frame' for i in range(4)]
        prefixes += [f'wave_{i}_per_point' for i in range(2)]
        for prefix in prefixes:
            for i in range(1,31):lines.append(prefix+str(i)+'='+('q1=q1+.001;'*20))
        code,_,error=self.parse('\n'.join(lines).encode())
        self.assertEqual(code,2)
        self.assertEqual(error.key,b'total formula storage')

    def test_eel_frame_budget_is_shared_and_stays_exhausted(self):
        program,symbols=Program(),Symbols()
        compile_fn=self.library.pm_compile_symbols
        compile_fn.argtypes=[ctypes.POINTER(Program),ctypes.c_char_p,ctypes.c_int,ctypes.POINTER(Symbols)]
        self.assertEqual(compile_fn(ctypes.byref(program),b'memset(0,1,1024);',1,ctypes.byref(symbols)),0)
        execute=self.library.pm_execute_runtime
        execute.argtypes=[ctypes.POINTER(Program),ctypes.POINTER(ctypes.c_float),ctypes.POINTER(ctypes.c_int),ctypes.POINTER(Runtime)]
        runtime=Runtime();values=(ctypes.c_float*VALUES)();error=ctypes.c_int()
        self.library.pm_begin_frame()
        for attempts in range(300):
            if not execute(ctypes.byref(program),values,ctypes.byref(error),ctypes.byref(runtime)):break
        else:self.fail('Aggregate frame budget was not enforced')
        self.assertGreater(attempts,200)
        self.assertEqual(execute(ctypes.byref(program),values,ctypes.byref(error),ctypes.byref(runtime)),0)
        self.library.pm_begin_frame()
        self.assertEqual(execute(ctypes.byref(program),values,ctypes.byref(error),ctypes.byref(runtime)),1)

    def test_eel_pixel_state_is_independent_and_persistent(self):
        code,preset,_=self.parse(b'[preset00]\nper_frame_init_1=q1=0;\n'
            b'per_pixel_1=counter+=1;q1+=1;megabuf(0)+=1;dx=q1*.001;dy=counter*.0001;\n')
        self.assertEqual(code,0)
        fn=self.library.md_eval_pixel_grid
        fn.argtypes=[ctypes.POINTER(Preset),ctypes.POINTER(Warp),ctypes.c_float,
                     ctypes.POINTER(Signal),ctypes.POINTER(PresetState),ctypes.POINTER(Warp),ctypes.POINTER(Error)]
        state=PresetState();points=(Warp*GRID_POINTS)();error=Error()
        for frame_number in range(2):
            _,frame,_=self.evaluate_state(preset,state,frame_number)
            self.assertEqual(fn(ctypes.byref(preset),ctypes.byref(frame),frame_number,None,
                                ctypes.byref(state),points,ctypes.byref(error)),0)
            self.assertAlmostEqual(points[GRID_POINTS-1].dx,.001*GRID_POINTS,places=5)
            self.assertAlmostEqual(points[GRID_POINTS-1].dy,.0001*GRID_POINTS*(frame_number+1),places=5)
            self.assertEqual(state.pixel_runtime.memory[0],GRID_POINTS*(frame_number+1))
            self.assertEqual(state.frame_q[0],0)

    def test_expanded_file_and_line_limits(self):
        self.assertEqual(self.parse(b'[preset00]\nzoom=1\n'+b';note\n'*4000)[0],0)
        self.assertEqual(self.parse(b'[preset00]\nzoom=1\n'+b';note\n'*12000)[0],2)
        self.assertEqual(self.parse(b'[preset00]\nzoom=1\n;'+b'a'*2047)[0],2)

    def test_whitespace_bom_crlf_and_defaults(self):
        result, preset, _ = self.parse(b"\xef\xbb\xbf; note\r\n [preset00] \r\n zoom = 1.02 \r\n// comment\n")
        self.assertEqual(result, 0)
        self.assertAlmostEqual(preset.warp.zoom, 1.02, places=5)
        self.assertAlmostEqual(preset.warp.decay, .97, places=5)

    def test_unsupported_fields_are_not_ignored(self):
        for key in ("per_point_1", "warp_bad", "comp_1_typo", "warp_0",
                    "unknown", "wavecode_0_unknown"):
            result, _, error = self.parse(("[preset00]\nzoom=1\n" + key + "=0\n").encode())
            self.assertEqual(result, 3)
            self.assertEqual(error.line, 3)
            self.assertEqual(error.key.decode(), key)

    def test_known_desktop_ignored_numeric_fields(self):
        base=b'[preset00]\nzoom=1.02\nnWaveMode=0\n'
        code,expected,_=self.parse(base)
        self.assertEqual(code,0)
        keys=['nEchoWrap_x','nEchoWrap_y','nWrapMode_x','nWrapMode_y']
        for slot in range(4):
            keys.extend(f'wavecode_{slot}_{field}' for field in ('bDrawBack','x','y'))
            keys.extend(f'shapecode_{slot}_{field}' for field in
                        ('tex_capture','tex_cx','tex_cy','bDrawBack','x_wrap_mode','y_wrap_mode'))
        for key in keys:
            for value in ('0','1','-2.5'):
                code,actual,error=self.parse(base+f'{key}={value}\n'.encode())
                self.assertEqual(code,0,(key,error.key))
                self.assertEqual(bytes(actual),bytes(expected),key)
            for value in ('nan','inf','1e99','1oops',''):
                self.assertEqual(self.parse(base+f'{key}={value}\n'.encode())[0],2,key)
        for key in ('nEchoWrap_z','wavecode_0_bDrawBack_typo','shapecode_0_tex_capture_typo'):
            self.assertNotEqual(self.parse(base+f'{key}=0\n'.encode())[0],0,key)
        self.assertNotEqual(self.parse(b'[preset00]\nnEchoWrap_x=0\n')[0],0)

    def test_extra_desktop_slots_are_inert(self):
        base=b'[preset00]\nzoom=1.02\n'
        _,expected,_=self.parse(base)
        for slot in ('4','9','10','999999999999999999999999'):
            for key in (f'wavecode_{slot}_enabled',f'shapecode_{slot}_enabled',
                        f'wave_{slot}_per_point1',f'shape_{slot}_init1'):
                code,actual,error=self.parse(base+f'{key}=not executable!\n'.encode())
                self.assertEqual(code,0,(key,error.key))
                self.assertEqual(bytes(actual),bytes(expected))
        for key in ('wavecode_-4_enabled','wavecode_4_', 'shape_4bad_init1','wavecode_4_x!'):
            self.assertNotEqual(self.parse(base+f'{key}=0\n'.encode())[0],0)
        self.assertNotEqual(self.parse(b'[preset00]\nwavecode_4_enabled=1\n')[0],0)

    def test_shader_fallback_preserves_nonshader_preset(self):
        self.assertEqual(self.parse((ROOT / 'psp-client/presets/shader-fallback-demo.milk').read_bytes())[0], 0)
        base = b'[preset00]\nzoom=1.02\nnWaveMode=0\nper_frame_1=rot=sin(time)*0.1;\n'
        headers = b'MILKDROP_PRESET_VERSION=201\nPSVERSION=3\nPSVERSION_WARP=2\nPSVERSION_COMP=3\n'
        shaders = b'warp_1=`shader_body {\nwarp_2=`ret=tex2D(sampler_main,uv).xyz;\nwarp_3=`}\ncomp_1=`invalid HLSL deliberately ignored\n'
        code, expected, _ = self.parse(headers + base)
        self.assertEqual(code, 0)
        code, actual, error = self.parse(headers + base + shaders)
        self.assertEqual(code, 0, error.key)
        def fingerprint(p):
            blocks=[p.program,p.init_program,p.pixel_program]
            for shape in p.shape_program:blocks.extend((shape.init,shape.frame))
            for wave in p.waves:blocks.extend((wave.init,wave.frame,wave.point))
            raw=bytearray(bytes(p));compiled=[]
            for block in blocks:
                offset=ctypes.addressof(block)-ctypes.addressof(p)+Program.code.offset
                raw[offset:offset+ctypes.sizeof(ctypes.c_void_p)]=bytes(ctypes.sizeof(ctypes.c_void_p))
                compiled.append(b''.join(bytes(op) for op in block.code[:block.count]))
            return raw,compiled
        self.assertEqual(fingerprint(actual), fingerprint(expected))
        for suffix in (b'unknown=1\n', b'per_frame_2=rot=unknown_function(time);\n',
                       b'psp_texture_0=../bad.png\n', b'zoom=nan\n'):
            self.assertNotEqual(self.parse(headers + base + shaders + suffix)[0], 0)
        for value in (b'abc', b'-1', b'2junk', b''):
            self.assertEqual(self.parse(b'PSVERSION='+value+b'\n'+base)[0], 2)
        self.assertNotEqual(self.parse(headers+b'[preset00]\n'+shaders)[0], 0)
        self.assertEqual(self.parse(base+b'warp_1='+b'x'*2048)[0], 2)

    def test_invalid_numbers_duplicates_and_sections(self):
        for value in ("nan", "inf", "-inf", "1e99", "1e-999", "", "1 + bass",
                      "1; trailing"):
            result, _, error = self.parse(("[preset00]\nzoom=" + value).encode())
            self.assertEqual(result, 2, value)
            self.assertEqual(error.line, 2)
        for data in (b"", b"[preset00]", b"zoom=1", b"[preset00]\nzoom=1\nzoom=1",
                     b"[preset00]\nzoom=1\n[preset00]", b"[preset01]\nzoom=1"):
            self.assertEqual(self.parse(data)[0], 2)

    def test_bounds_for_every_supported_field(self):
        for key, low, high in (("zoom", .01, 100), ("rot", -100, 100), ("warp", -100, 100),
                               ("fWarpAnimSpeed", -100, 100), ("fWarpScale", .01, 100),
                               ("fDecay", 0, 1), ("wave_r", 0, 1),
                               ("wave_g", 0, 1), ("wave_b", 0, 1)):
            for value in (low, high):
                self.assertEqual(self.parse(f"[preset00]\n{key}={value}".encode())[0], 0)
            for value in (low-.01, high+.01):
                code,preset,_=self.parse(f"[preset00]\n{key}={value}".encode())
                self.assertEqual(code,0)
                fields={'zoom':preset.warp.zoom,'rot':preset.warp.rotation,'warp':preset.warp.warp,
                        'fWarpAnimSpeed':preset.warp.warp_speed,'fWarpScale':preset.warp.warp_scale,
                        'fDecay':preset.warp.decay,'wave_r':preset.red,'wave_g':preset.green,'wave_b':preset.blue}
                self.assertAlmostEqual(fields[key],min(high,max(low,value)),places=5)

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
        for source in ("rot==;", "rot=;", "rot=(1;", "rot=nan;",
                       "psp_low=0;", "rot=unknown_func(1);", "rot=1e99;",
                       "rot=" + "("*70 + "0" + ")"*70 + ";",
                       "rot=" + "+".join(["0"]*1100) + ";"):
            self.assertNotEqual(self.parse(f"[preset00]\nper_frame_1={source}".encode())[0], 0)
        for lines in ("per_frame_2=rot=0;", "per_frame_1=rot=0;\nper_frame_1=rot=0;",
                      "\n".join(f"per_frame_{i}=rot=0;" for i in range(1,RECORDS+2))):
            self.assertEqual(self.parse(f"[preset00]\n{lines}".encode())[0], 2)

    def test_finite_render_limits_are_silent_and_state_keeps_advancing(self):
        code,preset,error=self.parse(b'''[preset00]
nWaveMode=99
shapecode_0_enabled=1
shapecode_0_num_inst=1000000000
wavecode_0_samples=1000000000
wavecode_0_sep=1000000000
per_frame_1=counter=counter+1; zoom=0; rot=99; mv_a=20; gamma=99; wave_a=20;
shape_0_per_frame1=x=99; rad=99; sides=1e30; tex_zoom=0; a=99;
per_pixel_1=zoom=0; rot=-99; sx=0; cx=99;
''')
        self.assertEqual(code,0,(error.line,error.key))
        self.assertEqual(preset.wave_mode,8)
        self.assertEqual(preset.shape_instances[0],SHAPE_INSTANCES)
        self.assertEqual(preset.waves[0].samples,CUSTOM_POINTS)
        self.assertEqual(preset.waves[0].sep,128)
        state=PresetState()
        for frame in range(20):
            code,warp,error=self.evaluate_state(preset,state,frame)
            self.assertEqual(code,0,(error.line,error.key))
            self.assertEqual(state.user[0],frame+1)
            self.assertAlmostEqual(warp.zoom,.01)
            self.assertAlmostEqual(warp.rotation,99)
            self.assertEqual(self.last_decor.gamma,4)
            self.assertEqual(self.last_decor.wave_alpha,20)
            for i in (0,4,5,6,7,8,9,10):
                shape=self.last_decor.shapes[i]
                self.assertEqual((shape.x,shape.rad,shape.a),(4,4,99))
                self.assertAlmostEqual(shape.tex_zoom,.1)
            points=(Warp*GRID_POINTS)()
            fn=self.library.md_eval_pixel_grid
            fn.argtypes=[ctypes.POINTER(Preset),ctypes.POINTER(Warp),ctypes.c_float,
                         ctypes.POINTER(Signal),ctypes.POINTER(PresetState),ctypes.POINTER(Warp),ctypes.POINTER(Error)]
            self.assertEqual(fn(ctypes.byref(preset),ctypes.byref(warp),frame,None,
                                ctypes.byref(state),points,ctypes.byref(error)),0)
            self.assertAlmostEqual(points[0].zoom,.01)
            self.assertAlmostEqual(points[0].sx,.01)
            self.assertAlmostEqual(points[0].cx,4)

    def test_coordinates_are_ordinary_global_locals(self):
        code,preset,error=self.parse(b'[preset00]\nper_frame_init_1=rad=2; x=3; y=4;\n'
                                    b'per_frame_1=ang=time*2; q1=cos(ang); q2=rad+x+y;\n')
        self.assertEqual(code,0,(error.line,error.key))
        state=PresetState()
        for now in (0,.5,2,50):
            self.assertEqual(self.evaluate_state(preset,state,now)[0],0)
            self.assertAlmostEqual(state.frame_q[0],math.cos(now*2),places=5)
            self.assertEqual(state.frame_q[1],9)
        for name in ('x','y','rad','ang'):
            self.assertEqual(self.parse(f'[preset00]\nper_pixel_1={name}=0;'.encode())[0],0)

    def test_desktop_names_resolve_per_context(self):
        # Names with register-like prefixes are normal variables unless they
        # exactly identify a register. Case folding must not split state.
        code,preset,error=self.parse(b'[preset00]\nper_frame_init_1=Q33=1; q01=2; reg100=3; t9=4; sample=5;\n'
                                    b'per_frame_1=q1=q33+Q01+REG100+T9+sample;\n')
        self.assertEqual(code,0,(error.line,error.key))
        state=PresetState()
        self.assertEqual(self.evaluate_state(preset,state,0)[0],0)
        self.assertEqual(state.frame_q[0],15)
        for prefix,locals_ in (
                ('per_frame_',('x','y','rad','ang','sample','samples','value1','value2','q0','q33','q1x','reg100')),
                ('per_pixel_',('wave_r','wave_a','sample','samples','value1','q33','t9','reg100')),
                ('shape_0_per_frame',('zoom','wave_r','sample','samples','value1','t9','reg100')),
                ('wave_0_per_frame',('zoom','x','y','rad','ang','sample','value1','t9','reg100')),
                ('wave_0_per_point',('zoom','rad','ang','samples','t9','reg100'))):
            for name in locals_:
                code,preset,error=self.parse(f'[preset00]\n{prefix}1={name}=2;'.encode())
                self.assertEqual(code,0,(prefix,name,error.line,error.key))
        # Existing meanings remain unchanged: shape output, pixel coordinate,
        # wave point input and global frame variable cannot alias each other.
        code,preset,error=self.parse(b'[preset00]\nper_frame_1=zoom=1;\n'
            b'shapecode_0_enabled=1\nshape_0_per_frame1=zoom=zoom+.1; x=zoom;\n')
        self.assertEqual(code,0,(error.line,error.key))
        state=PresetState()
        for i in range(1,4):
            self.assertEqual(self.evaluate_state(preset,state,i)[0],0)
            self.assertAlmostEqual(self.last_decor.shapes[0].x,i*.1)

    def test_runtime_errors_are_atomic(self):
        for source in ("rot=megabuf(1/(time-1));", "warp=1;warp*=3e38*3e38;"):
            result, preset, _ = self.parse(f"[preset00]\nper_frame_1={source}".encode())
            self.assertEqual(result, 0)
            code, _, _, error = self.evaluate(preset, 1)
            self.assertEqual(code, 2)
            self.assertEqual(error.line, 2)

    def test_formula_fuzz_and_total_instruction_limit(self):
        # Short valid lines individually fit; their combined bytecode must not.
        lines = "\n".join(f"per_frame_{i}=rot=0+0+0+0+0;" for i in range(1,MAX_OPS//10+2))
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

    def test_psp_extension_inputs_stay_read_only(self):
        names = ("psp_low", "psp_mid", "psp_high", "psp_level",
                 "psp_low_smooth", "psp_mid_smooth", "psp_high_smooth")
        signal = Signal((ctypes.c_float * 13)(.1, .2, .3, .4, .5, .6, .7))
        for i, name in enumerate(names):
            code, preset, _ = self.parse(f"[preset00]\nper_frame_1=warp={name};".encode())
            self.assertEqual(code, 0)
            self.assertAlmostEqual(self.evaluate(preset, 0, signal)[1].warp, (i+1)/10, places=6)
            self.assertEqual(self.parse(f"[preset00]\nper_frame_1={name}=0;".encode())[0], 3)
        for name in ("bass", "mid", "treb", "bass_att", "mid_att", "treb_att"):
            self.assertEqual(self.parse(f"[preset00]\nper_frame_1={name}=0;".encode())[0], 0)
        for bad in (float("nan"), float("inf"), -1, 1.1):
            signal.values[0] = bad
            self.assertEqual(self.evaluate(preset, 0, signal)[0], 2)

    def test_extended_warp_ranges_stay_finite(self):
        class Vertex(ctypes.Structure):
            _fields_=[('u',ctypes.c_float),('v',ctypes.c_float),('color',ctypes.c_uint),
                      ('x',ctypes.c_float),('y',ctypes.c_float),('z',ctypes.c_float)]
        fn=self.library.md_warp_mesh
        fn.argtypes=[ctypes.POINTER(Vertex),ctypes.POINTER(Warp),ctypes.c_float]
        mesh=(Vertex*(GRID*GRID*6))()
        for zoom in (.01,.1,1,10,100):
            for exponent in (.01,1,100):
                for stretch in (.01,1,100):
                    code,preset,error=self.parse(f'''[preset00]
zoom={zoom}
fZoomExponent={exponent}
warp=100
fWarpAnimSpeed=-100
fWarpScale=.01
sx={stretch}
sy={stretch}
cx=-4
cy=4
dx=-4
dy=4
'''.encode())
                    self.assertEqual(code,0,(error.line,error.key))
                    self.assertAlmostEqual(preset.warp.zoom,zoom,places=5)
                    fn(mesh,ctypes.byref(preset.warp),123.5)
                    for v in mesh:
                        self.assertTrue(math.isfinite(v.u) and -4096<=v.u<=4096)
                        self.assertTrue(math.isfinite(v.v) and -4096<=v.v<=4096)

    def test_offscreen_triangle_and_border_clipping(self):
        class Vertex(ctypes.Structure):
            _fields_=[('u',ctypes.c_float),('v',ctypes.c_float),('color',ctypes.c_uint),
                      ('x',ctypes.c_float),('y',ctypes.c_float),('z',ctypes.c_float)]
        triangle=self.library.md_clip_triangle;segment=self.library.md_clip_segment
        for fn in (triangle,segment):
            fn.argtypes=[ctypes.POINTER(Vertex),ctypes.POINTER(Vertex),ctypes.c_float,ctypes.c_float]
        def vertex(x,y):return Vertex(x*.25+5,y*.5+7,0xffabcdef,x,y,0)
        def area(out,n):
            return sum(abs((out[i+1].x-out[i].x)*(out[i+2].y-out[i].y)-
                           (out[i+2].x-out[i].x)*(out[i+1].y-out[i].y))*.5 for i in range(0,n,3))
        out=(Vertex*15)()
        src=(Vertex*3)(vertex(-10,0),vertex(10,0),vertex(0,20))
        n=triangle(out,src,512,256);self.assertAlmostEqual(area(out,n),100)
        src[0].color=0xff0000ff;src[1].color=0xffff0000;src[2].color=0xff00ff00
        n=triangle(out,src,512,256)
        crossings=[v for v in out[:n] if v.x==0 and v.y==0]
        self.assertTrue(crossings)
        self.assertTrue(all(v.color==0xff800080 for v in crossings))
        src=(Vertex*3)(vertex(-100,-100),vertex(200,-100),vertex(-100,200))
        n=triangle(out,src,10,10);self.assertAlmostEqual(area(out,n),100)
        # Fully inside geometry must remain byte-identical.
        src=(Vertex*3)(vertex(10,10),vertex(30,10),vertex(10,30))
        self.assertEqual(triangle(out,src,512,256),3)
        self.assertEqual(bytes(out)[:ctypes.sizeof(src)],bytes(src))
        rng=random.Random(7301)
        for _ in range(1000):
            src=(Vertex*3)(*(vertex(rng.uniform(-2048,2048),rng.uniform(-2048,2048)) for i in range(3)))
            n=triangle(out,src,512,256)
            self.assertTrue(0<=n<=15 and n%3==0)
            self.assertLessEqual(area(out,n),512*256+.1)
            for v in out[:n]:
                self.assertTrue(0<=v.x<=512 and 0<=v.y<=256)
                self.assertAlmostEqual(v.u,v.x*.25+5,delta=.001)
                self.assertAlmostEqual(v.v,v.y*.5+7,delta=.001)
                self.assertEqual(v.color,0xffabcdef)
        line=(Vertex*2)(vertex(-10,128),vertex(522,128))
        self.assertEqual(segment(out,line,512,256),2)
        self.assertEqual((out[0].x,out[1].x),(0,512))
        line=(Vertex*2)(vertex(-10,-10),vertex(-1,256))
        self.assertEqual(segment(out,line,512,256),0)
        code,preset,error=self.parse(b'[preset00]\nshapecode_0_enabled=1\nshapecode_0_x=-.5\nshapecode_0_rad=2\nshape_0_per_frame1=y=1.5;')
        self.assertEqual(code,0,(error.line,error.key))
        self.assertEqual(self.evaluate_state(preset,PresetState(),0)[0],0)
        s=self.last_decor.shapes[0];self.assertEqual((s.x,s.y,s.rad),(-.5,1.5,2))

    def test_mutable_engine_inputs_are_context_local_and_reseeded(self):
        for name in ('time','fps','frame','progress','bass','mid','treb','bass_att',
                     'mid_att','treb_att','meshx','meshy','pixelsx','pixelsy','aspectx','aspecty'):
            for prefix in ('per_frame_init_1','per_frame_1','per_pixel_1',
                           'shape_0_init1','shape_0_per_frame1','wave_0_init1',
                           'wave_0_per_frame1','wave_0_per_point1'):
                self.assertEqual(self.parse(f'[preset00]\n{prefix}={name}+=1;'.encode())[0],0,(prefix,name))
        code,preset,error=self.parse(b'''[preset00]
per_frame_init_1=time=900; fps=900;
per_frame_1=q1=time; time=800; fps=800; frame=800; bass=800;
shapecode_0_enabled=1
shapecode_0_num_inst=2
shape_0_init1=time=700; fps=700;
shape_0_per_frame1=x=time/10; y=fps/100; r=q1/10; time=600; fps=600;
per_pixel_1=time+=.001; dx=time/10; fps+=1; dy=fps/1000;
''')
        self.assertEqual(code,0,(error.line,error.key))
        state=PresetState();points=(Warp*GRID_POINTS)()
        fn=self.library.md_eval_pixel_grid
        fn.argtypes=[ctypes.POINTER(Preset),ctypes.POINTER(Warp),ctypes.c_float,
                     ctypes.POINTER(Signal),ctypes.POINTER(PresetState),ctypes.POINTER(Warp),ctypes.POINTER(Error)]
        for frame,t in enumerate((1.,1.05)):
            result,warp,error=self.evaluate_state(preset,state,t)
            self.assertEqual(result,0)
            self.assertEqual(state.frames,frame+1)
            self.assertAlmostEqual(state.last_seconds,t,places=5)
            self.assertAlmostEqual(state.fps,20,places=3)
            for slot in (0,4):
                self.assertAlmostEqual(self.last_decor.shapes[slot].x,t/10,places=5)
                self.assertAlmostEqual(self.last_decor.shapes[slot].y,.2,places=4)
                self.assertAlmostEqual(self.last_decor.shapes[slot].r,t/10,places=5)
            self.assertEqual(fn(ctypes.byref(preset),ctypes.byref(warp),t,None,
                                ctypes.byref(state),points,ctypes.byref(error)),0)
            self.assertAlmostEqual(points[0].dx,(t+.001)/10,places=5)
            self.assertAlmostEqual(points[1].dx,(t+.002)/10,places=5)
            self.assertAlmostEqual(points[1].dy,.022,places=4)

    def test_wave_frame_inputs_do_not_leak_into_point_inputs(self):
        class Vertex(ctypes.Structure):
            _fields_=[('u',ctypes.c_float),('v',ctypes.c_float),('color',ctypes.c_uint),
                      ('x',ctypes.c_float),('y',ctypes.c_float),('z',ctypes.c_float)]
        class Geometry(ctypes.Structure):
            _fields_=[('count',ctypes.c_int),('vertices',Vertex*CUSTOM_POINTS)]
        code,preset,error=self.parse(b'''[preset00]
wavecode_0_enabled=1
wavecode_0_samples=3
wave_0_init1=time=999; fps=999;
wave_0_per_frame1=q1=time; time=800; fps=800;
wave_0_per_point1=x=time/10; y=q1/10; time+=1;
''')
        self.assertEqual(code,0,(error.line,error.key))
        state=PresetState();self.assertEqual(self.evaluate_state(preset,state,1)[0],0)
        fn=self.library.md_eval_custom_waves
        fn.argtypes=[ctypes.POINTER(Preset),ctypes.c_float,ctypes.POINTER(Signal),
                     ctypes.POINTER(ctypes.c_short),ctypes.POINTER(ctypes.c_short),
                     ctypes.POINTER(ctypes.c_float),ctypes.POINTER(ctypes.c_float),
                     ctypes.POINTER(PresetState),ctypes.POINTER(Geometry),ctypes.POINTER(Error)]
        pcm=(ctypes.c_short*576)();out=(Geometry*4)()
        for t in (1.,2.):
            self.library.pm_begin_frame()
            self.assertEqual(fn(ctypes.byref(preset),t,None,pcm,pcm,None,None,
                                ctypes.byref(state),out,ctypes.byref(error)),0)
            for i in range(3):
                self.assertAlmostEqual(out[0].vertices[i].x,(t+i)/10*256,places=4)
                self.assertAlmostEqual(out[0].vertices[i].y,t/10*256,places=4)

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
        # Desktop NSEEL deliberately takes fabs before fsqrt.
        for expression, expected in (("sqrt(-1)",1), ("sqrt(-9)",3),
                                     ("sqrt(-0)",0), ("sqrt(0)",0),
                                     ("sqrt(9)",3), ("sqrt(-.25)",.5),
                                     ("sqrt(sin(-1))",math.sqrt(abs(math.sin(-1))))):
            code, preset, _ = self.parse(f"[preset00]\nper_frame_1=warp={expression};".encode())
            self.assertEqual(code, 0)
            result, warp, _, _ = self.evaluate(preset, 0)
            self.assertEqual(result, 0, expression)
            self.assertAlmostEqual(warp.warp, expected, places=5)
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
        for field,value in (("bInvert",.5),("nWaveMode",1.5)):
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
            result,warp,_,_=self.evaluate(preset,0)
            self.assertEqual(result,0)
            self.assertEqual(warp.warp,0)

    def test_transforms_and_static_layers(self):
        data=b"[preset00]\nzoom=1\nfZoomExponent=1.5\ncx=.4\ncy=.6\nsx=2\nsy=.5\n"
        code,preset,_=self.parse(data+b"per_frame_1=cx=.5+.1*sin(time); sy=1;\n")
        self.assertEqual(code,0)
        self.assertAlmostEqual(self.evaluate(preset,0)[1].cx,.5)
        self.assertEqual(self.evaluate(preset,0)[1].sy,1)
        for key,value in (("bWaveDots",.5),
                          ("nVideoEchoOrientation",1.5)):
            self.assertEqual(self.parse(f"[preset00]\n{key}={value}".encode())[0],3)
        for key in ("cx","cy","sx","sy","zoomexp"):
            value=101 if key=='zoomexp' else 10
            code,preset,_=self.parse(f"[preset00]\nper_frame_1={key}={value};".encode())
            self.assertEqual(code,0)
            self.assertEqual(self.evaluate(preset,0)[0],0)
        code,preset,_=self.parse(b"[preset00]\nshapecode_0_enabled=1\nshapecode_0_sides=32\n"
            b"shapecode_0_textured=1\nshapecode_0_border_a=.5\nfVideoEchoAlpha=.5\n"
            b"nVideoEchoOrientation=3\nob_size=.1\nob_alpha=.5\nbWaveThick=1\n")
        self.assertEqual(code,0)
        self.assertEqual(preset.decor.shapes[0].sides,32)
        self.assertEqual(preset.decor.echo_orient,3)
        self.assertEqual(preset.decor.wave_thick,1)
        for key,value in (("shapecode_0_sides",3.5),
            ("shapecode_0_textured",.5),("shapecode_0_per_frame1",0)):
            self.assertNotEqual(self.parse(f"[preset00]\n{key}={value}".encode())[0],0)
        self.assertEqual(self.parse(b"[preset00]\nshapecode_0_x=.5\nshapecode_0_x=.5")[0],2)
        self.assertEqual(self.parse(b"[preset00]\nbModWaveAlphaByVolume=1\nfModWaveAlphaStart=1\nfModWaveAlphaEnd=1")[0],0)

    def test_stereo_script_wave(self):
        class Vertex(ctypes.Structure):
            _fields_=[('u',ctypes.c_float),('v',ctypes.c_float),('color',ctypes.c_uint),
                      ('x',ctypes.c_float),('y',ctypes.c_float),('z',ctypes.c_float)]
        capture=ctypes.c_int.in_dll(self.library,'md_wave_capture')
        capture.value=2
        pcm=(ctypes.c_short*1152)(*(int(24000*math.sin(i*(.07 if i%2 else .023))) for i in range(1152)))
        left,right=(ctypes.c_short*576)(),(ctypes.c_short*576)()
        self.library.md_wave_forget()
        self.library.visualization_pcm_publish(pcm,576)
        self.assertEqual(self.library.md_wave_snapshot_stereo(right,left),1)
        self.assertEqual(list(left),list(pcm)[::2])
        self.assertEqual(list(right),list(pcm)[1::2])
        self.assertEqual(self.library.md_wave_snapshot_stereo(right,left),0)
        self.library.visualization_pcm_publish(pcm,2)
        self.assertEqual(self.library.md_wave_snapshot_stereo(right,left),1)
        self.assertEqual(list(left)[2:],[0]*574)
        self.assertEqual(list(right)[2:],[0]*574)
        self.library.visualization_pcm_publish(pcm,576)
        self.assertEqual(self.library.md_wave_snapshot_stereo(right,left),1)
        capture.value=0
        vertices=(Vertex*241)()
        draw=self.library.md_wave_script
        draw.argtypes=[ctypes.POINTER(Vertex),ctypes.POINTER(ctypes.c_short),ctypes.POINTER(ctypes.c_short),
                       ctypes.c_float,ctypes.c_float,ctypes.c_uint,ctypes.POINTER(Decor)]
        for mystery in (-1,0,1):
            decor=Decor(); decor.wave_x=.5; decor.wave_y=.5; decor.wave_param=mystery
            self.assertEqual(draw(vertices,right,left,1,0,0xff123456,ctypes.byref(decor)),170)
            expected=[]; w=.45+.5*(mystery*.5+.5)
            for i in range(170):
                x=256*i/170+128*.44*right[i+228]/32768
                y=128-128*.47*left[i+203]/32768
                if i>1:
                    x=x*(1-w)+w*(2*expected[-1][0]-expected[-2][0])
                    y=y*(1-w)+w*(2*expected[-1][1]-expected[-2][1])
                expected.append((x,y))
                self.assertAlmostEqual(vertices[i].x,x,delta=.003)
                self.assertAlmostEqual(vertices[i].y,y,delta=.003)
                self.assertEqual(vertices[i].color,0xff123456)
        self.assertEqual(self.parse(b'[preset00]\nnWaveMode=4')[0],0)
        for mode in (4.5,):
            self.assertNotEqual(self.parse(f'[preset00]\nnWaveMode={mode}'.encode())[0],0)

    def test_spiral_wave_geometry(self):
        class Vertex(ctypes.Structure):
            _fields_=[('u',ctypes.c_float),('v',ctypes.c_float),('color',ctypes.c_uint),
                      ('x',ctypes.c_float),('y',ctypes.c_float),('z',ctypes.c_float)]
        right=(ctypes.c_short*576)(*(int(24000*math.sin(i*.09)) for i in range(576)))
        left=(ctypes.c_short*576)(*(int(18000*math.cos(i*.03)) for i in range(576)))
        vertices=(Vertex*241)(); vertices[240].color=0xdeadbeef
        draw=self.library.md_wave_spiral
        draw.argtypes=[ctypes.POINTER(Vertex),ctypes.POINTER(ctypes.c_short),ctypes.POINTER(ctypes.c_short),
                       ctypes.c_float,ctypes.c_float,ctypes.c_float,ctypes.c_float,ctypes.c_uint,ctypes.POINTER(Decor)]
        for smoothing in (0,.5,1):
            r=[right[0]/32768]; l=[left[0]/32768]
            for i in range(1,576):
                r.append(right[i]/32768*(1-smoothing)+r[-1]*smoothing)
                l.append(left[i]/32768*(1-smoothing)+l[-1]*smoothing)
            for aspect in (.25,.5625,2/3):
                decor=Decor(); decor.wave_x=.5; decor.wave_y=.5; decor.wave_param=.1
                self.assertEqual(draw(vertices,right,left,1,smoothing,3,aspect,0xff112233,ctypes.byref(decor)),240)
                for i in range(240):
                    rad=.53+.43*r[i]+.1; angle=l[i+32]*1.57+3*2.3
                    self.assertAlmostEqual(vertices[i].x,128+128*rad*math.cos(angle)*aspect,delta=.0002)
                    self.assertAlmostEqual(vertices[i].y,128-128*rad*math.sin(angle),delta=.0002)
                self.assertEqual(vertices[240].color,0xdeadbeef)
        self.assertEqual(self.parse(b'[preset00]\nnWaveMode=1')[0],0)

    def test_frame_clock_inputs(self):
        code,preset,_=self.parse(b'[preset00]\nper_frame_1=q1=frame; q2=fps;q3=1/fps;')
        self.assertEqual(code,0)
        state=PresetState()
        for index,seconds in enumerate((0,.1,.2,.4,.4)):
            self.assertEqual(self.evaluate_state(preset,state,seconds)[0],0)
            self.assertEqual(state.frame_q[0],index)
            self.assertAlmostEqual(state.frame_q[1],(20,10,10,5,5)[index],places=4)
            self.assertAlmostEqual(state.frame_q[2],1/state.frame_q[1],places=6)

    def test_fft_and_remaining_waveforms(self):
        class Vertex(ctypes.Structure):
            _fields_=[('u',ctypes.c_float),('v',ctypes.c_float),('color',ctypes.c_uint),
                      ('x',ctypes.c_float),('y',ctypes.c_float),('z',ctypes.c_float)]
        spectrum=(ctypes.c_short*1024)(*(int(16384*math.sin(2*math.pi*32*i/1024)) for i in range(1024)))
        bins=(ctypes.c_float*512)()
        fft=self.library.md_wave_spectrum
        fft.argtypes=[ctypes.POINTER(ctypes.c_short),ctypes.POINTER(ctypes.c_float)]
        fft(spectrum,bins)
        self.assertEqual(max(range(512),key=lambda i:bins[i]),32)
        self.assertAlmostEqual(bins[32],.5,delta=.003)
        silence=(ctypes.c_short*1024)(); fft(silence,bins)
        self.assertEqual(list(bins),[0]*512)
        capture=ctypes.c_int.in_dll(self.library,'md_wave_capture'); capture.value=3
        pcm=(ctypes.c_short*2048)()
        for i in range(1024): pcm[2*i]=spectrum[i]; pcm[2*i+1]=123
        self.library.visualization_pcm_publish(pcm,1024)
        snapshot=(ctypes.c_short*1024)()
        self.assertEqual(self.library.md_spectrum_snapshot(snapshot),1)
        self.assertEqual(list(snapshot),list(spectrum))
        self.assertEqual(self.library.md_spectrum_snapshot(snapshot),0)
        capture.value=0
        left=(ctypes.c_short*576)(*[int(20000*math.sin(i*.05)) for i in range(576)])
        right=(ctypes.c_short*576)(*[int(10000*math.cos(i*.08)) for i in range(576)])
        vertices=(Vertex*481)(); vertices[480].color=0xdeadbeef
        decor=Decor(); decor.wave_x=.5; decor.wave_y=.2; decor.wave_param=.2
        fn=self.library.md_wave_extra
        fn.argtypes=[ctypes.POINTER(Vertex),ctypes.c_int,ctypes.POINTER(ctypes.c_short),ctypes.POINTER(ctypes.c_short),
                     ctypes.POINTER(ctypes.c_short),ctypes.c_float,ctypes.c_float,ctypes.c_float,ctypes.c_float,
                     ctypes.c_uint,ctypes.POINTER(Decor),ctypes.POINTER(ctypes.c_int)]
        for mode,count in ((2,480),(3,480),(5,480),(6,170),(7,340),(8,256)):
            split=ctypes.c_int()
            self.assertEqual(fn(vertices,mode,right,left,spectrum,1,0,1,.5,0xffaabbcc,ctypes.byref(decor),ctypes.byref(split)),count)
            self.assertEqual(split.value,170 if mode==7 else 0)
            self.assertEqual(vertices[480].color,0xdeadbeef)
            for vertex in vertices[:count]: self.assertTrue(math.isfinite(vertex.x) and math.isfinite(vertex.y))
            if mode in (2,3):
                for i in (0,32,479):
                    self.assertAlmostEqual(vertices[i].x,128+64*right[i]/32768,places=4)
                    self.assertAlmostEqual(vertices[i].y,.8*256-128*left[i+32]/32768,delta=.00003)
            if mode==7: self.assertNotEqual(vertices[0].y,vertices[170].y)
        for mode in range(9): self.assertEqual(self.parse(f'[preset00]\nnWaveMode={mode}'.encode())[0],0)

    def test_dynamic_wave_and_motion_fields(self):
        code,preset,_=self.parse(b'[preset00]\nnWaveMode=0\nmv_a=.5\nper_frame_1=wave_mode=floor(time); mv_l=1+time;')
        self.assertEqual(code,0)
        state=PresetState()
        for mode in range(9):
            self.assertEqual(self.evaluate_state(preset,state,mode)[0],0)
            self.assertEqual(state.wave_mode,mode)
            self.assertAlmostEqual(state.motion[8],mode+1)
        self.assertEqual(self.evaluate_state(preset,state,9)[0],0)
        self.assertEqual(state.wave_mode,8)
        for expression in ('mv_x=1/0;','mv_y=1/0;'):
            code,preset,_=self.parse(f'[preset00]\nper_frame_1={expression}'.encode())
            self.assertEqual(code,0)
            self.assertEqual(self.evaluate_state(preset,PresetState(),0)[0],0)
        code,preset,_=self.parse(b'[preset00]\nper_frame_1=wave_mode=4; mv_a=.5;\nper_pixel_1=rot=.01*wave_mode*mv_a;')
        self.assertEqual(code,0)
        state=PresetState(); result,warp,_=self.evaluate_state(preset,state,1)
        self.assertEqual(result,0)
        grid=(Warp*GRID_POINTS)(); error=Error()
        self.library.md_eval_pixel_grid.argtypes=[ctypes.POINTER(Preset),ctypes.POINTER(Warp),ctypes.c_float,ctypes.POINTER(Signal),ctypes.POINTER(PresetState),ctypes.POINTER(Warp),ctypes.POINTER(Error)]
        self.assertEqual(self.library.md_eval_pixel_grid(ctypes.byref(preset),ctypes.byref(warp),1,None,ctypes.byref(state),grid,ctypes.byref(error)),0)
        self.assertAlmostEqual(grid[0].rotation,.02,places=6)
        self.assertNotEqual(self.parse(b'[preset00]\nper_pixel_1=wave_mode=1;')[0],0)

    def test_snapshot_kind_switch_rejects_old_payload(self):
        capture=ctypes.c_int.in_dll(self.library,'md_wave_capture')
        pcm=(ctypes.c_short*2048)(*range(2048))
        right=(ctypes.c_short*576)(*([123]*576)); left=(ctypes.c_short*576)(*([456]*576))
        spectrum=(ctypes.c_short*1024)(*([789]*1024))
        capture.value=1; self.library.visualization_pcm_publish(pcm,1024)
        self.assertEqual(self.library.md_wave_snapshot_stereo(right,left),0)
        self.assertEqual(left[0],456)
        self.assertEqual(self.library.md_spectrum_snapshot(spectrum),0)
        self.assertEqual(spectrum[0],789)
        capture.value=3; self.library.visualization_pcm_publish(pcm,1024)
        self.assertEqual(self.library.md_wave_snapshot_stereo(right,left),0)
        self.assertEqual(right[0],123)
        self.assertEqual(self.library.md_spectrum_snapshot(spectrum),1)
        self.assertEqual(spectrum[100],200)
        capture.value=2; self.library.visualization_pcm_publish(pcm,1024)
        self.assertEqual(self.library.md_spectrum_snapshot(spectrum),0)
        self.assertEqual(self.library.md_wave_snapshot_stereo(right,left),1)
        self.assertEqual((right[100],left[100]),(201,200))
        capture.value=0

    def test_thick_shape_outline_flags(self):
        for slot in range(4):
            for value in (0,1):
                code,preset,_=self.parse(f"[preset00]\nshapecode_{slot}_thickOutline={value}".encode())
                self.assertEqual(code,0)
                self.assertEqual(preset.decor.shapes[slot].thick_outline,value)
        for value in ('.5','nan','inf'):
            self.assertNotEqual(self.parse(f"[preset00]\nshapecode_0_thickOutline={value}".encode())[0],0)
        self.assertEqual(self.parse(b"[preset00]\nshapecode_0_thickOutline=1\nshapecode_0_thickOutline=0")[0],2)

    def test_shape_formula_flags_use_desktop_integer_truth(self):
        for value in (0, .5, -.5, .999, -.999, 1, -1, 1.5, 1e30):
            code,preset,error=self.parse(('[preset00]\nshapecode_0_enabled=1\n'
                f'shape_0_per_frame1=additive={value};textured={value};thick={value};').encode())
            self.assertEqual(code,0)
            self.assertEqual(self.evaluate_state(preset,PresetState(),0)[0],0)
            shape=self.last_decor.shapes[0]
            for actual in (shape.additive,shape.textured,shape.thick_outline):
                self.assertEqual(actual,abs(value)>=1)

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
        # Exceptional math is allowed until ordinary assignment.
        for expression,expected in (("if(1,1/0,2)",0),("if(0,2,log(0))",0),
                           ("band(0,1/0)",0),("bor(1,1/0)",1),("asin(2)",0),("acos(-2)",0)):
            code,preset,_=self.parse(f"[preset00]\nper_frame_1=warp={expression};".encode())
            self.assertEqual(code,0)
            result,warp,_,_=self.evaluate(preset,0)
            self.assertEqual(result,0,expression)
            self.assertEqual(warp.warp,expected,expression)

    def test_conditional_syntax_budgets_and_bytecode_safety(self):
        for expression in ("if()","if(1,2)","if(1,2,3,4)","if(,2,3)",
                           "if(1,,3)","if(1,2,)"):
            self.assertNotEqual(self.parse(f"[preset00]\nper_frame_1=warp={expression};".encode())[0],0)
        expression="0"
        for _ in range(70): expression=f"if(0,0,{expression})"
        self.assertNotEqual(self.parse(f"[preset00]\nper_frame_1=warp={expression};".encode())[0],0)
        lines="\n".join(f"per_frame_{i}=warp=if(1,if(0,0,1),if(1,1,0));" for i in range(1,MAX_OPS//10+2))
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
                damaged=Program()
                self.assertEqual(compile_fn(ctypes.byref(damaged),b"warp=1;",2),0)
                self.assertEqual(compile_fn(ctypes.byref(damaged),b"warp=if(0,2,3);",3),0)
                damaged.code[count].value=1  # also exercise unconditional jump
                damaged.code[index].arg=destination
                values=(ctypes.c_float*VALUES)(*([.5]*VALUES)); before=bytes(values)
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
        self.last_decor=decor
        return result,warp,error

    def test_reference_shape_color_bytes_wrap_without_undefined_casts(self):
        fn=self.library.md_shape_rgba
        fn.argtypes=[ctypes.c_float]*4
        fn.restype=ctypes.c_uint
        for value in (0,.5,1,2,-1,-.5,99,1e30,-1e30):
            stored=ctypes.c_float(value).value
            product=ctypes.c_float(stored*255).value
            byte=math.trunc(product if math.isfinite(product) else stored*255)&255
            self.assertEqual(fn(value,value,value,value),byte*0x01010101)
        code,preset,_=self.parse(b'[preset00]\nshapecode_0_enabled=1\nshapecode_0_r=2\n'
                                b'shape_0_per_frame1=g=-1; a=2; border_a=-.5;\n')
        self.assertEqual(code,0)
        self.assertEqual(self.evaluate_state(preset,PresetState(),0)[0],0)
        shape=self.last_decor.shapes[0]
        self.assertEqual((shape.r,shape.g,shape.a,shape.border_a),(2,-1,2,-.5))

    def test_shape_side_limit_is_applied_after_formulas(self):
        fn=self.library.md_shape_sides
        fn.argtypes=[ctypes.c_float]
        fn.restype=ctypes.c_int
        for value,expected in ((-1e30,3),(0,3),(3.9,3),(99.9,99),(101,100),(1e30,100),
                               (float('nan'),0),(float('inf'),0)):
            self.assertEqual(fn(value),expected)
        code,preset,error=self.parse(b'[preset00]\nshapecode_0_enabled=1\nshapecode_0_sides=101\n'
                                    b'shape_0_per_frame1=x=sides/202; sides=99.9;\n')
        self.assertEqual(code,0,(error.line,error.key))
        self.assertEqual(preset.decor.shapes[0].sides,101)
        self.assertEqual(self.evaluate_state(preset,PresetState(),0)[0],0)
        self.assertEqual(self.last_decor.shapes[0].x,.5)
        self.assertAlmostEqual(self.last_decor.shapes[0].sides,99.9,places=4)

    def test_shape_contexts_and_atomic_failure(self):
        code,preset,error=self.parse(b'''[preset00]
per_frame_1=q1=.25;
shapecode_0_enabled=1
shapecode_1_enabled=1
shape_0_init1=t1=.2; counter=0;
shape_0_per_frame1=counter=counter+1; x=q1; rad=t1; t1=.9; q1=.8;
shape_1_init1=t1=.3; counter=10;
shape_1_per_frame1=counter=counter+2; rad=t1; x=q1;
''')
        self.assertEqual(code,0)
        state=PresetState()
        for i in range(1,5):
            self.assertEqual(self.evaluate_state(preset,state,i)[0],0)
            self.assertAlmostEqual(state.shape[0].t[0],.2)
            self.assertAlmostEqual(state.shape[1].t[0],.3)
            self.assertEqual(state.shape[0].user[0],i)
            self.assertEqual(state.shape[1].user[0],10+2*i)
            self.assertAlmostEqual(state.frame_q[0],.25)
            self.assertAlmostEqual(self.last_decor.shapes[0].rad,.2)
            self.assertAlmostEqual(self.last_decor.shapes[1].rad,.3)
            self.assertAlmostEqual(self.last_decor.shapes[1].x,.25)
        self.assertEqual(self.evaluate_state(preset,PresetState(),0)[0],0)
        preset.decor.shapes[0].enabled=0
        disabled=PresetState()
        self.assertEqual(self.evaluate_state(preset,disabled,0)[0],0)
        self.assertFalse(disabled.shape[0].ready)
        for expr in ('sides=1/0;','x=1/0;'):
            code,preset,_=self.parse(('[preset00]\nshapecode_0_enabled=1\nshape_0_per_frame1='+expr).encode())
            self.assertEqual(code,0)
            self.assertEqual(self.evaluate_state(preset,PresetState(),0)[0],0)
        for expr in ('psp_low=2;',):
            self.assertNotEqual(self.parse(('[preset00]\nshape_0_per_frame1='+expr).encode())[0],0)

    def test_dense_waves_share_frame_fuel_without_losing_endpoints(self):
        class Vertex(ctypes.Structure):
            _fields_=[('u',ctypes.c_float),('v',ctypes.c_float),('color',ctypes.c_uint),
                      ('x',ctypes.c_float),('y',ctypes.c_float),('z',ctypes.c_float)]
        class Geometry(ctypes.Structure):
            _fields_=[('count',ctypes.c_int),('vertices',Vertex*CUSTOM_POINTS)]
        lines=['[preset00]','fWaveScale=1']
        for slot in range(4):
            lines += [f'wavecode_{slot}_enabled=1',f'wavecode_{slot}_samples={CUSTOM_POINTS}',
                      f'wavecode_{slot}_smoothing=0',f'wavecode_{slot}_scaling=1',
                      f'wave_{slot}_per_point1='+'q1=q1+0;'*30+'x=sample;y=value1;']
        code,preset,error=self.parse('\n'.join(lines).encode())
        self.assertEqual(code,0,(error.line,error.key))
        fn=self.library.md_eval_custom_waves
        fn.argtypes=[ctypes.POINTER(Preset),ctypes.c_float,ctypes.POINTER(Signal),
                     ctypes.POINTER(ctypes.c_short),ctypes.POINTER(ctypes.c_short),
                     ctypes.POINTER(ctypes.c_float),ctypes.POINTER(ctypes.c_float),
                     ctypes.POINTER(PresetState),ctypes.POINTER(Geometry),ctypes.POINTER(Error)]
        state=PresetState();out=(Geometry*4)()
        samples=(ctypes.c_short*576)(*[i*20 for i in range(576)])
        for frame in range(3):
            self.assertEqual(self.evaluate_state(preset,state,frame/30)[0],0)
            self.assertEqual(fn(ctypes.byref(preset),frame/30,None,samples,samples,None,None,
                                ctypes.byref(state),out,ctypes.byref(error)),0,(error.line,error.key))
            self.assertGreaterEqual(self.library.pm_frame_remaining(),0)
            for wave in out:
                self.assertGreater(wave.count,2)
                self.assertLess(wave.count,CUSTOM_POINTS)
                self.assertEqual(wave.vertices[0].x,0)
                self.assertEqual(wave.vertices[wave.count-1].x,256)
                self.assertAlmostEqual(wave.vertices[0].y,0,places=5)
                self.assertAlmostEqual(wave.vertices[wave.count-1].y,575*20/32768*256,places=4)
        # Reducing density must not swallow a genuinely invalid expression.
        code,preset,error=self.parse(b'[preset00]\nwavecode_0_enabled=1\nwave_0_per_point1=x=megabuf(-1);')
        self.assertEqual(code,0)
        state=PresetState();self.assertEqual(self.evaluate_state(preset,state,0)[0],0)
        self.assertNotEqual(fn(ctypes.byref(preset),0,None,samples,samples,None,None,
                               ctypes.byref(state),out,ctypes.byref(error)),0)

    def test_custom_wave_context_and_geometry(self):
        class Vertex(ctypes.Structure):
            _fields_=[('u',ctypes.c_float),('v',ctypes.c_float),('color',ctypes.c_uint),('x',ctypes.c_float),('y',ctypes.c_float),('z',ctypes.c_float)]
        class Geometry(ctypes.Structure):
            _fields_=[('count',ctypes.c_int),('vertices',Vertex*CUSTOM_POINTS)]
        code,preset,_=self.parse(b'''[preset00]
wavecode_0_enabled=1
wavecode_0_samples=64
wavecode_0_smoothing=0
wave_0_init1=t1=.2; counter=0;
wave_0_per_frame1=counter=counter+1; t2=t1; t1=.9;
wave_0_per_point1=x=sample; y=t2+.1*value1; t2=t2+.001;
''')
        self.assertEqual(code,0)
        fn=self.library.md_eval_custom_waves
        fn.argtypes=[ctypes.POINTER(Preset),ctypes.c_float,ctypes.POINTER(Signal),ctypes.POINTER(ctypes.c_short),ctypes.POINTER(ctypes.c_short),ctypes.POINTER(ctypes.c_float),ctypes.POINTER(ctypes.c_float),ctypes.POINTER(PresetState),ctypes.POINTER(Geometry),ctypes.POINTER(Error)]
        state=PresetState(); out=(Geometry*4)(); error=Error()
        # Unused geometry is not touched, but stale disabled-wave counts must
        # be cleared. Active vertices still initialize every GU attribute.
        ctypes.memset(out,0x42,ctypes.sizeof(out))
        unused=bytes(out[0].vertices[64])
        left=(ctypes.c_short*576)(*([16384]*576)); right=(ctypes.c_short*576)()
        for frame in range(3):
            self.assertEqual(self.evaluate_state(preset,state,frame)[0],0)
            self.assertEqual(fn(ctypes.byref(preset),frame,None,right,left,None,None,ctypes.byref(state),out,ctypes.byref(error)),0)
            self.assertEqual(out[0].count,64)
            self.assertEqual(bytes(out[0].vertices[64]),unused)
            self.assertEqual([out[i].count for i in range(1,4)],[0,0,0])
            self.assertEqual((out[0].vertices[0].u,out[0].vertices[0].v,out[0].vertices[0].z),(0,0,0))
            self.assertAlmostEqual(out[0].vertices[0].y,.25*256,places=4)
            self.assertEqual(out[0].vertices[63].x,256)
            self.assertEqual(state.waves[0].frame.user[0],frame+1)
            self.assertAlmostEqual(state.waves[0].frame.t[0],.2)
        code,bad,_=self.parse(b'[preset00]\nwavecode_0_enabled=1\nwave_0_per_point1=x=sample; y=megabuf(1/(1-sample));')
        self.assertEqual(code,0)
        before=bytes(state),bytes(out)
        self.assertNotEqual(fn(ctypes.byref(bad),1,None,right,left,None,None,ctypes.byref(state),out,ctypes.byref(error)),0)
        self.assertEqual((bytes(state),bytes(out)),before)
        preset.waves[0].spectrum=1
        spectral=(ctypes.c_float*512)(*([.5]*512))
        fresh=PresetState()
        self.assertNotEqual(fn(ctypes.byref(preset),1,None,right,left,None,None,ctypes.byref(fresh),out,ctypes.byref(error)),0)
        self.assertFalse(fresh.waves[0].frame.ready)
        self.assertEqual(fn(ctypes.byref(preset),1,None,right,left,spectral,spectral,ctypes.byref(fresh),out,ctypes.byref(error)),0)
        self.assertAlmostEqual(out[0].vertices[0].y,.25*256,places=4)
        for key,value in [('samples','513'),('bSpectrum','2'),('sep','129')]:
            code,preset,_=self.parse(f'[preset00]\nwavecode_0_{key}={value}'.encode())
            self.assertEqual(code,0)
            self.assertEqual(getattr(preset.waves[0],{'bSpectrum':'spectrum'}.get(key,key)),
                             {'samples':513,'bSpectrum':1,'sep':128}[key])
        for expr in ('psp_low=0;',):
            self.assertNotEqual(self.parse(('[preset00]\nwave_0_per_point1='+expr).encode())[0],0)

        # New wave density and mutable point inputs: all PCM reads stay inside
        # the original 576-sample snapshot, including odd/maximal separation.
        for samples in (513,576,1024,1000000):
            for sep in (0,1,127,128):
                code,preset,error=self.parse(f'''[preset00]
wavecode_0_enabled=1
wavecode_0_samples={samples}
wavecode_0_sep={sep}
wavecode_0_smoothing=0
wave_0_per_point1=sample=1-sample;value1=0;value2=0;x=sample;y=.5;
'''.encode())
                self.assertEqual(code,0,(error.line,error.key))
                self.library.pm_begin_frame()
                self.assertEqual(fn(ctypes.byref(preset),0,None,right,left,spectral,spectral,
                    ctypes.byref(PresetState()),out,ctypes.byref(error)),0)
                count=min(samples,CUSTOM_POINTS)
                self.assertEqual(out[0].count,count)
                self.assertEqual(out[0].vertices[0].x,256)
                self.assertEqual(out[0].vertices[count-1].x,0)
                self.assertEqual(out[0].vertices[count-1].y,128)
                # Exercise the spectrum interpolation path as well.
                preset.waves[0].spectrum=1
                self.library.pm_begin_frame()
                self.assertEqual(fn(ctypes.byref(preset),0,None,right,left,spectral,spectral,
                    ctypes.byref(PresetState()),out,ctypes.byref(error)),0)

    def test_combined_pcm_fft_snapshot(self):
        capture=ctypes.c_int.in_dll(self.library,'md_wave_capture')
        pcm=(ctypes.c_short*2048)(*range(2048))
        right,left=(ctypes.c_short*576)(),(ctypes.c_short*576)()
        spectrum=(ctypes.c_short*1024)()
        capture.value=2; self.library.visualization_pcm_publish(pcm,1024)
        self.assertEqual(self.library.md_wave_snapshot_combined(right,left,spectrum),0)
        capture.value=4; self.library.visualization_pcm_publish(pcm,1024)
        self.assertEqual(self.library.md_wave_snapshot_combined(right,left,spectrum),1)
        self.assertEqual((right[100],left[100],spectrum[900]),(201,200,1800))
        self.assertEqual(self.library.md_wave_snapshot_combined(right,left,spectrum),0)
        capture.value=0

    def test_stereo_spectrum_capture(self):
        capture=ctypes.c_int.in_dll(self.library,'md_wave_capture')
        pcm=(ctypes.c_short*2048)()
        for i in range(1024):
            pcm[2*i]=int(20000*math.sin(2*math.pi*32*i/1024))
            pcm[2*i+1]=int(18000*math.sin(2*math.pi*64*i/1024))
        right,left=(ctypes.c_short*576)(),(ctypes.c_short*576)()
        sl,sr=(ctypes.c_short*1024)(),(ctypes.c_short*1024)()
        capture.value=4; self.library.visualization_pcm_publish(pcm,1024)
        self.assertEqual(self.library.md_wave_snapshot_full(right,left,sl,sr),0)
        capture.value=5; self.library.visualization_pcm_publish(pcm,1024)
        self.assertEqual(self.library.md_wave_snapshot_full(right,left,sl,sr),1)
        self.assertEqual(list(sl),list(pcm)[::2]); self.assertEqual(list(sr),list(pcm)[1::2])
        bl,br=(ctypes.c_float*512)(),(ctypes.c_float*512)()
        self.library.md_wave_spectrum(sl,bl); self.library.md_wave_spectrum(sr,br)
        self.assertEqual(max(range(512),key=lambda i:bl[i]),32)
        self.assertEqual(max(range(512),key=lambda i:br[i]),64)
        capture.value=0

    def test_wave_spline_endpoints_and_bounds(self):
        class Vertex(ctypes.Structure):
            _fields_=[('u',ctypes.c_float),('v',ctypes.c_float),('color',ctypes.c_uint),('x',ctypes.c_float),('y',ctypes.c_float),('z',ctypes.c_float)]
        source=(Vertex*64)(); output=(Vertex*127)()
        for i in range(64): source[i]=Vertex(0,0,0xff00ff00,i*256/63,256 if i%2 else 0,0)
        self.assertEqual(self.library.md_wave_smooth(output,source,64),127)
        for i in range(64): self.assertEqual(bytes(output[2*i]),bytes(source[i]))
        for v in output:
            self.assertTrue(0<=v.x<=256 and 0<=v.y<=256)
        self.assertEqual(self.library.md_wave_smooth(output,source,1),0)
        self.assertEqual(self.library.md_wave_smooth(output,source,CUSTOM_POINTS+1),0)

    def test_image_effect_flags(self):
        for key in ('bDarkenCenter','bBrighten','bDarken','bSolarize','bInvert'):
            for value in (0,1): self.assertEqual(self.parse(f'[preset00]\n{key}={value}'.encode())[0],0)
            self.assertNotEqual(self.parse(f'[preset00]\n{key}=.5'.encode())[0],0)
        code,preset,_=self.parse(b'[preset00]\nper_frame_1=brighten=1; darken=1; invert=1; solarize=1; darken_center=1;')
        self.assertEqual(code,0); state=PresetState()
        self.assertEqual(self.evaluate_state(preset,state,0)[0],0)
        self.assertEqual(list(state.effects),[1]*5)
        code,preset,_=self.parse(b'[preset00]\nper_frame_1=invert=time;')
        self.assertEqual(code,0)
        self.assertEqual(self.evaluate_state(preset,state,.5)[0],0)
        self.assertEqual(state.effects[4],1)

    def test_expanded_shape_order_budget_and_failure(self):
        fn=self.library.md_eval_preset_shapes
        fn.argtypes=[ctypes.POINTER(Preset),ctypes.c_float,ctypes.POINTER(Signal),ctypes.POINTER(PresetState),
                     ctypes.POINTER(Warp),ctypes.POINTER(ctypes.c_uint),ctypes.POINTER(Decor),ctypes.POINTER(Error),ctypes.POINTER(ShapeFrame)]
        source=b'[preset00]\nshapecode_0_enabled=1\nshapecode_0_num_inst=311\nshape_0_per_frame1=counter+=1;x=instance/num_inst;y=counter*.0001;'
        code,preset,error=self.parse(source);self.assertEqual(code,0)
        state=PresetState();out=ShapeFrame();warp=Warp();color=ctypes.c_uint();decor=Decor()
        for frame in range(3):
            self.assertEqual(fn(ctypes.byref(preset),frame,None,ctypes.byref(state),ctypes.byref(warp),ctypes.byref(color),ctypes.byref(decor),ctypes.byref(error),ctypes.byref(out)),0)
            self.assertEqual(list(out.count),[311,0,0,0])
            for i in (0,7,8,31,32,310):
                self.assertAlmostEqual(out.shapes[0][i].x,i/311,places=6)
                self.assertAlmostEqual(out.shapes[0][i].y,(frame*311+i+1)*.0001,places=6)
        code,bad,error=self.parse(source+b'\nshape_0_per_frame2=x=megabuf(1/(310-instance));')
        self.assertEqual(code,0);before=bytes(state),bytes(decor),bytes(warp),color.value
        self.assertNotEqual(fn(ctypes.byref(bad),3,None,ctypes.byref(state),ctypes.byref(warp),ctypes.byref(color),ctypes.byref(decor),ctypes.byref(error),ctypes.byref(out)),0)
        self.assertEqual((bytes(state),bytes(decor),bytes(warp),color.value),before)
        self.assertEqual(list(out.count),[0,0,0,0])
        code,heavy,error=self.parse(b'[preset00]\nshapecode_0_enabled=1\nshapecode_0_num_inst=512\nshape_0_per_frame1=loop(10,x=.5;);')
        self.assertEqual(code,0);state=PresetState()
        self.assertEqual(fn(ctypes.byref(heavy),0,None,ctypes.byref(state),ctypes.byref(warp),ctypes.byref(color),ctypes.byref(decor),ctypes.byref(error),ctypes.byref(out)),0)
        self.assertGreaterEqual(out.count[0],8);self.assertLess(out.count[0],512)
        # Potentially expensive downstream waves must not lose their budget
        # to extra shapes, even when their sample count is frame-dependent.
        code,reserved,error=self.parse(source+b'\nwavecode_0_enabled=1\nwave_0_per_frame1=samples=64;\nwave_0_per_point1=loop(100,x=sample;);')
        self.assertEqual(code,0);state=PresetState()
        self.assertEqual(fn(ctypes.byref(reserved),0,None,ctypes.byref(state),ctypes.byref(warp),ctypes.byref(color),ctypes.byref(decor),ctypes.byref(error),ctypes.byref(out)),0)
        self.assertEqual(out.count[0],8)

    def test_shape_instances_and_progress(self):
        code,preset,_=self.parse(b'''[preset00]
shapecode_0_enabled=1
shapecode_0_num_inst=8
shape_0_per_frame1=x=instance/instances; y=.5; rad=.02;
per_frame_1=wave_r=progress;
''')
        self.assertEqual(code,0);state=PresetState()
        duration=ctypes.c_float.in_dll(self.library,'md_preset_duration');old=duration.value;duration.value=60
        try:
            self.assertEqual(self.evaluate_state(preset,state,30)[0],0)
            for i,index in enumerate([0,4,5,6,7,8,9,10]):
                self.assertEqual(self.last_decor.shapes[index].enabled,1)
                self.assertAlmostEqual(self.last_decor.shapes[index].x,i/8)
            for key in ('instance','instances'):
                self.assertEqual(self.parse(f'[preset00]\nshape_0_per_frame1={key}=1;'.encode())[0],0)
            code,limited,_=self.parse(b'[preset00]\nshapecode_0_num_inst=311')
            self.assertEqual(code,0)
            self.assertEqual(limited.shape_instances[0],311)
            self.assertEqual(self.parse(b'[preset00]\nper_frame_1=progress=1;')[0],0)
        finally: duration.value=old

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
        for name in ("psp_low",):
            self.assertEqual(self.parse(f"[preset00]\nper_frame_init_1={name}=1;".encode())[0],3)
        for lines in ("per_frame_init_2=q1=0;",
                      "per_frame_init_1=q1=0;\nper_frame_init_1=q1=1;",
                      "\n".join(f"per_frame_init_{i}=q1=0;" for i in range(1,RECORDS+2)),
                      "\n".join(f"per_frame_init_{i}=q1=0+0+0+0+0;" for i in range(1,MAX_OPS//10+2))):
            self.assertEqual(self.parse(f"[preset00]\n{lines}".encode())[0],2)
        code,preset,_=self.parse(b"[preset00]\nper_frame_init_1=q1=1/0;\n")
        self.assertEqual(code,0)
        result,_,error=self.evaluate_state(preset,PresetState(),0)
        self.assertEqual(result,0)
        # Keep testing rollback on an actual unsafe address, not benign 1/0.
        code,preset,_=self.parse(b"[preset00]\nper_frame_init_1=q1=2;\nper_frame_1=warp=megabuf(1/(time-1));\n")
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

    def test_named_variables_persist_but_q_and_outputs_reset(self):
        code,preset,_=self.parse(b"[preset00]\nper_frame_1=counter=counter+1; q1=q1+1; warp=counter;\n"
            b"per_frame_init_1=counter=0; q1=2;\n")
        self.assertEqual(code,0)
        self.assertEqual(preset.symbols.count,1)
        state=PresetState()
        for frame in range(4):
            result,warp,_=self.evaluate_state(preset,state,frame)
            self.assertEqual(result,0)
            self.assertEqual(warp.warp,frame+1)
            self.assertEqual(state.user[0],frame+1)
            self.assertEqual(state.q[0],2)
        self.assertEqual(self.evaluate_state(preset,PresetState(),0)[1].warp,1)
        # Persistent variables continue beyond the former warp cap of four.
        code,warp,_=self.evaluate_state(preset,state,5)
        self.assertEqual(code,0)
        self.assertEqual(warp.warp,5)
        self.assertEqual(state.user[0],5)
        code,preset,_=self.parse(b"[preset00]\nper_frame_1=warp=unset_value;\n")
        self.assertEqual(code,0)
        self.assertEqual(self.evaluate_state(preset,PresetState(),0)[1].warp,0)

    def test_named_variable_limits_and_namespace_rollback(self):
        lines="\n".join(f"per_frame_{i+1}=custom_{i}={i};" for i in range(USERS))
        code,preset,_=self.parse(f"[preset00]\n{lines}".encode())
        self.assertEqual(code,0)
        self.assertEqual(preset.symbols.count,USERS)
        self.assertEqual(self.parse(f"[preset00]\nper_frame_init_1=extra=1;\n{lines}".encode())[0],2)
        for name in ("psp_low","sin"):
            self.assertNotEqual(self.parse(f"[preset00]\nper_frame_1={name}=0;".encode())[0],0)
        self.assertEqual(self.parse(("[preset00]\nper_frame_1="+"a"*31+"=1;").encode())[0],0)
        self.assertNotEqual(self.parse(("[preset00]\nper_frame_1="+"a"*32+"=1;").encode())[0],0)
        program,symbols=Program(),Symbols()
        fn=self.library.pm_compile_symbols
        fn.argtypes=[ctypes.POINTER(Program),ctypes.c_char_p,ctypes.c_int,ctypes.POINTER(Symbols)]
        self.assertEqual(fn(ctypes.byref(program),b"existing=1;",2,ctypes.byref(symbols)),0)
        before=bytes(symbols); count=program.count
        self.assertNotEqual(fn(ctypes.byref(program),b"new_name=if(1,2,);",3,ctypes.byref(symbols)),0)
        self.assertEqual(bytes(symbols),before)
        self.assertEqual(program.count,count)

    def test_named_memory_demo_long_run_and_release(self):
        code,preset,_=self.parse((ROOT / "psp-client/presets/memory-pulse-demo.milk").read_bytes())
        self.assertEqual(code,0)
        state=PresetState()
        held_index=next(i for i in range(preset.symbols.count)
                        if bytes(preset.symbols.names[i]).split(b"\0")[0]==b"held")
        for frame in range(18000):
            level=1 if frame%100==0 else 0
            signal=Signal((ctypes.c_float*13)(*([level]*7+[level*4]*6)))
            self.assertEqual(self.evaluate_state(preset,state,frame*.1,signal)[0],0)
            if frame%100==1: self.assertGreater(state.user[held_index],.7)
            if frame%100==99: self.assertLess(state.user[held_index],.00001)

    def test_pixel_grid_coordinates_q_and_atomic_failure(self):
        fn=self.library.md_eval_pixel_grid
        fn.argtypes=[ctypes.POINTER(Preset),ctypes.POINTER(Warp),ctypes.c_float,
                     ctypes.POINTER(Signal),ctypes.POINTER(PresetState),ctypes.POINTER(Warp),ctypes.POINTER(Error)]
        code,preset,_=self.parse(b"[preset00]\nper_frame_1=q1=.1;\nper_pixel_1=dx=x*q1; dy=y*q1; rot=ang*.01;\n")
        self.assertEqual(code,0)
        state=PresetState()
        result,frame,_=self.evaluate_state(preset,state,0)
        self.assertEqual(result,0)
        points=(Warp*GRID_POINTS)(); error=Error()
        self.assertEqual(fn(ctypes.byref(preset),ctypes.byref(frame),0,None,ctypes.byref(state),points,ctypes.byref(error)),0)
        for y in range(GRID+1):
            for x in range(GRID+1):
                self.assertAlmostEqual(points[y*(GRID+1)+x].dx,x/GRID*.1,places=6)
                self.assertAlmostEqual(points[y*(GRID+1)+x].dy,y/GRID*.1,places=6)
        self.assertEqual(points[GRID_POINTS//2].rotation,0)
        # Desktop permits coordinate assignments within one mesh point.
        # Every subsequent point/evaluation starts with fresh coordinates.
        code,modified,_=self.parse(b'[preset00]\nper_pixel_1=x=x*.5; y=1-y; rad=rad+1; ang=0;\n'
                                  b'per_pixel_2=dx=x; dy=y; rot=ang; q1=rad;\n')
        self.assertEqual(code,0)
        for _ in range(2):
            self.assertEqual(fn(ctypes.byref(modified),ctypes.byref(frame),0,None,
                                ctypes.byref(state),points,ctypes.byref(error)),0)
            for y in range(GRID+1):
                for x in range(GRID+1):
                    self.assertAlmostEqual(points[y*(GRID+1)+x].dx,x/(2*GRID),places=6)
                    self.assertAlmostEqual(points[y*(GRID+1)+x].dy,1-y/GRID,places=6)
                    self.assertEqual(points[y*(GRID+1)+x].rotation,0)
        # A late grid-point failure cannot partially replace a prepared grid.
        code,preset,_=self.parse(b"[preset00]\nper_pixel_1=dx=megabuf(.01/(1-x));\n")
        self.assertEqual(code,0)
        before=bytes(points)
        self.assertEqual(fn(ctypes.byref(preset),ctypes.byref(frame),0,None,ctypes.byref(state),points,ctypes.byref(error)),2)
        self.assertEqual(bytes(points),before)

    def test_warp_cached_rotation_and_radius_match_uniform_path(self):
        class Vertex(ctypes.Structure):
            _fields_=[('u',ctypes.c_float),('v',ctypes.c_float),('color',ctypes.c_uint),
                      ('x',ctypes.c_float),('y',ctypes.c_float),('z',ctypes.c_float)]
        fn=self.library.md_warp_mesh_varying
        fn.argtypes=[ctypes.POINTER(Vertex),ctypes.POINTER(Warp),ctypes.POINTER(Warp),ctypes.c_float]
        base=Warp(1.1,.03,0,1,1,.98,.01,-.02,.5,.5,1,1,1.5)
        points=(Warp*GRID_POINTS)()
        for i in range(GRID_POINTS):
            points[i]=Warp.from_buffer_copy(base)
            points[i].rotation=(i//3%5-2)*.02
            points[i].warp=0 if i%2 else .5
        actual=(Vertex*(GRID*GRID*6))(); expected=type(actual)()
        for seconds in (0,1.25,17):
            fn(actual,ctypes.byref(base),points,seconds)
            for cell in (0,1,3,GRID,GRID*GRID//2,GRID*GRID-1):
                y,x=divmod(cell,GRID)
                a=y*(GRID+1)+x
                for offset,node in enumerate((a,a+1,a+GRID+1,a+1,a+GRID+2,a+GRID+1)):
                    fn(expected,ctypes.byref(points[node]),None,seconds)
                    index=cell*6+offset
                    self.assertEqual(bytes(actual[index]),bytes(expected[index]))

    def test_cached_grid_coordinates_both_densities(self):
        fn=self.library.md_eval_pixel_grid
        fn.argtypes=[ctypes.POINTER(Preset),ctypes.POINTER(Warp),ctypes.c_float,
                     ctypes.POINTER(Signal),ctypes.POINTER(PresetState),ctypes.POINTER(Warp),ctypes.POINTER(Error)]
        data=b'[preset00]\nper_pixel_1=dx=rad*.1; dy=x*y; rot=ang*.01; cx=meshx/32;'
        # Reserve expensive wave work so the scheduler chooses the 8x8 grid.
        heavy=b'\nwavecode_0_enabled=1\nwavecode_0_samples=1024\nwave_0_per_point1=loop(100,x=sample);'
        for suffix,grid in ((b'',GRID),(heavy,8),(b'',GRID)):
            code,preset,error=self.parse(data+suffix)
            self.assertEqual(code,0,(error.line,error.key))
            state=PresetState(); result,frame,_=self.evaluate_state(preset,state,0)
            self.assertEqual(result,0)
            points=(Warp*GRID_POINTS)()
            self.assertEqual(fn(ctypes.byref(preset),ctypes.byref(frame),0,None,
                ctypes.byref(state),points,ctypes.byref(error)),0)
            self.assertEqual(points[0].cx,grid/32)
            stride=GRID//grid
            for y in range(grid+1):
                for x in range(grid+1):
                    p=points[y*stride*(GRID+1)+x*stride]
                    px,py=2*x/grid-1,1-2*y/grid
                    self.assertAlmostEqual(p.dx,math.hypot(px,py)*.1,places=6)
                    self.assertAlmostEqual(p.dy,x*y/(grid*grid),places=6)
                    self.assertAlmostEqual(p.rotation,(math.atan2(py,px) if px or py else 0)*.01,places=6)

    def test_pixel_restrictions_and_demo(self):
        for formula in ("psp_low=0;",):
            self.assertEqual(self.parse(f"[preset00]\nper_pixel_1={formula}".encode())[0],3)
        lines="\n".join(f"per_pixel_{i}=dx=0+0+0+0+0;" for i in range(1,MAX_OPS//10+2))
        self.assertEqual(self.parse(f"[preset00]\n{lines}".encode())[0],2)
        self.assertEqual(self.parse(b"[preset00]\nper_pixel_2=dx=0;")[0],2)
        code,preset,_=self.parse((ROOT / "psp-client/presets/grid-twist-demo.milk").read_bytes())
        self.assertEqual(code,0)
        self.assertLessEqual(preset.pixel_program.count,64)

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
