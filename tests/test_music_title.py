"""Exercise the actual title rasterizer, not a duplicate text implementation."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import re

ROOT=Path(__file__).resolve().parents[1]


class TitleTests(unittest.TestCase):
    def test_monkey_normalized_uv_and_milkdrop_pixel_uv(self):
        source=(ROOT/'psp-client/milkdrop_title.h').read_text()
        cave=source.split('if(cave && cave_scene) {',1)[1].split('} else {',1)
        def uv(block):
            return [(int(u),int(v)) for u,v in re.findall(r'\(MdVertex\)\{(\d+),(\d+),',block)]
        self.assertEqual(uv(cave[0]),[(0,0),(0,1),(1,0),(1,1)])
        self.assertEqual(uv(cave[1]),[(0,0),(512,64)])

    def test_utf8_bounds_duration_and_explicit_repeat(self):
        source=(ROOT/'psp-client/milkdrop_title.h').read_text().split('static void md_title_draw(')[0]
        code='''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static void *memalign(int alignment,size_t size){(void)alignment;return malloc(size);}
static void sceKernelDcacheWritebackRange(void *p,int size){assert(p&&size==65536);}
'''+source+'''
int main(void){
    unsigned char font[81920];memset(font,255,sizeof(font));
    md_title(font,"Grüße","Song",100,0);assert(md_title_pixels&&md_title_until==5000100);
    md_title(font,"Grüße","Song",1000,0);assert(md_title_until==5000100);
    md_title(font,"Grüße","Song",1000,1);assert(md_title_until==5001000);
    char long_title[600];memset(long_title,'A',599);long_title[599]=0;
    md_title(font,long_title,long_title,2000,1);
    assert(md_title_until==5002000);
    for(int i=0;i<512*64;i++)assert(md_title_pixels[i]==0||md_title_pixels[i]==65535);
    free(md_title_pixels);return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            binary=str(Path(tmp)/'title')
            subprocess.run(['cc','-x','c','-','-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-o',binary],input=code,text=True,check=True)
            subprocess.run([binary],check=True,timeout=5)
