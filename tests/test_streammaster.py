from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StreamMasterTests(unittest.TestCase):
    def test_protocol_and_network_configuration(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "protocol"
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined",
                            str(ROOT / "tests/streammaster_protocol_harness.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
