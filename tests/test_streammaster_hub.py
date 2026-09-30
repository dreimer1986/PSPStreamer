"""Regression checks for the generated polling code, not the whole USB stack."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import sys

ROOT = Path(__file__).resolve().parents[1]


class HubPollTests(unittest.TestCase):
    def test_generated_poll_and_status_dispatch(self):
        sdk = ROOT / '.toolchain/esp-idf/components/usb/ext_hub.c'
        if not sdk.exists():
            self.skipTest('ESP-IDF v5.5.1 checkout required')
        with tempfile.TemporaryDirectory() as folder:
            generated = Path(folder) / 'hub.c'
            subprocess.run([sys.executable, str(ROOT/'streammaster/hub_poll_build.py'),
                            str(sdk), str(generated)], check=True)
            source = generated.read_text()
            response = source[source.index('static void handle_port_status('):
                              source.index('static bool device_control_request(')]
            poll = source[source.index('void sm_usb_hub_poll(void)'):]
            harness = (ROOT/'tests/streammaster_hub_harness.c').read_text()
            harness = harness.replace('/* RESPONSE */', response).replace('/* POLL */', poll)
            binary = Path(folder)/'hub-test'
            subprocess.run(['cc','-x','c','-','-std=c11','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-o',str(binary)],
                           input=harness, text=True, check=True)
            subprocess.run([str(binary)], check=True, timeout=5)

