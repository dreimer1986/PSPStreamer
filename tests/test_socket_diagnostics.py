import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class SocketDiagnosticsTests(unittest.TestCase):
    def test_balance_errors_and_periodic_snapshots(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'sockets'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=undefined', '-I', str(ROOT/'psp-client'),
                            str(ROOT/'tests/socket_diagnostics_harness.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=3)
