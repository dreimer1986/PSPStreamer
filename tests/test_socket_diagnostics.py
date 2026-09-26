import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class SocketDiagnosticsTests(unittest.TestCase):
    def test_actual_close_ownership_and_no_retry(self):
        source = (ROOT/'psp-client/tls_transport.c').read_text()
        close = source[source.index('int tls_close(int fd) {'):]
        close = close[:close.index('\n}')+2]
        harness = (ROOT/'tests/socket_close_harness.c').read_text().replace('/* ACTUAL_CLOSE */', close)
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory)/'close'
            subprocess.run(['cc', '-x', 'c', '-', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=undefined', '-o', str(binary)], input=harness, text=True, check=True)
            subprocess.run([str(binary)], check=True, timeout=3)

    def test_balance_errors_and_periodic_snapshots(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'sockets'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=undefined', '-I', str(ROOT/'psp-client'),
                            str(ROOT/'tests/socket_diagnostics_harness.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=3)
