import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('check_xmb_capture', ROOT/'tools/check_xmb_capture.py')
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class XmbProbeTests(unittest.TestCase):
    def test_memory_boundaries(self):
        source = r'''
#include <assert.h>
#include "xmb_probe_bounds.h"
int main(void) {
    assert(xmb_probe_range(0x08800000,8192));
    assert(xmb_probe_range(0x88000000,8192));
    assert(xmb_probe_range(0x08900000,2*1024*1024));
    assert(!xmb_probe_range(0,8192));
    assert(!xmb_probe_range(0x04000000,8192));
    assert(!xmb_probe_range(0xbc000000,8192));
    assert(!xmb_probe_range(0x08800000,0));
    assert(!xmb_probe_range(0x09ffffff,2));
    assert(!xmb_probe_range(0x08800000,0xffffffff));
    assert(!xmb_probe_range(0xffffffff,1));
    assert(!xmb_probe_range(0x28800000,8192));
    assert(!xmb_probe_range(0x48800000,8192));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/'bounds.c'
            path.write_text(source)
            subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                            '-I',str(ROOT/'psp-controller'),str(path),'-o',str(path.with_suffix(''))],check=True)
            subprocess.run([str(path.with_suffix(''))],check=True)

    def test_capture_integrity_and_incomplete_session(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            text = 'PSPConsolizer XMB probe v1 READ ONLY\n'
            for name in ('scePaf_Module','game_plugin_module','vsh_module'):
                text += f'module={name} segments=1\n'
                (path/f'{name}-0.bin').write_bytes(b'a')
                text += f'segment={name}-0.bin address=08800000 size=1 fnv1a=E40C292C complete=1\n'
            (path/'capture.txt').write_text(text)
            with self.assertRaisesRegex(ValueError,'Incomplete'):
                capture.validate(path)
            (path/'capture.txt').write_text(text+'finish=complete completed_mask=7\n')
            self.assertEqual(len(capture.validate(path)),3)
            (path/'vsh_module-0.bin').write_bytes(b'b')
            with self.assertRaisesRegex(ValueError,'Checksum mismatch'):
                capture.validate(path)


if __name__ == '__main__':
    unittest.main()
