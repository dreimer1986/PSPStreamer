from pathlib import Path
import hashlib
import subprocess
import tempfile
import unittest
import ctypes.util
import os

ROOT = Path(__file__).resolve().parents[1]


class OfflinePipelineTests(unittest.TestCase):
    def test_download_instrumentation_compiles(self):
        source = (ROOT/'psp-client/offline_ui.h').read_text()
        code = (ROOT/'tests/offline_http_harness.c').read_text().replace(
            '/* OFFLINE_HTTP */', source[source.index('static int offline_connect('):source.index('static int offline_hash(')])
        with tempfile.TemporaryDirectory() as folder:
            subprocess.run(['cc', '-x', 'c', '-', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT/'tests'), '-o', str(Path(folder)/'download')],
                           input=code, text=True, check=True)

    def test_esp_metric_logs_fit_recovery_lines(self):
        source=(ROOT/'psp-client/offline_ui.h').read_text()
        function=source[source.index('static void offline_usb_metrics_log('):source.index('static int offline_connect(')]
        harness=r'''
#include <assert.h>
#include <stdio.h>
#include "streammaster/protocol.h"
static unsigned logs;
static int stm_usb_metrics(SmUsbMetrics *out){memset(out,255,sizeof(*out));return 1;}
static void recovery_log(const char *event,int rc,int code,const char *detail){
    char line[256];int n=snprintf(line,sizeof(line),"tick_ms=%llu event=%s result=%d http=%d stage=%s\n",
                                ~0ULL,event,rc,code,detail);
    assert(n>0 && (unsigned)n<sizeof(line));logs++;
}
/* METRICS */
int main(void){SmUsbMetrics baseline={0};offline_usb_metrics_log(&baseline);assert(logs==3);}
'''.replace('/* METRICS */',function)
        with tempfile.TemporaryDirectory() as folder:
            binary=Path(folder)/'metrics'
            subprocess.run(['cc','-x','c','-','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT),'-o',str(binary)],
                           input=harness,text=True,check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

    def test_batched_sha256_matches_mbedtls(self):
        sdk = Path(os.environ.get('PSPDEV', str(Path.home()/'psp-streamer/.toolchain/pspdev')))
        header = sdk/'psp/include/mbedtls/sha256.h'
        library = 'libmbedcrypto.so.7'
        try:
            ctypes.CDLL(library)
        except OSError:
            self.skipTest('Requires Mbed TLS 2.x host library')
        if not header.exists():
            self.skipTest('Requires Mbed TLS 2.x host library and PSP SDK header')
        harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "offline_sha256.h"
int main(void) {
    unsigned char bytes[1500003],a[32],b[32];
    for(unsigned i=0;i<sizeof(bytes);i++)bytes[i]=i%251;
    const unsigned lengths[]={0,1,55,56,63,64,65,127,128,129,131071,131072,131073,1500003};
    const unsigned chunks[]={1,7,63,64,65,131072,1500003};
    for(unsigned i=0;i<sizeof(lengths)/sizeof(*lengths);i++) {
        for(unsigned j=0;j<sizeof(chunks)/sizeof(*chunks);j++) {
            mbedtls_sha256_context stock,fast;
            mbedtls_sha256_init(&stock);mbedtls_sha256_init(&fast);
            assert(!mbedtls_sha256_starts_ret(&stock,0));assert(!mbedtls_sha256_starts_ret(&fast,0));
            for(unsigned pos=0;pos<lengths[i];) {
                unsigned n=lengths[i]-pos;if(n>chunks[j])n=chunks[j];
                assert(!mbedtls_sha256_update_ret(&stock,bytes+pos,n));
                assert(!offline_sha256_update(&fast,bytes+pos,n));pos+=n;
                assert(!memcmp(stock.state,fast.state,sizeof(stock.state)));
                assert(!memcmp(stock.total,fast.total,sizeof(stock.total)));
            }
            assert(!mbedtls_sha256_finish_ret(&stock,a));assert(!mbedtls_sha256_finish_ret(&fast,b));
            assert(!memcmp(a,b,32));mbedtls_sha256_free(&stock);mbedtls_sha256_free(&fast);
        }
    }
    /* Counter carry without allocating a 4 GiB file. */
    mbedtls_sha256_context stock,fast;mbedtls_sha256_init(&stock);
    assert(!mbedtls_sha256_starts_ret(&stock,0));stock.total[0]=0xffffffc0U;fast=stock;
    assert(!mbedtls_sha256_update_ret(&stock,bytes,128));assert(!offline_sha256_update(&fast,bytes,128));
    assert(fast.total[0]==64 && fast.total[1]==1 && !memcmp(stock.state,fast.state,sizeof(stock.state)));
    puts("Batched SHA-256: 98 chunk/boundary combinations and counter carry match Mbed TLS");
}
'''
        with tempfile.TemporaryDirectory() as folder:
            base=Path(folder);(base/'mbedtls').mkdir()
            (base/'mbedtls/sha256.h').write_text(header.read_text())
            (base/'mbedtls/config.h').write_text('')
            binary=base/'sha'
            subprocess.run(['cc','-x','c','-','-std=c11','-O2','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-I',folder,'-I',str(ROOT/'psp-client'),
                            str(ROOT/'psp-client/offline_sha256.c'),'-l:'+library,'-o',str(binary)],
                           input=harness,text=True,check=True)
            subprocess.run([str(binary)],check=True,timeout=10)

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
typedef unsigned SceSize;
static int sceKernelCreateThread(const char *n,int (*fn)(SceSize,void *),int p,int s,int a,void *o){(void)n;(void)fn;(void)p;(void)s;(void)a;(void)o;return -1;}
static int sceKernelStartThread(int t,int a,void *p){(void)t;(void)a;(void)p;return -1;}
static int sceKernelDeleteThread(int t){(void)t;return 0;}
static int sceKernelWaitThreadEnd(int t,void *p){(void)t;(void)p;return 0;}
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
    recovery_download_timing();
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

    def test_recovery_async_writer(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / 'async-log'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-pthread',
                            str(ROOT/'tests/recovery_async_harness.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
