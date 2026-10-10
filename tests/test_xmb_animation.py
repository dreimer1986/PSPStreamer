import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
import struct
import shutil
import sys

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('asset',ROOT/'tools/build_xmb_background.py')
asset=importlib.util.module_from_spec(spec);spec.loader.exec_module(asset)

class AnimationTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('ffmpeg'),'FFmpeg unavailable')
    def test_cli_short_video_loops_and_names_output(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d);video=path/'short.mp4'
            subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','color=blue:s=32x32:r=12',
                '-t','0.1','-c:v','mpeg4',str(video)],check=True)
            subprocess.run([sys.executable,str(ROOT/'tools/build_xmb_background.py'),
                '--video',str(video),'--pic1',str(ROOT/'psp-client/assets/pic1.png'),
                '--output-dir',str(path/'animations'),'--width','16','--height','8',
                '--duration','0.5'],check=True,capture_output=True)
            output=path/'animations/28108CC0-198253.xba'
            data=output.read_bytes();header=asset.HEADER.unpack_from(data)
            self.assertEqual(header[8],6)
            self.assertEqual(len(data),48+header[7]*6)
            self.assertEqual(header[10],asset.fnv(data[48:]))

    def test_pbp_extraction_and_identity(self):
        pic1=(ROOT/'psp-client/assets/pic1.png').read_bytes()
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'EBOOT.PBP'
            # PIC1 is the fifth PBP section; SND0 follows it.
            offsets=[40]*5+[40+len(pic1)]*3
            original=struct.pack('<10I',0x50425000,0x10000,*offsets)+pic1
            path.write_bytes(original)
            self.assertEqual(asset.read_pic1(path,True),pic1)
            self.assertEqual(path.read_bytes(),original)
            self.assertEqual(asset.asset_name(pic1),'28108CC0-198253.xba')
            path.write_bytes(struct.pack('<10I',0x50425000,0x10000,*([40]*8)))
            with self.assertRaises(ValueError):asset.read_pic1(path,True)
            offsets[6]=0
            path.write_bytes(struct.pack('<10I',0x50425000,0x10000,*offsets)+pic1)
            with self.assertRaises(ValueError):asset.read_pic1(path,True)

    def test_payload(self):
        with tempfile.TemporaryDirectory() as d:
            exe=str(Path(d)/'payload')
            subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
                '-fsanitize=undefined','-I',str(ROOT/'psp-controller'),
                str(ROOT/'tests/xmb_animation_test.c'),'-o',exe],check=True)
            subprocess.run([exe],check=True)

    def test_swizzle_and_edge_padding(self):
        for w,h in ((16,8),(17,9),(320,180),(480,272)):
            raw=bytes((i*37)%256 for i in range(w*h*2))
            data=asset.swizzle(raw,w,h);stride,rows,size=asset.geometry(w,h)
            self.assertEqual(len(data),size)
            for y in range(rows):
                for x in range(stride):
                    dest=(y//8)*stride*8+(x//16)*128+(y%8)*16+x%16
                    src=(min(y,h-1)*w+min(x//2,w-1))*2+x%2
                    self.assertEqual(data[dest],raw[src])
        with self.assertRaises(ValueError):asset.swizzle(b'',16,8)

    def test_shipped_asset(self):
        data=(ROOT/'psp-controller/assets/xmb-background.xba').read_bytes()
        magic,version,size,hash_,w,h,stride,frame,frames,period,checksum,reserved=asset.HEADER.unpack_from(data)
        pic1=(ROOT/'psp-client/assets/pic1.png').read_bytes()
        self.assertEqual((magic,version,reserved),(asset.MAGIC,1,0))
        self.assertEqual((size,hash_),(len(pic1),asset.fnv(pic1)))
        self.assertEqual((stride,frame),(asset.geometry(w,h)[0],asset.geometry(w,h)[2]))
        self.assertEqual(len(data),asset.HEADER.size+frame*frames)
        self.assertLessEqual(frame*frames,asset.MAX_BYTES)
        self.assertEqual(asset.fnv(data[asset.HEADER.size:]),checksum)
        self.assertGreaterEqual(period,33333)
        self.assertNotEqual(data[48:48+frame],data[48+frame:48+2*frame])

if __name__=='__main__':unittest.main()
