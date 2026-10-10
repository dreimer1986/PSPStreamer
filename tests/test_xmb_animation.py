import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('asset',ROOT/'tools/build_xmb_background.py')
asset=importlib.util.module_from_spec(spec);spec.loader.exec_module(asset)

class AnimationTests(unittest.TestCase):
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
