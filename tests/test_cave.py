"""Only the new geometry and music GU integration; no preset collection sweep."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class CaveTests(unittest.TestCase):
    def test_cave_bounds_and_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'cave'
            subprocess.run(['cc','-std=c11','-O3','-Wall','-Wextra','-Werror',
                '-fsanitize=undefined,float-cast-overflow','-I',str(ROOT/'psp-client'),
                str(ROOT/'tests/cave_harness.c'),str(ROOT/'psp-client/cave_visual.c'),
                str(ROOT/'psp-client/cave_paths.c'),
                '-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)

    def test_gu_adapter_lcd_tv_and_teardown(self):
        adapter=(ROOT/'psp-client/milkdrop_gu.c').read_text()
        adapter=re.sub(r'#include <psp\w+\.h>\n','',adapter)
        harness=(ROOT/'tests/milkdrop_harness.c').read_text().replace('/* GU_ADAPTER */',adapter)
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'gu.c';binary=Path(directory)/'gu'
            source.write_text(harness)
            sources=['milkdrop_warp','milkdrop_preset','preset_math','milkdrop_signal',
                     'milkdrop_wave','milkdrop_wave_extra','milkdrop_decor','milkdrop_texture','cave_visual','cave_paths']
            subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                '-I',str(ROOT/'psp-client'),str(source),
                *[str(ROOT/'psp-client'/(s+'.c')) for s in sources],
                '-lpng','-ljpeg','-lz','-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary),'--cave'],check=True,timeout=10)
