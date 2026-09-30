from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BluetoothUsbTests(unittest.TestCase):
    def test_discovery_and_diagnostics(self):
        source = (ROOT/'streammaster/main/gamepad.c').read_text()
        record = source[source.index('static void record_probe('):source.index('static SmPad pad=')]
        discovery = source[source.index('static int open_adapter('):source.index('static void usb_task(')]
        harness = (ROOT/'tests/streammaster_bt_usb_harness.c').read_text()
        harness = harness.replace('/* RECORD */', record).replace('/* OPEN */', discovery)
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder)/'bt-usb'
            subprocess.run(['cc','-x','c','-','-std=c11','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-I',str(ROOT),'-o',str(binary)],
                           input=harness, text=True, check=True)
            subprocess.run([str(binary)], check=True, timeout=5)
