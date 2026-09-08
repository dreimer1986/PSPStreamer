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
                            "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)

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


if __name__ == "__main__":
    unittest.main()
