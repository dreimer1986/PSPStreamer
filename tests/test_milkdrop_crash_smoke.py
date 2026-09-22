"""Opt-in, narrowly scoped renderer check for the two reported Martin crashes."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CrashSmokeTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('MILKDROP_CRASH_PRESETS'), 'external Martin fixtures')
    def test_martin_renderer_and_trace(self):
        collection = Path(os.environ['MILKDROP_CRASH_PRESETS'])
        fixtures = [collection / f'martin - {name}.milk'
                    for name in ('lock and release', 'sphery tales')]
        adapter = (ROOT/'psp-client/milkdrop_gu.c').read_text()
        adapter = re.sub(r'#include <psp\w+\.h>\n', '', adapter)
        harness = (ROOT/'tests/milkdrop_harness.c').read_text().split('int main(')[0]
        harness = harness.replace('/* GU_ADAPTER */', adapter)
        # These presets control gamma/echo themselves, unlike the fixed fixtures.
        harness = harness.replace('assert(composition_width<=512*expected_passes);', '')
        harness = harness.replace('assert(composition_width==512*expected_passes);', '')
        harness = re.sub(r'assert\(count==MD_CUSTOM_POINTS.*?\);',
                         'assert(count>0 && count<=2*MD_CUSTOM_POINTS);', harness, flags=re.S)
        harness += r'''
static int persisted, stages;
static void trace(const char *stage,int persist) {
    assert(stage && *stage); stages++; persisted+=persist;
}
int main(int argc,char **argv) {
    assert(argc==3);
    void *vram=(void *)0x44000000;
    assert(mmap(vram,edram_size,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==vram);
    unsigned char bands[12];memset(bands,50,sizeof(bands));
    short pcm[1152];for(int i=0;i<1152;i++)pcm[i]=(short)(8000*sin(i*.03));
    md_trace_hook=trace;
    for(int file=1;file<argc;file++) {
        MdFileError error;pm_reset_globals();
        assert(md_load_preset(argv[file],&md_custom_preset,&error)==MD_FILE_OK);
        for(int tv=0;tv<2;tv++) {
            expected_left=expected_top=0;
            expected_width=tv?720:480;expected_height=tv?480:272;
            assert(md_start());persisted=stages=0;
            for(int frame=0;frame<12;frame++) {
                visualization_pcm_publish(pcm,576);test_time+=100000;
                assert(md_frame(tv,1,bands,50,test_time,3)==1);
                assert(covered_width==expected_width);
            }
            assert(persisted==12 && stages==72);
            md_stop();
        }
    }
    md_free_preset(&md_custom_preset);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)/'smoke.c'
            binary = Path(directory)/'smoke'
            source.write_text(harness)
            sources = ['milkdrop_warp', 'milkdrop_preset', 'preset_math', 'milkdrop_signal',
                       'milkdrop_wave', 'milkdrop_wave_extra', 'milkdrop_decor', 'milkdrop_texture']
            subprocess.run(['cc', '-std=c11', '-fsanitize=address,undefined',
                '-I', str(ROOT/'psp-client'), str(source),
                *[str(ROOT/'psp-client'/f'{name}.c') for name in sources],
                '-lpng', '-ljpeg', '-lz', '-lm', '-o', str(binary)], check=True)
            subprocess.run([str(binary), *map(str, fixtures)], check=True, timeout=30)
