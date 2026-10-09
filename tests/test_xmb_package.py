"""Focused packaging checks; no player, network or preset regression suite."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package_xmb', ROOT/'tools/package_xmb.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class XmbPackageTests(unittest.TestCase):
    def donor(self, path):
        parts = [b'SFO unchanged', b'old-icon', b'', b'', b'old-background', b'',
                 b'EXECUTABLE unchanged\x00\xff', b'ARCHIVE unchanged']
        pos = 40; offsets = []
        for part in parts:
            offsets.append(pos); pos += len(part)
        path.write_bytes(b'\0PBP'+struct.pack('<I8I', 0x10000, *offsets)+b''.join(parts))
        return parts

    def test_actual_assets_preserve_executable_and_sfo(self):
        with tempfile.TemporaryDirectory() as temp:
            src, dst = Path(temp)/'old.pbp', Path(temp)/'new.pbp'
            original = self.donor(src)
            module.package(src, dst)
            version, actual = module.read_pbp(dst)
            self.assertEqual(version, struct.pack('<I', 0x10000))
            for i in (0, 3, 6, 7): self.assertEqual(actual[i], original[i])
            self.assertTrue(actual[2].startswith(b'PSMF0014'))
            self.assertTrue(actual[5].startswith(b'RIFF'))
            self.assertLess(len(actual[2])+len(actual[5]), 500*1024)

    def test_no_in_place_or_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp)/'old.pbp'; self.donor(src)
            with self.assertRaises(ValueError): module.package(src, src)
            dst = Path(temp)/'existing.pbp'; dst.write_bytes(b'keep')
            with self.assertRaises(FileExistsError): module.package(src, dst)
            self.assertEqual(dst.read_bytes(), b'keep')

    def test_invalid_offsets(self):
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp)/'bad.pbp'; self.donor(src)
            blob = bytearray(src.read_bytes()); struct.pack_into('<I', blob, 12, 0)
            src.write_bytes(blob)
            with self.assertRaises(ValueError): module.read_pbp(src)


if __name__ == '__main__': unittest.main()
