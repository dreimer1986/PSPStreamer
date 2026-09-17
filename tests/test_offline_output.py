import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class OfflineOutputTests(unittest.TestCase):
    def test_pc_copied_unicode_names_resolve_through_fat_alias(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / 'filename'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                            '-I', str(ROOT / 'psp-client'), str(ROOT / 'tests/offline_filename_harness.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=3)

    def test_profile_guard_distinguishes_output_from_decoder_errors(self):
        source = (ROOT / 'psp-client/main.c').read_text()
        start = source.index('    tvout_video_active = tvout_begin_video() == 0;')
        end = source.index('    if (tvout_video_active) memset', start)
        guard = source[start:end]
        harness = '''
#include <assert.h>
#include <stdint.h>
static int offline_active, offline_profile_tv, tvout_video_active, cable, restored;
static const char *video_step;
#define TXT_DOWNLOAD_PROFILE 1
static const char *tr(int id) {(void)id;return "profile mismatch";}
static int tvout_begin_video(void) {return cable?0:-1;}
static void tvout_end_video(void) {restored++;}
static int check(void) {
''' + guard + '''
return 0;
}
int main(void) {
    offline_active=1;
    for(cable=0;cable<=1;cable++)for(offline_profile_tv=0;offline_profile_tv<=1;offline_profile_tv++) {
        restored=0;video_step=0;
        int result=check();
        if(cable==offline_profile_tv) assert(!result && !restored);
        else {
            assert((uint32_t)result==0xFFFFFA87u && video_step);
            assert(!tvout_video_active && restored==cable);
        }
    }
    offline_active=0;
    for(cable=0;cable<=1;cable++)assert(!check());
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / 'profile'
            subprocess.run(['cc', '-x', 'c', '-', '-Wall', '-Wextra', '-Werror', '-o', str(binary)],
                           input=harness, text=True, check=True)
            subprocess.run([str(binary)], check=True, timeout=3)
