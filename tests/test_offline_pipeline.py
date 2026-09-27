from pathlib import Path
import hashlib
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OfflinePipelineTests(unittest.TestCase):
    def test_hash_read_ahead_and_failures(self):
        source = (ROOT / "psp-client/offline_ui.h").read_text()
        function = source[source.index("static int offline_hash("):source.index("static int offline_download_worker(")]
        harness = (ROOT / "tests/offline_hash_harness.c").read_text().replace("/* HASH */", function)
        with tempfile.TemporaryDirectory() as folder:
            binary, media = Path(folder) / "hash", Path(folder) / "media"
            subprocess.run(["cc", "-x", "c", "-", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-deprecated-declarations", "-fsanitize=address,undefined",
                            "-I", str(ROOT / "tests"), "-lcrypto", "-o", str(binary)],
                           input=harness, text=True, check=True)
            for size in (0, 1, 55, 56, 63, 64, 65, 131071, 131072, 131073, 1500003):
                data = (bytes(range(251)) * (size // 251 + 1))[:size]
                media.write_bytes(data)
                digest = hashlib.sha256(data).hexdigest()
                for mode in (0, 1, 4, 5):
                    subprocess.run([str(binary), str(media), "0" * 64 if mode == 5 else digest, str(mode)],
                                   check=True, timeout=5)
                if size:
                    for mode in (2, 3):
                        subprocess.run([str(binary), str(media), digest, str(mode)], check=True, timeout=5)

    def test_recovery_download_batching(self):
        source = (ROOT / "psp-client/recovery_log.h").read_text()
        harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef int SceUID;
#define PSP_O_WRONLY 1
#define PSP_O_CREAT 2
#define PSP_O_APPEND 4
static int debug_enabled=1,opens,closes;
static unsigned long long now;
static char disk[65536];static unsigned used;
static unsigned long long sceKernelGetSystemTimeWide(void){return now;}
static void sceKernelDelayThread(unsigned us){(void)us;}
static int sceIoOpen(const char *p,int f,int m){(void)p;(void)f;(void)m;opens++;return 4;}
static int sceIoWrite(int f,const void *p,unsigned n){assert(f==4);if(n>53)n=53;assert(used+n<sizeof(disk));memcpy(disk+used,p,n);used+=n;return n;}
static int sceIoClose(int f){assert(f==4);closes++;return 0;}
/* LOG */
int main(void){
    recovery_log("before",0,0,"direct");assert(opens==1&&closes==1);
    recovery_batch_begin();
    for(int i=0;i<10;i++)recovery_log("progress",0,0,"buffered");
    assert(opens==1);now=4999999;recovery_flush_due();assert(opens==1);
    now=5000000;recovery_flush_due();assert(opens==2&&closes==2);
    assert(strstr(disk,"buffered"));
    recovery_batch_lock=1;recovery_log("busy",0,0,"dropped");recovery_batch_lock=0;
    for(int i=0;i<100;i++)recovery_log("fill",0,0,"bounded memory");
    assert(recovery_batch_size<=sizeof(recovery_batch));
    recovery_batch_end();assert(opens==3&&strstr(disk,"diagnostic overflow"));
    recovery_log("after",0,0,"direct");assert(opens==4);
    debug_enabled=0;recovery_log("disabled",0,0,"");assert(opens==4);
    return 0;
}
'''.replace("/* LOG */", source)
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "log"
            subprocess.run(["cc", "-x", "c", "-", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-o", str(binary)], input=harness, text=True, check=True)
            subprocess.run([str(binary)], check=True, timeout=5)
