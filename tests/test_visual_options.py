from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class VisualOptionsTests(unittest.TestCase):
    def test_config_and_transition_rules(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'options'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=undefined', '-I', str(ROOT / 'psp-client'),
                            str(ROOT / 'tests/visual_options_harness.c'),
                            str(ROOT / 'psp-client/milkdrop_signal.c'),
                            '-lm', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=5)
