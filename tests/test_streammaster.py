from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StreamMasterTests(unittest.TestCase):
    def test_socket_channels_and_psp_transport(self):
        # Replace only platform headers; compile the actual production code.
        esp = (ROOT / "streammaster/main/sockets.c").read_text()
        psp = (ROOT / "psp-client/streammaster_transport.c").read_text()
        esp = "\n".join(line for line in esp.splitlines()
                        if not line.startswith('#include "'))
        psp = "\n".join(line for line in psp.splitlines()
                        if not line.startswith(('#include <psp', '#include <kubridge', '#include "')))
        harness = (ROOT / "tests/streammaster_transport_harness.c").read_text()
        harness = harness.replace("/* ACTUAL_ESP_SOCKETS */", esp).replace("/* ACTUAL_PSP_TRANSPORT */", psp)
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "transport"
            subprocess.run(["cc", "-x", "c", "-", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-I", str(ROOT), "-o", str(binary)],
                           input=harness, text=True, check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_protocol_and_network_configuration(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "protocol"
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined",
                            str(ROOT / "tests/streammaster_protocol_harness.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
