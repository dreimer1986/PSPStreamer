from pathlib import Path
import importlib.util
import unittest

ROOT = Path(__file__).resolve().parents[1]

class HidRestoreTests(unittest.TestCase):
    def test_exact_sdk_change_and_version_guard(self):
        spec = importlib.util.spec_from_file_location('hid_restore', ROOT/'streammaster/hid_restore_build.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        path = ROOT/'.toolchain/esp-idf/components/bt/host/bluedroid/btc/profile/std/hid/btc_hh.c'
        if not path.exists():
            self.skipTest('ESP-IDF checkout unavailable')
        source = path.read_bytes()
        patched = module.generate(source)
        self.assertEqual(patched, source.replace(b'// btc_storage_load_bonded_hid_info();', b'btc_storage_load_bonded_hid_info();'))
        self.assertEqual(path.read_bytes(), source)
        with self.assertRaises(ValueError):
            module.generate(source+b'\n')
