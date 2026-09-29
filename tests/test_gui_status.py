from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class GuiStatusTests(unittest.TestCase):
    def test_wifi_sources_throttle_disconnect_and_playback_pause(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = str(Path(folder) / 'wifi')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=undefined', '-I', str(ROOT/'psp-client'),
                            str(ROOT/'tests/wifi_status_harness.c'), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_lcd_alpha_edges_and_clipping(self):
        main = (ROOT/'psp-client/main.c').read_text()
        glyph = main[main.index('static void gui_small_glyph('):main.index('static void gui_draw_small_glyph(')]
        code = '''#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t u32;
#define SUBTITLE_FONT_CELL_HEIGHT 20
#define SUBTITLE_FONT_CELL_WIDTH 16
static unsigned char atlas[256*320];
static unsigned char *subtitle_font=atlas;
''' + glyph + '''
int main(void){
    u32 pixels[100];memset(pixels,0,sizeof(pixels));
    atlas[0]=128;gui_small_glyph(pixels,10,10,10,0,0,0,0xFFFFFF);
    assert(pixels[0]==0x808080);assert(pixels[1]==0);
    atlas[0]=255;gui_small_glyph(pixels,10,10,10,0,9,9,0x123456);
    assert(pixels[99]==0x123456);
    gui_small_glyph(pixels,10,10,10,0,-6,-8,0xFFFFFF);
    assert(pixels[0]==0x808080);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'glyph.c';source.write_text(code)
            binary=str(Path(folder)/'glyph')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(source),'-o',binary],check=True)
            subprocess.run([binary],check=True)

    def test_transfer_clock_covers_worker_and_restores_before_error_ui(self):
        source=(ROOT/'psp-client/offline_ui.h').read_text()
        source=source[source.index('static int offline_transfer('):source.index('static void offline_scan(')]
        self.assertLess(source.index('playback_clock(download_cpu_mhz)'), source.index('sceKernelStartThread'))
        self.assertIn('sceKernelDeleteThread(worker);playback_clock_idle();return started;',source)
        self.assertLess(source.index('download_running=0;\n    playback_clock_idle();'),source.index('if(download_result<0)'))
