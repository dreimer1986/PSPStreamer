"""Exercise the actual remote playback loop with deterministic media outcomes."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class RemoteAutoplayTests(unittest.TestCase):
    def test_seek_is_consumed_before_eof_stop_or_error(self):
        source = (ROOT / 'psp-client/main.c').read_text()
        start = source.index('                do {\n                    result = remote_is_audio ?')
        end = source.index('                } while (1);', start) + len('                } while (1);')
        harness = (ROOT / 'tests/remote_autoplay_harness.c').read_text()
        harness = harness.replace('/* REMOTE_PLAYBACK_LOOP */', source[start:end])
        with tempfile.TemporaryDirectory() as directory:
            c_file = Path(directory) / 'autoplay.c'
            binary = Path(directory) / 'autoplay'
            c_file.write_text(harness)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=undefined', str(c_file), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=5)
