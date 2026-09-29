import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from tools.vaapi_psp_test import command_for, create_case


class VaapiTestToolTests(unittest.TestCase):
    def test_only_video_options_change(self):
        for tv in (False, True):
            def cmd(hw):
                return command_for('source.mkv', 1, hardware=hw, device='/dev/dri/renderD128',
                                   tv=tv, seconds=60, start=3)
            sw, hw = cmd(False), cmd(True)
            for option in ('-af', '-ar', '-ac', '-c:a', '-b:a', '-flvflags', '-f', '-ss', '-t'):
                self.assertEqual(sw[sw.index(option)+1], hw[hw.index(option)+1])
            self.assertEqual(hw[hw.index('-coder')+1], 'cabac')
            self.assertEqual(hw[hw.index('-bf')+1], '0')
            self.assertEqual(hw[hw.index('-refs')+1], '1')
            self.assertEqual(hw[hw.index('-level:v')+1], '30')
            self.assertLess(hw.index('-vaapi_device'), hw.index('-i'))
            self.assertNotIn('-hwaccel', hw)
            self.assertNotIn('-x264-params', hw)
            self.assertEqual(hw[hw.index('-vf')+1],
                             sw[sw.index('-vf')+1].removesuffix('format=yuv420p')+'format=nv12,hwupload')

    def test_software_package_and_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root/'input.mkv'
            subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=s=128x96:r=20',
                            '-f', 'lavfi', '-i', 'sine=frequency=440', '-t', '2',
                            '-c:v', 'libx264', '-c:a', 'pcm_s16le', str(source)], check=True)
            folder = create_case(source, root/'test', False, '', False, 1, 0, 0)
            job = json.loads((folder/'job.json').read_text())
            self.assertEqual(job['state'], 'ready')
            self.assertEqual((folder/'ready').read_text(), '1')
            self.assertEqual(len(job['files']), 3)
            for entry in job['files']:
                data = (folder/entry['name']).read_bytes()
                self.assertEqual(len(data), entry['size'])
                self.assertEqual(hashlib.sha256(data).hexdigest(), entry['sha256'])
            self.assertGreater((folder/'seek.idx').stat().st_size, 0)
