"""Focused host checks of the actual OC allocation function and pixel renderer."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class OverlayAllocationTests(unittest.TestCase):
    def test_oc_allocation_paths(self):
        source = (ROOT / 'psp-overclock/main.c').read_text()
        function = source.split('static int overlay_allocate(void) {', 1)[1].split('\n}\n', 1)[0]
        harness = r'''
#include <assert.h>
#include <string.h>
#include <stddef.h>
typedef struct { unsigned char pixels[32]; } OcOverlay;
#define PSP_SMEM_High 1
static int overlay_enabled, overlay_memory=-1, oc_hook_buffer_count;
static OcOverlay *overlay, *oc_hook_buffers, storage[4];
static int calls, frees, result=7, no_address;
static int sceKernelAllocPartitionMemory(int p,const char *n,int t,size_t b,void *a) {
    assert(p==2 && n && t==PSP_SMEM_High && b==sizeof(storage) && !a);
    calls++;return result;
}
static void *sceKernelGetBlockHeadAddr(int id){assert(id==7);return no_address?NULL:storage;}
static void sceKernelFreePartitionMemory(int id){assert(id==7);frees++;}
static int overlay_allocate(void) {
''' + function + r'''
}
int main(void) {
    assert(overlay_allocate()==0 && calls==0 && !overlay);
    overlay_enabled=2;result=-5;
    assert(overlay_allocate()==-5 && !overlay_enabled && calls==1 && !overlay);
    overlay_enabled=1;result=7;no_address=1;
    assert(overlay_allocate()<0 && !overlay_enabled && frees==1 && overlay_memory==-1);
    overlay_enabled=2;no_address=0;memset(storage,255,sizeof(storage));
    assert(overlay_allocate()==0 && overlay==storage && oc_hook_buffers==storage+1);
    assert(oc_hook_buffer_count==3 && overlay_memory==7);
    for(unsigned i=0;i<sizeof(storage);i++)assert(((unsigned char *)storage)[i]==0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / 'allocation.c'
            path.write_text(harness)
            binary = path.with_suffix('')
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                            str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_pixel_renderer(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = pathlib.Path(temp) / 'pixels'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                            '-I', str(ROOT / 'psp-overclock'),
                            str(ROOT / 'tests/oc_overlay_harness.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
