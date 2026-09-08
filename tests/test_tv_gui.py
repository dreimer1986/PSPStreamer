"""Native layout/ownership tests; not a PSP/OSSC hardware emulator."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class TvGuiTests(unittest.TestCase):
    def test_native_renderer_and_mode_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "tv_gui"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-I", str(ROOT / "psp-client"),
                            str(ROOT / "tests/tv_gui_harness.c"),
                            str(ROOT / "psp-client/language.c"), "-o", str(binary)],
                           check=True, cwd=ROOT / "psp-client")
            subprocess.run([str(binary)], check=True, cwd=ROOT / "psp-client", timeout=15)

    def test_skin_dimensions_and_no_lcd_debug_reset_after_playback(self):
        self.assertEqual((ROOT / "psp-client/assets/menu_skin_tv.raw").stat().st_size, 720 * 480 * 4)
        source = (ROOT / "psp-client/main.c").read_text()
        main = source[source.index("int main(void)"):]
        self.assertEqual(main.count("pspDebugScreenInit();"), 1)  # startup before choosing TV
        self.assertEqual(main.count("ui_restore_after_playback();"), 2)
        self.assertIn('tv_ui=%s\\n', source)
        self.assertIn('!strcmp(line + 6, "auto")', source)


if __name__ == "__main__":
    unittest.main()
