"""Targeted regression for full-HD MPEG-2 pictures exceeding the old 256 KiB cap."""
import struct
import subprocess
import unittest
from unittest.mock import patch
from psp_streamer import xbox_player as xbox


class PacketBounds(unittest.TestCase):
    def test_large_real_i_picture(self):
        data = subprocess.check_output([
            'ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
            'testsrc2=size=1920x1080:rate=24000/1001,noise=alls=45:allf=t',
            '-t', '0.6', '-an', '-c:v', 'mpeg2video', '-b:v', '16000k',
            '-maxrate', '32000k', '-bufsize', '32000k', '-bf', '0', '-g', '12',
            '-f', 'mpegts', 'pipe:1'])
        with patch.object(xbox, 'MAX_PES', 256*1024):
            with self.assertRaisesRegex(ValueError, 'PES too large'):
                list(xbox.Transport().feed(data))
        transport = xbox.Transport()
        records = []
        for offset in range(0, len(data), 997):
            records.extend(transport.feed(data[offset:offset+997]))
        records.extend(transport.finish())
        sizes = [struct.unpack_from('<I', r, 4)[0] for r in records if r[:1] == b'V']
        self.assertGreater(max(sizes), 256*1024)
        self.assertLessEqual(max(sizes), xbox.MAX_PACKET)
        self.assertEqual(records[-1][:1], b'E')

    def test_limit_still_bounded(self):
        self.assertEqual(len(xbox.record(b'V', 0, bytes(xbox.MAX_PACKET))), xbox.MAX_PACKET+16)
        with self.assertRaises(ValueError):
            xbox.record(b'V', 0, bytes(xbox.MAX_PACKET+1))
        transport = xbox.Transport()
        transport.pes[0x100] = bytearray(xbox.MAX_PES)
        # Payload continuation on the same PID must reject unbounded PES data.
        with self.assertRaisesRegex(ValueError, 'PES too large'):
            list(transport.feed(bytes([0x47, 1, 0, 0x10])+bytes(184)))


if __name__ == '__main__':
    unittest.main()
