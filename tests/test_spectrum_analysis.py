from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class SpectrumAnalysisTests(unittest.TestCase):
    def test_analysis_and_bounds(self):
        with tempfile.TemporaryDirectory() as folder:
            binary=Path(folder)/'analysis'
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                '-I',str(ROOT/'psp-client'),str(ROOT/'tests/spectrum_analysis_harness.c'),'-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)

    @unittest.skipUnless(os.environ.get('MILKDROP_REFERENCE'), 'set MILKDROP_REFERENCE to original vis_milk2 source')
    def test_desktop_reference(self):
        source=Path(os.environ['MILKDROP_REFERENCE'])
        with tempfile.TemporaryDirectory() as folder:
            obj=Path(folder)/'signal.o';binary=Path(folder)/'reference'
            subprocess.run(['cc','-std=c11','-O2','-I',str(ROOT/'psp-client'),'-c',str(ROOT/'psp-client/milkdrop_signal.c'),'-o',str(obj)],check=True)
            subprocess.run(['c++','-O2','-I',str(ROOT/'psp-client'),'-I',str(source),
                str(ROOT/'tests/spectrum_reference.cpp'),str(source/'fft.cpp'),str(obj),'-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)

    def test_fullscreen_bands(self):
        with tempfile.TemporaryDirectory() as folder:
            binary=Path(folder)/'fullscreen'
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                '-I',str(ROOT/'psp-client'),str(ROOT/'tests/spectrum_fullscreen_harness.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=5)
