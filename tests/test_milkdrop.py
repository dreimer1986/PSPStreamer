import re
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class MilkDropTests(unittest.TestCase):
    def test_phase_budgets_native_clear_and_global_memory(self):
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'phase'
            subprocess.run(['cc','-std=c11','-O3','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined,float-cast-overflow','-I',str(ROOT/'psp-client'),
                str(ROOT/'tests/preset_phase_harness.c'),str(ROOT/'psp-client/preset_math.c'),
                '-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)

    def test_fft_profiling_preserves_spectrum(self):
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'fft-profile'
            subprocess.run(['cc','-std=c11','-O3','-Wall','-Wextra','-Werror',
                '-fsanitize=undefined','-I',str(ROOT/'psp-client'),
                str(ROOT/'tests/fft_profile_harness.c'),
                str(ROOT/'psp-client/milkdrop_wave_extra.c'),'-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)

    def test_owned_formula_storage_failure_and_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);binary=root/'storage';other=root/'other.milk'
            lines=['[preset00]','zoom=1']
            for prefix in ('per_frame_','per_pixel_','wave_0_per_point'):
                for i in range(1,31):
                    lines.append(prefix+str(i)+'='+'q1=q1+.001;'*20)
            other.write_text('\n'.join(lines))
            sources=['milkdrop_preset.c','preset_math.c','milkdrop_signal.c','milkdrop_wave.c',
                     'milkdrop_wave_extra.c','milkdrop_decor.c','milkdrop_warp.c']
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',
                '-fsanitize=undefined,float-cast-overflow','-I',str(ROOT/'psp-client'),
                str(ROOT/'tests/preset_storage_harness.c'),
                *[str(ROOT/'psp-client'/s) for s in sources],
                '-Wl,--wrap=calloc,--wrap=realloc,--wrap=free','-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary),str(ROOT/'psp-client/presets/eel-sqrt-demo.milk'),str(other)],
                           check=True,timeout=60)

    def test_integer_conversion_matches_reference_x87(self):
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'integers'
            subprocess.run(['cc','-std=c11','-O3','-Wall','-Wextra','-Werror',
                            '-fsanitize=undefined,float-cast-overflow','-I',str(ROOT/'psp-client'),
                            str(ROOT/'tests/preset_integer_harness.c'),'-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_exact_trig_cache_and_preset_output(self):
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'trig'
            sources=['milkdrop_preset.c','milkdrop_signal.c','milkdrop_wave.c',
                     'milkdrop_wave_extra.c','milkdrop_decor.c','milkdrop_warp.c']
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                            '-I',str(ROOT/'psp-client'),str(ROOT/'tests/preset_trig_harness.c'),
                            *[str(ROOT/'psp-client'/name) for name in sources],'-lm','-o',str(binary)],check=True)
            presets=[ROOT/'psp-client/presets/dense-wave-demo.milk']
            if os.environ.get('GEISS_PRESET_DIR'):
                presets.append(Path(os.environ['GEISS_PRESET_DIR'])/'Fed + Geiss - Cauldron painterly 5 strippy rmx 1 auraltshift.milk')
            subprocess.run([str(binary),*[str(p) for p in presets]],check=True,timeout=30)

    def test_optional_phase_profiler(self):
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'profile'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                            '-I',str(ROOT/'psp-client'),str(ROOT/'tests/milkdrop_profile_harness.c'),
                            '-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_compact_thick_wave_preserves_delayed_geometry(self):
        adapter=(ROOT/'psp-client/milkdrop_gu.c').read_text()
        helper=adapter[adapter.index('typedef struct { unsigned int color;'):adapter.index('static unsigned int *md_list;')]
        harness=(ROOT/'tests/milkdrop_thick_harness.c').read_text().replace('/* THICK_HELPER */',helper)
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'thick.c';binary=Path(directory)/'thick'
            source.write_text(harness)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                            '-I',str(ROOT/'psp-client'),str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_square_only_toggles_spectrum_and_file_preset(self):
        source=(ROOT / "psp-client/main.c").read_text()
        music=source[source.index("static int play_audio_once("):source.index("static int play_audio(")]
        block=music[music.index("if ((pad.Buttons & PSP_CTRL_SQUARE)"):]
        block=block[:block.index("old = pad.Buttons;")]
        harness=(ROOT / "tests/music_visual_toggle.c").read_text().replace("/* SQUARE_BLOCK */",block)
        with tempfile.TemporaryDirectory() as directory:
            source_file=Path(directory)/"toggle.c"
            binary=Path(directory)/"toggle"
            source_file.write_text(harness)
            subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-fsanitize=undefined",
                            str(source_file),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

    def test_warp_and_gu_ownership(self):
        adapter = (ROOT / "psp-client/milkdrop_gu.c").read_text()
        adapter = re.sub(r"#include <psp\w+\.h>\n", "", adapter)
        harness = (ROOT / "tests/milkdrop_harness.c").read_text().replace(
            "/* GU_ADAPTER */", adapter)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "milkdrop.c"
            binary = Path(directory) / "milkdrop"
            source.write_text(harness)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-I", str(ROOT / "psp-client"),
                            str(source), str(ROOT / "psp-client/milkdrop_warp.c"),
                            str(ROOT / "psp-client/milkdrop_preset.c"),
                            str(ROOT / "psp-client/preset_math.c"),
                            str(ROOT / "psp-client/milkdrop_signal.c"),
                            str(ROOT / "psp-client/milkdrop_wave.c"),
                            str(ROOT / "psp-client/milkdrop_wave_extra.c"),
                            str(ROOT / "psp-client/milkdrop_decor.c"),
                            str(ROOT / "psp-client/milkdrop_texture.c"),
                            "-lpng", "-ljpeg", "-lz", "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary),str(ROOT / "psp-client/presets/branch-beat-demo.milk"),
                            str(ROOT / "psp-client/presets/init-orbit-demo.milk"),
                            str(ROOT / "psp-client/presets/memory-pulse-demo.milk"),
                            str(ROOT / "psp-client/presets/grid-twist-demo.milk"),
                            str(ROOT / "psp-client/presets/outline-demo.milk"),
                            str(ROOT / "psp-client/presets/script-wave-demo.milk"),
                            str(ROOT / "psp-client/presets/spiral-wave-demo.milk"),
                            *[str(ROOT / ("psp-client/presets/"+name+"-demo.milk")) for name in
                              ("spiro","pulse-spiro","complex","line","dual-line","fft-spectrum","motion-clock","wave-switch","shape-orbits","custom-wave","custom-wave-fft","custom-spectrum","smooth-wave","image-effects","shape-instances","large-wave","eel-memory-orbit","eel-grid-logic","eel-wave-loop","external-texture","dense-wave","fine-mesh","extended-formula","extended-memory","extended-point-program","shape-batches")],
                            str(ROOT/'psp-client/presets/native-inputs-demo.milk'),
                            str(ROOT/'psp-client/presets/offscreen-shapes-demo.milk'),
                            str(ROOT/'psp-client/presets/geometry-range-demo.milk'),
                            str(ROOT/'psp-client/presets/eel-sqrt-demo.milk'),
                            str(ROOT/'psp-client/presets/formula-storage-demo.milk'),
                            *([str(Path(os.environ['GEISS_PRESET_DIR'])/'Geiss - Explosion nz+.milk')] if os.environ.get('GEISS_PRESET_DIR') else [])],
                           check=True, timeout=120)

    def test_opt_in_and_teardown(self):
        source = (ROOT / "psp-client/main.c").read_text()
        music = source[source.index("static int play_audio_once("):source.index("static int play_audio(")]
        self.assertIn("music_visual_active = 0;", music)
        self.assertIn("visual_preset == 4", music)
        self.assertLess(music.index("md_load_preset("), music.index("sceKernelCreateThread("))
        self.assertEqual(music.count("md_load_preset("), 2) # startup and gated automatic switch
        self.assertIn("next_preset_tick",music)
        self.assertIn("if (rendered <= 0)", music)
        self.assertIn("preset_error = md_runtime_error;", music)
        self.assertIn("PSP_CTRL_SQUARE", music)
        self.assertIn("if (!(music_visual_active && fullscreen))", music)
        self.assertIn("md_stop();\n    music_visual_active = 0;", music)
        audio = source[source.index("static int audio_thread(SceSize args, void *argp) {"):
                       source.index('#include "music_ui.h"')]
        self.assertNotIn("md_", audio)
        adapter = (ROOT / "psp-client/milkdrop_gu.c").read_text()
        for forbidden in ("sceDisplaySet", "sceGuDispBuffer", "sceGuSwapBuffers", "sceAudio", "sceMpeg"):
            self.assertNotIn(forbidden, adapter)

    def test_music_view_survives_track_replacement_without_retaining_gu(self):
        source = (ROOT / "psp-client/main.c").read_text()
        music = source[source.index("static int play_audio_once("):source.index("static int play_audio(")]
        self.assertIn("fullscreen = music_saved_fullscreen", music)
        self.assertIn("visual_preset = music_saved_visual_preset == 4 ? 4 : 0", music)
        self.assertIn("music_saved_fullscreen = fullscreen;", music)
        self.assertIn("music_saved_visual_preset = visual_preset;", music)
        self.assertIn("visual_preset != 4 || preset_result == MD_FILE_OK", music)
        self.assertLess(music.index("md_load_preset("), music.index("if (md_start())"))
        self.assertLess(music.index("if (md_start())"), music.index("sceKernelCreateThread("))
        failure = music[music.index("if (start_result < 0)"):music.index("remote_result = offline_music ? 0 : music_remote_start()")]
        self.assertIn("md_stop(); music_visual_active = 0;", failure)
        teardown = music[music.index("music_remote_stop();"):]
        self.assertIn("md_stop();", teardown)
        self.assertIn("music_visual_active = 0;", teardown)
        self.assertNotIn("music_saved_", teardown)
        video = source[source.index("static int play_h264("):]
        self.assertNotIn("music_saved_", video)


if __name__ == "__main__":
    unittest.main()
