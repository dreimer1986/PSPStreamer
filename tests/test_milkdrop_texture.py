import ctypes
import subprocess
import tempfile
import unittest
import io
import os
from PIL import Image as PillowImage
from pathlib import Path
from tools.generate_texture_fixture import png_bytes

ROOT = Path(__file__).resolve().parents[1]


class Image(ctypes.Structure):
    _fields_ = [('pixels', ctypes.c_void_p), ('width', ctypes.c_uint), ('height', ctypes.c_uint)]


class TextureTests(unittest.TestCase):
    def test_png_bounds_alpha_and_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            library = root / 'texture.so'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-shared', '-fPIC', '-fsanitize=undefined',
                            str(ROOT / 'psp-client/milkdrop_texture.c'),
                            '-lpng', '-ljpeg', '-lz', '-o', str(library)], check=True)
            lib = ctypes.CDLL(str(library))
            lib.md_image_load.argtypes = [ctypes.c_char_p, ctypes.POINTER(Image)]
            lib.md_image_free.argtypes = [ctypes.POINTER(Image)]
            path = root / 'test.png'
            for w, h in ((16, 16), (64, 128), (256, 256)):
                path.write_bytes(png_bytes(w, h))
                image = Image()
                self.assertEqual(lib.md_image_load(str(path).encode(), ctypes.byref(image)), 1)
                self.assertEqual((image.width, image.height), (w, h))
                self.assertEqual(image.pixels % 64, 0)
                pixels = ctypes.string_at(image.pixels, w*h*4)
                for y in range(h):
                    for x in range(w):
                        pos = ((y//8)*(w//4)+x//4)*128+(y%8)*16+(x%4)*4
                        color = bytes((30,200,255) if (x//16+y//16)%2 else (255,180,30))
                        alpha = 255 if (x-w/2)**2+(y-h/2)**2 < (w*.44)**2 else 0
                        self.assertEqual(pixels[pos:pos+4], color+bytes((alpha,)))
                lib.md_image_free(ctypes.byref(image))
                self.assertFalse(image.pixels)
                lib.md_image_free(ctypes.byref(image))
            for data in (b'', b'not a png', png_bytes()[:100],
                         png_bytes(257, 128), png_bytes(63, 64), png_bytes(1,1), b'x'*(1024*1024+1)):
                path.write_bytes(data)
                image = Image(None, 7, 9)
                self.assertEqual(lib.md_image_load(str(path).encode(), ctypes.byref(image)), 0)
                self.assertEqual((image.pixels, image.width, image.height), (None, 7, 9))
            self.assertEqual(lib.md_image_load(str(root/'missing.png').encode(), ctypes.byref(image)), 0)
            self.assertEqual((ROOT/'psp-client/presets/textures/checker.png').read_bytes(), png_bytes())
            def jpeg(w,h,mode='RGB',progressive=False):
                stream=io.BytesIO()
                color=(40,120,200) if mode=='RGB' else 120 if mode=='L' else (0,0,0,0)
                PillowImage.new(mode,(w,h),color).save(stream,format='JPEG',quality=95,progressive=progressive)
                return stream.getvalue()
            for w,h,mode,progressive in ((150,150,'RGB',False),(1024,512,'RGB',False),
                                         (33,65,'RGB',True),(1,1,'L',False)):
                path.write_bytes(jpeg(w,h,mode,progressive))
                image=Image()
                self.assertEqual(lib.md_image_load(str(path).encode(),ctypes.byref(image)),1)
                expected=lambda n: min(256,max(16,1<<(n-1).bit_length()))
                self.assertEqual((image.width,image.height),(expected(w),expected(h)))
                self.assertEqual(image.pixels%64,0)
                pixels=ctypes.string_at(image.pixels,image.width*image.height*4)
                self.assertTrue(all(a==255 for a in pixels[3::4]))
                reference=(40,120,200) if mode=='RGB' else (120,120,120)
                for offset in range(0,len(pixels),4):
                    self.assertTrue(all(abs(pixels[offset+c]-reference[c])<=3 for c in range(3)))
                lib.md_image_free(ctypes.byref(image))
            # Non-uniform, non-power-of-two JPEG: check exact column/row
            # selection as well as the GU swizzle, not just solid colours.
            pattern=PillowImage.new('RGB',(150,73))
            pattern.putdata([((x*13+y*3)%256,(x*7+y*11)%256,(x*3+y*17)%256)
                             for y in range(73) for x in range(150)])
            encoded=io.BytesIO();pattern.save(encoded,format='JPEG',quality=95)
            path.write_bytes(encoded.getvalue())
            reference=PillowImage.open(io.BytesIO(encoded.getvalue())).convert('RGB')
            image=Image()
            self.assertEqual(lib.md_image_load(str(path).encode(),ctypes.byref(image)),1)
            pixels=ctypes.string_at(image.pixels,image.width*image.height*4)
            for y in range(image.height):
                for x in range(image.width):
                    pos=((y//8)*(image.width//4)+x//4)*128+(y%8)*16+(x%4)*4
                    expected=reference.getpixel((x*150//image.width,y*73//image.height))
                    self.assertTrue(all(abs(pixels[pos+c]-expected[c])<=2 for c in range(3)))
            lib.md_image_free(ctypes.byref(image))
            for data in (jpeg(1025,16),jpeg(16,16,'CMYK'),jpeg(150,150)[:-30],b'\xff\xd8'+b'x'*100):
                path.write_bytes(data)
                image=Image(None,7,9)
                self.assertEqual(lib.md_image_load(str(path).encode(),ctypes.byref(image)),0)
                self.assertEqual((image.pixels,image.width,image.height),(None,7,9))
            if os.environ.get('GEISS_PRESET_DIR'):
                source=Path(os.environ['GEISS_PRESET_DIR'])/'Geiss - Artifact 6d (junky warp distortion).jpg'
                image=Image()
                self.assertEqual(lib.md_image_load(str(source).encode(),ctypes.byref(image)),1)
                self.assertEqual((image.width,image.height),(256,256))
                with PillowImage.open(source) as original:
                    rgb=original.convert('RGB')
                    pixels=ctypes.string_at(image.pixels,256*256*4)
                    for y in range(256):
                        for x in range(256):
                            pos=((y//8)*64+x//4)*128+(y%8)*16+(x%4)*4
                            expected=rgb.getpixel((x*rgb.width//256,y*rgb.height//256))
                            self.assertTrue(all(abs(pixels[pos+c]-expected[c])<=3 for c in range(3)))
                lib.md_image_free(ctypes.byref(image))

    def test_no_fixed_startup_fps(self):
        for language in ('de', 'en'):
            source = (ROOT / f'psp-client/lang_{language}.h').read_text()
            start = next(line for line in source.splitlines() if '[TXT_STARTING_VIDEO]' in line)
            self.assertNotIn('FPS', start.upper())
