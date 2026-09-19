"""Render every translated help page with the real PSP glyphs and drawing code."""
import subprocess
import re
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class HelpTests(unittest.TestCase):
    def test_fullscreen_triangle_and_legacy_chord_toggle_once(self):
        source = (ROOT / 'psp-client/main.c').read_text()
        conditions = re.findall(r'if \(([^\n]+)\) \{\n\s+(?:fullscreen|video_fullscreen) = !', source)
        self.assertEqual(len(conditions), 2)
        functions = []
        for i, condition in enumerate(conditions):
            condition = condition.replace('previous_buttons', 'old')
            functions.append(f'static int toggle{i}(Pad pad, unsigned int old) {{return {condition};}}')
        harness = '''#include <assert.h>
typedef struct {unsigned int Buttons;} Pad;
#define PSP_CTRL_CROSS 1
#define PSP_CTRL_TRIANGLE 2
''' + '\n'.join(functions) + '''
int main(void) {
    for(int mode=0;mode<2;mode++) {
        int (*toggle)(Pad,unsigned int)=mode?toggle1:toggle0;
        assert(toggle((Pad){2},0)); /* Triangle alone. */
        assert(toggle((Pad){3},0)); /* Old simultaneous chord. */
        assert(toggle((Pad){3},1)); /* X, then Triangle. */
        assert(!toggle((Pad){3},2)); /* Triangle, then X: no second toggle. */
        assert(!toggle((Pad){2},2));assert(!toggle((Pad){3},3));
        assert(!toggle((Pad){0},2));assert(!toggle((Pad){1},0));
    }
}
'''
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / 'fullscreen'
            subprocess.run(['cc', '-x', 'c', '-', '-Wall', '-Wextra', '-Werror', '-o', str(binary)],
                           input=harness, text=True, check=True)
            subprocess.run([str(binary)], check=True)

    def test_pages_layout_navigation_and_return(self):
        main = (ROOT / 'psp-client/main.c').read_text()
        tv = (ROOT / 'psp-client/tv_gui.h').read_text()
        def section(source, start, end):
            at = source.index(start)
            return source[at:source.index(end, at)]
        drawing = section(main, 'static void gui_rect(', 'static void gui_line(')
        drawing += section(main, 'static void gui_draw_small_glyph(', '/* Analogue VU ballistics:')
        drawing += section(tv, 'static int tv_text(', 'static u32 tv_indicator_color(')
        harness = (ROOT / 'tests/help_harness.c').read_text().replace('/* PRODUCTION_DRAWING */', drawing)
        with tempfile.TemporaryDirectory() as temp:
            source, binary = Path(temp) / 'help.c', Path(temp) / 'help'
            source.write_text(harness)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                '-I', str(ROOT / 'psp-client'), str(source), str(ROOT / 'psp-client/language.c'),
                '-o', str(binary)], check=True)
            subprocess.run([str(binary)], cwd=ROOT / 'psp-client', check=True, timeout=10)
