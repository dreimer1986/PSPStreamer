from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StreamMasterTests(unittest.TestCase):
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
static int download_running=1,server_https,server_port=8091,writes,cancel_after,body_reads,headpos;
static const char *server_host="test",*server_auth_header="";
static char download_error[128],head[256];
static unsigned download_bytes,download_total,download_speed,stored,position,received;
static unsigned char disk[100003];
static unsigned long long clock_us;
static unsigned long long sceKernelGetSystemTimeWide(void){return clock_us+=1000;}
static unsigned long long offline_size(const char *p){(void)p;return stored;}
static int sceNetInetSocket(int a,int b,int c){(void)a;(void)b;(void)c;return 3;}
static int offline_connect(int f){assert(f==3);return 0;}
static int sceNetInetSend(int f,const void *b,int n,int flags){(void)b;(void)flags;assert(f==3);return n;}
static int tls_send(int f,const void *b,int n,volatile int *r,int ms){(void)r;(void)ms;return sceNetInetSend(f,b,n,0);}
static int stream_recv(int f,void *b,unsigned n,int ms){
    (void)ms;assert(f==3);
    if(head[headpos]){assert(n==1);*(char *)b=head[headpos++];return 1;}
    if(n>4064)n=4064;
    for(unsigned i=0;i<n;i++)((unsigned char *)b)[i]=(received+i)%251;
    received+=n;
    if(cancel_after && ++body_reads==cancel_after)download_running=0;
    return n;
}
static int sceIoOpen(const char *p,int a,int b){(void)p;(void)a;(void)b;return 4;}
static int sceIoLseek(int f,unsigned p,int mode){assert(f==4&&!mode);position=p;return p;}
static int sceIoWrite(int f,const void *b,unsigned n){assert(f==4&&position+n<=sizeof(disk));memcpy(disk+position,b,n);position+=n;stored=position;writes++;return n;}
static int sceIoClose(int f){assert(f==4);return 0;}
static void connection_close(int f){assert(f==3);}
/* FUNCTION */
static void setup(void){
    received=stored;headpos=body_reads=0;download_running=1;
    if(stored)snprintf(head,sizeof(head),"HTTP/1.0 206 OK\r\nContent-Length: %u\r\nContent-Range: bytes %u-100002/100003\r\n\r\n",100003-stored,stored);
    else strcpy(head,"HTTP/1.0 200 OK\r\nContent-Length: 100003\r\n\r\n");
}
int main(void){
    (void)server_port;setup();assert(!offline_http("/file",NULL,NULL,0,"file",100003));
    assert(writes==4&&stored==100003&&download_bytes==stored);
    for(unsigned i=0;i<stored;i++)assert(disk[i]==i%251);
    stored=writes=0;cancel_after=10;setup();assert(offline_http("/file",NULL,NULL,0,"file",100003)<0);
    assert(stored==32768&&download_bytes==stored);
    cancel_after=0;setup();assert(!offline_http("/file",NULL,NULL,0,"file",100003));
    assert(stored==100003&&writes==4);
    for(unsigned i=0;i<stored;i++)assert(disk[i]==i%251);
}
'''.replace("/* FUNCTION */", function)
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "download"
            subprocess.run(["cc", "-x", "c", "-", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-o", str(binary)],
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
