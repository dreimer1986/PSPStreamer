"""Offline protocol tests; no serial port or console is accessed."""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('xbox_kd', Path(__file__).parents[1]/'xbox-client/tools/kd_serial.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def packet(kind, payload, ident=0x80800000):
    return struct.pack('<IHHII', 0x30303030, kind, len(payload), ident, sum(payload)) + payload + b'\xaa'


class KDTests(unittest.TestCase):
    def setUp(self):
        self.kd = module.KD(-1)
        self.sent = []
        self.kd.write = self.sent.append
        self.output = contextlib.redirect_stdout(io.StringIO())
        self.output.__enter__()

    def tearDown(self):
        self.output.__exit__(None, None, None)

    def test_fragmented_packet_and_duplicate_ack_without_resume(self):
        payload = bytearray(64)
        struct.pack_into('<I', payload, 0, 0x3030)
        struct.pack_into('<I', payload, 32, 0x80000003)
        data = packet(7, payload)
        for byte in data:
            self.kd.feed(bytes([byte]))
        self.kd.feed(data)
        self.assertEqual(len(self.sent), 2)
        self.assertTrue(all(struct.unpack_from('<H', x, 4)[0] == 4 for x in self.sent))
        self.assertTrue(self.kd.stopped)
        self.assertEqual(self.kd.exception, 0x80000003)

    def test_checksum_failure_requests_resend(self):
        data = bytearray(packet(3, b'payload'))
        data[-2] ^= 1
        self.kd.feed(data)
        self.assertEqual(struct.unpack_from('<H', self.sent[0], 4)[0], 5)

    def test_read_request_layout_and_ack_toggle(self):
        self.kd.stopped = True
        self.kd.request(0x3130, struct.pack('<QII', 0xffffffff80000000, 32, 0))
        wire = self.sent[0]
        self.assertEqual(len(wire), 73)
        self.assertEqual(struct.unpack_from('<Q', wire, 32)[0], 0xffffffff80000000)
        self.kd.feed(struct.pack('<IHHII',0x69696969,4,0,0x80800000,0))
        self.assertEqual(self.kd.txid, 0x80800001)
        self.assertIsNone(self.kd.pending)

    def test_live_capture_checksums(self):
        capture = Path('/tmp/xbox-reactos-serial.EErUuN.log')
        if not capture.exists():
            self.skipTest('Optional hardware capture absent')
        self.kd.feed(capture.read_bytes())
        self.assertGreater(len(self.sent), 100)
        self.assertTrue(any(struct.unpack_from('<H', x, 4)[0] == 4 for x in self.sent))


if __name__ == '__main__':
    unittest.main()
