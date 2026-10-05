"""Targeted Xbox text-page worker checks, no console or local TCP listener."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class XboxSubtitleTests(unittest.TestCase):
    def test_worker(self):
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'subtitles'
            subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined',
                '-fno-sanitize-recover=all','-I'+str(ROOT/'psp-client'),
                str(ROOT/'tests/xbox_subtitle_client.c'),'-pthread','-o',str(binary)],check=True)
            # The managed runner uses ptrace, incompatible with LSan; ASan and
            # UBSan remain active. The harness checks cleared resource owners.
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
            subprocess.run([str(binary)],env=env,check=True,timeout=10)

if __name__=='__main__':unittest.main()
