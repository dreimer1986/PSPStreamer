"""Exercise actual LCD drawing code with mapped host VRAM, not a PSP emulator."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class LcdMusicTests(unittest.TestCase):
    def test_incremental_frames_match_full_renderer(self):
        source = (ROOT / "psp-client/main.c").read_text()
        # Extract unchanged production primitives rather than duplicate them.
        def between(start, end):
            offset = source.index(start)
            return source[offset:source.index(end, offset)]

        functions = between("static void gui_rect(", "static void receiver_hud(int frames);")
        functions += between("static void gui_draw_small_glyph(", "/* Analogue VU ballistics:")
        functions += between("static void vu_ballistics_step(", "/* Integer square root")
        functions += between("static void menu_skin_load(", "/* Receiver strip for the non-fullscreen")
        functions += between("static void gui_library_shell(const char *section) {", "static void show(")
        harness = (ROOT / "tests/lcd_music_harness.c").read_text().replace(
            "/* PRODUCTION_FUNCTIONS */", functions)
        with tempfile.TemporaryDirectory() as directory:
            c_file = Path(directory) / "lcd_music.c"
            binary = Path(directory) / "lcd_music"
            c_file.write_text(harness)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-I", str(ROOT / "psp-client"),
                            str(c_file), str(ROOT / "psp-client/language.c"),
                            "-o", str(binary)], check=True, cwd=ROOT / "psp-client")
            subprocess.run([str(binary)], check=True, cwd=ROOT / "psp-client", timeout=15)


if __name__ == "__main__":
    unittest.main()
