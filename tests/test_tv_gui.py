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

    def test_music_priority_scope_and_initial_draw_precede_audio(self):
        source = (ROOT / "psp-client/main.c").read_text()
        music = source[source.index("static int play_audio("):source.index("static int play_h264(")]
        self.assertLess(music.index("tv_draw_music(title, 0);"),
                        music.index('sceKernelCreateThread("PSPStreamerMusic"'))
        self.assertLess(music.index("lcd_draw_music(title, 0);"),
                        music.index('sceKernelCreateThread("PSPStreamerMusic"'))
        # Both exit paths restore the caller's priority, including create failure.
        self.assertEqual(music.count("return "), 2)
        self.assertEqual(music.count("music_ui_restore_priority(previous_ui_priority);"), 2)
        self.assertNotIn("tv_draw_view(TV_VIEW_MUSIC", music)


if __name__ == "__main__":
    unittest.main()
