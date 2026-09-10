import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class MilkDropTests(unittest.TestCase):
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
                            str(ROOT / "psp-client/milkdrop_decor.c"),
                            "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary),str(ROOT / "psp-client/presets/branch-beat-demo.milk"),
                            str(ROOT / "psp-client/presets/init-orbit-demo.milk"),
                            str(ROOT / "psp-client/presets/memory-pulse-demo.milk"),
                            str(ROOT / "psp-client/presets/grid-twist-demo.milk")],
                           check=True, timeout=20)

    def test_opt_in_and_teardown(self):
        source = (ROOT / "psp-client/main.c").read_text()
        music = source[source.index("static int play_audio("):source.index("static int play_h264(")]
        self.assertIn("music_visual_active = 0;", music)
        self.assertIn("visual_preset == 4", music)
        self.assertLess(music.index("md_load_preset("), music.index("sceKernelCreateThread("))
        self.assertEqual(music.count("md_load_preset("), 1)
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
        music = source[source.index("static int play_audio("):source.index("static int play_h264(")]
        self.assertIn("fullscreen = music_saved_fullscreen", music)
        self.assertIn("visual_preset = music_saved_visual_preset", music)
        self.assertIn("music_saved_fullscreen = fullscreen;", music)
        self.assertIn("music_saved_visual_preset = visual_preset;", music)
        self.assertIn("visual_preset != 4 || preset_result == MD_FILE_OK", music)
        self.assertLess(music.index("md_load_preset("), music.index("if (md_start())"))
        self.assertLess(music.index("if (md_start())"), music.index("sceKernelCreateThread("))
        failure = music[music.index("if (start_result < 0)"):music.index("remote_result = music_remote_start()")]
        self.assertIn("md_stop(); music_visual_active = 0;", failure)
        teardown = music[music.index("music_remote_stop();"):]
        self.assertIn("md_stop();", teardown)
        self.assertIn("music_visual_active = 0;", teardown)
        self.assertNotIn("music_saved_", teardown)
        video = source[source.index("static int play_h264("):]
        self.assertNotIn("music_saved_", video)


if __name__ == "__main__":
    unittest.main()
