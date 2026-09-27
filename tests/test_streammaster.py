from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StreamMasterTests(unittest.TestCase):
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
