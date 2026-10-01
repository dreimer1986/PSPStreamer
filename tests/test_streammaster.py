from pathlib import Path
import subprocess
import tempfile
import unittest
import http.server
import threading

ROOT = Path(__file__).resolve().parents[1]


class StreamMasterTests(unittest.TestCase):
    def test_gamepad_hid_descriptors_and_reports(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / 'gamepad'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(ROOT),
                            str(ROOT / 'tests/streammaster_gamepad_harness.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=5)

    def test_saved_network_profiles(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / 'profiles'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(ROOT),
                            str(ROOT / 'tests/streammaster_profiles_harness.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=5)

    def test_ring_copy_boundaries(self):
        source=(ROOT/'streammaster/main/sockets.c').read_text()
        # Host checks exercise identical logic without ESP linker attributes.
        source=source.replace('SM_HOT_CODE ', '')
        functions=source[source.index('static void copy_out('):source.index('static int would_block(')]
        harness='''#include <assert.h>
#include <string.h>
#include <stdio.h>
'''+functions+'''
int main(void) {
    unsigned char ring[128],src[128],out[130],expected[128];
    for(unsigned i=0;i<128;i++)src[i]=i*193U;
    for(unsigned pos=0;pos<256;pos++)for(unsigned n=0;n<=128;n++) {
        memset(ring,0x5a,sizeof(ring));memcpy(expected,ring,sizeof(ring));
        for(unsigned i=0;i<n;i++)expected[(pos+i)%128]=src[i];
        copy_in(ring,128,pos,src,n);assert(!memcmp(ring,expected,128));
        memset(out,0xa5,sizeof(out));copy_out(out+1,ring,128,pos,n);
        assert(out[0]==0xa5 && out[n+1]==0xa5 && !memcmp(out+1,src,n));
    }
    puts("Ring copies: 33024 contiguous/wrapped/empty cases OK");
}'''
        with tempfile.TemporaryDirectory() as folder:
            binary=Path(folder)/'ring'
            subprocess.run(['cc','-x','c','-','-std=c11','-O2','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-o',str(binary)],input=harness,text=True,check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

    def test_internal_reply_pool_ownership(self):
        source=(ROOT/'streammaster/main/usb_bridge.c').read_text()
        types=source[source.index('typedef struct {uint32_t epoch;'):source.index('static QueueHandle_t')]
        pool=source[source.index('static int reply_prepare('):source.index('static void memory_report(')]
        worker=source[source.index('static void network_worker('):source.index('static void client_event(')]
        self.assertIn('replies=xQueueCreate(2,sizeof(Reply *))',source)
        self.assertIn('usb_host_transfer_alloc(SM_BULK_FRAME_SIZE+64,0,&tx)',source)
        self.assertNotIn('memset(answer,',source)
        harness=(ROOT/'tests/streammaster_reply_harness.c').read_text()
        harness=harness.replace('/* TYPES */',types).replace('/* POOL */',pool).replace('/* WORKER */',worker)
        with tempfile.TemporaryDirectory() as folder:
            binary=Path(folder)/'reply-pool'
            subprocess.run(['cc','-x','c','-','-std=c11','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-I',str(ROOT),'-o',str(binary)],
                           input=harness,text=True,check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

    def test_async_driver_buffer_ownership(self):
        source = (ROOT / "psp-client/streammaster_usb/bridge.c").read_text()
        functions = source[source.index("static int exchange_begin("):source.index("static int devctl(")]
        harness = (ROOT / "tests/streammaster_async_harness.c").read_text().replace("/* EXCHANGE */", functions)
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "async"
            subprocess.run(["cc", "-x", "c", "-", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-I", str(ROOT), "-o", str(binary)],
                           input=harness, text=True, check=True)
            subprocess.run([str(binary)], check=True, timeout=5)

    def test_download_real_http_range_and_truncated_body(self):
        source = (ROOT / "psp-client/offline_ui.h").read_text()
        functions = source[source.index("static int offline_connect("):source.index("static int offline_hash(")]
        harness = (ROOT / "tests/offline_http_harness.c").read_text().replace("/* OFFLINE_HTTP */", functions)
        payload = bytes(range(251)) * 6000

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                offset = int(self.headers.get("Range", "bytes=0-")[6:-1])
                self.send_response(206 if offset else 200)
                self.send_header("Content-Length", str(len(payload) - offset))
                if offset:
                    self.send_header("Content-Range", f"bytes {offset}-{len(payload)-1}/{len(payload)}")
                self.end_headers()
                self.wfile.write(payload[offset:600000] if self.path == "/short" else payload[offset:])

        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "http"
            subprocess.run(["cc", "-x", "c", "-", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-I", str(ROOT / "tests"), "-o", str(binary)],
                           input=harness, text=True, check=True)
            server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
            thread = threading.Thread(target=server.serve_forever)
            thread.start()
            try:
                target = Path(folder) / "media.part"
                def download(path, expected_result):
                    return subprocess.run([str(binary), str(server.server_port), path, str(target),
                                           str(len(payload)), str(expected_result)],
                                          capture_output=True, text=True, check=True, timeout=5).stderr
                log = download("/short", -1)
                self.assertIn("body eof", log)
                self.assertIn("http=200", log)
                self.assertEqual(target.read_bytes(), payload[:524288])
                log = download("/full", 0)
                self.assertIn("http=206", log)
                self.assertIn("resume=524288", log)
                self.assertEqual(target.read_bytes(), payload)
                target.unlink()
                self.assertIn("body complete", download("/full", 0))
                self.assertEqual(target.read_bytes(), payload)
            finally:
                server.shutdown()
                thread.join()
                server.server_close()

    def test_download_batches_storage_and_resumes_committed_bytes(self):
        source = (ROOT / "psp-client/offline_ui.h").read_text()
        function = source[source.index("static int offline_http("):source.index("static int offline_hash(")]
        harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PSP_O_WRONLY 1
#define PSP_O_CREAT 2
#define PSP_SEEK_SET 0
#define AF_INET 2
#define SOCK_STREAM 1
static int download_running=1,server_https,server_port=8091,writes,cancel_after,body_reads,headpos,send_calls,recv_calls;
static int debug_enabled=1;
static int stm_download_snapshot(int fd,char *line,unsigned size){(void)fd;(void)line;(void)size;return 0;}
static const char *server_host="test",*server_auth_header="";
static char download_error[128],head[256];
static unsigned download_bytes,download_total,download_speed,stored,position,received;
static unsigned char disk[1500003];
static unsigned long long clock_us;
static unsigned long long sceKernelGetSystemTimeWide(void){return clock_us+=1000;}
static unsigned long long offline_size(const char *p){(void)p;return stored;}
static int sceNetInetSocket(int a,int b,int c){(void)a;(void)b;(void)c;return 3;}
static int offline_connect(int f){assert(f==3);return 0;}
static int sceNetInetGetErrno(void){return 35;}
static int sceNetInetSend(int f,const void *b,int n,int flags){(void)b;(void)flags;assert(f==3);if(++send_calls%3==1)return -1;return n>17?17:n;}
static int tls_send(int f,const void *b,int n,volatile int *r,int ms){(void)r;(void)ms;return sceNetInetSend(f,b,n,0);}
static int sceNetInetRecv(int f,void *b,unsigned n,int flags){
    (void)flags;assert(f==3);if(++recv_calls%7==1)return -1;
    if(head[headpos]){assert(n==1);*(char *)b=head[headpos++];return 1;}
    if(n>4064)n=4064;
    for(unsigned i=0;i<n;i++)((unsigned char *)b)[i]=(received+i)%251;
    received+=n;
    if(cancel_after && ++body_reads==cancel_after)download_running=0;
    return n;
}
static int tls_recv(int f,void *b,int n,int ms){(void)ms;return sceNetInetRecv(f,b,n,0);}
#define SCE_NET_INET_POLLOUT 2
#define SCE_NET_INET_POLLIN 1
struct SceNetInetPollfd {int fd,events,revents;};
static int sceNetInetPoll(struct SceNetInetPollfd *p,int count,int ms){(void)count;(void)ms;p->revents=p->events;return 1;}
static void recovery_log(const char *e,int r,int h,const char *d){(void)e;(void)r;(void)h;assert(strlen(d)<176);}
/* TRANSPORT */
static int sceIoOpen(const char *p,int a,int b){(void)p;(void)a;(void)b;return 4;}
static int sceIoLseek(int f,unsigned p,int mode){assert(f==4&&!mode);position=p;return p;}
static int sceIoWrite(int f,const void *b,unsigned n){assert(f==4&&position+n<=sizeof(disk));memcpy(disk+position,b,n);position+=n;stored=position;writes++;return n;}
static int sceIoClose(int f){assert(f==4);return 0;}
static void connection_close(int f){assert(f==3);}
static void recovery_flush_due(void){}
#include "offline_async_mock.h"
#include "../psp-client/offline_io.h"
/* FUNCTION */
static void setup(void){
    received=stored;headpos=body_reads=0;download_running=1;
    if(stored)snprintf(head,sizeof(head),"HTTP/1.0 206 OK\r\nContent-Length: %u\r\nContent-Range: bytes %u-1500002/1500003\r\n\r\n",1500003-stored,stored);
    else strcpy(head,"HTTP/1.0 200 OK\r\nContent-Length: 1500003\r\n\r\n");
}
int main(void){
    (void)server_port;setup();assert(!offline_http("/file",NULL,NULL,0,"file",1500003));
    assert(writes==3&&stored==1500003&&download_bytes==stored&&!io_busy);
    for(unsigned i=0;i<stored;i++)assert(disk[i]==i%251);
    stored=writes=0;cancel_after=150;setup();assert(offline_http("/file",NULL,NULL,0,"file",1500003)<0);
    assert(stored==524288&&download_bytes==stored&&!io_busy);
    cancel_after=0;setup();assert(!offline_http("/file",NULL,NULL,0,"file",1500003));
    assert(stored==1500003&&writes==3&&!io_busy);
    for(unsigned i=0;i<stored;i++)assert(disk[i]==i%251);
    stored=writes=0;io_disabled=1;setup();assert(!offline_http("/file",NULL,NULL,0,"file",1500003));
    assert(stored==1500003 && !io_busy);
    stored=writes=0;io_disabled=0;io_fail=1;setup();assert(offline_http("/file",NULL,NULL,0,"file",1500003)<0);
    assert(!io_busy && !stored);
}
'''.replace("/* FUNCTION */", function)
        transport = (ROOT / "psp-client/playback_transport.h").read_text()
        harness = harness.replace("/* TRANSPORT */", transport[transport.index("static int playback_send("):])
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "download"
            subprocess.run(["cc", "-x", "c", "-", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-I", str(ROOT / "tests"), "-o", str(binary)],
                           input=harness, text=True, check=True)
            subprocess.run([str(binary)], check=True, timeout=5)

    def test_stale_usb_work_cannot_keep_connections_alive(self):
        source = (ROOT / "streammaster/main/usb_bridge.c").read_text()
        function = source[source.index("static int process_work("):source.index("static void network_worker(")]
        harness = r'''
#include <assert.h>
#include <stdatomic.h>
#include "streammaster/protocol.h"
typedef struct {uint32_t epoch;SmFrame frame;} Work;
static atomic_uint epoch;
static int executed,interrupted,sockets_reset,http_reset;
static void sm_network_idle(void){}
static void sm_network_command(const SmFrame *r,SmFrame *out){
    (void)r;(void)out;executed++;if(interrupted)atomic_fetch_add(&epoch,1);
}
static void sm_sockets_reset(void){sockets_reset++;}
static void sm_network_drop_http(void){http_reset++;}
/* WORK */
int main(void){
    Work work={0},reply={0};atomic_store(&epoch,3);work.epoch=2;
    assert(!process_work(&work,&reply)&&!executed);
    work.epoch=3;assert(process_work(&work,&reply)&&executed==1&&reply.epoch==3);
    interrupted=1;assert(!process_work(&work,&reply)&&executed==2&&sockets_reset==1&&http_reset==1);
    interrupted=0;work.epoch=4;assert(process_work(&work,&reply)&&executed==3);
    assert(sockets_reset==1&&http_reset==1);
}
'''.replace("/* WORK */", function)
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "usb-work"
            subprocess.run(["cc", "-x", "c", "-", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-I", str(ROOT), "-o", str(binary)],
                           input=harness, text=True, check=True)
            subprocess.run([str(binary)], check=True, timeout=5)

    def test_socket_channels_and_psp_transport(self):
        # Replace only platform headers; compile the actual production code.
        esp = (ROOT / "streammaster/main/sockets.c").read_text()
        psp = (ROOT / "psp-client/streammaster_transport.c").read_text()
        esp = "\n".join(line for line in esp.splitlines()
                        if not line.startswith('#include "'))
        esp = esp.replace('SM_HOT_CODE ', '')
        psp = "\n".join(line for line in psp.splitlines()
                        if not line.startswith(('#include <psp', '#include <kubridge', '#include "')))
        harness = (ROOT / "tests/streammaster_transport_harness.c").read_text()
        harness = harness.replace("/* ACTUAL_ESP_SOCKETS */", esp).replace("/* ACTUAL_PSP_TRANSPORT */", psp)
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "transport"
            subprocess.run(["cc", "-x", "c", "-", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-I", str(ROOT), "-o", str(binary)],
                           input=harness, text=True, check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_protocol_and_network_configuration(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "protocol"
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined",
                            str(ROOT / "tests/streammaster_protocol_harness.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
