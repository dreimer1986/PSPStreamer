from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class HidEventTests(unittest.TestCase):
    def test_actual_callback_sequence(self):
        source = (ROOT/'streammaster/main/gamepad.c').read_text()
        callback = source[source.index('static void hid('):source.index('static bool can_send(')]
        harness = (ROOT/'tests/streammaster_hid_events_harness.c').read_text().replace('/* HID_CALLBACK */', callback)
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder)/'hid-events'
            subprocess.run(['cc','-x','c','-','-std=c11','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-I',str(ROOT),'-o',str(binary)],
                           input=harness,text=True,check=True)
            subprocess.run([str(binary)],check=True,timeout=5)
