"""Focused optical transport checks; no PSP/default transport tests."""
import json
import shutil
import struct
import subprocess
import unittest
from types import SimpleNamespace
from psp_streamer.spdif import Transport, burst, command


class Optical(unittest.TestCase):
    def plan(self, codec, channels, mode, rate=48000):
        def probe(*args):
            return SimpleNamespace(stdout=json.dumps({'streams': [dict(
                codec_name=codec, channels=channels, sample_rate=str(rate))]}))
        return command(['ffmpeg', '-i', 'source', '-c:a', 'libmp3lame',
                        '-ar', '44100', '-ac', '2', '-f', 'flv', 'pipe:1'],
                       'source', 0, mode, probe)

    def test_conversion_modes(self):
        for codec in ('ac3', 'dts', 'truehd', 'eac3', 'aac', 'flac'):
            cmd, label = self.plan(codec, 6, 'spdif_pcm')
            self.assertIn('pcm_s16le', cmd)
            self.assertIn('asetnsamples=n=960:p=0', cmd[cmd.index('-af')+1])
            self.assertEqual(cmd[cmd.index('-ac')+1], '2')
        for codec in ('truehd', 'eac3', 'aac', 'flac'):
            cmd, label = self.plan(codec, 8, 'spdif_auto_ac3')
            self.assertEqual(cmd[cmd.index('-c:a')+1], 'ac3')
            self.assertEqual(cmd[cmd.index('-ac')+1], '6')
        for codec in ('ac3', 'dts'):
            cmd, label = self.plan(codec, 6, 'spdif_auto_ac3')
            self.assertEqual(cmd[cmd.index('-c:a')+1], 'copy')
            if codec == 'dts': self.assertIn('dca_core', cmd)

    @unittest.skipUnless(shutil.which('ffmpeg'), 'ffmpeg unavailable')
    def test_pcm_packetization_and_tail(self):
        data = subprocess.check_output(['ffmpeg', '-v', 'error', '-f', 'lavfi',
            '-i', 'sine=frequency=440:sample_rate=48000:duration=0.107',
            '-ac', '2', '-c:a', 'pcm_s16le', '-af',
            'asetnsamples=n=960:p=0', '-f', 'matroska', 'pipe:1'])
        transport = Transport(audio_only=True)
        output = []
        for at in range(0, len(data), 137):
            output.extend(transport.feed(data[at:at+137]))
        output.extend(transport.finish())
        packets = [p for p in output if p[0] == 8]
        lengths = [(int.from_bytes(p[1:4], 'big')-13)//4 for p in packets]
        self.assertEqual(lengths, [960]*5+[336])
        self.assertEqual([int.from_bytes(p[4:7], 'big') for p in packets],
                         [0, 20, 40, 60, 80, 100])
        for p in packets:
            self.assertEqual(p[11:16], b'\xf0SMA1')
            self.assertEqual(struct.unpack('<II', p[16:24]), (48000, 0))

    @unittest.skipUnless(shutil.which('ffmpeg'), 'ffmpeg unavailable')
    def test_ac3_carrier_matches_ffmpeg(self):
        ac3 = subprocess.check_output(['ffmpeg', '-v', 'error', '-f', 'lavfi',
            '-i', 'sine=frequency=440:sample_rate=48000', '-frames:a', '1',
            '-c:a', 'ac3', '-b:a', '448k', '-f', 'ac3', 'pipe:1'])
        # Encoder flushing may append a second frame despite frames:a=1.
        # This fixed 448 kbit/s, 48 kHz fixture has 1792 bytes per AC-3 frame.
        ac3 = ac3[:1792]
        reference = subprocess.run(['ffmpeg', '-v', 'error', '-f', 'ac3',
            '-i', 'pipe:0', '-c:a', 'copy', '-f', 'spdif', 'pipe:1'],
            input=ac3, stdout=subprocess.PIPE, check=True).stdout
        rate, actual = burst('A_AC3', ac3)
        self.assertEqual(rate, 48000)
        self.assertEqual(actual, reference)

    @unittest.skipUnless(shutil.which('ffmpeg'), 'ffmpeg unavailable')
    def test_dts_carrier_matches_ffmpeg(self):
        data = subprocess.check_output(['ffmpeg', '-v', 'error', '-f', 'lavfi',
            '-i', 'sine=frequency=440:sample_rate=48000', '-frames:a', '1',
            '-ac', '2', '-c:a', 'dca', '-strict', '-2', '-b:a', '768k',
            '-f', 'dts', 'pipe:1'])
        size = ((data[5] & 3) << 12 | data[6] << 4 | data[7] >> 4) + 1
        data = data[:size]
        reference = subprocess.run(['ffmpeg', '-v', 'error', '-f', 'dts',
            '-i', 'pipe:0', '-c:a', 'copy', '-f', 'spdif', 'pipe:1'],
            input=data, stdout=subprocess.PIPE, check=True).stdout
        rate, actual = burst('A_DTS', data)
        self.assertEqual(rate, 48000)
        self.assertEqual(actual, reference)


if __name__ == '__main__':
    unittest.main()
