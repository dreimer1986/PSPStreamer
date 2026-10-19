"""Focused host checks; these do not emulate the display controller or CRT."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class InterlacedTests(unittest.TestCase):
    def test_fields_scaling_and_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            for source in ('tv_interlaced_test.c', 'fusa_scale_test.c', 'fusa_display_policy_test.c'):
                with self.subTest(source=source):
                    output = pathlib.Path(directory) / source.removesuffix('.c')
                    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra',
                                    '-Werror', '-Wno-unused-function', '-fsanitize=undefined,address',
                                    str(ROOT / 'tests' / source), '-o', str(output)], check=True)
                    subprocess.run([str(output)], check=True, timeout=15)

    def test_sony_button_hold(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / 'hold'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tests/tvout_hold_test.c'), '-o', str(output)], check=True)
            for case in ('confirm', 'timeout', 'slow-log', 'suspend'):
                subprocess.run([str(output), case], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
