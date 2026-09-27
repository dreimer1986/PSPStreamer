from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
class SpectrumPaintTests(unittest.TestCase):
    def test_incremental_palette_segments_peaks(self):
        with tempfile.TemporaryDirectory() as folder:
            binary=Path(folder)/'paint'
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fsanitize=undefined,address',
                '-I',str(ROOT/'psp-client'),str(ROOT/'tests/spectrum_paint_harness.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=15)
